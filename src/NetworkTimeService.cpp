#include "NetworkTimeService.h"

#include <Arduino.h>
#include <cstring>
#include <esp_system.h>
#include <esp_sntp.h>
#include <esp_wifi.h>

#include "AppConfig.h"
#include "ClockCore.h"
#include "Diagnostics.h"
#include "PortalPage.h"

NetworkTimeService* NetworkTimeService::instance_ = nullptr;
volatile bool NetworkTimeService::ntpSynced_ = false;

namespace {
constexpr uint16_t kDnsPort = 53;
constexpr uint8_t kPortalWifiChannel = 1;
constexpr uint8_t kMaximumPortalStartAttempts = 3;
constexpr uint32_t kPortalStartRetryMs = 2000UL;
constexpr char kNtpServer1[] = "time.cloudflare.com";
constexpr char kNtpServer2[] = "pool.ntp.org";
static_assert(config::kMaximumWifiAttemptsPerWindow ==
                  clockcore::WifiCandidateRanker::kCapacity,
              "Wi-Fi candidate capacity must match the per-window attempt cap");

bool deadlineReached(const uint32_t now, const uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}
}  // namespace

NetworkTimeService::NetworkTimeService() : web_(80) {}

void NetworkTimeService::begin(const TimeUpdateHandler handler,
                               const int16_t utcOffsetMinutes,
                               const bool hasConfirmedSync) {
  handler_ = handler;
  utcOffsetMinutes_ = utcOffsetMinutes;
  hasConfirmedSync_ = hasConfirmedSync;
  instance_ = this;
  mode_ = Mode::kWaitingForBle;
  modeStartedMs_ = millis();
  nextAttemptMs_ = modeStartedMs_ + config::kBleWindowMs;

  web_.on("/", HTTP_GET, [this]() { handlePortalRoot(); });
  web_.on("/set-time", HTTP_POST, [this]() { handlePortalTime(); });
  web_.onNotFound([this]() { handlePortalRoot(); });
}

bool NetworkTimeService::wifiBusy() const {
  return mode_ == Mode::kScanning || mode_ == Mode::kConnecting ||
         mode_ == Mode::kWaitingForNtp;
}

void NetworkTimeService::startPortal() {
  char ssid[24] = {};
  snprintf(ssid, sizeof(ssid), "KidsClock-%04X",
           static_cast<unsigned>(ESP.getEfuseMac() & 0xFFFFU));
  ++portalStartAttempts_;
  const bool modeStarted = WiFi.mode(WIFI_AP);
  const bool accessPointStarted =
      modeStarted &&
      WiFi.softAP(ssid, nullptr, kPortalWifiChannel, false, 4);
  if (!accessPointStarted) {
    CLOCK_DIAGNOSTIC_PRINTF(
        "[WiFi] portal start=failed attempt=%u/%u mode=%s ssid=%s\n",
        portalStartAttempts_, kMaximumPortalStartAttempts,
        modeStarted ? "ok" : "failed", ssid);
    WiFi.softAPdisconnect(true);
    mode_ = Mode::kWaitingForBle;
    modeStartedMs_ = millis();
    if (portalStartAttempts_ < kMaximumPortalStartAttempts) {
      nextAttemptMs_ = modeStartedMs_ + kPortalStartRetryMs;
    } else {
      portalWasOffered_ = true;
      startScan();
    }
    return;
  }
  const IPAddress ip = WiFi.softAPIP();
  const bool dnsStarted = dns_.start(kDnsPort, "*", ip);
  web_.begin();
  mode_ = Mode::kPortal;
  modeStartedMs_ = millis();
  portalWasOffered_ = true;
  portalStartAttempts_ = 0;
  CLOCK_DIAGNOSTIC_PRINTF(
      "[WiFi] portal start=ok ssid=%s channel=%u ip=%s dns=%s "
      "window_ms=%lu\n",
      ssid, kPortalWifiChannel, ip.toString().c_str(),
      dnsStarted ? "ok" : "failed",
      static_cast<unsigned long>(config::kPortalWindowMs));
}

void NetworkTimeService::stopPortal() {
  dns_.stop();
  web_.stop();
  const bool stopped = WiFi.softAPdisconnect(true);
  CLOCK_DIAGNOSTIC_PRINTF("[WiFi] portal stop=%s\n",
                          stopped ? "ok" : "failed");
}

void NetworkTimeService::handlePortalRoot() {
  web_.sendHeader("Cache-Control", "no-store");
  web_.send(200, "text/html", FPSTR(portalpage::kHtml));
}

void NetworkTimeService::handlePortalTime() {
  if (portalAccepted_) {
    // Treat a browser retry as successful without applying the update twice.
    web_.send(200, "text/plain", "Clock already set");
    return;
  }
  if (web_.clientContentLength() > 64) {
    web_.send(413, "text/plain", "Request too large");
    return;
  }
  if (!web_.hasArg("epoch") || !web_.hasArg("offset")) {
    web_.send(400, "text/plain", "Missing epoch or offset");
    return;
  }
  if (web_.arg("epoch").length() > 20 || web_.arg("offset").length() > 6) {
    web_.send(413, "text/plain", "Request too large");
    return;
  }
  String payload;
  payload.reserve(web_.arg("epoch").length() + web_.arg("offset").length() + 2);
  payload = web_.arg("epoch");
  payload += ',';
  payload += web_.arg("offset");
  int64_t epoch = 0;
  int16_t offset = 0;
  if (!clockcore::parseTimeSyncPayload(
          reinterpret_cast<const uint8_t*>(payload.c_str()), payload.length(),
          epoch, offset) ||
      !clockcore::isAcceptableCorrection(
          hasConfirmedSync_, static_cast<int64_t>(time(nullptr)), epoch)) {
    web_.send(400, "text/plain", "Invalid time");
    return;
  }
  utcOffsetMinutes_ = offset;
  if (handler_ != nullptr) {
    handler_({epoch, utcOffsetMinutes_, TimeSource::kPortal});
  }
  portalAccepted_ = true;
  web_.send(200, "text/plain", "Clock set");
  // Give the captive browser time to receive the successful response.
  portalStopAtMs_ = millis() + 1500UL;
}

void NetworkTimeService::startScan() {
#if CLOCK_ENABLE_OPEN_WIFI_FALLBACK
  const uint32_t now = millis();
  if (wifiExhaustedForBoot_) {
    mode_ = Mode::kIdle;
    backgroundRefreshActive_ = false;
    nextAttemptMs_ = now + config::kResyncIntervalMs;
    return;
  }
  if (mode_ == Mode::kIdle || mode_ == Mode::kWaitingForBle ||
      mode_ == Mode::kPortal) {
    wifiWindowStartedMs_ = now;
    wifiAttemptsThisWindow_ = 0;
  }
  if (mode_ == Mode::kPortal) {
    stopPortal();
  }
  candidateRanker_.clear();
  nextCandidateIndex_ = 0;
  activeCandidateIndex_ = UINT8_MAX;
  WiFi.mode(WIFI_STA);
  if (!stationMacRandomized_) {
    uint8_t address[6] = {};
    esp_fill_random(address, sizeof(address));
    address[0] = (address[0] | 0x02U) & 0xFEU;
    if (esp_wifi_set_mac(WIFI_IF_STA, address) == ESP_OK) {
      stationMacRandomized_ = true;
    }
  }
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);
  WiFi.scanDelete();
  const int result = WiFi.scanNetworks(true, true);
  mode_ = Mode::kScanning;
  modeStartedMs_ = millis();
  if (result >= 0) {
    processScan(result);
  }
#else
  scheduleNextAttempt();
#endif
}

bool NetworkTimeService::wasFailed(const uint8_t bssid[6]) const {
  return failedBssids_.contains(bssid);
}

void NetworkTimeService::rememberFailure(const uint8_t bssid[6]) {
  if (!failedBssids_.contains(bssid) && !failedBssids_.add(bssid)) {
    wifiExhaustedForBoot_ = true;
  }
}

void NetworkTimeService::processScan(const int count) {
  for (int i = 0; i < count; ++i) {
    if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) {
      continue;
    }
    const String ssid = WiFi.SSID(i);
    const uint8_t* bssid = WiFi.BSSID(i);
    if (ssid.isEmpty() ||
        ssid.length() > clockcore::WifiCandidate::kMaximumSsidLength ||
        std::strlen(ssid.c_str()) != ssid.length() || bssid == nullptr ||
        wasFailed(bssid)) {
      continue;
    }
    candidateRanker_.consider(ssid.c_str(), bssid, WiFi.channel(i),
                              WiFi.RSSI(i));
  }
  WiFi.scanDelete();
  connectNextCandidate();
}

void NetworkTimeService::connectNextCandidate() {
  const uint32_t now = millis();
  if (wifiExhaustedForBoot_ ||
      wifiAttemptsThisWindow_ >= config::kMaximumWifiAttemptsPerWindow ||
      now - wifiWindowStartedMs_ >= config::kWifiWindowMs) {
    scheduleNextAttempt();
    return;
  }
  const clockcore::WifiCandidate* candidate =
      candidateRanker_.at(nextCandidateIndex_);
  if (candidate == nullptr) {
    scheduleNextAttempt();
    return;
  }
  activeCandidateIndex_ = nextCandidateIndex_++;
  ++wifiAttemptsThisWindow_;
  WiFi.begin(candidate->ssid, nullptr, candidate->channel,
             candidate->bssid, true);
  mode_ = Mode::kConnecting;
  modeStartedMs_ = now;
}

void NetworkTimeService::startNtp() {
  ntpSynced_ = false;
  ntpBaselineEpoch_ = static_cast<int64_t>(time(nullptr));
  ntpBaselineMs_ = millis();
  sntp_set_time_sync_notification_cb(ntpCallback);
  configTime(0, 0, kNtpServer1, kNtpServer2);
  mode_ = Mode::kWaitingForNtp;
  modeStartedMs_ = millis();
}

void NetworkTimeService::ntpCallback(struct timeval*) {
  ntpSynced_ = true;
}

void NetworkTimeService::finishNtp(const bool success) {
  const clockcore::WifiCandidate* candidate =
      candidateRanker_.at(activeCandidateIndex_);
  bool accepted = success;
  const int64_t candidateEpoch = static_cast<int64_t>(time(nullptr));
  const int64_t expectedEpoch =
      ntpBaselineEpoch_ +
      static_cast<int64_t>((millis() - ntpBaselineMs_) / 1000UL);
  if (accepted && hasConfirmedSync_ &&
      !clockcore::isPlausibleCorrection(expectedEpoch, candidateEpoch)) {
    const timeval restore = {static_cast<time_t>(expectedEpoch), 0};
    settimeofday(&restore, nullptr);
    accepted = false;
  }
  esp_sntp_stop();
  ntpSynced_ = false;
  if (accepted && handler_ != nullptr) {
    handler_({candidateEpoch, utcOffsetMinutes_,
              TimeSource::kNtp});
  } else if (candidate != nullptr) {
    rememberFailure(candidate->bssid);
  }
  if (accepted) {
    scheduleNextAttempt();
  } else {
    WiFi.disconnect(false, false);
    connectNextCandidate();
  }
}

void NetworkTimeService::scheduleNextAttempt() {
  if (mode_ == Mode::kWaitingForNtp) {
    esp_sntp_stop();
    ntpSynced_ = false;
  }
  if (mode_ == Mode::kPortal) {
    stopPortal();
  }
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true, false);
  }
  mode_ = Mode::kIdle;
  bleResyncGraceActive_ = false;
  backgroundRefreshActive_ = false;
  nextAttemptMs_ = millis() + config::kResyncIntervalMs;
}

void NetworkTimeService::onExternalTimeSync(
    const int16_t utcOffsetMinutes) {
  utcOffsetMinutes_ = utcOffsetMinutes;
  hasConfirmedSync_ = true;
  if (mode_ == Mode::kPortal) {
    portalStopAtMs_ = millis() + 1500UL;
  } else {
    // BLE or the local portal is a higher-priority timezone-aware source.
    // Abort any lower-trust opportunistic network attempt immediately.
    scheduleNextAttempt();
  }
}

void NetworkTimeService::tick(const bool bleConnected) {
  const uint32_t now = millis();
  switch (mode_) {
    case Mode::kWaitingForBle:
      if (deadlineReached(now, nextAttemptMs_)) {
        if (!portalWasOffered_) {
          startPortal();
        } else {
          startScan();
        }
      }
      break;
    case Mode::kPortal:
      dns_.processNextRequest();
      web_.handleClient();
      if (portalStopAtMs_ != 0 && deadlineReached(now, portalStopAtMs_)) {
        portalStopAtMs_ = 0;
        scheduleNextAttempt();
      } else if (now - modeStartedMs_ >= config::kPortalWindowMs) {
        startScan();
      }
      break;
    case Mode::kScanning: {
      const int result = WiFi.scanComplete();
      if (result >= 0) {
        processScan(result);
      } else if (result == WIFI_SCAN_FAILED ||
                 now - modeStartedMs_ >= 15000UL) {
        scheduleNextAttempt();
      }
      break;
    }
    case Mode::kConnecting:
      if (WiFi.status() == WL_CONNECTED) {
        startNtp();
      } else if (now - modeStartedMs_ >= config::kWifiConnectTimeoutMs) {
        const clockcore::WifiCandidate* candidate =
            candidateRanker_.at(activeCandidateIndex_);
        if (candidate != nullptr) {
          rememberFailure(candidate->bssid);
        }
        WiFi.disconnect(false, false);
        connectNextCandidate();
      }
      break;
    case Mode::kWaitingForNtp:
      if (ntpSynced_) {
        ntpSynced_ = false;
        finishNtp(true);
      } else if (now - modeStartedMs_ >= config::kNtpTimeoutMs) {
        finishNtp(false);
      }
      break;
    case Mode::kIdle:
      if (bleResyncGraceActive_ && !bleConnected) {
        bleResyncGraceActive_ = false;
        startScan();
      } else if (deadlineReached(now, nextAttemptMs_)) {
        backgroundRefreshActive_ = true;
        if (bleConnected && !bleResyncGraceActive_) {
          bleResyncGraceActive_ = true;
          nextAttemptMs_ = now + config::kBleResyncGraceMs;
          CLOCK_DIAGNOSTIC_PRINTF(
              "[WiFi] deferring fallback for connected BLE grace_ms=%lu\n",
              static_cast<unsigned long>(config::kBleResyncGraceMs));
        } else {
          bleResyncGraceActive_ = false;
          startScan();
        }
      }
      break;
  }
}
