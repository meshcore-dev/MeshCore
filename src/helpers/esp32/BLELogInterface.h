#pragma once

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// Nordic UART Service UUIDs (standard, recognised by nRF Connect and bleak)
#define NUS_SERVICE_UUID                "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_UUID                     "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define BLE_LOG_ADVERT_RESTART_DELAY_MS 1000

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
  BLEServer *_server;
  BLECharacteristic *_tx_char;
  bool _connected;
  uint16_t _conn_id;
  unsigned long _adv_restart_time;
  char _line_buf[256];
  int _line_len;

  // Returns the max bytes per notification for the current connection.
  // BLE notifications carry ATT_MTU-3 bytes of payload. Falls back to 20
  // (the minimum guaranteed by the spec) if MTU has not been negotiated yet.
  int notifyPayloadSize() const {
    const uint16_t mtu = _server->getPeerMTU(_conn_id);
    return (mtu > 3) ? mtu - 3 : 20;
  }

  void flushLine() {
    if (_line_len == 0 || !_connected) {
      _line_len = 0;
      return;
    }
    const int chunk = notifyPayloadSize();
    int offset = 0;
    while (offset < _line_len) {
      int n = _line_len - offset;
      if (n > chunk) n = chunk;
      _tx_char->setValue((uint8_t *)_line_buf + offset, n);
      _tx_char->notify();
      offset += n;
    }
    _line_len = 0;
  }

  void onConnect(BLEServer *pServer) override {
    _connected = true;
  }

  void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
    _connected = true;
    _conn_id = param->connect.conn_id;
  }

  void onDisconnect(BLEServer *pServer) override {
    _connected = false;
    _line_len = 0; // discard partial line
    _adv_restart_time = millis() + BLE_LOG_ADVERT_RESTART_DELAY_MS;
  }

public:
  BLELogInterface()
      : _server(nullptr), _tx_char(nullptr), _connected(false), _conn_id(0), _adv_restart_time(0),
        _line_len(0) {}

  void begin(const char *device_name) {
    BLEDevice::init(device_name);
    BLEDevice::setMTU(256); // raise the ceiling; client may negotiate a larger MTU

    // Explicitly disable bonding so the ESP32 does not send security requests.
    // Without this the BLE stack initiates Just Works pairing by default, which
    // fails with AUTH FAILED when no security callbacks are registered.
    BLESecurity *pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_NO_BOND);
    pSecurity->setCapability(ESP_IO_CAP_NONE);

    _server = BLEDevice::createServer();
    _server->setCallbacks(this);

    BLEService *service = _server->createService(NUS_SERVICE_UUID);
    _tx_char = service->createCharacteristic(NUS_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    _tx_char->addDescriptor(new BLE2902());

    // Characteristic Presentation Format (0x2904): declare value as UTF-8 string
    // Format: format(1) exponent(1) unit(2) namespace(1) description(2)
    static const uint8_t utf8_format[] = { 0x19, 0x00, 0x00, 0x27, 0x01, 0x00, 0x00 };
    BLEDescriptor *pFormat = new BLEDescriptor((uint16_t)0x2904);
    pFormat->setValue(const_cast<uint8_t *>(utf8_format), sizeof(utf8_format));
    _tx_char->addDescriptor(pFormat);
    service->start();

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06); // helps iOS find and stay connected to the device
    adv->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
  }

  void loop() {
    if (!_adv_restart_time || millis() < _adv_restart_time) return;
    _adv_restart_time = 0;
    if (_server->getConnectedCount() == 0) {
      BLEDevice::startAdvertising();
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

  size_t write(const uint8_t *buf, size_t size) override {
    for (size_t i = 0; i < size; i++)
      write(buf[i]);
    return size;
  }
};
