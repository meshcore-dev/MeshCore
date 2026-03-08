#pragma once

#if defined(ESP32) && defined(COMPANION_ALL_TRANSPORTS)

#include <WiFi.h>
#include "../BaseSerialInterface.h"
#include "../ArduinoSerialInterface.h"
#include "SerialBLEInterface.h"
#include "SerialWifiInterface.h"

class CompanionTransportInterface : public BaseSerialInterface {
public:
  enum WirelessMode : uint8_t {
    WIRELESS_MODE_BLE = 0,
    WIRELESS_MODE_TCP = 1
  };
  enum WifiMode : uint8_t {
    WIFI_MODE_AP_ONLY = 0,
    WIFI_MODE_STA_CLIENT = 1
  };

private:
  static CompanionTransportInterface* _instance;

  ArduinoSerialInterface _usb;
  SerialBLEInterface _ble;
  SerialWifiInterface _wifi;

  bool _isEnabled;
  bool _wifi_server_started;
  uint8_t _last_source;  // 0=usb,1=ble,2=wifi
  WirelessMode _mode;
  bool _wifi_started;

  char _node_name[33];
  char _wifi_ssid[33];
  char _wifi_pwd[65];
  WifiMode _wifi_mode;
  char _wifi_ap_ssid[33];
  char _wifi_ap_pwd[65];
  uint16_t _tcp_port;

public:
  CompanionTransportInterface();

  static CompanionTransportInterface* instance() { return _instance; }

  void begin(Stream& usb_serial, const char* ble_prefix, char* node_name, uint32_t ble_pin, uint16_t tcp_port);
  void begin(HardwareSerial& usb_serial, const char* ble_prefix, char* node_name, uint32_t ble_pin, uint16_t tcp_port);

  bool setWirelessMode(WirelessMode mode, bool persist_only = false);
  WirelessMode getWirelessMode() const { return _mode; }

  bool setWifiCredentials(const char* ssid, const char* pwd);
  bool setWifiMode(WifiMode mode);
  WifiMode getWifiMode() const { return _wifi_mode; }
  bool setWifiApCredentials(const char* ssid, const char* pwd);
  const char* getWifiSSID() const { return _wifi_ssid; }
  const char* getWifiApSSID() const { return _wifi_ap_ssid; }
  const char* getWifiApPassword() const { return _wifi_ap_pwd; }
  bool hasWifiCredentials() const { return _wifi_ssid[0] != 0; }

  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }
  bool isConnected() const override;
  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

private:
  void startWireless();
  void stopWireless();
  void startWifi();
  void stopWifi();
  void connectWifi();
  void startWifiAP();
};

#endif
