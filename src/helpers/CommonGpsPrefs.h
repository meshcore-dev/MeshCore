#pragma once

#include "ConfigSerializer.h"
#include "SensorManager.h"

class CommonGpsPrefs : public ConfigSerializer {
  bool _is_dirty = false;

protected:
  CommonGpsPrefs() = default;

public:
  void markDirty() { _is_dirty = true; }
  void clearDirty() { _is_dirty = false; }
  bool isDirty() const { return _is_dirty; }

  virtual bool isGpsEnabled() const = 0;
  virtual void setGpsEnabled(bool enabled) = 0;
  virtual uint32_t getGpsPeriod() const = 0;
  virtual void setGpsPeriod(uint32_t seconds) = 0;
  // Saved first-fix timeout in seconds; false = not set, use the build default.
  virtual bool getFirstFixTimeout(uint32_t& seconds) const { return false; }
  virtual void setFirstFixTimeout(uint32_t seconds) { }

  // Reference position for the anti-spoofing guard (GPS_GUARD), e.g. the configured node position.
  virtual bool getGuardReference(double& lat, double& lon) const { return false; }
  // Saved fence radius for the guard; false = not set, use the build default.
  virtual bool getGuardFence(uint32_t& metres) const { return false; }
  virtual void setGuardFence(uint32_t metres) { }
  // Saved on/off state of the guard; false = not set, guard on.
  virtual bool getGuardEnabled(bool& enabled) const { return false; }
  virtual void setGuardEnabled(bool enabled) { }

  bool setGpsEnabled(SensorManager& sensors, bool enabled);
  void applyGuardReference(SensorManager& sensors) const;
  static void onClockSetManually(SensorManager& sensors);
  bool setGpsPeriod(SensorManager& sensors, uint32_t seconds);
  bool apply(SensorManager& sensors) const;
  bool handleCommand(const char* command, SensorManager& sensors, char* reply);
};
