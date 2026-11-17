#include "BLEProvInterface.h"

#ifdef NRF52_PLATFORM

BLEProvInterface* BLEProvInterface::_instance = nullptr;

/* ------------------------------------------------------------------ */
/* BLE callbacks                                                        */
/* ------------------------------------------------------------------ */

void BLEProvInterface::onConnect(uint16_t conn_handle) {
    // Nothing to do — wait for RX data
    (void)conn_handle;
}

void BLEProvInterface::onDisconnect(uint16_t conn_handle, uint8_t reason) {
    (void)conn_handle;
    (void)reason;
    // If not yet provisioned, restart advertising
    if (_instance && _instance->_enabled) {
        Bluefruit.Advertising.start(0);  // 0 = no timeout
    }
}

void BLEProvInterface::onRxReceived(uint16_t conn_handle) {
    if (!_instance) return;

    // Drain all available data into _rx_buf
    size_t total = 0;
    while (Bluefruit.connected(conn_handle) &&
           _instance->_uart.available() &&
           total < sizeof(_instance->_rx_buf) - 1)
    {
        int c = _instance->_uart.read();
        if (c < 0) break;
        _instance->_rx_buf[total++] = (uint8_t)c;
    }

    if (total > 0) {
        _instance->_rx_buf[total] = '\0';  // null-terminate for JSON parsing
        _instance->_rx_len  = total;
        _instance->_has_data = true;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void BLEProvInterface::begin() {
    if (_enabled) return;
    _instance  = this;
    _has_data  = false;
    _rx_len    = 0;

    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    Bluefruit.begin();
    Bluefruit.setTxPower(0);   // 0 dBm for short-range provisioning
    Bluefruit.setName("KSTRL-PROV");

    // No pairing required for provisioning — the payload itself is the credential
    Bluefruit.Security.setMITM(false);
    Bluefruit.Security.setIOCaps(false, false, false);

    Bluefruit.Periph.setConnectCallback(onConnect);
    Bluefruit.Periph.setDisconnectCallback(onDisconnect);

    _uart.setRxCallback(onRxReceived);
    _uart.begin();

    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(_uart);
    Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.setInterval(32, 244);   // 20ms – 152.5ms
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);   // advertise until stopped

    _enabled = true;
}

void BLEProvInterface::stop() {
    if (!_enabled) return;
    Bluefruit.Advertising.stop();
    // Disconnect any connected client
    for (uint16_t h = 0; h < BLE_MAX_CONN; h++) {
        if (Bluefruit.connected(h)) {
            Bluefruit.disconnect(h);
        }
    }
    _enabled  = false;
    _has_data = false;
    _rx_len   = 0;
}

#endif  // NRF52_PLATFORM
