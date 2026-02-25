#include "OneWireSensorHub.h"

#ifdef ENV_INCLUDE_ONEWIRE

#include <Mesh.h>

static SoftwareHalfSerial oneWireSerial(ONEWIRE_PIN);

OneWireSensorHub* OneWireSensorHub::_instance = nullptr;

void OneWireSensorHub::_onewire_callback(const uint8_t pid, const uint8_t sid,
                                          const SNHUBAPI_EVT_E eid, uint8_t* msg, uint16_t len) {
  if (_instance) {
    _instance->handleEvent(pid, sid, eid, msg, len);
  }
}

bool OneWireSensorHub::begin() {
  _instance = this;
  _found_pid_count = 0;
  _has_voltage = false;

  for (int i = 0; i < ONEWIRE_MAX_PIDS; i++) {
    _found_pids[i] = 0xFF;
  }

  pinMode(WB_IO2, OUTPUT);
  digitalWrite(WB_IO2, HIGH);
  delay(100);

  oneWireSerial.begin(9600);
  RakSNHub_Protocl_API.init(_onewire_callback);

  MESH_DEBUG_PRINTLN("OneWire: Scanning for sensor probes (%d ms)...", ONEWIRE_DISCOVERY_TIMEOUT_MS);

  unsigned long start = millis();
  while ((millis() - start) < ONEWIRE_DISCOVERY_TIMEOUT_MS) {
    while (oneWireSerial.available()) {
      if (_rxlen < sizeof(_rxbuf)) {
        _rxbuf[_rxlen++] = oneWireSerial.read();
      }
      delay(5);
    }

    if (_rxlen > 0) {
      RakSNHub_Protocl_API.process(_rxbuf, _rxlen);
      _rxlen = 0;
    }

    delay(100);
  }

  MESH_DEBUG_PRINTLN("OneWire: Discovery complete. Found %d sensor probe(s)", _found_pid_count);

  if (_found_pid_count == 0) {
    digitalWrite(WB_IO2, LOW);
    return false;
  }

  _next_poll_ms = millis() + 2000;
  _current_poll_idx = 0;

  return true;
}

void OneWireSensorHub::loop() {
  if (_found_pid_count == 0) return;

  while (oneWireSerial.available()) {
    if (_rxlen < sizeof(_rxbuf)) {
      _rxbuf[_rxlen++] = oneWireSerial.read();
    } else {
      break;
    }
    _last_rx_time = millis();
  }

  if (_rxlen > 0 && (millis() - _last_rx_time) >= 10) {
    RakSNHub_Protocl_API.process(_rxbuf, _rxlen);
    _rxlen = 0;
  }

  if (millis() >= _next_poll_ms) {
    if (_current_poll_idx < _found_pid_count) {
      uint8_t pid = _found_pids[_current_poll_idx];
      if (pid != 0xFF) {
        RakSNHub_Protocl_API.get.data(pid);
        MESH_DEBUG_PRINTLN("OneWire: Requested data from PID %d", pid);
      }
      _current_poll_idx++;
    }

    if (_current_poll_idx >= _found_pid_count) {
      _current_poll_idx = 0;
      _next_poll_ms = millis() + ONEWIRE_POLL_INTERVAL_MS;
    } else {
      // Stagger between PID requests
      _next_poll_ms = millis() + 500;
    }
  }
}

void OneWireSensorHub::handleEvent(uint8_t pid, uint8_t sid, SNHUBAPI_EVT_E eid,
                                    uint8_t* msg, uint16_t len) {
  switch (eid) {
    case SNHUBAPI_EVT_QSEND:
      oneWireSerial.write(msg, len);
      break;

    case SNHUBAPI_EVT_ADD_PID:
      registerPid(msg[0]);
      break;

    case SNHUBAPI_EVT_ADD_SID:
      MESH_DEBUG_PRINTLN("OneWire: Added SID 0x%02X for PID %d", msg[0], pid);
      break;

    case SNHUBAPI_EVT_SDATA_REQ: {
      uint8_t ipso_type = msg[0];
      uint16_t val_len = len - 1;

      uint8_t ordered[256];
      for (uint16_t i = 0; i < val_len; i += 2) {
        if (i + 1 < val_len) {
          ordered[i] = msg[1 + i + 1];
          ordered[i + 1] = msg[1 + i];
        } else {
          ordered[i] = msg[1 + i];
        }
      }
      MESH_DEBUG_PRINTLN("OneWire: SDATA_REQ SID=0x%02X IPSO=%d len=%d", sid, ipso_type, val_len);
      parseSensorData(sid, ipso_type, ordered, val_len);
      break;
    }

    case SNHUBAPI_EVT_REPORT: {
      uint8_t ipso_type = msg[0];
      uint16_t val_len = len - 1;
      MESH_DEBUG_PRINTLN("OneWire: REPORT SID=0x%02X IPSO=%d len=%d", sid, ipso_type, val_len);
      parseSensorData(sid, ipso_type, &msg[1], val_len);
      break;
    }

    case SNHUBAPI_EVT_CHKSUM_ERR:
      MESH_DEBUG_PRINTLN("OneWire: Checksum error");
      break;

    case SNHUBAPI_EVT_SEQ_ERR:
      MESH_DEBUG_PRINTLN("OneWire: Sequence error");
      break;

    default:
      break;
  }
}

void OneWireSensorHub::registerPid(uint8_t pid) {
  for (int i = 0; i < ONEWIRE_MAX_PIDS; i++) {
    if (_found_pids[i] == pid) {
      MESH_DEBUG_PRINTLN("OneWire: PID %d already registered", pid);
      return;
    }
  }
  for (int i = 0; i < ONEWIRE_MAX_PIDS; i++) {
    if (_found_pids[i] == 0xFF) {
      _found_pids[i] = pid;
      _found_pid_count++;
      MESH_DEBUG_PRINTLN("OneWire: Registered PID %d (total: %d)", pid, _found_pid_count);
      return;
    }
  }
  MESH_DEBUG_PRINTLN("OneWire: No slots for PID %d", pid);
}

void OneWireSensorHub::parseSensorData(uint8_t sid, uint8_t ipso_type, uint8_t* data, uint16_t data_len) {
  switch (ipso_type) {
    case RAK_IPSO_BATTERVALUE:  // 116 (3316-3200): battery voltage, 2 bytes, /100
    case RAK_IPSO_DC_VOLTAGE: { // 186 (3386-3200): DC voltage, 2 bytes, /100
      if (data_len >= 2) {
        int16_t raw = ((int16_t)data[0] << 8) | (int16_t)data[1];
        _cached_voltage = (float)raw / 100.0f;
        _has_voltage = true;
        MESH_DEBUG_PRINTLN("OneWire: Battery Voltage = %.2fV (IPSO %d, raw=%d)", _cached_voltage, ipso_type, raw);
      }
      break;
    }

    case RAK_IPSO_DC_CURRENT: { // 185 (3385-3200): DC current, 2 bytes, mA
      if (data_len >= 2) {
        int16_t raw = ((int16_t)data[0] << 8) | (int16_t)data[1];
        _cached_current_ma = raw;
        _has_current = true;
        MESH_DEBUG_PRINTLN("OneWire: Battery Current = %dmA (IPSO %d, raw=%d)", _cached_current_ma, ipso_type, raw);
      }
      break;
    }

    case RAK_IPSO_CAPACITY: { // 184 (3384-3200): battery percentage, 1 byte
      if (data_len >= 1) {
        _cached_battery_pct = data[0];
        if (_cached_battery_pct > 100) _cached_battery_pct = 100;
        _has_battery_pct = true;
        MESH_DEBUG_PRINTLN("OneWire: Battery SOC = %d%% (IPSO %d)", _cached_battery_pct, ipso_type);
      }
      break;
    }

    case RAK_IPSO_TEMP_SENSOR: { // 103 (3303-3200): temperature, 2 bytes, /10
      if (data_len >= 2) {
        int16_t raw = ((int16_t)data[0] << 8) | (int16_t)data[1];
        _cached_temperature = (float)raw / 10.0f;
        _has_temperature = true;
        MESH_DEBUG_PRINTLN("OneWire: Battery Temperature = %.1fC (IPSO %d, raw=%d)", _cached_temperature, ipso_type, raw);
      }
      break;
    }

    case RAK_IPSO_SSN: { // 126 (3326-3200): serial number, 3 bytes
      if (data_len >= 3) {
        _cached_serial = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
        _has_serial = true;
        MESH_DEBUG_PRINTLN("OneWire: Serial Number = %06lX (IPSO %d)", (unsigned long)_cached_serial, ipso_type);
      }
      break;
    }

    case RAK_IPSO_BINARY2BYTE: { // 243 (0xF3): 2-byte binary, SID distinguishes error vs FW version
      if (data_len >= 2) {
        uint16_t val = ((uint16_t)data[0] << 8) | data[1];
        if (sid == 0x19) {
          _cached_error = val;
          _has_error = true;
          MESH_DEBUG_PRINTLN("OneWire: Battery Error = 0x%04X (IPSO %d, SID 0x%02X)", val, ipso_type, sid);
        } else if (sid == 0x1A) {
          _cached_fw_version = val;
          _has_fw_version = true;
          MESH_DEBUG_PRINTLN("OneWire: Battery FW Version = v%02d.%02d (IPSO %d, SID 0x%02X)", val >> 8, val & 0xFF, ipso_type, sid);
        } else {
          MESH_DEBUG_PRINTLN("OneWire: BINARY2BYTE = 0x%04X (IPSO %d, SID 0x%02X)", val, ipso_type, sid);
        }
      }
      break;
    }

    default:
      MESH_DEBUG_PRINTLN("OneWire: Unhandled IPSO %d (len=%d)", ipso_type, data_len);
      break;
  }
}

bool OneWireSensorHub::hasVoltage() const { return _has_voltage; }
float OneWireSensorHub::getVoltage() const { return _cached_voltage; }
bool OneWireSensorHub::hasCurrent() const { return _has_current; }
float OneWireSensorHub::getCurrent() const { return (float)_cached_current_ma / 100.0f; }
bool OneWireSensorHub::hasBatteryPercent() const { return _has_battery_pct; }
uint8_t OneWireSensorHub::getBatteryPercent() const { return _cached_battery_pct; }
bool OneWireSensorHub::hasTemperature() const { return _has_temperature; }
float OneWireSensorHub::getTemperature() const { return _cached_temperature; }
bool OneWireSensorHub::hasSerialNumber() const { return _has_serial; }
uint32_t OneWireSensorHub::getSerialNumber() const { return _cached_serial; }
bool OneWireSensorHub::hasError() const { return _has_error; }
uint16_t OneWireSensorHub::getError() const { return _cached_error; }
bool OneWireSensorHub::hasFwVersion() const { return _has_fw_version; }
uint16_t OneWireSensorHub::getFwVersion() const { return _cached_fw_version; }
uint8_t OneWireSensorHub::getNumPids() const { return _found_pid_count; }

#endif // ENV_INCLUDE_ONEWIRE
