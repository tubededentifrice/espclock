#pragma once

#include <atomic>

#include <DNSServer.h>
#include <WiFi.h>

#include "CaptivePortalAutofill.h"
#include "ClockCore.h"
#include "ClockTypes.h"

class NetworkTimeService {
 public:
  enum class Mode : uint8_t {
    kWaitingForBle,
    kPortal,
    kScanning,
    kConnecting,
    kWaitingForNtp,
    kWaitingForPortalAutomation,
    kIdle,
  };

  NetworkTimeService();
  void begin(TimeUpdateHandler handler, int16_t utcOffsetMinutes,
             bool hasConfirmedSync, SyncRoute syncRoute,
             int64_t lastSyncUtc, int64_t currentUtc);
  void tick(bool bleConnected);
  void onExternalTimeSync(TimeSource source, int16_t utcOffsetMinutes,
                          int64_t appliedUtc);
  Mode mode() const { return mode_; }
  bool portalActive() const { return mode_ == Mode::kPortal; }
  bool wifiBusy() const;
  bool setupPairingDisplayActive() const {
    return initialSelectionActive_ &&
           (mode_ == Mode::kWaitingForBle || mode_ == Mode::kPortal);
  }
  bool syncOverdue() const { return syncOverdue_; }
  bool takeBleSyncRequest();

 private:
  void startPortal(bool persistent);
  void stopPortal();
  void startScan();
  void processScan(int count);
  void connectNextCandidate();
  void startNtp();
  void finishNtp(bool success);
  void startPortalAutomation();
  void finishPortalAutomation(CaptivePortalAutofill::Result result);
  bool wifiWindowExpired(uint32_t now) const;
  void stopNetworkActivity();
  void armResync(uint32_t delayMs);
  void finishFailedAttempt();
  bool wasFailed(const uint8_t bssid[6]) const;
  void rememberFailure(const uint8_t bssid[6]);
  void handlePortalRoot();
  void handlePortalTime(const uint8_t* body, size_t length);
  void servicePortalHttp();
  void resetPortalHttpClient();
  bool processPortalHttpLine();
  void sendPortalHttpResponse(int status, const char* body,
                              bool html = false);
  int64_t trustedUtcNow() const;
  static void ntpCallback(struct timeval*);

  enum class PortalHttpState : uint8_t {
    kRequestLine,
    kHeaders,
    kBody,
  };

  DNSServer dns_;
  WiFiServer web_;
  WiFiClient webClient_;
  CaptivePortalAutofill portalAutofill_;
  TimeUpdateHandler handler_ = nullptr;
  Mode mode_ = Mode::kWaitingForBle;
  clockcore::WifiCandidateRanker candidateRanker_;
  clockcore::BssidAttemptTracker failedBssids_;
  uint32_t modeStartedMs_ = 0;
  uint32_t bootStartedMs_ = 0;
  uint32_t nextAttemptMs_ = 0;
  uint32_t portalStopAtMs_ = 0;
  uint32_t wifiWindowStartedMs_ = 0;
  uint32_t ntpBaselineMs_ = 0;
  uint32_t networkGeneration_ = 0;
  int64_t ntpBaselineEpoch_ = 0;
  int64_t trustedBaselineUtc_ = 0;
  uint64_t trustedBaselineUs_ = 0;
  int16_t utcOffsetMinutes_ = 0;
  SyncRoute syncRoute_ = SyncRoute::kUnselected;
  uint8_t wifiAttemptsThisWindow_ = 0;
  uint8_t nextCandidateIndex_ = 0;
  uint8_t activeCandidateIndex_ = UINT8_MAX;
  uint8_t portalStartAttempts_ = 0;
  bool portalAccepted_ = false;
  bool portalPersistent_ = false;
  bool scheduleAfterPortalResponse_ = false;
  bool wifiExhaustedForBoot_ = false;
  bool stationMacRandomized_ = false;
  bool hasConfirmedSync_ = false;
  bool initialSelectionActive_ = false;
  bool resyncDueArmed_ = false;
  bool syncOverdue_ = false;
  bool bleResyncGraceActive_ = false;
  bool bleSyncRequestPending_ = false;
  bool captivePortalAutofillReady_ = false;
  bool ntpRetriedAfterPortal_ = false;
  PortalHttpState portalHttpState_ = PortalHttpState::kRequestLine;
  uint32_t portalHttpStartedMs_ = 0;
  uint32_t portalHttpLastDataMs_ = 0;
  size_t portalHttpHeaderBytes_ = 0;
  size_t portalHttpLineLength_ = 0;
  size_t portalHttpContentLength_ = 0;
  size_t portalHttpBodyLength_ = 0;
  char portalHttpLine_[257] = {};
  uint8_t portalHttpBody_[clockcore::kMaximumPortalTimeFormLength] = {};
  bool portalHttpPost_ = false;
  bool portalHttpHead_ = false;
  bool portalHttpContentLengthSeen_ = false;
  bool portalHttpContentTypeSeen_ = false;
  bool portalHttpContentTypeValid_ = false;

  static std::atomic_bool ntpSynced_;
};
