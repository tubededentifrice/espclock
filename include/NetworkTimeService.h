#pragma once

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

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
    kIdle,
  };

  NetworkTimeService();
  void begin(TimeUpdateHandler handler, int16_t utcOffsetMinutes,
             bool hasConfirmedSync, SyncRoute syncRoute,
             int64_t lastSyncUtc, int64_t currentUtc);
  void tick(bool bleConnected);
  void onExternalTimeSync(TimeSource source, int16_t utcOffsetMinutes);
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
  bool wifiWindowExpired(uint32_t now) const;
  void stopNetworkActivity();
  void armResync(uint32_t delayMs);
  void finishFailedAttempt();
  bool wasFailed(const uint8_t bssid[6]) const;
  void rememberFailure(const uint8_t bssid[6]);
  void handlePortalRoot();
  void handlePortalTime();
  static void ntpCallback(struct timeval*);

  DNSServer dns_;
  WebServer web_;
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
  int64_t ntpBaselineEpoch_ = 0;
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

  static NetworkTimeService* instance_;
  static volatile bool ntpSynced_;
};
