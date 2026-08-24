#pragma once
//
// Concrete MeshCore platform bindings for Zephyr RTOS.
//
// These implement the abstract interfaces the mesh core depends on
// (mesh::MillisecondClock, mesh::RTCClock, mesh::RNG, mesh::MainBoard) using
// Zephyr kernel APIs, so an application can bring up the engine without any
// Arduino board package. Radio and transport bindings are separate (see the
// RadioLib wrappers / RadioLibZephyrHal.h).

#include <Mesh.h>
#include <Utils.h>

namespace mesh_zephyr {

// Monotonic millisecond clock backed by k_uptime_get().
class ZephyrMillisClock : public mesh::MillisecondClock {
public:
  unsigned long getMillis() override;
};

// Volatile UNIX-epoch clock. Advances from a settable base using uptime; use a
// hardware RTC binding instead where wall-clock persistence is required.
class ZephyrRTCClock : public mesh::RTCClock {
  uint32_t base_time;
  uint64_t base_uptime_ms;
public:
  ZephyrRTCClock();
  uint32_t getCurrentTime() override;
  void setCurrentTime(uint32_t time) override;
};

// Hardware RNG via Zephyr's entropy/random subsystem.
class ZephyrRNG : public mesh::RNG {
public:
  void random(uint8_t* dest, size_t sz) override;
};

// Minimal board: satisfies the pure-virtual mesh::MainBoard surface with sane
// defaults. Subclass and override for real battery/temperature/reboot support.
class ZephyrBoard : public mesh::MainBoard {
public:
  uint16_t getBattMilliVolts() override { return 0; }
  const char* getManufacturerName() const override { return "MeshCore/Zephyr"; }
  uint8_t getStartupReason() const override { return 0; }
  void reboot() override;
};

} // namespace mesh_zephyr
