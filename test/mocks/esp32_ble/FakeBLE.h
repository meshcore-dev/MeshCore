#pragma once
#include "Arduino.h"
#include <vector>

struct esp_ble_auth_cmpl_t { bool success; uint8_t fail_reason; };
struct esp_ble_gatts_cb_param_t {
  struct { uint16_t conn_id; } connect, mtu;
  struct { uint16_t conn_id; uint8_t reason; } disconnect;
};
constexpr int ESP_LE_AUTH_REQ_SC_MITM_BOND = 1;
constexpr int ESP_GATT_PERM_READ_ENC_MITM = 1;
constexpr int ESP_GATT_PERM_WRITE_ENC_MITM = 2;
class BLECharacteristic;
class BLEServer;
class BLESecurityCallbacks {
public:
  virtual ~BLESecurityCallbacks() = default;
  virtual uint32_t onPassKeyRequest() { return 0; }
  virtual void onPassKeyNotify(uint32_t) {}
  virtual bool onConfirmPIN(uint32_t) { return true; }
  virtual bool onSecurityRequest() { return true; }
  virtual void onAuthenticationComplete(esp_ble_auth_cmpl_t) {}
};
class BLEServerCallbacks {
public:
  virtual ~BLEServerCallbacks() = default;
  virtual void onConnect(BLEServer*) {}
  virtual void onConnect(BLEServer*, esp_ble_gatts_cb_param_t*) {}
  virtual void onDisconnect(BLEServer*) {}
  virtual void onDisconnect(BLEServer*, esp_ble_gatts_cb_param_t*) {}
  virtual void onMtuChanged(BLEServer*, esp_ble_gatts_cb_param_t*) {}
};
class BLECharacteristicCallbacks {
public:
  enum Status { SUCCESS_NOTIFY, ERROR_GATT, ERROR_NOTIFY_DISABLED, ERROR_NO_CLIENT };
  virtual ~BLECharacteristicCallbacks() = default;
  virtual void onWrite(BLECharacteristic*, esp_ble_gatts_cb_param_t*) {}
  virtual void onStatus(BLECharacteristic*, Status, uint32_t) {}
};
class BLE2902 {};
class BLECharacteristic {
public:
  enum { PROPERTY_READ=1, PROPERTY_NOTIFY=2, PROPERTY_WRITE=4 };
  BLECharacteristicCallbacks* callbacks = nullptr;
  BLECharacteristicCallbacks::Status result = BLECharacteristicCallbacks::SUCCESS_NOTIFY;
  bool report_status = true;
  unsigned attempts = 0;
  std::vector<uint8_t> value;
  std::vector<std::vector<uint8_t>> sent;
  void setCallbacks(BLECharacteristicCallbacks* cb) { callbacks = cb; }
  void setAccessPermissions(int) {}
  void addDescriptor(BLE2902* descriptor) { delete descriptor; }
  void setValue(const uint8_t* data, size_t len) { value.assign(data, data + len); }
  uint8_t* getData() { return value.data(); }
  size_t getLength() { return value.size(); }
  void notify() {
    attempts++;
    if (result == BLECharacteristicCallbacks::SUCCESS_NOTIFY && report_status) sent.push_back(value);
    // Arduino-ESP32 Bluedroid invokes onStatus inline from notify().
    if (callbacks && report_status) callbacks->onStatus(this, result, 0);
  }
};
class BLEService {
public:
  BLECharacteristic tx, rx;
  BLECharacteristic* createCharacteristic(const char*, int properties) {
    return properties & BLECharacteristic::PROPERTY_NOTIFY ? &tx : &rx;
  }
  void start() {}
  void stop() {}
};
class BLEAdvertising {
public:
  void addServiceUUID(const char*) {}
  void start() {}
  void stop() {}
};
class BLEServer {
public:
  BLEService service;
  BLEAdvertising advertising;
  BLEServerCallbacks* callbacks = nullptr;
  void setCallbacks(BLEServerCallbacks* cb) { callbacks = cb; }
  BLEService* createService(const char*) { return &service; }
  BLEAdvertising* getAdvertising() { return &advertising; }
  uint16_t getPeerMTU(uint16_t) { return 176; }
  uint16_t getConnId() { return 0; }
  void disconnect(uint16_t) {}
  unsigned getConnectedCount() { return 0; }
};
class BLEDevice {
public:
  static inline BLEServer server;
  static void init(const char*) { server = BLEServer(); }
  static void setSecurityCallbacks(BLESecurityCallbacks*) {}
  static void setMTU(int) {}
  static BLEServer* createServer() { return &server; }
};
class BLESecurity {
public:
  void setStaticPIN(uint32_t) {}
  void setAuthenticationMode(int) {}
};
