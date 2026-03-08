#include "CompanionTransportInterface.h"
#if defined(ESP32) && defined(COMPANION_ALL_TRANSPORTS)
#include <stdio.h>
#include <string.h>

CompanionTransportInterface* CompanionTransportInterface::_instance = nullptr;

CompanionTransportInterface::CompanionTransportInterface()
    : _isEnabled(false), _wifi_server_started(false), _last_source(0), _mode(WIRELESS_MODE_BLE), _wifi_started(false),
      _wifi_mode(WIFI_MODE_AP_ONLY), _tcp_port(5000) {
  _node_name[0] = 0;
  _wifi_ssid[0] = 0;
  _wifi_pwd[0] = 0;
  _wifi_ap_ssid[0] = 0;
  _wifi_ap_pwd[0] = 0;
  _instance = this;
}

void CompanionTransportInterface::begin(Stream& usb_serial, const char* ble_prefix, char* node_name, uint32_t ble_pin, uint16_t tcp_port) {
  _tcp_port = tcp_port;
  snprintf(_node_name, sizeof(_node_name), "%s", node_name);

  _usb.begin(usb_serial);
  _ble.begin(ble_prefix, node_name, ble_pin);
}

void CompanionTransportInterface::begin(HardwareSerial& usb_serial, const char* ble_prefix, char* node_name, uint32_t ble_pin, uint16_t tcp_port) {
  begin((Stream&)usb_serial, ble_prefix, node_name, ble_pin, tcp_port);
}

void CompanionTransportInterface::connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(_wifi_ssid, _wifi_pwd);
}

void CompanionTransportInterface::startWifiAP() {
  WiFi.mode(WIFI_AP);
  char ap_name[33] = {0};
  if (_wifi_ap_ssid[0] != 0) {
    snprintf(ap_name, sizeof(ap_name), "%s", _wifi_ap_ssid);
  } else {
    snprintf(ap_name, sizeof(ap_name), "MeshCore-%s", _node_name);
  }

  if (_wifi_ap_pwd[0] != 0 && strlen(_wifi_ap_pwd) >= 8) {
    WiFi.softAP(ap_name, _wifi_ap_pwd);
  } else {
    WiFi.softAP(ap_name);
  }
}

void CompanionTransportInterface::startWifi() {
  _wifi_started = true;
  if (_wifi_mode == WIFI_MODE_STA_CLIENT) {
    connectWifi();
  } else {
    startWifiAP();
  }
}

void CompanionTransportInterface::stopWifi() {
  if (!_wifi_started) return;
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  _wifi_started = false;
}

void CompanionTransportInterface::startWireless() {
  if (_mode == WIRELESS_MODE_TCP) {
    startWifi();
    if (!_wifi_server_started) {
      _wifi.begin(_tcp_port);
      _wifi_server_started = true;
    }
    _wifi.enable();
    _ble.disable();
  } else {
    _wifi.disable();
    stopWifi();
    _ble.enable();
  }
}

void CompanionTransportInterface::stopWireless() {
  _ble.disable();
  _wifi.disable();
  stopWifi();
}

bool CompanionTransportInterface::setWirelessMode(WirelessMode mode, bool persist_only) {
  if (mode != WIRELESS_MODE_BLE && mode != WIRELESS_MODE_TCP) {
    return false;
  }
  _mode = mode;
  if (_isEnabled && !persist_only) {
    startWireless();
  }
  return true;
}

bool CompanionTransportInterface::setWifiCredentials(const char* ssid, const char* pwd) {
  if (!ssid || !pwd) return false;
  if (strlen(ssid) > 32 || strlen(pwd) > 64) return false;
  snprintf(_wifi_ssid, sizeof(_wifi_ssid), "%s", ssid);
  snprintf(_wifi_pwd, sizeof(_wifi_pwd), "%s", pwd);

  if (_isEnabled && _mode == WIRELESS_MODE_TCP && _wifi_mode == WIFI_MODE_STA_CLIENT) {
    startWifi();
  }
  return true;
}

bool CompanionTransportInterface::setWifiMode(WifiMode mode) {
  if (mode != WIFI_MODE_AP_ONLY && mode != WIFI_MODE_STA_CLIENT) {
    return false;
  }
  _wifi_mode = mode;
  if (_isEnabled && _mode == WIRELESS_MODE_TCP) {
    startWifi();
  }
  return true;
}

bool CompanionTransportInterface::setWifiApCredentials(const char* ssid, const char* pwd) {
  if (!ssid || !pwd) return false;
  if (strlen(ssid) > 32 || strlen(pwd) > 64) return false;
  if (pwd[0] != 0 && strlen(pwd) < 8) return false;  // WPA2 requirement
  snprintf(_wifi_ap_ssid, sizeof(_wifi_ap_ssid), "%s", ssid);
  snprintf(_wifi_ap_pwd, sizeof(_wifi_ap_pwd), "%s", pwd);

  if (_isEnabled && _mode == WIRELESS_MODE_TCP && _wifi_mode == WIFI_MODE_AP_ONLY) {
    startWifi();
  }
  return true;
}

void CompanionTransportInterface::enable() {
  if (_isEnabled) return;
  _isEnabled = true;
  _last_source = 0;
  _usb.enable();
  startWireless();
}

void CompanionTransportInterface::disable() {
  _isEnabled = false;
  stopWireless();
  _usb.disable();
}

bool CompanionTransportInterface::isConnected() const {
  if (_last_source == 2) return _wifi.isConnected();
  if (_last_source == 1) return _ble.isConnected();
  if (_last_source == 0) return _usb.isConnected();
  return _usb.isConnected() || _ble.isConnected() || _wifi.isConnected();
}

bool CompanionTransportInterface::isWriteBusy() const {
  if (_last_source == 2) return _wifi.isWriteBusy();
  if (_last_source == 1) return _ble.isWriteBusy();
  return _usb.isWriteBusy();
}

size_t CompanionTransportInterface::writeFrame(const uint8_t src[], size_t len) {
  if (_last_source == 2 && _wifi.isConnected()) return _wifi.writeFrame(src, len);
  if (_last_source == 1 && _ble.isConnected()) return _ble.writeFrame(src, len);
  return _usb.writeFrame(src, len);
}

size_t CompanionTransportInterface::checkRecvFrame(uint8_t dest[]) {
  if (_mode == WIRELESS_MODE_TCP) {
    static unsigned long last_wifi_retry_at = 0;
    if (_wifi_mode == WIFI_MODE_STA_CLIENT && _wifi_ssid[0] != 0 && WiFi.status() != WL_CONNECTED) {
      unsigned long now = millis();
      if (last_wifi_retry_at == 0 || (unsigned long)(now - last_wifi_retry_at) >= 10000) {
        // Keep trying to reconnect in STA mode, but avoid tight reconnect loops.
        WiFi.disconnect();
        connectWifi();
        last_wifi_retry_at = now;
      }
    }

    size_t wifi_len = _wifi.checkRecvFrame(dest);
    if (wifi_len > 0) {
      _last_source = 2;
      return wifi_len;
    }
  } else {
    size_t ble_len = _ble.checkRecvFrame(dest);
    if (ble_len > 0) {
      _last_source = 1;
      return ble_len;
    }
  }

  size_t usb_len = _usb.checkRecvFrame(dest);
  if (usb_len > 0) {
    _last_source = 0;
    return usb_len;
  }
  return 0;
}

#endif
