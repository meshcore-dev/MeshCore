#include "CommonGpsPrefs.h"
#include <helpers/sensors/LocationProvider.h>

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

bool CommonGpsPrefs::apply(SensorManager& sensors) const {
  LocationProvider* location = sensors.getLocationProvider();
  if (location == nullptr) return false;
  location->setGpsPeriod(getGpsPeriod());
  location->setGpsOn(isGpsEnabled());
  return true;
}

bool CommonGpsPrefs::handleCommand(const char* command, SensorManager& sensors, char* reply) {
  if (strcmp(command, "gps") == 0) {
    LocationProvider* location = sensors.getLocationProvider();
    char mode[32];
    if (getGpsPeriod() == 0) {
      strcpy(mode, "tracking");
    } else {
      snprintf(mode, sizeof(mode), "periodic (%lus)",
               (unsigned long)getGpsPeriod());
    }
    if (location == nullptr) {
      strcpy(reply, "gps provider not found");
    } else if (location->isGpsOn()) {
      char fix[32];
      if (location->isValid()) {
        const uint32_t fixAge = location->getLastFixAge();
        if (fixAge == UINT32_MAX) {
          strcpy(fix, "fix");
        } else {
          snprintf(fix, sizeof(fix), "fix (%lus ago)",
                   (unsigned long)fixAge);
        }
      } else {
        strcpy(fix, "no fix");
      }
      sprintf(reply, "on, %s, %s, %d sats, mode: %s",
              location->isEnabled() ? "active" : "sleeping",
              fix,
              location->satellitesCount(), mode);
    } else {
      snprintf(reply, 160, "off, mode: %s", mode);
    }
    return true;
  }
  if (memcmp(command, "gps on", 6) == 0) {
    if (!setGpsEnabled(sensors, true)) {
      strcpy(reply, "gps toggle not found");
    } else {
      strcpy(reply, "ok");
    }
    return true;
  }
  if (memcmp(command, "gps off", 7) == 0) {
    if (!setGpsEnabled(sensors, false)) {
      strcpy(reply, "gps toggle not found");
    } else {
      strcpy(reply, "ok");
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
    char* end = nullptr;
    unsigned long period = strtoul(command + 11, &end, 10);
    if (command[11] == '\0' || *end != '\0' || period > 86400) {
      strcpy(reply, "gps period must be 0-86400 seconds");
    } else {
      if (!setGpsPeriod(sensors, (uint32_t)period) ||
          !setGpsEnabled(sensors, true)) {
        strcpy(reply, "gps provider not found");
      } else {
        strcpy(reply, "ok");
      }
    }
    return true;
  }
  if (memcmp(command, "gps sync", 8) == 0) {
    LocationProvider* location = sensors.getLocationProvider();
    if (location == nullptr) {
      strcpy(reply, "gps provider not found");
    } else {
      location->syncTime();
      strcpy(reply, "ok");
    }
    return true;
  }
  if (strcmp(command, "gps diag") == 0) {
    LocationProvider* location = sensors.getLocationProvider();
    if (location == nullptr) {
      strcpy(reply, "gps provider not found");
    } else {
      int prefix = snprintf(reply, 160, "req:%u ", isGpsEnabled() ? 1U : 0U);
      if (prefix >= 0 && prefix < 160) location->formatDiagnostics(reply + prefix, 160 - prefix);
    }
    return true;
  }
  return false;
}
