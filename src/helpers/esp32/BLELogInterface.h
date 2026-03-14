#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Nordic UART Service UUIDs (standard, recognised by nRF Connect and bleak)
#define NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define BLE_LOG_ADVERT_RESTART_DELAY_MS  1000

/**
 * Unsecured BLE UART (Nordic UART Service) logger for ESP32 platforms.
 * Implements Print so it can be passed to Dispatcher::setPacketLogStream().
 * Any BLE NUS client (e.g. nRF Connect, a Raspberry Pi running bleak) can
 * connect without pairing and receive the log stream as plain text.
 *
 * Lines are buffered and flushed to BLE on each newline character.
 * Call loop() from the Arduino loop() to handle advertising restart on disconnect.
 */
class BLELogInterface : public Print, BLEServerCallbacks {
  BLEServer*          _server;
  BLECharacteristic*  _tx_char;
  bool                _connected;
  unsigned long       _adv_restart_time;
  char                _line_buf[256];
  int                 _line_len;

  void flushLine() {
    if (_line_len > 0 && _connected) {
      // BLE notifications are capped at ATT_MTU-3 bytes; MTU negotiation is not
      // guaranteed, so always chunk at 20 bytes (the safe minimum) to ensure the
      // full line is delivered regardless of the negotiated MTU.
      const int CHUNK = 20;
      int offset = 0;
      while (offset < _line_len) {
        int n = _line_len - offset;
        if (n > CHUNK) n = CHUNK;
        _tx_char->setValue((uint8_t*)_line_buf + offset, n);
        _tx_char->notify();
        offset += n;
      }
    }
    _line_len = 0;
  }

  void onConnect(BLEServer* pServer) override {
    _connected = true;
  }

  void onDisconnect(BLEServer* pServer) override {
    _connected = false;
    _line_len = 0;  // discard partial line
    _adv_restart_time = millis() + BLE_LOG_ADVERT_RESTART_DELAY_MS;
  }

public:
  BLELogInterface()
    : _server(nullptr), _tx_char(nullptr),
      _connected(false), _adv_restart_time(0), _line_len(0) {}

  void begin(const char* device_name) {
    BLEDevice::init(device_name);
    BLEDevice::setMTU(256);  // default 23-byte MTU caps notifications at 20 bytes; request more

    // Explicitly disable bonding so the ESP32 does not send security requests.
    // Without this the BLE stack initiates Just Works pairing by default, which
    // fails with AUTH FAILED when no security callbacks are registered.
    BLESecurity* pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_NO_BOND);
    pSecurity->setCapability(ESP_IO_CAP_NONE);

    _server = BLEDevice::createServer();
    _server->setCallbacks(this);

    BLEService* service = _server->createService(NUS_SERVICE_UUID);
    _tx_char = service->createCharacteristic(NUS_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    _tx_char->addDescriptor(new BLE2902());

    // Characteristic Presentation Format (0x2904): declare value as UTF-8 string
    // Format: format(1) exponent(1) unit(2) namespace(1) description(2)
    static const uint8_t utf8_format[] = { 0x19, 0x00, 0x00, 0x27, 0x01, 0x00, 0x00 };
    BLEDescriptor* pFormat = new BLEDescriptor((uint16_t)0x2904);
    pFormat->setValue(const_cast<uint8_t*>(utf8_format), sizeof(utf8_format));
    _tx_char->addDescriptor(pFormat);
    service->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);  // helps iOS find and stay connected to the device
    adv->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
  }

  void loop() {
    if (_adv_restart_time && millis() >= _adv_restart_time) {
      if (_server->getConnectedCount() == 0) {
        BLEDevice::startAdvertising();
      }
      _adv_restart_time = 0;
    }
  }

  size_t write(uint8_t c) override {
    if (_line_len < (int)sizeof(_line_buf) - 1) {
      _line_buf[_line_len++] = c;
    }
    if (c == '\n' || _line_len >= (int)sizeof(_line_buf) - 1) {
      flushLine();
    }
    return 1;
  }

  size_t write(const uint8_t* buf, size_t size) override {
    for (size_t i = 0; i < size; i++) write(buf[i]);
    return size;
  }
};
