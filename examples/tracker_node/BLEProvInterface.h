#pragma once

#include <stdint.h>
#include <stddef.h>

// BLE provisioning is nRF52-only.
// ESP32 does have SerialBLEInterface, but uses a different stack (BLEDevice/Arduino-BLE).
// Stub the interface on non-nRF52 targets so main.cpp compiles everywhere.

#ifdef NRF52_PLATFORM

#include <bluefruit.h>

// Nordic UART Service UUID — same as companion radio
// 6E400001-B5A3-F393-E0A9-E50E24DCCA9E  (service)
// 6E400002-...                           (TX char — device->phone)
// 6E400003-...                           (RX char — phone->device, we read this)

class BLEProvInterface {
public:
    BLEProvInterface() {
        _enabled      = false;
        _has_data     = false;
        _rx_len       = 0;
    }

    // Start advertising as "KSTRL-PROV", open NUS RX
    void begin();

    // Stop advertising and disconnect any client
    void stop();

    bool isEnabled() const { return _enabled; }

    // Returns true if a full JSON payload has been received via RX char
    bool hasData() const { return _has_data; }
    const uint8_t* getData() const { return _rx_buf; }
    size_t getDataLen() const { return _rx_len; }
    void clearData() { _has_data = false; _rx_len = 0; }

private:
    bool    _enabled;
    bool    _has_data;
    size_t  _rx_len;
    uint8_t _rx_buf[256];

    BLEUart _uart;

    static BLEProvInterface* _instance;

    static void onConnect(uint16_t conn_handle);
    static void onDisconnect(uint16_t conn_handle, uint8_t reason);
    static void onRxReceived(uint16_t conn_handle);
};

#else  // !NRF52_PLATFORM

// Stub — compile-out on ESP32 / RP2040
class BLEProvInterface {
public:
    void begin()  {}
    void stop()   {}
    bool isEnabled()  const { return false; }
    bool hasData()    const { return false; }
    const uint8_t* getData() const { return nullptr; }
    size_t getDataLen() const { return 0; }
    void clearData() {}
};

#endif  // NRF52_PLATFORM
