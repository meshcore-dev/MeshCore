// Host-only state model. Production configuration functions/declarations are
// extracted at test runtime, not copied into this fixture. This models successful
// register writes and documented CELL resets; it does not simulate I2C failures,
// real MPPT tracking, ADC measurements, temperature or electrical transients.
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

enum bq25798_cell_count_t { BQ25798_CELL_COUNT_1S, BQ25798_CELL_COUNT_2S };
enum { BQ25798_TREG_60C = 60, BQ25798_WDT_DISABLE, BQ25798_VOC_DLY_2S,
       BQ25798_VOC_RATE_2MIN, BQ25798_VOC_PCT_81_25, BQ25798_TS_COOL_5C,
       BQ25798_TS_WARM_55C, BQ25798_JEITA_VSET_UNCHANGED };
#define MESH_DEBUG_PRINTLN(...) do {} while (false)
#define BQ_CE_PIN 42
enum { OUTPUT, LOW, HIGH };
static int ce_level = LOW;
static void pinMode(int, int) {}
static void digitalWrite(int, int level) { ce_level = level; }

struct FakeBQ {
  struct Event {
    std::string name;
    double value;
  };
  std::vector<Event> events;
  double battery = 2.63;
  bool source_present = true;
  double sysmin = 3.5;
  double vreg = 4.2;
  double ichg = 1.0;
  int cells = 1;
  int treg = 120;
  bool mppt = false;
  bool charging = false;

  void record(const char* name, double value) { events.push_back({name, value}); }
  bool linear() const { return source_present && charging && battery < sysmin; }
  void setCellCount(bq25798_cell_count_t count) {
    cells = count == BQ25798_CELL_COUNT_2S ? 2 : 1;
    // CELL writes restore defaults even when the cell count is unchanged.
    sysmin = cells == 2 ? 7.0 : 3.5;
    vreg = cells == 2 ? 8.4 : 4.2;
    ichg = 1.0;
    if (battery < sysmin) mppt = false;
    record("CELL", cells);
  }
  void setMinSystemV(float voltage) {
    // REG00 starts at 2.5 V in 250 mV steps.
    if (voltage < 2.5 || std::abs((voltage - 2.5) / 0.25 -
                                 std::round((voltage - 2.5) / 0.25)) > 0.001)
      throw std::runtime_error("Invalid VSYSMIN register value");
    sysmin = voltage;
    if (battery < sysmin) mppt = false;
    record("VSYSMIN", voltage);
  }
  void setChargeLimitV(float voltage) { vreg = voltage; record("VREG", voltage); }
  void setChargeLimitA(float amps) { ichg = amps; record("ICHG", amps); }
  void setChargeEnable(bool enabled) { charging = enabled; record("CHARGE", enabled); }
  void setMPPTenable(bool enabled) {
    mppt = enabled && source_present && battery >= sysmin;
    record("MPPT", enabled);
  }
  void setThermRegulationThresh(int threshold) { treg = threshold; }
  void setRechargeThreshOffsetV(float) {}
  void setPrechargeTimerEnable(bool) {}
  void setFastChargeTimerEnable(bool) {}
  void setTsIgnore(bool) {}
  void setWDT(int) {}
  void setExtILIMpin(bool) {}
  void setInputLimitA(float) {}
  void setICOEnable(bool) {}
  void setVOCdelay(int) {}
  void setVOCrate(int) {}
  void setVOCpercent(int) {}
  void setAutoDPinsDetection(bool) {}
  void setStatPinEnable(bool) {}
  void setTsCool(int) {}
  void setTsWarm(int) {}
  void setJeitaVSet(int) {}
  void setAutoIBATDIS(bool) {}
  void getTelemetryData(int) {}
};

class BoardConfigContainer {
 public:
#include "production_declarations.inc"
  FakeBQ bq;
  bool bqInitialized = true;
  bool leds_enabled = false;
  bool mppt_wish = true;
  uint16_t imax_mA = 200;
  bool configureBaseBQ();
  bool configureChemistry(BatteryType type);
  static const BatteryProperties* getBatteryProperties(BatteryType type);
  bool getMPPTEnabled() const { return mppt_wish; }
  uint16_t getMaxChargeCurrent_mA() const { return imax_mA; }
  void applyJeitaIgnore(const BatteryProperties*) { bq.record("JEITA", bq.ichg); }
};

#include "production_functions.inc"

static void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
static void near(double actual, double expected, const char* message) {
  require(std::abs(actual - expected) < 0.0001, message);
}
static void configure(BoardConfigContainer& board, BoardConfigContainer::BatteryType type) {
  // begin() and setBatteryType() both use this production sequence.
  require(board.configureBaseBQ(), "base configuration failed");
  require(!board.bq.mppt, "base must leave MPPT disabled before CELL programming");
  require(board.configureChemistry(type), "chemistry configuration failed");
}
static void check_final(const BoardConfigContainer& board, int cells, double vreg, double ichg) {
  const auto& bq = board.bq;
  near(bq.sysmin, 2.5, "VSYSMIN must be 2.5 V after CELL reset");
  near(bq.vreg, vreg, "chemistry VREG must be restored after CELL reset");
  near(bq.ichg, ichg, "configured ICHG must be restored after CELL reset");
  require(bq.cells == cells, "incorrect cell count");
  require(bq.treg == 60, "60 C thermal regulation must remain configured");
  require(bq.charging && ce_level == HIGH, "known chemistry should allow charging");
  require(!bq.linear(), "final configuration must leave BATFET outside linear region");
  int cell = -1, minimum = -1, current = -1, jeita = -1, mppt = -1;
  for (size_t i = 0; i < bq.events.size(); ++i) {
    const auto& event = bq.events[i];
    if (event.name == "CELL") cell = static_cast<int>(i);
    if (event.name == "VSYSMIN") minimum = static_cast<int>(i);
    if (event.name == "ICHG") current = static_cast<int>(i);
    if (event.name == "JEITA") jeita = static_cast<int>(i);
    if (event.name == "MPPT" && event.value) mppt = static_cast<int>(i);
  }
  require(cell >= 0 && minimum > cell, "VSYSMIN must be written after last CELL write");
  require(current > cell && jeita > current, "ICHG must be restored before JEITA override");
  if (board.mppt_wish) {
    require(mppt > minimum && mppt > current, "MPPT must be requested after final register restoration");
    require(bq.mppt, "MPPT should be accepted with source and VBAT above VSYSMIN");
  } else {
    require(mppt == -1 && !bq.mppt, "disabled MPPT preference must never be enabled");
  }
}

int main() {
  unsigned passed = 0;
  auto test = [&passed](const char* name, auto run) {
    run();
    ++passed;
    std::cout << "PASS " << name << '\n';
  };
  try {
    test("Na-ion at 2.63 V restores 2.50 V and immediately enables requested MPPT", [] {
      BoardConfigContainer board;
      configure(board, BoardConfigContainer::NAION_1S);
      check_final(board, 1, 3.9, 0.2);
    });
    test("LTO 2S at 4.90 V restores 2.50 V / 5.40 V / configured 1.50 A", [] {
      BoardConfigContainer board;
      board.bq.battery = 4.9;
      board.imax_mA = 1500;
      configure(board, BoardConfigContainer::LTO_2S);
      check_final(board, 2, 5.4, 1.5);
    });
    test("repeated chemistry changes restore all settings after every CELL write", [] {
      BoardConfigContainer board;
      for (int cycle = 0; cycle < 3; ++cycle) {
        board.bq.battery = 4.9;
        board.imax_mA = 1500;
        configure(board, BoardConfigContainer::LTO_2S);
        check_final(board, 2, 5.4, 1.5);
        board.bq.battery = 2.63;
        board.imax_mA = 200;
        configure(board, BoardConfigContainer::NAION_1S);
        check_final(board, 1, 3.9, 0.2);
      }
    });
    test("MPPT off preference never requests enable", [] {
      BoardConfigContainer board;
      board.mppt_wish = false;
      configure(board, BoardConfigContainer::NAION_1S);
      check_final(board, 1, 3.9, 0.2);
    });
    test("unknown battery disables charging, CE and previously active MPPT", [] {
      BoardConfigContainer board;
      configure(board, BoardConfigContainer::NAION_1S);
      require(board.bq.mppt, "precondition: MPPT active");
      configure(board, BoardConfigContainer::BAT_UNKNOWN);
      require(!board.bq.charging && ce_level == LOW, "unknown battery must prohibit charging");
      require(!board.bq.mppt, "unknown chemistry must not re-enable MPPT");
      require(board.mppt_wish, "configuration must preserve the stored MPPT preference");
    });
    test("below 2.50 V hardware rejects MPPT without erasing saved preference", [] {
      BoardConfigContainer board;
      board.bq.battery = 2.4;
      configure(board, BoardConfigContainer::NAION_1S);
      near(board.bq.sysmin, 2.5, "minimum register value must remain 2.5 V");
      require(!board.bq.mppt && board.mppt_wish, "MPPT must be rejected but its preference retained");
      require(board.bq.linear(), "below 2.5 V the model must still report linear operation");
    });
    test("absent source rejects MPPT while preserving saved preference", [] {
      BoardConfigContainer board;
      board.bq.source_present = false;
      configure(board, BoardConfigContainer::NAION_1S);
      require(!board.bq.mppt && board.mppt_wish, "no source: hardware MPPT off, preference on");
    });
    test("uninitialized BQ is not written", [] {
      BoardConfigContainer board;
      board.bqInitialized = false;
      require(!board.configureBaseBQ(), "uninitialized base should fail");
      require(!board.configureChemistry(BoardConfigContainer::NAION_1S), "uninitialized chemistry should fail");
      require(board.bq.events.empty(), "uninitialized BQ must not receive writes");
    });
    test("LiFePO4 chemistry retains 3.50 V charge voltage", [] {
      BoardConfigContainer board;
      board.bq.battery = 3.0;
      configure(board, BoardConfigContainer::LIFEPO4_1S);
      check_final(board, 1, 3.5, 0.2);
    });
    test("Li-ion chemistry retains 4.10 V charge voltage", [] {
      BoardConfigContainer board;
      board.bq.battery = 3.7;
      configure(board, BoardConfigContainer::LIION_1S);
      check_final(board, 1, 4.1, 0.2);
    });
  } catch (const std::exception& error) {
    std::cerr << "FAIL after " << passed << " passing cases: " << error.what() << '\n';
    return 1;
  }
  std::cout << passed << " cases passed (production configuration, simulated BQ register behavior)\n";
  return 0;
}
