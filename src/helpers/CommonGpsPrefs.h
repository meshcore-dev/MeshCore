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

  bool setGpsEnabled(SensorManager& sensors, bool enabled);
  bool setGpsPeriod(SensorManager& sensors, uint32_t seconds);
  bool apply(SensorManager& sensors) const;
  bool handleCommand(const char* command, SensorManager& sensors, char* reply);
};
