#include "CubeCellSensors.h"
#include "CubeCellGPS.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

bool CubeCellSensors::begin() { cubecell_gps.begin(); return true; }
void CubeCellSensors::loop() { cubecell_gps.loop(millis()); }

static const char* const NAMES[7] = {"gps", "gps_interval", "gps_fix", "gps_sats", "gps_power", "gps_sent", "oled"};

const char* CubeCellSensors::getSettingName(int i) const { return (i >= 0 && i < 7) ? NAMES[i] : NULL; }

const char* CubeCellSensors::getSettingValue(int i) const {
  char* v = const_cast<char*>(_val);
  switch (i) {
    case 0: snprintf(v, sizeof(_val), "%d", cubecell_gps.enabled() ? 1 : 0); break;
    case 1: snprintf(v, sizeof(_val), "%u", (unsigned)cubecell_gps.interval()); break;
    case 2: snprintf(v, sizeof(_val), "%d", cubecell_gps.data().pos_valid ? 1 : 0); break;
    case 3: snprintf(v, sizeof(_val), "%u", (unsigned)cubecell_gps.data().sats); break;
    case 4: snprintf(v, sizeof(_val), "%d", cubecell_gps.powered() ? 1 : 0); break;
    case 5: snprintf(v, sizeof(_val), "%u", (unsigned)cubecell_gps.sentences_ok()); break;
    case 6: return _oled;
    default: return NULL;
  }
  return _val;
}

bool CubeCellSensors::setSettingValue(const char* name, const char* value) {
  if (strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") != 0 && strcmp(value, "1") != 0) return false;
    cubecell_gps.apply(value[0] == '1', cubecell_gps.interval());
    return true;
  }
  if (strcmp(name, "gps_interval") == 0) {
    if (!*value) return false;
    unsigned long v = 0;
    for (const char* p = value; *p; p++) {
      if (*p < '0' || *p > '9') return false;
      v = v * 10 + (unsigned long)(*p - '0');
      if (v > 86400UL) return false;
    }
    cubecell_gps.apply(cubecell_gps.enabled(), (uint32_t)v);
    return true;
  }
  return false;                                              // read-only or unknown key
}
