#include "NetworkTimeService.h"

#include <Arduino.h>
#include <ctype.h>
#include <cstring>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <stdlib.h>
#include <strings.h>

#include "AppConfig.h"
#include "ClockCore.h"
#include "Diagnostics.h"
#include "PortalPage.h"

std::atomic_bool NetworkTimeService::ntpSynced_{false};

namespace {
constexpr uint16_t kDnsPort = 53;
constexpr uint8_t kPortalWifiChannel = 1;
constexpr uint8_t kMaximumPortalStartAttempts = 3;
constexpr uint32_t kPortalStartRetryMs = 2000UL;
constexpr uint32_t kPortalHttpIdleTimeoutMs = 2000UL;
constexpr uint32_t kPortalHttpTotalTimeoutMs = 5000UL;
constexpr size_t kMaximumPortalHeaderBytes = 1024;
constexpr size_t kMaximumPortalBytesPerTick = 256;
constexpr char kNtpServer1[] = "time.cloudflare.com";
constexpr char kNtpServer2[] = "pool.ntp.org";
static_assert(config::kMaximumWifiAttemptsPerWindow ==
                  clockcore::WifiCandidateRanker::kCapacity,
              "Wi-Fi candidate capacity must match the per-window attempt cap");

bool deadlineReached(const uint32_t now, const uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

const char* httpReason(const int status) {
  switch (status) {
    case 200:
      return "OK";
    case 400:
      return "Bad Request";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 411:
      return "Length Required";
    case 413:
      return "Payload Too Large";
    case 415:
      return "Unsupported Media Type";
    case 431:
      return "Request Header Fields Too Large";
    case 503:
      return "Service Unavailable";
    default:
      return "Error";
  }
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
  if (hasConfirmedSync_ && clockcore::isValidEpoch(currentUtc)) {
    trustedBaselineUtc_ = currentUtc;
    trustedBaselineUs_ = static_cast<uint64_t>(esp_timer_get_time());
  }
  syncRoute_ = hasConfirmedSync ? syncRoute : SyncRoute::kUnselected;
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
  resetPortalHttpClient();
  const bool stopped = WiFi.softAPdisconnect(true);
  (void)stopped;
  CLOCK_DIAGNOSTIC_PRINTF("[WiFi] portal stop=%s\n",
                          stopped ? "ok" : "failed");
}

void NetworkTimeService::handlePortalRoot() {
  sendPortalHttpResponse(200, nullptr, true);
}

void NetworkTimeService::handlePortalTime(const uint8_t* body,
                                          const size_t length) {
  if (portalAccepted_) {
    // Treat a browser retry as successful without applying the update twice.
    sendPortalHttpResponse(200, "Clock already set");
    return;
  }
  int64_t epoch = 0;
  int16_t offset = 0;
  const int64_t trustedNow = trustedUtcNow();
  if (!clockcore::parsePortalTimeForm(body, length, epoch, offset) ||
      !clockcore::isAcceptableCorrection(
          hasConfirmedSync_ && clockcore::isValidEpoch(trustedNow),
          trustedNow, epoch)) {
    sendPortalHttpResponse(400, "Invalid time");
    return;
  }
  utcOffsetMinutes_ = offset;
  if (handler_ == nullptr) {
    sendPortalHttpResponse(503, "Clock unavailable");
    return;
  }
  handler_({epoch, utcOffsetMinutes_, TimeSource::kPortal});
  portalAccepted_ = true;
  sendPortalHttpResponse(200, "Clock set");
  // Give the captive browser time to receive the successful response.
  portalStopAtMs_ = millis() + 1500UL;
}

int64_t NetworkTimeService::trustedUtcNow() const {
  return clockcore::extrapolateMonotonicEpoch(
      trustedBaselineUtc_, trustedBaselineUs_,
      static_cast<uint64_t>(esp_timer_get_time()));
}

void NetworkTimeService::resetPortalHttpClient() {
  if (webClient_) {
    webClient_.stop();
  }
  webClient_ = WiFiClient();
  portalHttpState_ = PortalHttpState::kRequestLine;
  portalHttpHeaderBytes_ = 0;
  portalHttpLineLength_ = 0;
  portalHttpContentLength_ = 0;
  portalHttpBodyLength_ = 0;
  portalHttpPost_ = false;
  portalHttpHead_ = false;
  portalHttpContentLengthSeen_ = false;
  portalHttpContentTypeSeen_ = false;
  portalHttpContentTypeValid_ = false;
}

void NetworkTimeService::sendPortalHttpResponse(const int status,
                                                const char* body,
                                                const bool html) {
  if (!webClient_) {
    return;
  }
  const size_t bodyLength = html ? strlen_P(portalpage::kHtml)
                                 : (body == nullptr ? 0 : strlen(body));
  char header[256] = {};
  const int written = snprintf(
      header, sizeof(header),
      "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
      "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
      status, httpReason(status), html ? "text/html" : "text/plain",
      static_cast<unsigned>(bodyLength));
  if (written > 0 && static_cast<size_t>(written) < sizeof(header)) {
    webClient_.write(reinterpret_cast<const uint8_t*>(header),
                     static_cast<size_t>(written));
    if (!portalHttpHead_ && bodyLength > 0) {
      if (html) {
        webClient_.write_P(portalpage::kHtml, bodyLength);
      } else {
        webClient_.write(reinterpret_cast<const uint8_t*>(body), bodyLength);
      }
    }
  }
  resetPortalHttpClient();
}

bool NetworkTimeService::processPortalHttpLine() {
  portalHttpLine_[portalHttpLineLength_] = '\0';
  if (portalHttpState_ == PortalHttpState::kRequestLine) {
    char* firstSpace = strchr(portalHttpLine_, ' ');
    char* secondSpace = firstSpace == nullptr ? nullptr
                                               : strchr(firstSpace + 1, ' ');
    if (firstSpace == nullptr || secondSpace == nullptr ||
        strchr(secondSpace + 1, ' ') != nullptr) {
      sendPortalHttpResponse(400, "Invalid request");
      return false;
    }
    *firstSpace = '\0';
    *secondSpace = '\0';
    const char* method = portalHttpLine_;
    char* target = firstSpace + 1;
    const char* version = secondSpace + 1;
    if ((strcmp(version, "HTTP/1.1") != 0 &&
         strcmp(version, "HTTP/1.0") != 0) ||
        target[0] != '/') {
      sendPortalHttpResponse(400, "Invalid request");
      return false;
    }
    char* query = strchr(target, '?');
    if (query != nullptr) {
      *query = '\0';
    }
    portalHttpPost_ = strcmp(method, "POST") == 0;
    portalHttpHead_ = strcmp(method, "HEAD") == 0;
    if (!portalHttpPost_ && strcmp(method, "GET") != 0 &&
        !portalHttpHead_) {
      sendPortalHttpResponse(405, "Method not allowed");
      return false;
    }
    if (portalHttpPost_ && strcmp(target, "/set-time") != 0) {
      sendPortalHttpResponse(404, "Not found");
      return false;
    }
    portalHttpState_ = PortalHttpState::kHeaders;
    return true;
  }

  if (portalHttpState_ != PortalHttpState::kHeaders) {
    return false;
  }
  if (portalHttpLineLength_ == 0) {
    if (!portalHttpPost_) {
      handlePortalRoot();
      return false;
    }
    if (!portalHttpContentLengthSeen_) {
      sendPortalHttpResponse(411, "Content-Length required");
      return false;
    }
    if (!portalHttpContentTypeValid_) {
      sendPortalHttpResponse(415, "Unsupported media type");
      return false;
    }
    if (portalHttpContentLength_ == 0) {
      handlePortalTime(nullptr, 0);
      return false;
    }
    portalHttpState_ = PortalHttpState::kBody;
    return true;
  }

  char* colon = strchr(portalHttpLine_, ':');
  if (colon == nullptr || colon == portalHttpLine_) {
    sendPortalHttpResponse(400, "Invalid header");
    return false;
  }
  *colon = '\0';
  char* value = colon + 1;
  while (*value != '\0' && isspace(static_cast<unsigned char>(*value))) {
    ++value;
  }
  char* valueEnd = value + strlen(value);
  while (valueEnd > value &&
         isspace(static_cast<unsigned char>(valueEnd[-1]))) {
    *--valueEnd = '\0';
  }
  if (strcasecmp(portalHttpLine_, "Content-Length") == 0) {
    if (portalHttpContentLengthSeen_) {
      sendPortalHttpResponse(400, "Duplicate Content-Length");
      return false;
    }
    if (*value == '\0') {
      sendPortalHttpResponse(400, "Invalid Content-Length");
      return false;
    }
    size_t parsed = 0;
    for (const char* digit = value; *digit != '\0'; ++digit) {
      if (*digit < '0' || *digit > '9' ||
          parsed > (clockcore::kMaximumPortalTimeFormLength -
                    static_cast<size_t>(*digit - '0')) /
                       10U) {
        sendPortalHttpResponse(413, "Request too large");
        return false;
      }
      parsed = parsed * 10U + static_cast<size_t>(*digit - '0');
    }
    portalHttpContentLengthSeen_ = true;
    portalHttpContentLength_ = parsed;
  } else if (strcasecmp(portalHttpLine_, "Content-Type") == 0) {
    if (portalHttpContentTypeSeen_) {
      sendPortalHttpResponse(400, "Duplicate Content-Type");
      return false;
    }
    portalHttpContentTypeSeen_ = true;
    constexpr char kFormType[] = "application/x-www-form-urlencoded";
    portalHttpContentTypeValid_ =
        strncasecmp(value, kFormType, sizeof(kFormType) - 1U) == 0 &&
        (value[sizeof(kFormType) - 1U] == '\0' ||
         value[sizeof(kFormType) - 1U] == ';');
  } else if (strcasecmp(portalHttpLine_, "Transfer-Encoding") == 0) {
    sendPortalHttpResponse(400, "Transfer-Encoding is not supported");
    return false;
  }
  return true;
}

void NetworkTimeService::servicePortalHttp() {
  const uint32_t now = millis();
  if (!webClient_) {
    webClient_ = web_.available();
    if (!webClient_) {
      return;
    }
    portalHttpState_ = PortalHttpState::kRequestLine;
    portalHttpHeaderBytes_ = 0;
    portalHttpLineLength_ = 0;
    portalHttpContentLength_ = 0;
    portalHttpBodyLength_ = 0;
    portalHttpPost_ = false;
    portalHttpHead_ = false;
    portalHttpContentLengthSeen_ = false;
    portalHttpContentTypeSeen_ = false;
    portalHttpContentTypeValid_ = false;
    portalHttpStartedMs_ = now;
    portalHttpLastDataMs_ = now;
    webClient_.setTimeout(1);
  }

  if (clockcore::monotonicIntervalElapsed(
          now, portalHttpStartedMs_, kPortalHttpTotalTimeoutMs)) {
    resetPortalHttpClient();
    return;
  }

  size_t processed = 0;
  while (webClient_ && webClient_.available() > 0 &&
         processed++ < kMaximumPortalBytesPerTick) {
    const int incoming = webClient_.read();
    if (incoming < 0) {
      break;
    }
    portalHttpLastDataMs_ = millis();
    const uint8_t value = static_cast<uint8_t>(incoming);
    if (portalHttpState_ == PortalHttpState::kBody) {
      if (portalHttpBodyLength_ >= portalHttpContentLength_ ||
          portalHttpBodyLength_ >= sizeof(portalHttpBody_)) {
        sendPortalHttpResponse(413, "Request too large");
        break;
      }
      portalHttpBody_[portalHttpBodyLength_++] = value;
      if (portalHttpBodyLength_ == portalHttpContentLength_) {
        handlePortalTime(portalHttpBody_, portalHttpBodyLength_);
        break;
      }
      continue;
    }

    if (++portalHttpHeaderBytes_ > kMaximumPortalHeaderBytes) {
      sendPortalHttpResponse(431, "Headers too large");
      break;
    }
    if (value == '\n') {
      if (portalHttpLineLength_ == 0 ||
          portalHttpLine_[portalHttpLineLength_ - 1U] != '\r') {
        sendPortalHttpResponse(400, "Invalid line ending");
        break;
      }
      --portalHttpLineLength_;
      if (!processPortalHttpLine()) {
        break;
      }
      portalHttpLineLength_ = 0;
      continue;
    }
    if (value == '\0' || (value < 0x20U && value != '\r' && value != '\t') ||
        value == 0x7FU ||
        portalHttpLineLength_ + 1U >= sizeof(portalHttpLine_)) {
      sendPortalHttpResponse(400, "Invalid request");
      break;
    }
    portalHttpLine_[portalHttpLineLength_++] = static_cast<char>(value);
  }

  if (webClient_ &&
      ((!webClient_.connected() && webClient_.available() <= 0) ||
       clockcore::monotonicIntervalElapsed(
           millis(), portalHttpLastDataMs_, kPortalHttpIdleTimeoutMs) ||
       clockcore::monotonicIntervalElapsed(
           millis(), portalHttpStartedMs_, kPortalHttpTotalTimeoutMs))) {
    resetPortalHttpClient();
  }
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
  ntpSynced_.store(false);
  ntpBaselineEpoch_ = trustedUtcNow();
  if (!clockcore::isValidEpoch(ntpBaselineEpoch_)) {
    ntpBaselineEpoch_ = static_cast<int64_t>(time(nullptr));
  }
  ntpBaselineMs_ = millis();
  sntp_set_time_sync_notification_cb(ntpCallback);
  configTime(0, 0, kNtpServer1, kNtpServer2);
  mode_ = Mode::kWaitingForNtp;
  modeStartedMs_ = millis();
}

void NetworkTimeService::ntpCallback(struct timeval*) {
  ntpSynced_.store(true);
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
  ntpSynced_.store(false);
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
                 captivePortalAutofillReady_, ntpRetriedAfterPortal_,
                 wifiWindowExpired(millis())) ==
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
    ntpSynced_.store(false);
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
    const TimeSource source, const int16_t utcOffsetMinutes,
    const int64_t appliedUtc) {
  utcOffsetMinutes_ = utcOffsetMinutes;
  hasConfirmedSync_ = true;
  trustedBaselineUtc_ = appliedUtc;
  trustedBaselineUs_ = static_cast<uint64_t>(esp_timer_get_time());
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
      servicePortalHttp();
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
      } else if (ntpSynced_.exchange(false)) {
        finishNtp(true);
      } else if (now - modeStartedMs_ >= config::kNtpTimeoutMs) {
        finishNtp(false);
      }
      break;
    case Mode::kWaitingForPortalAutomation: {
      const bool windowExpired = wifiWindowExpired(now);
      const bool automationExpired =
          captiveportal::automationWindowExpired(
              now, modeStartedMs_, config::kCaptivePortalTimeoutMs);
      if (windowExpired || automationExpired) {
        const clockcore::WifiCandidate* candidate =
            candidateRanker_.at(activeCandidateIndex_);
        if (candidate != nullptr) {
          rememberFailure(candidate->bssid);
        }
        portalAutofill_.cancel(++networkGeneration_);
        if (captiveportal::actionAfterPortalTimeout(windowExpired) ==
            captiveportal::PortalTimeoutAction::kFinishWindow) {
          finishFailedAttempt();
        } else {
          WiFi.disconnect(false, false);
          connectNextCandidate();
        }
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
