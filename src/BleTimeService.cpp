#include "BleTimeService.h"

#include <Arduino.h>
#include <cstring>

#include "AppConfig.h"
#include "ClockCore.h"
#include "TimeKeeper.h"

namespace {
// Custom service payload is documented in ClockCore.h. Using a custom
// characteristic is intentional: the standard Current Time Service models the
// clock as the time server and does not carry a UTC offset.
constexpr char kServiceUuid[] = "7f510000-1b15-4dc7-9f3f-19b30a6f6a21";
constexpr char kTimeUuid[] = "7f510001-1b15-4dc7-9f3f-19b30a6f6a21";
constexpr char kStatusUuid[] = "7f510002-1b15-4dc7-9f3f-19b30a6f6a21";
constexpr uint32_t kSyncRequestRetryMs = 30000UL;
constexpr uint8_t kMaximumSyncRequestRetries = 2;
constexpr uint32_t kAdvertisingRecoveryIntervalMs = 5000UL;
constexpr uint8_t kMaximumAdvertisingRecoveryAttempts = 3;
constexpr uint32_t kGapFailureLogIntervalMs = 2000UL;
constexpr uint32_t kPolicyRejectionLogIntervalMs = 5000UL;
constexpr uint32_t kTimeCharacteristicProperties =
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
    NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::NOTIFY;

constexpr uint16_t advertisingIntervalUnits(const uint32_t milliseconds) {
  return static_cast<uint16_t>((milliseconds * 1000UL) / 625UL);
}

struct BoundedGapLog {
  uint32_t lastLogMs = 0;
  uint16_t suppressed = 0;
  bool hasLogged = false;
};

BoundedGapLog disconnectLog;
BoundedGapLog connectionFailureLog;

static_assert(
    (kTimeCharacteristicProperties & NIMBLE_PROPERTY::WRITE) != 0 &&
        (kTimeCharacteristicProperties & NIMBLE_PROPERTY::WRITE_ENC) != 0,
    "The time characteristic must advertise writes and require encryption");

void setStatusValue(NimBLECharacteristic* characteristic,
                    const char* status) {
  characteristic->setValue(
      reinterpret_cast<const uint8_t*>(status), std::strlen(status));
}

void logGapFailure(BoundedGapLog& log, const char* event,
                   const int reason) {
  const uint32_t now = millis();
  if (!log.hasLogged ||
      now - log.lastLogMs >= kGapFailureLogIntervalMs) {
    Serial.printf("[BLE] %s reason=%d (%s) suppressed=%u\n", event,
                  reason, NimBLEUtils::returnCodeToString(reason),
                  log.suppressed);
    log.lastLogMs = now;
    log.suppressed = 0;
    log.hasLogged = true;
  } else if (log.suppressed < UINT16_MAX) {
    ++log.suppressed;
  }
}

int logGapEvent(ble_gap_event* event, void*) {
  if (event == nullptr) {
    return 0;
  }
  if (event->type == BLE_GAP_EVENT_DISCONNECT) {
    logGapFailure(disconnectLog, "disconnect",
                  event->disconnect.reason);
  } else if (event->type == BLE_GAP_EVENT_CONNECT &&
             event->connect.status != 0) {
    logGapFailure(connectionFailureLog, "connection attempt failed",
                  event->connect.status);
  }
  return 0;
}
}  // namespace

class BleTimeService::ServerCallbacks : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(BleTimeService& owner) : owner_(owner) {}

  void onConnect(NimBLEServer*, ble_gap_conn_desc* description) override {
    owner_.onConnect(description);
  }

  void onDisconnect(NimBLEServer*, ble_gap_conn_desc* description) override {
    owner_.onDisconnect(description);
  }

  void onAuthenticationComplete(ble_gap_conn_desc* description) override {
    owner_.onAuthenticationComplete(description);
  }

 private:
  BleTimeService& owner_;
};

class BleTimeService::TimeCallbacks : public NimBLECharacteristicCallbacks {
 public:
  explicit TimeCallbacks(BleTimeService& owner) : owner_(owner) {}

  void onWrite(NimBLECharacteristic* characteristic) override {
    owner_.onTimeWrite(characteristic);
  }

 private:
  BleTimeService& owner_;
};

class BleTimeService::StatusCallbacks : public NimBLECharacteristicCallbacks {
 public:
  explicit StatusCallbacks(BleTimeService& owner) : owner_(owner) {}

  void onSubscribe(NimBLECharacteristic*, ble_gap_conn_desc*,
                   uint16_t subscriptionValue) override {
    owner_.onStatusSubscribe(subscriptionValue);
  }

 private:
  BleTimeService& owner_;
};

void BleTimeService::begin(const TimeUpdateHandler handler,
                           const bool hasConfirmedSync) {
  handler_ = handler;
  hasConfirmedSync_ = hasConfirmedSync;
  bootStartedMs_ = millis();

  const uint64_t chipId = ESP.getEfuseMac();
  char deviceName[24] = {};
  snprintf(deviceName, sizeof(deviceName), "KidsClock-%04X",
           static_cast<unsigned>(chipId & 0xFFFFU));

  NimBLEDevice::init(deviceName);
  NimBLEDevice::setCustomGapHandler(logGapEvent);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  // Just Works + bonding: no PIN or button is required. This encrypts the radio
  // link but, without an input/display confirmation, does not provide MITM
  // authentication.
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  server_ = NimBLEDevice::createServer();
  server_->setCallbacks(new ServerCallbacks(*this));
  NimBLEService* service = server_->createService(kServiceUuid);
  timeCharacteristic_ = service->createCharacteristic(
      kTimeUuid, kTimeCharacteristicProperties);
  statusCharacteristic_ = service->createCharacteristic(
      kStatusUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  timeCharacteristic_->setCallbacks(new TimeCallbacks(*this));
  statusCharacteristic_->setCallbacks(new StatusCallbacks(*this));
  setStatusValue(statusCharacteristic_, "time-needed");
  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advertisementData;
  advertisementData.setFlags(BLE_HS_ADV_F_DISC_GEN |
                             BLE_HS_ADV_F_BREDR_UNSUP);
  advertisementData.setCompleteServices(NimBLEUUID(kServiceUuid));
  advertisementData.setPreferredParams(120, 240);
  NimBLEAdvertisementData scanResponseData;
  scanResponseData.setName(deviceName);
  const size_t advertisementBytes =
      advertisementData.getPayload().size();
  const size_t scanResponseBytes = scanResponseData.getPayload().size();
  advertising->setAdvertisementData(advertisementData);
  advertising->setScanResponseData(scanResponseData);
  advertising->setScanResponse(true);
  advertising->setMinInterval(
      advertisingIntervalUnits(config::kBleFastAdvertisingMinMs));
  advertising->setMaxInterval(
      advertisingIntervalUnits(config::kBleFastAdvertisingMaxMs));
  const bool advertisingStarted = advertising->start();
  Serial.printf(
      "[BLE] advertising start=%s name=%s service=%s adv_bytes=%u "
      "scan_response_bytes=%u connectable=yes bondable=yes "
      "pairing_window_ms=%lu interval_ms=%lu-%lu\n",
      advertisingStarted ? "ok" : "failed", deviceName, kServiceUuid,
      static_cast<unsigned>(advertisementBytes),
      static_cast<unsigned>(scanResponseBytes),
      static_cast<unsigned long>(config::kNewBlePairingWindowMs),
      static_cast<unsigned long>(config::kBleFastAdvertisingMinMs),
      static_cast<unsigned long>(config::kBleFastAdvertisingMaxMs));
}

bool BleTimeService::advertising() const {
  return NimBLEDevice::getAdvertising()->isAdvertising();
}

bool BleTimeService::pairingOpen() const {
  return millis() - bootStartedMs_ < config::kNewBlePairingWindowMs;
}

void BleTimeService::onConnect(ble_gap_conn_desc* description) {
  ++connectionCount_;
  const bool encrypted =
      description != nullptr && description->sec_state.encrypted;
  const bool bonded =
      description != nullptr && description->sec_state.bonded;
  activeConnectionBonded_.store(encrypted && bonded);
  if (description != nullptr) {
    connectionHandle_.store(description->conn_handle);
  }
  const bool knownBond =
      description != nullptr &&
      NimBLEDevice::isBonded(NimBLEAddress(description->peer_id_addr));
  const bool rejectOutsidePairingWindow =
      description != nullptr && !encrypted && !knownBond &&
      !pairingOpen();
  if (rejectOutsidePairingWindow) {
    const uint32_t now = millis();
    if (!policyRejectionLogged_ ||
        now - lastPolicyRejectionLogMs_ >=
            kPolicyRejectionLogIntervalMs) {
      Serial.printf(
          "[BLE] rejected unbonded connection outside pairing window "
          "suppressed=%u\n",
          suppressedPolicyRejections_);
      lastPolicyRejectionLogMs_ = now;
      suppressedPolicyRejections_ = 0;
      policyRejectionLogged_ = true;
    } else if (suppressedPolicyRejections_ < UINT16_MAX) {
      ++suppressedPolicyRejections_;
    }
    const int disconnectResult =
        server_->disconnect(description->conn_handle);
    policyDisconnectPending_ = disconnectResult == 0;
    if (disconnectResult != 0) {
      Serial.printf(
          "[BLE] policy disconnect failed reason=%d (%s)\n",
          disconnectResult,
          NimBLEUtils::returnCodeToString(disconnectResult));
    }
    return;
  }
  Serial.printf(
      "[BLE] connected handle=%u encrypted=%s bonded=%s known_bond=%s "
      "pairing_open=%s\n",
      description != nullptr ? description->conn_handle : UINT16_MAX,
      encrypted ? "yes" : "no", bonded ? "yes" : "no",
      knownBond ? "yes" : "no", pairingOpen() ? "yes" : "no");
  if (description != nullptr && !encrypted) {
    // Make room deterministically for another family phone. Bond storage is
    // finite; with no UI for choosing a phone, evicting the first enumerated
    // bond is preferable to making normal onboarding require a firmware erase.
    if (!knownBond &&
        NimBLEDevice::getNumBonds() >= CONFIG_BT_NIMBLE_MAX_BONDS) {
      NimBLEDevice::deleteBond(NimBLEDevice::getBondedAddress(0));
    }
    Serial.printf("[BLE] authentication requested handle=%u\n",
                  description->conn_handle);
    NimBLEDevice::startSecurity(description->conn_handle);
  }
  requestSync();
}

void BleTimeService::onAuthenticationComplete(
    ble_gap_conn_desc* description) {
  if (description == nullptr || !description->sec_state.encrypted ||
      !description->sec_state.bonded) {
    activeConnectionBonded_.store(false);
    Serial.println("[BLE] authentication failed; disconnecting");
    if (description != nullptr) {
      server_->disconnect(description->conn_handle);
    }
    return;
  }
  activeConnectionBonded_.store(true);
  Serial.printf("[BLE] authentication complete handle=%u bonded=yes\n",
                description->conn_handle);
  requestSync();
}

void BleTimeService::onDisconnect(ble_gap_conn_desc* description) {
  const bool policyDisconnect = policyDisconnectPending_;
  policyDisconnectPending_ = false;
  if (!policyDisconnect) {
    Serial.printf("[BLE] disconnected handle=%u; restarting advertising\n",
                  description != nullptr ? description->conn_handle
                                         : UINT16_MAX);
  }
  if (connectionCount_ > 0) {
    --connectionCount_;
  }
  connectionHandle_.store(UINT16_MAX);
  activeConnectionBonded_.store(false);
  syncRequestPending_.store(false);
  if (!pairingOpen() && !slowAdvertisingConfigured_.load()) {
    // The main task owns advertising-parameter changes. It will transition and
    // restart within one main-loop interval, avoiding concurrent mutation of
    // NimBLE advertising state from this callback.
    return;
  }
  const bool restarted = NimBLEDevice::startAdvertising();
  if (restarted) {
    advertisingRecoveryAttempts_ = 0;
  }
  if (!policyDisconnect || !restarted) {
    Serial.printf("[BLE] advertising restart=%s\n",
                  restarted ? "ok" : "failed");
  }
}

void BleTimeService::onTimeWrite(NimBLECharacteristic* characteristic) {
  syncRequestPending_.store(false);
  if (!activeConnectionBonded_.load()) {
    setStatusValue(statusCharacteristic_, "time-rejected");
    statusCharacteristic_->notify();
    return;
  }
  const uint32_t now = millis();
  const uint32_t lastAccepted = lastAcceptedWriteMs_.load();
  if (timeWritePending_.load() ||
      (lastAccepted != 0 && now - lastAccepted < 30000UL)) {
    setStatusValue(statusCharacteristic_, "rate-limited");
    statusCharacteristic_->notify();
    return;
  }
  const std::string value = characteristic->getValue();
  int64_t epoch = 0;
  int16_t offset = 0;
  if (!clockcore::parseTimeSyncPayload(
          reinterpret_cast<const uint8_t*>(value.data()), value.size(),
          epoch, offset) ||
      !clockcore::isAcceptableCorrection(
          hasConfirmedSync_.load(), static_cast<int64_t>(time(nullptr)),
          epoch)) {
    setStatusValue(statusCharacteristic_, "invalid-time");
    statusCharacteristic_->notify();
    return;
  }
  if (handler_ == nullptr) {
    setStatusValue(statusCharacteristic_, "time-rejected");
    statusCharacteristic_->notify();
    return;
  }
  // Publish the pending state before making the update visible to the main
  // task. Otherwise the main loop could apply and acknowledge the value before
  // this callback sets timeWritePending_, leaving the client stuck.
  timeWritePending_.store(true);
  setStatusValue(statusCharacteristic_, "time-pending");
  statusCharacteristic_->notify();
  handler_({epoch, offset, TimeSource::kBle});
}

void BleTimeService::onStatusSubscribe(const uint16_t subscriptionValue) {
  if ((subscriptionValue & 0x01U) != 0U) {
    // Connection/authentication normally happens before a central can enable
    // notifications. Re-issuing the request here places an actionable status
    // after subscription is active; bounded retries cover a lost notification.
    requestSync();
  }
}

void BleTimeService::clearBonds() {
  NimBLEDevice::deleteAllBonds();
}

void BleTimeService::reportTimeResult(const bool accepted) {
  if (!timeWritePending_.load()) {
    return;
  }
  timeWritePending_.store(false);
  if (accepted) {
    lastAcceptedWriteMs_.store(millis());
    const uint16_t handle = connectionHandle_.load();
    if (handle != UINT16_MAX) {
      // Units: interval 1.25 ms, latency connection events, timeout 10 ms.
      // 150-300 ms with latency 4 and a 6 s timeout keeps the six-hour
      // notification connection inexpensive while satisfying Apple's limits.
      server_->updateConnParams(handle, 120, 240, 4, 600);
    }
  }
  setStatusValue(
      statusCharacteristic_,
      accepted ? "time-accepted" : "time-rejected");
  if (connected()) {
    statusCharacteristic_->notify();
  }
}

void BleTimeService::updateTimeValue(const TimeKeeper& clock,
                                     const bool notify) {
  uint8_t packet[12] = {1};
  uint64_t epoch = static_cast<uint64_t>(clock.utcNow());
  for (uint8_t i = 0; i < 8; ++i) {
    packet[i + 1] = static_cast<uint8_t>(epoch >> (8U * i));
  }
  const uint16_t offset = static_cast<uint16_t>(clock.utcOffsetMinutes());
  packet[9] = static_cast<uint8_t>(offset);
  packet[10] = static_cast<uint8_t>(offset >> 8U);
  packet[11] = clock.hasValidTime() ? 1 : 0;
  timeCharacteristic_->setValue(packet, sizeof(packet));
  if (notify && connected()) {
    timeCharacteristic_->notify();
  }
}

void BleTimeService::requestSync() {
  lastSyncRequestMs_.store(millis());
  syncRequestRetries_.store(0);
  syncRequestPending_.store(true);
  setStatusValue(statusCharacteristic_, "sync-request");
  if (connected()) {
    statusCharacteristic_->notify();
  }
}

bool BleTimeService::transitionToSlowAdvertising() {
  if (slowAdvertisingConfigured_.load()) {
    return true;
  }
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (advertising->isAdvertising() && !advertising->stop()) {
    return false;
  }
  advertising->setMinInterval(
      advertisingIntervalUnits(config::kBleSlowAdvertisingMinMs));
  advertising->setMaxInterval(
      advertisingIntervalUnits(config::kBleSlowAdvertisingMaxMs));
  slowAdvertisingConfigured_.store(true);
  const bool shouldAdvertise = !connected();
  const bool restarted = !shouldAdvertise || advertising->start();
  Serial.printf(
      "[BLE] advertising duty=slow interval_ms=%lu-%lu restart=%s\n",
      static_cast<unsigned long>(config::kBleSlowAdvertisingMinMs),
      static_cast<unsigned long>(config::kBleSlowAdvertisingMaxMs),
      restarted ? "ok" : "failed");
  return true;
}

void BleTimeService::tick(const TimeKeeper& clock) {
  const uint32_t now = millis();
  if (!pairingWindowStatusLogged_ &&
      now - bootStartedMs_ >= config::kNewBlePairingWindowMs &&
      transitionToSlowAdvertising()) {
    pairingWindowStatusLogged_ = true;
    Serial.printf("[BLE] pairing window closed advertising=%s connected=%s\n",
                  advertising() ? "active" : "inactive",
                  connected() ? "yes" : "no");
  }
  if (!connected() && !advertising() &&
      advertisingRecoveryAttempts_ <
          kMaximumAdvertisingRecoveryAttempts &&
      now - lastAdvertisingRecoveryMs_ >=
          kAdvertisingRecoveryIntervalMs) {
    lastAdvertisingRecoveryMs_ = now;
    ++advertisingRecoveryAttempts_;
    const bool restarted = NimBLEDevice::startAdvertising();
    Serial.printf("[BLE] advertising recovery=%s attempt=%u/%u\n",
                  restarted ? "ok" : "failed",
                  advertisingRecoveryAttempts_,
                  kMaximumAdvertisingRecoveryAttempts);
    if (restarted) {
      advertisingRecoveryAttempts_ = 0;
    }
  }
  if (now - lastValueUpdateMs_ >= 60000UL || lastValueUpdateMs_ == 0) {
    lastValueUpdateMs_ = now;
    updateTimeValue(clock, false);
  }
  if (connected() &&
      syncRequestPending_.load() &&
      now - lastSyncRequestMs_.load() >= kSyncRequestRetryMs) {
    const uint8_t retries = syncRequestRetries_.load();
    if (retries < kMaximumSyncRequestRetries) {
      syncRequestRetries_.store(retries + 1);
      lastSyncRequestMs_.store(now);
      setStatusValue(statusCharacteristic_, "sync-request");
      statusCharacteristic_->notify();
    } else {
      syncRequestPending_.store(false);
    }
  } else if (connected() && !syncRequestPending_.load() &&
             now - lastSyncRequestMs_.load() >=
                 config::kResyncIntervalMs) {
    requestSync();
  }
}
