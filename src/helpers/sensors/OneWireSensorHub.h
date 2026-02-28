#pragma once

#ifdef ENV_INCLUDE_ONEWIRE

#include <RAK-OneWireSerial.h>

#ifndef ONEWIRE_DISCOVERY_TIMEOUT_MS
#define ONEWIRE_DISCOVERY_TIMEOUT_MS 8000
#endif

#ifndef ONEWIRE_POLL_INTERVAL_MS
#define ONEWIRE_POLL_INTERVAL_MS 30000
#endif

#ifndef ONEWIRE_PIN
#define ONEWIRE_PIN PIN_SERIAL2_RX
#endif

#define ONEWIRE_MAX_PIDS 5

class OneWireSensorHub {
public:
  bool begin();
  void loop();

  bool hasVoltage() const;
  float getVoltage() const;
  bool hasCurrent() const;
  float getCurrent() const;
  bool hasBatteryPercent() const;
  uint8_t getBatteryPercent() const;
  bool hasTemperature() const;
  float getTemperature() const;
  bool hasSerialNumber() const;
  uint32_t getSerialNumber() const;
  bool hasError() const;
  uint16_t getError() const;
  bool hasFwVersion() const;
  uint16_t getFwVersion() const;
  uint8_t getNumPids() const;

private:
  uint8_t _found_pids[ONEWIRE_MAX_PIDS];
  uint8_t _found_pid_count = 0;

  volatile float _cached_voltage = 0.0f;
  volatile bool  _has_voltage = false;
  volatile int16_t _cached_current_ma = 0;
  volatile bool  _has_current = false;
  volatile uint8_t _cached_battery_pct = 0;
  volatile bool  _has_battery_pct = false;
  volatile float _cached_temperature = 0.0f;
  volatile bool  _has_temperature = false;
  volatile uint32_t _cached_serial = 0;
  volatile bool  _has_serial = false;
  volatile uint16_t _cached_error = 0;
  volatile bool  _has_error = false;
  volatile uint16_t _cached_fw_version = 0;
  volatile bool  _has_fw_version = false;

  unsigned long _next_poll_ms = 0;
  unsigned long _last_rx_time = 0;
  uint8_t _current_poll_idx = 0;

  uint8_t _rxbuf[256];
  uint16_t _rxlen = 0;

  static OneWireSensorHub* _instance;
  static void _onewire_callback(const uint8_t pid, const uint8_t sid,
                                 const SNHUBAPI_EVT_E eid, uint8_t* msg, uint16_t len);

  void handleEvent(uint8_t pid, uint8_t sid, SNHUBAPI_EVT_E eid, uint8_t* msg, uint16_t len);
  void registerPid(uint8_t pid);
  void parseSensorData(uint8_t sid, uint8_t ipso_type, uint8_t* data, uint16_t data_len);
};

#endif // ENV_INCLUDE_ONEWIRE
