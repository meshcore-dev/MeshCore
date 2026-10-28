"""Native regressions for the Muzi GPS provider lifecycle and switch polling.

Run after building muzi_base_uno_superIO_companion_radio_ble so MicroNMEA is
available, or pass --micronmea /path/to/MicroNMEA/src. Only hardware is mocked.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--micronmea", type=Path, default=ROOT / ".pio/libdeps/muzi_base_uno_superIO_companion_radio_ble/MicroNMEA/src")
args = parser.parse_args()
compiler = shutil.which("g++")
if compiler is None:
    raise SystemExit("g++ is required")
if not (args.micronmea / "MicroNMEA.cpp").is_file():
    raise SystemExit("Build the Super IO environment first, or provide --micronmea")

ARDUINO = r'''
#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cctype>
#include "Stream.h"
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
inline uint32_t nowMs = 0;
inline int pins[64] = {};
inline uint32_t millis() { return nowMs; }
inline void delay(uint32_t amount) { nowMs += amount; }
inline void yield() {}
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int value) { pins[pin] = value; }
inline int digitalRead(int pin) { return pins[pin]; }
'''
MESH = r'''
#pragma once
#include <cstdint>
namespace mesh {
class RTCClock {
public:
  void setCurrentTime(uint32_t) {}
};
}
'''
RTC = r'''
#pragma once
#include <cstdint>
class DateTime {
public:
  DateTime(int, int, int, int, int, int) {}
  uint32_t unixtime() const { return 0; }
};
'''
PROVIDER = r'''
#include <cassert>
#include <deque>
#include <iostream>
#include <string>
#include <helpers/sensors/MicroNMEALocationProvider.h>
// The inherited declaration is unrelated to these lifecycle checks.
void LocationProvider::sendSentence(const char*) {}
class SerialInput : public Stream {
public:
  std::deque<char> input;
  void feed(const char* text) { while (*text) input.push_back(*text++); }
  int available() override { return int(input.size()); }
  int read() override {
    if (input.empty()) return -1;
    char value = input.front(); input.pop_front(); return value;
  }
};
int main() {
  SerialInput serial;
  RefCountedDigitalPin sharedPower(7);
  sharedPower.begin();
  MicroNMEALocationProvider location(serial, nullptr, 4, 3, &sharedPower);
  serial.feed("old UART bytes\r\n");
  location.begin();
#ifdef MUZI_BASE_SUPERIO
  assert(serial.available() == 0);
#else
  assert(serial.available() != 0);
  location.drain();
#endif
  assert(location.isEnabled() && pins[7] == HIGH);
  // Synthetic NMEA fixture; these are not coordinates read from a device.
  serial.feed("$GPGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*69\r\n");
  location.loop();
  assert(location.isValid() && location.satellitesCount() == 8);
#ifdef MUZI_BASE_SUPERIO
  // Repeated begin must not reset a live fix or leak a shared-power claim.
  location.begin();
  assert(location.isValid());
  char diagnostics[160]; location.formatDiagnostics(diagnostics, sizeof(diagnostics));
  assert(std::strstr(diagnostics, "bc:1 sc:0"));
#endif
  location.stop();
  assert(!location.isEnabled() && pins[7] == LOW);
#ifdef MUZI_BASE_SUPERIO
  assert(!location.isValid() && location.satellitesCount() == 0);
  serial.feed("stale UART bytes\r\n");
  location.begin();
  assert(!location.isValid() && serial.available() == 0);
  location.stop();
  assert(pins[7] == LOW);
#else
  // Boards outside Super IO keep their existing lifecycle behavior.
  assert(location.isValid());
#endif
  std::cout << "GPS provider lifecycle: PASS\n";
}
'''
MANAGER = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <Arduino.h>
#define ENV_INCLUDE_GPS 1
#define PIN_GPS_SWITCH 12
#define BUTTON_EVENT_CLICK 1
#define USER_BTN_POWER_OFF_CLICKS 5
struct LocationProvider {};
struct Button { void begin() {} int check() { return 0; } } pwr_btn;
static void muziPowerOff() { assert(false); }
class EnvironmentSensorManager {
protected:
  bool gps_active = false;
  void start_gps() { starts++; gps_active = true; }
  void stop_gps() { stops++; gps_active = false; }
public:
  int starts = 0, stops = 0;
  explicit EnvironmentSensorManager(LocationProvider&) {}
  virtual bool begin() { return true; }
  virtual void loop() {}
  bool active() const { return gps_active; }
};
#include "manager.inc"
int main() {
  LocationProvider location;
  pins[PIN_GPS_SWITCH] = HIGH;
  MuziBaseSensorManager manager(location);
  assert(manager.begin() && manager.active() && manager.starts == 1);
  assert(manager.begin() && manager.starts == 1);
  nowMs = 1; manager.loop();
  assert(manager.starts == 1 && manager.stops == 0);
  pins[PIN_GPS_SWITCH] = LOW;
  nowMs = 1001; manager.loop();
  assert(!manager.active() && manager.stops == 1);
  nowMs = 2001; manager.loop();
  assert(manager.stops == 1);
  // Exercise the future deadline when it wraps below the current timestamp.
  nowMs = 0x7FFFFFF0U; manager.loop();
  nowMs = 0xFFFFFFE0U; manager.loop();
  nowMs = 0xFFFFFFF0U; manager.loop();
  pins[PIN_GPS_SWITCH] = HIGH;
  nowMs = 0xFFFFFFF1U; manager.loop();
  assert(!manager.active());
  nowMs = 967U; manager.loop();
  assert(!manager.active());
  nowMs = 968U; manager.loop();
  assert(manager.active() && manager.starts == 2);
  std::cout << "GPS switch transitions and rollover: PASS\n";
}
'''


def section(path, start, end):
    source = path.read_text(encoding="utf-8")
    if source.count(start) != 1 or source.count(end) != 1:
        raise AssertionError("Production extraction anchors changed: " + str(path))
    return source[source.index(start):source.index(end, source.index(start))]


with tempfile.TemporaryDirectory(prefix="muzi-gps-") as temporary:
    output = Path(temporary)
    (output / "Arduino.h").write_text(ARDUINO)
    shutil.copyfile(ROOT / "test/mocks/Stream.h", output / "Stream.h")
    (output / "Mesh.h").write_text(MESH)
    (output / "RTClib.h").write_text(RTC)
    (output / "provider.cpp").write_text(PROVIDER)
    common = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-sign-compare",
              "-I" + str(output), "-I" + str(ROOT / "src"), "-I" + str(args.micronmea)]
    for superio in (False, True):
        executable = output / ("provider-" + str(superio) + ".exe")
        flags = ["-DMUZI_BASE_SUPERIO"] if superio else []
        subprocess.run([*common, *flags, str(output / "provider.cpp"),
                        str(args.micronmea / "MicroNMEA.cpp"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    declaration = section(ROOT / "variants/muzi_base/target.h",
                          "class MuziBaseSensorManager : public EnvironmentSensorManager {",
                          "extern MuziBaseSensorManager sensors;")
    methods = section(ROOT / "variants/muzi_base/target.cpp",
                      "bool MuziBaseSensorManager::begin() {", "\n#ifdef DISPLAY_CLASS\n")
    (output / "manager.inc").write_text(declaration + "\n" + methods)
    (output / "manager.cpp").write_text(MANAGER)
    executable = output / "manager.exe"
    subprocess.run([*common, str(output / "manager.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
