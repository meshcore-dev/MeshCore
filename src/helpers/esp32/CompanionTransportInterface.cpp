#include "CompanionTransportInterface.h"
#if defined(ESP32) && defined(COMPANION_ALL_TRANSPORTS)
#include <stdio.h>
#include <string.h>

CompanionTransportInterface* CompanionTransportInterface::_instance = nullptr;

namespace {
constexpr unsigned long WIFI_STA_RETRY_BASE_MS = 10000;
constexpr unsigned long WIFI_STA_RETRY_MAX_MS = 300000;
constexpr uint8_t WIFI_STA_MAX_RETRIES = 5;
}

CompanionTransportInterface::CompanionTransportInterface()
    : _isEnabled(false), _wifi_server_started(false), _last_source(0), _mode(WIRELESS_MODE_BLE), _wifi_started(false),
      _last_usb_activity_at(0), _wifi_retry_count(0), _wifi_retry_suspended(false), _next_wifi_retry_at(0),
      _wifi_mode(WIFI_MODE_AP_ONLY), _tcp_port(5000) {
  _node_name[0] = 0;
  _wifi_ssid[0] = 0;
  _wifi_pwd[0] = 0;
  _wifi_ap_ssid[0] = 0;
  _wifi_ap_pwd[0] = 0;
  _instance = this;
}

bool CompanionTransportInterface::usbSessionActive() const {
  return _last_usb_activity_at != 0 && (unsigned long)(millis() - _last_usb_activity_at) < USB_ACTIVITY_TIMEOUT_MS;
}

void CompanionTransportInterface::resetWifiRetryState() {
  _wifi_retry_count = 0;
  _wifi_retry_suspended = false;
  _next_wifi_retry_at = 0;
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
  if (_wifi_ssid[0] == 0) {
    _wifi_retry_suspended = true;
    _next_wifi_retry_at = 0;
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.begin(_wifi_ssid, _wifi_pwd);

  _wifi_retry_count++;
  unsigned long retry_delay = WIFI_STA_RETRY_BASE_MS;
  if (_wifi_retry_count > 1) {
    retry_delay <<= (_wifi_retry_count - 1);
    if (retry_delay > WIFI_STA_RETRY_MAX_MS) retry_delay = WIFI_STA_RETRY_MAX_MS;
  }
  _next_wifi_retry_at = millis() + retry_delay;
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
  stopWifi();
  resetWifiRetryState();

  if (_wifi_mode == WIFI_MODE_STA_CLIENT) {
    if (_wifi_ssid[0] == 0) return;
    _wifi_started = true;
    connectWifi();
  } else {
    _wifi_started = true;
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
    resetWifiRetryState();
    _ble.enable();
  }
}

void CompanionTransportInterface::stopWireless() {
  _ble.disable();
  _wifi.disable();
  stopWifi();
  resetWifiRetryState();
}

bool CompanionTransportInterface::setWirelessMode(WirelessMode mode, bool persist_only) {
  if (mode != WIRELESS_MODE_BLE && mode != WIRELESS_MODE_TCP) {
    return false;
  }
  _mode = mode;
  if (_isEnabled && !persist_only) {
    // Keep active USB control session stable: if command/control is over USB,
    // defer live wireless stack switch until after USB disconnect/reconnect.
    if (!(_last_source == 0 && _usb.isConnected())) {
      startWireless();
    }
  }
  return true;
}

bool CompanionTransportInterface::setWifiCredentials(const char* ssid, const char* pwd) {
  if (!ssid || !pwd) return false;
  if (strlen(ssid) > 32 || strlen(pwd) > 64) return false;
  snprintf(_wifi_ssid, sizeof(_wifi_ssid), "%s", ssid);
  snprintf(_wifi_pwd, sizeof(_wifi_pwd), "%s", pwd);
  resetWifiRetryState();

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
  resetWifiRetryState();
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

bool CompanionTransportInterface::setTcpPort(uint16_t port) {
  if (port == 0) return false;
  _tcp_port = port;

  // If TCP server is already running, restart it on the new port.
  if (_wifi_server_started) {
    _wifi.disable();
    _wifi.begin(_tcp_port);
    if (_mode == WIRELESS_MODE_TCP) {
      _wifi.enable();
    }
  }
  return true;
}

void CompanionTransportInterface::enable() {
  if (_isEnabled) return;
  _isEnabled = true;
  _last_source = 0xFF;
  _last_usb_activity_at = 0;
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
  if (_last_source == 0) return usbSessionActive();
  return _ble.isConnected() || _wifi.isConnected() || usbSessionActive();
}

bool CompanionTransportInterface::isWriteBusy() const {
  if (_last_source == 2) return _wifi.isWriteBusy();
  if (_last_source == 1) return _ble.isWriteBusy();
  return _usb.isWriteBusy();
}

size_t CompanionTransportInterface::writeFrame(const uint8_t src[], size_t len) {
  if (_last_source == 0) _last_usb_activity_at = millis();
  if (_last_source == 2 && _wifi.isConnected()) return _wifi.writeFrame(src, len);
  if (_last_source == 1 && _ble.isConnected()) return _ble.writeFrame(src, len);
  return _usb.writeFrame(src, len);
}

size_t CompanionTransportInterface::checkRecvFrame(uint8_t dest[]) {
  // Prioritise USB input so local serial control remains reliable even
  // when wireless transport is active or reconnecting.
  size_t usb_len = _usb.checkRecvFrame(dest);
  if (usb_len > 0) {
    _last_source = 0;
    _last_usb_activity_at = millis();
    return usb_len;
  }

  if (_mode == WIRELESS_MODE_TCP) {
    if (_wifi_mode == WIFI_MODE_STA_CLIENT) {
      if (WiFi.status() == WL_CONNECTED) {
        resetWifiRetryState();
      } else if (_wifi_started && !_wifi_retry_suspended && _wifi_ssid[0] != 0) {
        unsigned long now = millis();
        if ((long)(now - _next_wifi_retry_at) >= 0) {
          if (_wifi_retry_count >= WIFI_STA_MAX_RETRIES) {
            stopWifi();
            _wifi_retry_suspended = true;
          } else {
            WiFi.disconnect(true, false);
            connectWifi();
          }
        }
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
  return 0;
}

#endif
