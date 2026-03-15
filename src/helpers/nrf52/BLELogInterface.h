#pragma once

#include <Arduino.h>
#include <bluefruit.h>

#ifndef BLE_LOG_TX_POWER
  #define BLE_LOG_TX_POWER 4
#endif

/**
 * Unsecured BLE UART (Nordic UART Service) logger for nRF52 platforms.
 * Implements Print so it can be passed to Dispatcher::setPacketLogStream().
 * Any BLE NUS client (e.g. nRF Connect, a Raspberry Pi running bleak) can
 * connect without pairing and receive the log stream as plain text.
 */
class BLELogInterface : public Print {
  BLEUart _uart;

public:
  void begin(const char* device_name) {
    Bluefruit.begin();
    Bluefruit.setTxPower(BLE_LOG_TX_POWER);
    Bluefruit.setName(device_name);

    _uart.begin();

    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(_uart);
    Bluefruit.ScanResponse.addName();

    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.start(0);
  }

  size_t write(uint8_t c) override {
    return _uart.write(c);
  }

  size_t write(const uint8_t* buf, size_t size) override {
    return _uart.write(buf, size);
  }
};
