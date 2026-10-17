#pragma once

#include <string.h>

namespace rak3401 {

enum class OnboardRadioState { Absent, Asleep, BusyTimeout, SleepFailed };

// IO owns the temporary SPI pin assignment; every outcome must release it.
template <typename IO>
OnboardRadioState sleepOnboardRadio(IO& io, const char* expectedVersion) {
  io.begin();
  io.reset();
  const auto state = [&]() {
    // A missing chip also times out: BUSY is pulled high until a radio drives it low.
    if (!io.waitBusy(false, 20)) return OnboardRadioState::BusyTimeout;

    char version[6];
    io.readVersion(version);
    if (memcmp(version, expectedVersion, sizeof(version)) != 0) {
      return OnboardRadioState::Absent;
    }
    io.releaseProbePulls();
    if (!io.waitBusy(false, 20)) return OnboardRadioState::BusyTimeout;

    io.sleep();
    // Reading over SPI here would wake the radio again. BUSY is a passive check.
    return io.waitBusy(true, 20) ? OnboardRadioState::Asleep : OnboardRadioState::SleepFailed;
  }();
  io.end();
  return state;
}

}
