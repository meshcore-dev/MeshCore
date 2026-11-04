#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

// PRODUCTION_DEFINES
#define MESH_DEBUG_PRINTLN(...) ((void)0)
void delay(int) {}
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

class Ina228Driver {
public:
  uint16_t control = 0;
  uint16_t flags = 0;
  unsigned writes = 0;
  unsigned staleReads = 0;
  void enableAlert(bool enable_uvlo, bool active_high, bool latch_alert);
  bool writeRegister16(uint8_t reg, uint16_t value) {
    check(reg == INA228_REG_DIAG_ALRT, "unexpected register write");
    ++writes;
    control = value;
    return true;
  }
  uint16_t readRegister16(uint8_t reg) {
    check(reg == INA228_REG_DIAG_ALRT, "unexpected register read");
    if (staleReads) {
      --staleReads;
      return (control & ~INA228_DIAG_ALRT_SLOWALERT) | flags;
    }
    return control | flags;
  }
};

// PRODUCTION_METHOD

// INA228 datasheet Table 7-16: SLOWALERT selects raw or averaged comparison.
bool underVoltage(uint16_t diag, unsigned rawMv, unsigned averagedMv) {
  return ((diag & 0x2000) ? averagedMv : rawMv) < 2500;
}

int main() {
  const std::vector<std::pair<const char*, std::function<void()>>> tests = {
    {"UVLO uses averaging with latch and active-low polarity", [] {
      Ina228Driver ina;
      ina.enableAlert(true, false, true);
      check(ina.control == 0xa000, "expected SLOWALERT + ALATCH (0xa000)");
      check(ina.writes == 1, "successful configuration unexpectedly retried");
    }},
    {"latch and polarity remain independent of averaging", [] {
      Ina228Driver ina;
      ina.enableAlert(true, false, false);
      check(ina.control == 0x2000, "transparent active-low should retain averaging");
      ina.enableAlert(true, true, true);
      check(ina.control == 0xb000, "active-high should retain latch and averaging");
    }},
    {"disarming clears averaging and latch", [] {
      Ina228Driver ina;
      ina.enableAlert(true, false, true);
      ina.enableAlert(false, false, false);
      check(ina.control == 0, "disarm retained configuration bits");
      ina.enableAlert(false, true, true);
      check(ina.control == 0x9000, "disabled UVLO changed requested latch or polarity");
    }},
    {"readback retries if SLOWALERT did not stick", [] {
      Ina228Driver ina;
      ina.staleReads = 1;
      ina.enableAlert(true, false, true);
      check(ina.writes == 2, "missing SLOWALERT was accepted during readback");
      check(ina.control == 0xa000, "retry lost averaging");
    }},
    {"changing status flags do not trigger configuration retries", [] {
      Ina228Driver ina;
      ina.flags = 0x0fff;
      ina.enableAlert(true, false, true);
      check(ina.writes == 1, "status flags affected configuration verification");
    }},
    {"persistent configuration mismatch retains bounded retries", [] {
      Ina228Driver ina;
      ina.staleReads = 10;
      ina.enableAlert(true, false, true);
      check(ina.writes == 3, "configuration retry limit changed");
    }},
    {"raw voltage dip is filtered; sustained undervoltage still alerts", [] {
      Ina228Driver ina;
      ina.enableAlert(true, false, true);
      check(!underVoltage(ina.control, 2400, 3250), "brief raw dip triggered UVLO");
      check(underVoltage(ina.control, 2400, 2440), "real averaged undervoltage missed");
      check(!underVoltage(ina.control, 2400, 2500), "threshold equality triggered UVLO");
      check(underVoltage(0x8000, 2400, 3250), "unfiltered negative control did not alert");
    }}
  };
  unsigned failures = 0;
  for (const auto& test : tests) {
    try {
      test.second();
      std::cout << "PASS " << test.first << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
    }
  }
  std::cout << tests.size() - failures << '/' << tests.size() << " passed\n";
  return failures ? 1 : 0;
}
