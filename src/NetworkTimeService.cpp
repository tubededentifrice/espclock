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
                               const bool hasConfirmedSync,
                               const SyncRoute syncRoute,
                               const int64_t lastSyncUtc,
                               const int64_t currentUtc) {
  handler_ = handler;
  utcOffsetMinutes_ = utcOffsetMinutes;
  hasConfirmedSync_ = hasConfirmedSync;
  syncRoute_ = hasConfirmedSync ? syncRoute : SyncRoute::kUnselected;
  instance_ = this;
  bootStartedMs_ = millis();
  modeStartedMs_ = bootStartedMs_;
  initialSelectionActive_ = syncRoute_ == SyncRoute::kUnselected;
  if (initialSelectionActive_) {
    mode_ = Mode::kWaitingForBle;
    nextAttemptMs_ = bootStartedMs_ + config::kBleWindowMs;
  } else {
    mode_ = Mode::kIdle;
    const uint32_t delayMs = clockcore::remainingResyncDelayMs(
        syncRoute_, lastSyncUtc, currentUtc, config::kResyncIntervalMs,
        config::kWifiResyncIntervalMs);
    armResync(delayMs);
  }

  web_.on("/", HTTP_GET, [this]() { handlePortalRoot(); });
  web_.on("/set-time", HTTP_POST, [this]() { handlePortalTime(); });
  web_.onNotFound([this]() { handlePortalRoot(); });
#if CLOCK_ENABLE_OPEN_WIFI_FALLBACK && CLOCK_ENABLE_CAPTIVE_PORTAL_AUTOFILL
  captivePortalAutofillReady_ = portalAutofill_.begin();
  CLOCK_DIAGNOSTIC_PRINTF("[WiFi] captive autofill worker=%s\n",
                          captivePortalAutofillReady_ ? "ready" : "failed");
#endif
}

bool NetworkTimeService::wifiBusy() const {
  return mode_ == Mode::kScanning || mode_ == Mode::kConnecting ||
         mode_ == Mode::kWaitingForNtp ||
         mode_ == Mode::kWaitingForPortalAutomation;
}

bool NetworkTimeService::takeBleSyncRequest() {
  const bool pending = bleSyncRequestPending_;
  bleSyncRequestPending_ = false;
  return pending;
}

void NetworkTimeService::startPortal(const bool persistent) {
  portalPersistent_ = persistent;
  portalAccepted_ = false;
  portalStopAtMs_ = 0;
  scheduleAfterPortalResponse_ = false;
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
    } else if (initialSelectionActive_) {
      // A broken AP must not shorten the BLE-first onboarding contract.
      // Keep showing PAIR and wait for the common two-minute NTP boundary.
      nextAttemptMs_ =
          bootStartedMs_ + config::kInitialSetupWindowMs;
    } else {
      finishFailedAttempt();
    }
    return;
  }
  const IPAddress ip = WiFi.softAPIP();
  const bool dnsStarted = dns_.start(kDnsPort, "*", ip);
  (void)dnsStarted;
  web_.begin();
  mode_ = Mode::kPortal;
  modeStartedMs_ = millis();
  portalStartAttempts_ = 0;
  CLOCK_DIAGNOSTIC_PRINTF(
      "[WiFi] portal start=ok ssid=%s channel=%u ip=%s dns=%s "
      "persistent=%s setup_deadline_ms=%lu\n",
      ssid, kPortalWifiChannel, ip.toString().c_str(),
      dnsStarted ? "ok" : "failed",
      persistent ? "yes" : "no",
      static_cast<unsigned long>(
          persistent ? 0UL : config::kInitialSetupWindowMs));
}

void NetworkTimeService::stopPortal() {
  dns_.stop();
  web_.stop();
  const bool stopped = WiFi.softAPdisconnect(true);
  (void)stopped;
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
  if (handler_ == nullptr) {
    web_.send(503, "text/plain", "Clock unavailable");
    return;
  }
  handler_({epoch, utcOffsetMinutes_, TimeSource::kPortal});
  portalAccepted_ = true;
  web_.send(200, "text/plain", "Clock set");
  // Give the captive browser time to receive the successful response.
  portalStopAtMs_ = millis() + 1500UL;
}

void NetworkTimeService::startScan() {
#if CLOCK_ENABLE_OPEN_WIFI_FALLBACK
  const uint32_t now = millis();
  if (wifiExhaustedForBoot_) {
    finishFailedAttempt();
    return;
  }
  initialSelectionActive_ = false;
  wifiWindowStartedMs_ = now;
  wifiAttemptsThisWindow_ = 0;
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
  finishFailedAttempt();
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
      wifiWindowExpired(now)) {
    finishFailedAttempt();
    return;
  }
  const clockcore::WifiCandidate* candidate =
      candidateRanker_.at(nextCandidateIndex_);
  if (candidate == nullptr) {
    finishFailedAttempt();
    return;
  }
  activeCandidateIndex_ = nextCandidateIndex_++;
  ++wifiAttemptsThisWindow_;
  ntpRetriedAfterPortal_ = false;
  WiFi.begin(candidate->ssid, nullptr, candidate->channel,
             candidate->bssid, true);
  mode_ = Mode::kConnecting;
  modeStartedMs_ = now;
}

bool NetworkTimeService::wifiWindowExpired(const uint32_t now) const {
  return now - wifiWindowStartedMs_ >= config::kWifiWindowMs;
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
  const int64_t candidateEpoch = static_cast<int64_t>(time(nullptr));
  bool accepted = success && handler_ != nullptr &&
                  clockcore::isValidEpoch(candidateEpoch);
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
  }
  if (accepted) {
    // The main task will atomically apply the queued update and arm the
    // route-specific next refresh. Stop this Wi-Fi attempt in the meantime.
    stopNetworkActivity();
    resyncDueArmed_ = false;
  } else if (candidate != nullptr &&
             captiveportal::actionAfterNtpFailure(
                 captivePortalAutofillReady_, ntpRetriedAfterPortal_) ==
                 captiveportal::NtpFailureAction::kTryPortal) {
    startPortalAutomation();
  } else {
    if (candidate != nullptr) {
      rememberFailure(candidate->bssid);
    }
    WiFi.disconnect(false, false);
    connectNextCandidate();
  }
}

void NetworkTimeService::startPortalAutomation() {
#if CLOCK_ENABLE_OPEN_WIFI_FALLBACK && CLOCK_ENABLE_CAPTIVE_PORTAL_AUTOFILL
  const uint32_t generation = ++networkGeneration_;
  if (!portalAutofill_.start(generation, esp_random())) {
    const clockcore::WifiCandidate* candidate =
        candidateRanker_.at(activeCandidateIndex_);
    if (candidate != nullptr) {
      rememberFailure(candidate->bssid);
    }
    WiFi.disconnect(false, false);
    connectNextCandidate();
    return;
  }
  mode_ = Mode::kWaitingForPortalAutomation;
  modeStartedMs_ = millis();
  CLOCK_DIAGNOSTIC_PRINTF(
      "[WiFi] captive autofill start generation=%lu timeout_ms=%lu\n",
      static_cast<unsigned long>(generation),
      static_cast<unsigned long>(config::kCaptivePortalTimeoutMs));
#else
  const clockcore::WifiCandidate* candidate =
      candidateRanker_.at(activeCandidateIndex_);
  if (candidate != nullptr) {
    rememberFailure(candidate->bssid);
  }
  WiFi.disconnect(false, false);
  connectNextCandidate();
#endif
}

void NetworkTimeService::finishPortalAutomation(
    const CaptivePortalAutofill::Result result) {
  if (captiveportal::actionAfterPortalResult(result) ==
      captiveportal::PortalResultAction::kRetryNtp) {
    ntpRetriedAfterPortal_ = true;
    startNtp();
    return;
  }
  const clockcore::WifiCandidate* candidate =
      candidateRanker_.at(activeCandidateIndex_);
  if (candidate != nullptr) {
    rememberFailure(candidate->bssid);
  }
  WiFi.disconnect(false, false);
  connectNextCandidate();
}

void NetworkTimeService::stopNetworkActivity() {
  if (mode_ == Mode::kWaitingForNtp) {
    esp_sntp_stop();
    ntpSynced_ = false;
  }
  if (mode_ == Mode::kPortal) {
    stopPortal();
  }
  portalAutofill_.cancel(++networkGeneration_);
  WiFi.scanDelete();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true, false);
  }
  mode_ = Mode::kIdle;
  bleResyncGraceActive_ = false;
  portalPersistent_ = false;
  scheduleAfterPortalResponse_ = false;
}

void NetworkTimeService::armResync(const uint32_t delayMs) {
  stopNetworkActivity();
  resyncDueArmed_ = true;
  nextAttemptMs_ = millis() + delayMs;
}

void NetworkTimeService::finishFailedAttempt() {
  initialSelectionActive_ = false;
  syncOverdue_ = hasConfirmedSync_;
  stopNetworkActivity();
  resyncDueArmed_ = true;
  nextAttemptMs_ = millis() + config::kWifiResyncIntervalMs;
}

void NetworkTimeService::onExternalTimeSync(
    const TimeSource source, const int16_t utcOffsetMinutes) {
  utcOffsetMinutes_ = utcOffsetMinutes;
  hasConfirmedSync_ = true;
  initialSelectionActive_ = false;
  syncOverdue_ = false;
  const SyncRoute candidateRoute =
      clockcore::syncRouteForSource(source);
  if (candidateRoute == SyncRoute::kBle ||
      syncRoute_ == SyncRoute::kUnselected) {
    syncRoute_ = candidateRoute;
  }

  if (source == TimeSource::kPortal && mode_ == Mode::kPortal) {
    scheduleAfterPortalResponse_ = true;
    portalStopAtMs_ = millis() + 1500UL;
  } else {
    const uint32_t intervalMs =
        syncRoute_ == SyncRoute::kBle
            ? config::kResyncIntervalMs
            : config::kWifiResyncIntervalMs;
    // BLE is always the highest-priority route and immediately tears down
    // any captive portal, scan, association, or NTP work.
    armResync(intervalMs);
  }
}

void NetworkTimeService::tick(const bool bleConnected) {
  const uint32_t now = millis();
  switch (mode_) {
    case Mode::kWaitingForBle:
      if (deadlineReached(now, nextAttemptMs_)) {
        if (initialSelectionActive_) {
          const clockcore::InitialSyncPhase phase =
              clockcore::initialSyncPhase(
                  now - bootStartedMs_, config::kBleWindowMs,
                  config::kInitialSetupWindowMs);
          if (phase == clockcore::InitialSyncPhase::kNtp) {
            startScan();
          } else if (phase ==
                     clockcore::InitialSyncPhase::kPortal) {
            startPortal(false);
          }
        } else if (syncRoute_ == SyncRoute::kPortal) {
          startPortal(true);
        } else {
          finishFailedAttempt();
        }
      }
      break;
    case Mode::kPortal:
      dns_.processNextRequest();
      web_.handleClient();
      if (portalStopAtMs_ != 0 && deadlineReached(now, portalStopAtMs_)) {
        portalStopAtMs_ = 0;
        if (scheduleAfterPortalResponse_) {
          scheduleAfterPortalResponse_ = false;
          armResync(config::kWifiResyncIntervalMs);
        } else {
          finishFailedAttempt();
        }
      } else if (
          !portalPersistent_ &&
          clockcore::initialSyncPhase(
              now - bootStartedMs_, config::kBleWindowMs,
              config::kInitialSetupWindowMs) ==
              clockcore::InitialSyncPhase::kNtp) {
        startScan();
      }
      break;
    case Mode::kScanning: {
      const int result = WiFi.scanComplete();
      if (wifiWindowExpired(now)) {
        finishFailedAttempt();
      } else if (result >= 0) {
        processScan(result);
      } else if (result == WIFI_SCAN_FAILED ||
                 now - modeStartedMs_ >= 15000UL) {
        finishFailedAttempt();
      }
      break;
    }
    case Mode::kConnecting:
      if (wifiWindowExpired(now)) {
        const clockcore::WifiCandidate* candidate =
            candidateRanker_.at(activeCandidateIndex_);
        if (candidate != nullptr) {
          rememberFailure(candidate->bssid);
        }
        finishFailedAttempt();
      } else if (WiFi.status() == WL_CONNECTED) {
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
      if (wifiWindowExpired(now)) {
        finishNtp(false);
      } else if (ntpSynced_) {
        ntpSynced_ = false;
        finishNtp(true);
      } else if (now - modeStartedMs_ >= config::kNtpTimeoutMs) {
        finishNtp(false);
      }
      break;
    case Mode::kWaitingForPortalAutomation: {
      if (wifiWindowExpired(now) ||
          captiveportal::automationWindowExpired(
              now, modeStartedMs_, config::kCaptivePortalTimeoutMs)) {
        const clockcore::WifiCandidate* candidate =
            candidateRanker_.at(activeCandidateIndex_);
        if (candidate != nullptr) {
          rememberFailure(candidate->bssid);
        }
        portalAutofill_.cancel(++networkGeneration_);
        finishFailedAttempt();
        break;
      }
      uint32_t generation = 0;
      CaptivePortalAutofill::Result result =
          CaptivePortalAutofill::Result::kFailed;
      if (portalAutofill_.poll(generation, result) &&
          captiveportal::completionMatchesGeneration(networkGeneration_,
                                                     generation)) {
        finishPortalAutomation(result);
      }
      break;
    }
    case Mode::kIdle:
      if (resyncDueArmed_ && deadlineReached(now, nextAttemptMs_)) {
        if (syncRoute_ == SyncRoute::kBle) {
          resyncDueArmed_ = false;
          syncOverdue_ = true;
          bleSyncRequestPending_ = true;
        } else if ((syncRoute_ == SyncRoute::kPortal ||
                    syncRoute_ == SyncRoute::kNtp) &&
                   !bleResyncGraceActive_) {
          syncOverdue_ = true;
          bleResyncGraceActive_ = true;
          bleSyncRequestPending_ = true;
          nextAttemptMs_ = now + config::kBleResyncGraceMs;
          CLOCK_DIAGNOSTIC_PRINTF(
              "[WiFi] route=%u waiting for BLE grace_ms=%lu connected=%s\n",
              static_cast<unsigned>(syncRoute_),
              static_cast<unsigned long>(config::kBleResyncGraceMs),
              bleConnected ? "yes" : "no");
        } else if (syncRoute_ == SyncRoute::kPortal) {
          resyncDueArmed_ = false;
          bleResyncGraceActive_ = false;
          startPortal(true);
        } else {
          resyncDueArmed_ = false;
          bleResyncGraceActive_ = false;
          startScan();
        }
      }
      break;
  }
}
