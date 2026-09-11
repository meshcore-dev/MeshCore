#include "TBeam1WBoard.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>

void TBeam1WBoard::begin() {
  ESP32Board::begin();

  // Power on radio module (must be done before radio init)
  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);
  radio_powered = true;
  delay(10);  // Allow radio to power up

  // RF switch RXEN pin handled by RadioLib via setRfSwitchPins()

  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // NTC ADC (PA-adjacent thermistor)
  pinMode(NTC_PIN, INPUT);
  analogSetPinAttenuation(NTC_PIN, ADC_11db);
  analogReadResolution(12);

  // Fan: auto/onoff. Thermal on at 36C / off below 30C; TX still forces a cooldown.
  pinMode(FAN_CTRL_PIN, OUTPUT);
  digitalWrite(FAN_CTRL_PIN, HIGH);
  _fan_on = true;
  _temp_c = readNtcTempC();
  startFanTask();
}

void TBeam1WBoard::startFanTask() {
  if (_fan_task) return;
  xTaskCreate(fanTaskThunk, "tbeam1w_fan", 4096, this, 1, &_fan_task);
}

void TBeam1WBoard::fanTaskThunk(void* arg) {
  auto* self = static_cast<TBeam1WBoard*>(arg);
  for (;;) {
    self->updateFan();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void TBeam1WBoard::onBeforeTransmit() {
  digitalWrite(LED_PIN, HIGH);  // TX LED on
  portENTER_CRITICAL(&_fan_mux);
  _tx_active = true;
  if (_mode == FAN_AUTO && !_stopped) {
    setFanOutputLocked(true);
  }
  portEXIT_CRITICAL(&_fan_mux);
}

void TBeam1WBoard::onAfterTransmit() {
  digitalWrite(LED_PIN, LOW);   // TX LED off
  portENTER_CRITICAL(&_fan_mux);
  if (!_stopped) {
    _tx_until_ms = millis() + FAN_TX_COOLDOWN_MS;
    _tx_cooldown_active = true;
  }
  _tx_active = false;
  portEXIT_CRITICAL(&_fan_mux);
}

uint16_t TBeam1WBoard::getBattMilliVolts() {
  // T-Beam 1W uses 7.4V battery with voltage divider
  analogReadResolution(12);
  uint32_t raw = 0;
  for (int i = 0; i < 8; i++) {
    raw += analogRead(BATTERY_PIN);
  }
  raw = raw / 8;
  return static_cast<uint16_t>((raw * 3300 * ADC_MULTIPLIER) / 4095);
}

const char* TBeam1WBoard::getManufacturerName() const {
  return "LilyGo T-Beam 1W";
}

void TBeam1WBoard::powerOff() {
  portENTER_CRITICAL(&_fan_mux);
  _stopped = true;
  _tx_active = false;
  _tx_cooldown_active = false;
  setFanOutputLocked(false);
  portEXIT_CRITICAL(&_fan_mux);

  // Turn off radio LNA (CTRL pin must be LOW when not receiving)
  digitalWrite(SX126X_RXEN, LOW);

  // Turn off radio power
  digitalWrite(SX126X_POWER_EN, LOW);
  radio_powered = false;

  digitalWrite(LED_PIN, LOW);

  ESP32Board::powerOff();
}

void TBeam1WBoard::setFanEnabled(bool enabled) {
  portENTER_CRITICAL(&_fan_mux);
  setFanOutputLocked(enabled && !_stopped);
  portEXIT_CRITICAL(&_fan_mux);
}

bool TBeam1WBoard::isFanEnabled() const {
  portENTER_CRITICAL(&_fan_mux);
  bool enabled = _fan_on;
  portEXIT_CRITICAL(&_fan_mux);
  return enabled;
}

float TBeam1WBoard::readNtcTempC() {
  analogReadMilliVolts(NTC_PIN);  // settle
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) {
    uint32_t sample_mv = analogReadMilliVolts(NTC_PIN);
    // GPIO14 is ADC2 on ESP32-S3. Wi-Fi/ESP-NOW arbitration failures are
    // reported by Arduino as 0 mV; never average a failed sample into a
    // plausible-but-low temperature.
    if (sample_mv == 0 || sample_mv >= NTC_VCC_MV) return NAN;
    sum += sample_mv;
  }
  float mv = sum / 8.0f;

  // R_ntc = R_fixed * (Vcc - V) / V  for 3.3V-NTC-ADC-10k-GND
  float r_ntc = NTC_R_FIXED * (NTC_VCC_MV - mv) / mv;
  if (r_ntc <= 0.0f) return NAN;

  float temp_k = 1.0f / (1.0f / 298.15f + (1.0f / NTC_B) * logf(r_ntc / NTC_R25));
  return temp_k - 273.15f;
}

bool TBeam1WBoard::ntcImplausible(float temp_c) const {
  return isnan(temp_c) || temp_c < -20.0f || temp_c > 120.0f;
}

bool TBeam1WBoard::isTxCoolingLocked(uint32_t now) {
  if (_tx_active) return true;
  if (!_tx_cooldown_active) return false;
  if ((int32_t)(now - _tx_until_ms) >= 0) {
    _tx_cooldown_active = false;
    return false;
  }
  return true;
}

int TBeam1WBoard::cooldownSecsLocked() {
  if (_tx_active) return (FAN_TX_COOLDOWN_MS + 999) / 1000;
  if (!_tx_cooldown_active) return 0;
  int32_t remain_ms = (int32_t)(_tx_until_ms - millis());
  if (remain_ms <= 0) {
    _tx_cooldown_active = false;
    return 0;
  }
  return (remain_ms + 999) / 1000;
}

void TBeam1WBoard::setFanOutputLocked(bool enabled) {
  _fan_on = enabled;
  digitalWrite(FAN_CTRL_PIN, enabled ? HIGH : LOW);
}

void TBeam1WBoard::updateFan() {
  float t = readNtcTempC();
  uint32_t now = millis();

  portENTER_CRITICAL(&_fan_mux);
  if (_stopped) {
    portEXIT_CRITICAL(&_fan_mux);
    return;
  }

  _temp_c = t;
  bool tx_cooling = isTxCoolingLocked(now);

  bool enabled;
  if (_mode == FAN_ON) {
    enabled = true;
  } else if (_mode == FAN_OFF) {
    enabled = false;
  } else if (ntcImplausible(t)) {
    enabled = true;  // fail-safe: treat bad NTC as hot
  } else {
    // TX cooldown must not latch _thermal_on, or a TX at 30C keeps the fan
    // running until the temperature dips under lo.
    if (t >= (float)_hi_c) _thermal_on = true;
    else if (t < (float)_lo_c) _thermal_on = false;
    enabled = _thermal_on || tx_cooling;
  }

  setFanOutputLocked(enabled);
  portEXIT_CRITICAL(&_fan_mux);
}

bool TBeam1WBoard::persistKey(const char* key, const char* value) {
  return _prefs && _prefs->setByKey(key, value);
}

bool TBeam1WBoard::parseIntArg(const char* text, int& value) {
  if (!text || !*text) return false;
  errno = 0;
  char* end = nullptr;
  long parsed = strtol(text, &end, 10);
  if (errno == ERANGE || end == text || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) {
    return false;
  }
  value = (int)parsed;
  return true;
}

void TBeam1WBoard::loadFanPrefs() {
  if (!_prefs) return;

  FanMode mode = FAN_AUTO;
  char buf[12];
  buf[0] = 0;
  if (_prefs->getByKey("fan", buf, 11)) {
    if (strcmp(buf, "off") == 0) mode = FAN_OFF;
    else if (strcmp(buf, "on") == 0) mode = FAN_ON;
  }

  int lo = FAN_DEFAULT_LO_C;
  int hi = FAN_DEFAULT_HI_C;
  buf[0] = 0;
  if (_prefs->getByKey("fan_lo", buf, 11)) lo = atoi(buf);
  buf[0] = 0;
  if (_prefs->getByKey("fan_hi", buf, 11)) hi = atoi(buf);

  portENTER_CRITICAL(&_fan_mux);
  _mode = mode;
  if (lo >= 0 && hi <= 120 && lo < hi) {
    _lo_c = lo;
    _hi_c = hi;
  }
  portEXIT_CRITICAL(&_fan_mux);
}

void TBeam1WBoard::attachDynamicPrefs(KeyValueStore* prefs) {
  _prefs = prefs;
  loadFanPrefs();
  updateFan();
}

const char* TBeam1WBoard::modeNameLocked() const {
  if (_mode == FAN_AUTO) return "auto";
  if (_mode == FAN_OFF) return "off";
  return "on";
}

bool TBeam1WBoard::handleCommand(const char* command, uint32_t sender_timestamp, char* reply) {
  (void)sender_timestamp;

  if (strcmp(command, "get fan") == 0) {
    portENTER_CRITICAL(&_fan_mux);
    int cd = cooldownSecsLocked();
    float temp_c = _temp_c;
    bool enabled = _fan_on;
    const char* mode = modeNameLocked();
    portEXIT_CRITICAL(&_fan_mux);
    if (ntcImplausible(temp_c)) {
      sprintf(reply, "> %s n/a fan=%s cd=%ds", mode, enabled ? "on" : "off", cd);
    } else {
      sprintf(reply, "> %s %.1fC fan=%s cd=%ds",
              mode, (double)temp_c, enabled ? "on" : "off", cd);
    }
    return true;
  }

  if (strncmp(command, "set fan.lo ", 11) == 0) {
    int lo;
    portENTER_CRITICAL(&_fan_mux);
    int hi_limit = _hi_c;
    portEXIT_CRITICAL(&_fan_mux);
    if (!parseIntArg(&command[11], lo) || lo < 0 || lo >= hi_limit || lo > 100) {
      strcpy(reply, "Error: fan.lo must be 0..100 and < fan.hi");
    } else if (!persistKey("fan_lo", &command[11])) {
      strcpy(reply, "Error: failed to save fan.lo");
    } else {
      portENTER_CRITICAL(&_fan_mux);
      _lo_c = lo;
      portEXIT_CRITICAL(&_fan_mux);
      sprintf(reply, "OK - fan.lo %d", lo);
    }
    return true;
  }

  if (strncmp(command, "set fan.hi ", 11) == 0) {
    int hi;
    portENTER_CRITICAL(&_fan_mux);
    int lo_limit = _lo_c;
    portEXIT_CRITICAL(&_fan_mux);
    if (!parseIntArg(&command[11], hi) || hi <= lo_limit || hi > 120) {
      strcpy(reply, "Error: fan.hi must be > fan.lo and <= 120");
    } else if (!persistKey("fan_hi", &command[11])) {
      strcpy(reply, "Error: failed to save fan.hi");
    } else {
      portENTER_CRITICAL(&_fan_mux);
      _hi_c = hi;
      portEXIT_CRITICAL(&_fan_mux);
      sprintf(reply, "OK - fan.hi %d", hi);
    }
    return true;
  }

  if (strncmp(command, "set fan ", 8) == 0) {
    const char* arg = &command[8];
    if (strcmp(arg, "on") == 0) {
      if (!persistKey("fan", "on")) {
        strcpy(reply, "Error: failed to save fan mode");
      } else {
        portENTER_CRITICAL(&_fan_mux);
        _mode = FAN_ON;
        if (!_stopped) setFanOutputLocked(true);
        portEXIT_CRITICAL(&_fan_mux);
        strcpy(reply, "OK - fan on");
      }
    } else if (strcmp(arg, "off") == 0) {
      if (!persistKey("fan", "off")) {
        strcpy(reply, "Error: failed to save fan mode");
      } else {
        portENTER_CRITICAL(&_fan_mux);
        _mode = FAN_OFF;
        setFanOutputLocked(false);
        portEXIT_CRITICAL(&_fan_mux);
        strcpy(reply, "OK - fan off");
      }
    } else if (strcmp(arg, "auto") == 0) {
      if (!persistKey("fan", "auto")) {
        strcpy(reply, "Error: failed to save fan mode");
      } else {
        portENTER_CRITICAL(&_fan_mux);
        _mode = FAN_AUTO;
        portEXIT_CRITICAL(&_fan_mux);
        updateFan();
        strcpy(reply, "OK - fan auto");
      }
    } else {
      strcpy(reply, "Error: fan must be on, off, or auto");
    }
    return true;
  }

  return false;
}
