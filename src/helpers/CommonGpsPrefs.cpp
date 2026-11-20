#include "CommonGpsPrefs.h"
#include <helpers/sensors/LocationProvider.h>
#ifdef GPS_GUARD
#include <helpers/sensors/GpsGuard.h>
#endif

bool CommonGpsPrefs::setGpsEnabled(SensorManager& sensors, bool enabled) {
  LocationProvider* location = sensors.getLocationProvider();
  if (location == nullptr) return false;
  location->setGpsOn(enabled);
  setGpsEnabled(enabled);
  return true;
}

bool CommonGpsPrefs::setGpsPeriod(SensorManager& sensors, uint32_t seconds) {
  LocationProvider* location = sensors.getLocationProvider();
  if (location == nullptr) return false;
  location->setGpsPeriod(seconds);
  setGpsPeriod(seconds);
  return true;
}

void CommonGpsPrefs::applyGuardReference(SensorManager& sensors) const {
#ifdef GPS_GUARD
  LocationProvider* location = sensors.getLocationProvider();
  GpsGuard* guard = location != nullptr ? location->getGuard() : nullptr;
  double lat, lon;
  if (guard != nullptr && getGuardReference(lat, lon)) guard->setReference(lat, lon);
  uint32_t fence;
  if (guard != nullptr && getGuardFence(fence)) guard->setFence(fence);
  bool enabled;
  if (guard != nullptr && getGuardEnabled(enabled)) guard->setEnabled(enabled);
#endif
}

// The clock was set by hand: the guard drops its GPS time anchor and any alert.
void CommonGpsPrefs::onClockSetManually(SensorManager& sensors) {
#ifdef GPS_GUARD
  LocationProvider* location = sensors.getLocationProvider();
  GpsGuard* guard = location != nullptr ? location->getGuard() : nullptr;
  if (guard != nullptr) guard->clockSetManually();
#endif
}

bool CommonGpsPrefs::apply(SensorManager& sensors) const {
  LocationProvider* location = sensors.getLocationProvider();
  if (location == nullptr) return false;
  applyGuardReference(sensors);
  uint32_t first_fix;
  if (getFirstFixTimeout(first_fix)) location->setFirstFixTimeout(first_fix);
  location->setGpsPeriod(getGpsPeriod());
  location->setGpsOn(isGpsEnabled());
  return true;
}

// single unit, rounded down: "45s", "12m", "3h"
static void formatDuration(char* buf, size_t size, uint32_t seconds) {
  if (seconds < 60) {
    snprintf(buf, size, "%lus", (unsigned long)seconds);
  } else if (seconds < 3600) {
    snprintf(buf, size, "%lum", (unsigned long)(seconds / 60));
  } else {
    snprintf(buf, size, "%luh", (unsigned long)(seconds / 3600));
  }
}

// "<n>[s|m|h|d]", seconds by default
static bool parseDuration(const char* text, uint32_t& seconds) {
  char* end;
  unsigned long value = strtoul(text, &end, 10);
  unsigned long mult = 1;
  if (*end == 'm') mult = 60;
  else if (*end == 'h') mult = 3600;
  else if (*end == 'd') mult = 86400;
  if (mult > 1 || *end == 's') end++;   // skip the suffix char
  if (text[0] == '\0' || *end != '\0') return false;
  seconds = (uint32_t)(value * mult);
  return true;
}

bool CommonGpsPrefs::handleCommand(const char* command, SensorManager& sensors, char* reply) {
  if (strcmp(command, "gps") == 0) {
    LocationProvider* location = sensors.getLocationProvider();
    char mode[32];
    if (getGpsPeriod() == 0) {
      strcpy(mode, "continuous");
    } else {
      snprintf(mode, sizeof(mode), "periodic (%lus)",
               (unsigned long)getGpsPeriod());
    }
    if (location == nullptr) {
      strcpy(reply, "Error, gps provider not found");
    } else if (location->isGpsOn()) {
      const char* activity = "active";
      if (!location->isActive()) {
        if (location->hasHardwareEnableState() && !location->isHardwareEnabled()) {
          activity = "off(hw)";
        } else if (getGpsPeriod() > 0) {
          activity = "waiting";
        } else {
          activity = "inactive";
        }
      }
      char fix[32];
      if (location->hasValidFix()) {
        char age[12];
        formatDuration(age, sizeof(age), location->getLastFixAge());
        snprintf(fix, sizeof(fix), "%ld sats %s ago", location->satellitesCount(), age);
      } else {
        strcpy(fix, "no fix");
      }
      snprintf(reply, 160, "> on, %s, %s, %s", activity, fix, mode);
    } else {
      snprintf(reply, 160, "> off, %s", mode);
    }
    return true;
  }
  if (strcmp(command, "gps fix") == 0) {
    LocationProvider* location = sensors.getLocationProvider();
    char acq[16] = "";
    if (location == nullptr) {
      strcpy(reply, "Error, gps provider not found");
      return true;
    }
    if (location->isActive()) {
      char t[12];
      formatDuration(t, sizeof(t), location->getAcquisitionAge());
      snprintf(acq, sizeof(acq), "acq %s", t);
    }
    if (!location->hasValidFix()) {
      snprintf(reply, 160, "> No fix%s%s", acq[0] ? ", " : "", acq);
    } else {
      const long latitude = location->getLatitude();
      const long longitude = location->getLongitude();
      char pos[40];
      snprintf(pos, sizeof(pos), "%.6f, %.6f", latitude / 1e6, longitude / 1e6);
      if (location->isValid()) {
        snprintf(reply, 160, "> Fix (%d sat): %s", location->satellitesCount(), pos);
      } else {
        char age[12];
        formatDuration(age, sizeof(age), location->getLastFixAge());
        snprintf(reply, 160, "> Fix %s ago (%d sat)%s%s: %s", age, location->satellitesCount(),acq[0] ? ", " : "", acq, pos);
      }
    }
    return true;
  }
  if (memcmp(command, "gps once", 8) == 0) {    // before "gps on"
    LocationProvider* location = sensors.getLocationProvider();

    if (location == nullptr) {
      strcpy(reply, "Error, gps provider not found");
      return true;
    } 

    if (location->hasHardwareEnableState() && !location->isHardwareEnabled()) {
      strcpy(reply, "Error, gps hw switch off");
      return true;
    }

    if (strstr(command, " -s") != nullptr) location->syncTime();

    const char* state = !location->isGpsOn() ? "turning on and acquiring"
        : (location->getGpsPeriod() > 0 && !location->isActive()) ? "waking gps"
        : "gps already on";

    if (location->startOnce()) {
      snprintf(reply, 160, "OK - %s", state);
    } else { 
      strcpy(reply, "Error, gps once not supported");
    }

    return true;
  }
  if (memcmp(command, "gps on", 6) == 0) {
    if (!setGpsEnabled(sensors, true)) {
      strcpy(reply, "Error, gps toggle not found");
    } else {
      strcpy(reply, "OK");
    }
    return true;
  }
  if (memcmp(command, "gps off", 7) == 0) {
    if (!setGpsEnabled(sensors, false)) {
      strcpy(reply, "Error, gps toggle not found");
    } else {
      strcpy(reply, "OK");
    }
    return true;
  }
  if (strcmp(command, "gps period") == 0) {
    if (getGpsPeriod() == 0) {
      strcpy(reply, "> 0 (continuous)");
    } else {
      sprintf(reply, "> %lu", (unsigned long)getGpsPeriod());
    }
    return true;
  }
  if (memcmp(command, "gps period ", 11) == 0) {
    uint32_t period;
    if (!parseDuration(command + 11, period)) {
      strcpy(reply, "Error, invalid gps period");
    } else if (!setGpsPeriod(sensors, period) ||
               !setGpsEnabled(sensors, true)) {
      strcpy(reply, "Error, gps provider not found");
    } else {
      strcpy(reply, "OK");
    }
    return true;
  }
  if (strcmp(command, "gps firstfix") == 0) {
    LocationProvider* location = sensors.getLocationProvider();
    if (location == nullptr) {
      strcpy(reply, "Error, gps provider not found");
    } else if (location->getFirstFixTimeout() == 0) {
      strcpy(reply, "> 0 (no limit)");
    } else {
      sprintf(reply, "> %lu", (unsigned long)location->getFirstFixTimeout());
    }
    return true;
  }
  if (memcmp(command, "gps firstfix ", 13) == 0) {
    LocationProvider* location = sensors.getLocationProvider();
    uint32_t seconds;
    if (location == nullptr) {
      strcpy(reply, "Error, gps provider not found");
    } else if (!parseDuration(command + 13, seconds) || seconds > 86400UL) {  // at most a day
      strcpy(reply, "Error, invalid first fix timeout");
    } else {
      location->setFirstFixTimeout(seconds);
      setFirstFixTimeout(seconds);  // saved by the caller (isDirty)
      strcpy(reply, "OK");
    }
    return true;
  }
  if (memcmp(command, "gps sync", 8) == 0) {
    LocationProvider* location = sensors.getLocationProvider();
    if (location == nullptr) {
      strcpy(reply, "Error, gps provider not found");
    } else {
      location->syncTime();
      strcpy(reply, "OK");
    }
    return true;
  }
  if (strcmp(command, "gps fix_stats") == 0) {
    LocationProvider* location = sensors.getLocationProvider();
    if (location == nullptr) {
      strcpy(reply, "Error, gps provider not found");
    } else {
      const LocationProvider::FixStats s = location->getFixStats();
      char last[12] = "-", max[12] = "-";
      if (s.maxTtffMs > 0) {
        formatDuration(last, sizeof(last), (s.lastTtffMs + 500) / 1000);
        formatDuration(max, sizeof(max), (s.maxTtffMs + 500) / 1000);
      }
      snprintf(reply, 160, "> ttf:%s/%s ok:%lu miss:%lu", last, max,
               (unsigned long)s.consecutiveFixes, (unsigned long)s.consecutiveMisses);
    }
    return true;
  }
#ifdef GPS_GUARD
  if (memcmp(command, "gps guard", 9) == 0 || memcmp(command, "gps fence", 9) == 0 ||
      strcmp(command, "gps ref") == 0) {
    LocationProvider* location = sensors.getLocationProvider();
    GpsGuard* guard = location != nullptr ? location->getGuard() : nullptr;
    if (guard == nullptr) {
      strcpy(reply, "Error, gps guard not supported");
    } else if (strcmp(command, "gps guard") == 0) {
      reply[0] = '>'; reply[1] = ' ';
      guard->formatStatus(reply + 2, 158);
    } else if (strcmp(command, "gps guard on") == 0 || strcmp(command, "gps guard off") == 0) {
      const bool enabled = command[11] == 'n';
      guard->setEnabled(enabled);
      setGuardEnabled(enabled);  // saved by the caller (isDirty)
      strcpy(reply, "OK");
    } else if (strcmp(command, "gps guard accept") == 0) {
      // operator confirms that the alerted GPS time is right: apply it to the clock
      long offset;
      if (!guard->isTimeAlert()) {
        strcpy(reply, "Error, no time alert");
      } else if (!location->getClockOffset(offset) ||
                 !guard->acceptAlertedTime(offset, (uint32_t)location->getTimestamp()) ||
                 !location->applyTimeNow()) {
        strcpy(reply, "Error, no current trusted fix with the alerted time");
      } else {
        strcpy(reply, "OK - clock set from GPS");
      }
    } else if (strcmp(command, "gps fence") == 0) {
      sprintf(reply, "> %lu", (unsigned long)guard->getFence());
    } else if (memcmp(command, "gps fence ", 10) == 0) {
      char* end;
      unsigned long metres = strtoul(command + 10, &end, 10);
      if (command[10] == '\0' || *end != '\0' || metres > 20000000UL) {  // at most half the Earth
        strcpy(reply, "Error, invalid fence");
      } else {
        guard->setFence((uint32_t)metres);
        setGuardFence((uint32_t)metres);  // saved by the caller (isDirty)
        strcpy(reply, "OK");
      }
    } else if (strcmp(command, "gps ref") == 0) {
      snprintf(reply, 160, "> %.6f, %.6f", guard->getRefLat(), guard->getRefLon());
    } else {
      return false;
    }
    return true;
  }
#endif
  if (strcmp(command, "gps diag") == 0) {
    LocationProvider* location = sensors.getLocationProvider();
    if (location == nullptr) {
      strcpy(reply, "Error, gps provider not found");
    } else {
      int prefix = snprintf(reply, 160, "> req:%u ", isGpsEnabled() ? 1U : 0U);
      if (prefix >= 0 && prefix < 160) location->formatDiagnostics(reply + prefix, 160 - prefix);
    }
    return true;
  }
  return false;
}
