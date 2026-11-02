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
      const char* activity = "active";
      if (!location->isEnabled()) {
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
              activity,
              fix,
              location->satellitesCount(), mode);
    } else {
      snprintf(reply, 160, "off, mode: %s", mode);
    }
    return true;
  }
  if (strcmp(command, "gps fix") == 0) {
    LocationProvider* location = sensors.getLocationProvider();
    if (location == nullptr) {
      strcpy(reply, "gps provider not found");
    } else if (!location->hasValidFix()) {
      strcpy(reply, "no fix");
    } else {
      const long latitude = location->getLatitude();
      const long longitude = location->getLongitude();
      const uint32_t fixAge = location->getLastFixAge();
      char age[24];
      if (fixAge == UINT32_MAX) {
        strcpy(age, "age unavailable");
      } else {
        snprintf(age, sizeof(age), "%lus ago", (unsigned long)fixAge);
      }
      snprintf(reply, 160, "%s%ld.%06ld, %s%ld.%06ld, %ldm, %s",
               latitude < 0 ? "-" : "", labs(latitude) / 1000000L,
               labs(latitude) % 1000000L,
               longitude < 0 ? "-" : "", labs(longitude) / 1000000L,
               labs(longitude) % 1000000L, location->getAltitude() / 1000L, age);
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
