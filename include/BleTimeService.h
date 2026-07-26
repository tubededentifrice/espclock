#pragma once

#include <NimBLEDevice.h>
#include <atomic>

#include "AppConfig.h"
#include "ClockTypes.h"

class TimeKeeper;

class BleTimeService {
 public:
  void begin(TimeUpdateHandler handler, bool hasConfirmedSync);
  void tick(const TimeKeeper& clock);
  bool connected() const { return connectionCount_.load() > 0; }
  bool advertising() const;
  bool pairingOpen() const;
  void requestSync();
  void reportTimeResult(bool accepted);
  void markTimeConfirmed() { hasConfirmedSync_.store(true); }
  void clearBonds();

 private:
  class ServerCallbacks;
  class TimeCallbacks;
  class StatusCallbacks;
  friend class ServerCallbacks;
  friend class TimeCallbacks;
  friend class StatusCallbacks;

  void onConnect(ble_gap_conn_desc* description);
  void onDisconnect(ble_gap_conn_desc* description);
  void onAuthenticationComplete(ble_gap_conn_desc* description);
  void onTimeWrite(NimBLECharacteristic* characteristic);
  void onStatusSubscribe(uint16_t subscriptionValue);
  void updateTimeValue(const TimeKeeper& clock, bool notify);
  bool transitionToSlowAdvertising();

  NimBLEServer* server_ = nullptr;
  NimBLECharacteristic* timeCharacteristic_ = nullptr;
  NimBLECharacteristic* statusCharacteristic_ = nullptr;
  TimeUpdateHandler handler_ = nullptr;
  std::atomic<uint8_t> connectionCount_{0};
  std::atomic<uint16_t> connectionHandle_{UINT16_MAX};
  uint32_t lastValueUpdateMs_ = 0;
  std::atomic<uint32_t> lastSyncRequestMs_{0};
  std::atomic<uint8_t> syncRequestRetries_{0};
  std::atomic_bool syncRequestPending_{false};
  std::atomic<uint32_t> lastAcceptedWriteMs_{0};
  uint32_t bootStartedMs_ = 0;
  uint32_t lastAdvertisingRecoveryMs_ = 0;
#if CLOCK_ENABLE_DIAGNOSTICS
  uint32_t lastPolicyRejectionLogMs_ = 0;
  uint16_t suppressedPolicyRejections_ = 0;
#endif
  uint8_t advertisingRecoveryAttempts_ = 0;
  bool pairingWindowStatusLogged_ = false;
  std::atomic_bool slowAdvertisingConfigured_{false};
#if CLOCK_ENABLE_DIAGNOSTICS
  bool policyRejectionLogged_ = false;
#endif
  bool policyDisconnectPending_ = false;
  std::atomic_bool timeWritePending_{false};
  std::atomic_bool activeConnectionBonded_{false};
  std::atomic_bool hasConfirmedSync_{false};
};
