#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <esp_system.h>

#include "ClockCore.h"

namespace {
constexpr char kServiceUuid[] = "7f510000-1b15-4dc7-9f3f-19b30a6f6a21";
constexpr char kTimeUuid[] = "7f510001-1b15-4dc7-9f3f-19b30a6f6a21";
constexpr char kStatusUuid[] = "7f510002-1b15-4dc7-9f3f-19b30a6f6a21";
constexpr uint32_t kHeartbeatIntervalMs = 2000UL;
constexpr uint32_t kTimeProperties =
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
    NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::NOTIFY;

NimBLEServer* server = nullptr;
NimBLECharacteristic* statusCharacteristic = nullptr;
std::atomic<uint8_t> connectionCount{0};
std::atomic_bool authenticated{false};
std::atomic_bool bleRoundTripPassed{false};
bool wifiApStarted = false;
bool wifiScanComplete = false;
int wifiNetworkCount = -1;
int wifiStrongestRssi = -127;
uint32_t lastHeartbeatMs = 0;
char bleName[24] = {};
char wifiName[32] = {};

void setStatus(const char* status, const bool notify) {
  statusCharacteristic->setValue(
      reinterpret_cast<const uint8_t*>(status), std::strlen(status));
  if (notify && connectionCount.load() > 0) {
    statusCharacteristic->notify();
  }
}

class ServerCallbacks final : public NimBLEServerCallbacks {
 public:
  void onConnect(NimBLEServer*, ble_gap_conn_desc* description) override {
    ++connectionCount;
    const bool encrypted =
        description != nullptr && description->sec_state.encrypted;
    const bool bonded =
        description != nullptr && description->sec_state.bonded;
    authenticated.store(encrypted && bonded);
    Serial.printf(
        "ESP_DIAG event=ble_connected encrypted=%s bonded=%s\n",
        encrypted ? "yes" : "no", bonded ? "yes" : "no");
    if (description != nullptr && !encrypted) {
      NimBLEDevice::startSecurity(description->conn_handle);
    }
  }

  void onAuthenticationComplete(ble_gap_conn_desc* description) override {
    const bool passed =
        description != nullptr && description->sec_state.encrypted &&
        description->sec_state.bonded;
    authenticated.store(passed);
    Serial.printf("ESP_DIAG event=ble_authenticated result=%s\n",
                  passed ? "pass" : "fail");
    if (!passed && description != nullptr) {
      server->disconnect(description->conn_handle);
      return;
    }
    setStatus("sync-request", true);
  }

  void onDisconnect(NimBLEServer*, ble_gap_conn_desc*) override {
    if (connectionCount.load() > 0) {
      --connectionCount;
    }
    authenticated.store(false);
    const bool restarted = NimBLEDevice::startAdvertising();
    Serial.printf("ESP_DIAG event=ble_disconnected adv_restart=%s\n",
                  restarted ? "yes" : "no");
  }
};

class TimeCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic* characteristic) override {
    const std::string value = characteristic->getValue();
    int64_t epoch = 0;
    int16_t utcOffsetMinutes = 0;
    const bool validPacket =
        authenticated.load() &&
        clockcore::parseTimeSyncPayload(
            reinterpret_cast<const uint8_t*>(value.data()), value.size(),
            epoch, utcOffsetMinutes);
    bleRoundTripPassed.store(validPacket);
    setStatus(validPacket ? "time-accepted" : "time-rejected", true);
    Serial.printf(
        "ESP_DIAG event=ble_time_write authenticated=%s bytes=%u result=%s\n",
        authenticated.load() ? "yes" : "no",
        static_cast<unsigned>(value.size()),
        validPacket ? "pass" : "fail");
  }
};

class StatusCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  void onSubscribe(NimBLECharacteristic*, ble_gap_conn_desc*,
                   uint16_t subscriptionValue) override {
    if ((subscriptionValue & 0x01U) != 0U) {
      setStatus("sync-request", true);
      Serial.println("ESP_DIAG event=ble_status_subscribed result=pass");
    }
  }
};

void startBle() {
  NimBLEDevice::init(bleName);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  NimBLEService* service = server->createService(kServiceUuid);
  NimBLECharacteristic* timeCharacteristic =
      service->createCharacteristic(kTimeUuid, kTimeProperties);
  statusCharacteristic = service->createCharacteristic(
      kStatusUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  timeCharacteristic->setCallbacks(new TimeCallbacks());
  statusCharacteristic->setCallbacks(new StatusCallbacks());
  setStatus("time-needed", false);
  service->start();

  NimBLEAdvertisementData advertisementData;
  advertisementData.setFlags(BLE_HS_ADV_F_DISC_GEN |
                             BLE_HS_ADV_F_BREDR_UNSUP);
  advertisementData.setCompleteServices(NimBLEUUID(kServiceUuid));
  advertisementData.setPreferredParams(120, 240);
  NimBLEAdvertisementData scanResponseData;
  scanResponseData.setName(bleName);

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setAdvertisementData(advertisementData);
  advertising->setScanResponseData(scanResponseData);
  advertising->setScanResponse(true);
  advertising->setMinInterval(48);  // 30 ms in 0.625 ms units.
  advertising->setMaxInterval(96);  // 60 ms in 0.625 ms units.
  const bool started = advertising->start();
  Serial.printf(
      "ESP_DIAG event=ble_ready started=%s name=%s service=%s\n",
      started ? "yes" : "no", bleName, kServiceUuid);
}

void scanWifi() {
  wifiNetworkCount = WiFi.scanNetworks(false, true);
  wifiStrongestRssi = -127;
  if (wifiNetworkCount > 0) {
    for (int index = 0; index < wifiNetworkCount; ++index) {
      wifiStrongestRssi =
          std::max(wifiStrongestRssi, static_cast<int>(WiFi.RSSI(index)));
    }
  }
  WiFi.scanDelete();
  wifiScanComplete = true;
  Serial.printf(
      "ESP_DIAG event=wifi_scan complete=yes networks=%d strongest_rssi=%d\n",
      wifiNetworkCount, wifiStrongestRssi);
}

void printHeartbeat() {
  Serial.printf(
      "ESP_DIAG event=heartbeat uptime_ms=%lu free_heap=%u wifi_ap=%s "
      "wifi_stations=%u wifi_scan_complete=%s wifi_networks=%d "
      "ble_advertising=%s ble_connections=%u ble_authenticated=%s "
      "ble_round_trip=%s\n",
      static_cast<unsigned long>(millis()),
      static_cast<unsigned>(ESP.getFreeHeap()),
      wifiApStarted ? "yes" : "no",
      static_cast<unsigned>(WiFi.softAPgetStationNum()),
      wifiScanComplete ? "yes" : "no", wifiNetworkCount,
      NimBLEDevice::getAdvertising()->isAdvertising() ? "yes" : "no",
      static_cast<unsigned>(connectionCount.load()),
      authenticated.load() ? "yes" : "no",
      bleRoundTripPassed.load() ? "yes" : "no");
}
}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t serialWaitStartedMs = millis();
  while (!Serial && millis() - serialWaitStartedMs < 3000UL) {
    delay(10);
  }
  delay(250);

  const uint64_t chipId = ESP.getEfuseMac();
  snprintf(bleName, sizeof(bleName), "ESPClock-%04X",
           static_cast<unsigned>(chipId & 0xFFFFU));
  snprintf(wifiName, sizeof(wifiName), "ESPClock-RadioTest-%04X",
           static_cast<unsigned>(chipId & 0xFFFFU));

  Serial.printf(
      "ESP_DIAG event=boot result=pass reset_reason=%d chip_model=%s "
      "revision=%u cpu_mhz=%u\n",
      static_cast<int>(esp_reset_reason()), ESP.getChipModel(),
      static_cast<unsigned>(ESP.getChipRevision()),
      static_cast<unsigned>(ESP.getCpuFreqMHz()));

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  wifiApStarted = WiFi.softAP(wifiName, nullptr, 1, false, 1);
  Serial.printf(
      "ESP_DIAG event=wifi_ap started=%s ssid=%s channel=1 ip=%s\n",
      wifiApStarted ? "yes" : "no", wifiName,
      WiFi.softAPIP().toString().c_str());

  startBle();
  scanWifi();
  printHeartbeat();
  lastHeartbeatMs = millis();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastHeartbeatMs >= kHeartbeatIntervalMs) {
    lastHeartbeatMs = now;
    printHeartbeat();
  }
  delay(10);
}
