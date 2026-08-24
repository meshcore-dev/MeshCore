//
// Zephyr platform bindings for the MeshCore abstract interfaces.
//
#include <MeshCoreZephyr.h>

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/reboot.h>

namespace mesh_zephyr {

// --- ZephyrMillisClock -----------------------------------------------------
unsigned long ZephyrMillisClock::getMillis() {
  return (unsigned long)k_uptime_get();
}

// --- ZephyrRTCClock --------------------------------------------------------
ZephyrRTCClock::ZephyrRTCClock() {
  base_time = 1715770351;  // 15 May 2024, matches the Arduino VolatileRTCClock default
  base_uptime_ms = (uint64_t)k_uptime_get();
}

uint32_t ZephyrRTCClock::getCurrentTime() {
  uint64_t elapsed_ms = (uint64_t)k_uptime_get() - base_uptime_ms;
  return base_time + (uint32_t)(elapsed_ms / 1000U);
}

void ZephyrRTCClock::setCurrentTime(uint32_t time) {
  base_time = time;
  base_uptime_ms = (uint64_t)k_uptime_get();
}

// --- ZephyrRNG -------------------------------------------------------------
void ZephyrRNG::random(uint8_t* dest, size_t sz) {
  sys_rand_get(dest, sz);
}

// --- ZephyrBoard -----------------------------------------------------------
void ZephyrBoard::reboot() {
  sys_reboot(SYS_REBOOT_COLD);
}

} // namespace mesh_zephyr
