// Production function bodies are extracted by run.py. This fixture supplies
// only hardware/dependency fakes; the sleep and retry decisions are production.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

struct EnteredSystemOff {};
struct Model {
  uint32_t now = 0;
  uint16_t voltage = 2400;
  std::vector<bool> rtc_results{false, false};
  std::vector<int> rtc_levels{1, 1};
  int rtc_calls = 0, recoveries = 0, rtc_level = 1, ina_level = 1;
  int initialized = 0, wdt_started = 0, wdt_fed = 0;
  int stopped = 0, radio_off = 0, ics_off = 0, ina_off = 0;
  int mppt = 0, soc = 0, hourly = 0, wire_ends = 0, wire_begins = 0;
  int flag_clears = 0, voltage_reads = 0;
  bool sticky_ina = false;
  std::vector<uint16_t> ticks;
  std::vector<std::string> events;
} model;

enum { LOW = 0, HIGH = 1, INPUT = 0, OUTPUT = 1, INPUT_PULLUP = 2, FALLING = 3 };
#define RTC_INT_PIN 17
#define INA_ALERT_PIN 34
#define BQ_CE_PIN 4
#define PIN_BOARD_SDA 13
#define PIN_BOARD_SCL 14
#define SX126X_POWER_EN 37
#define PIN_VBAT_READ 5
#define LED_BLUE 11
#define PIN_LED1 11
#define PIN_LED2 12
#define RTC_I2C_ADDR 0x52
#define INA228_I2C_ADDR 0x40
#define BQ25798_I2C_ADDR 0x6B
#define BME280_I2C_ADDR 0x76
#define RV3028_REG_STATUS 0x0E
#define LOW_VOLTAGE_SLEEP_MINUTES 60
#define SHUTDOWN_REASON_NONE 0
#define SHUTDOWN_REASON_LOW_VOLTAGE 1
#define GPREGRET2_LOW_VOLTAGE_SLEEP 4
#define SOLAR_MPPT_INTERVAL_MS 60000UL
enum { GPIO_PIN_CNF_DIR_Input = 0, GPIO_PIN_CNF_DIR_Pos = 0,
       GPIO_PIN_CNF_INPUT_Connect = 0, GPIO_PIN_CNF_INPUT_Pos = 0,
       GPIO_PIN_CNF_PULL_Pullup = 0, GPIO_PIN_CNF_PULL_Pos = 0,
       GPIO_PIN_CNF_DRIVE_S0S1 = 0, GPIO_PIN_CNF_DRIVE_Pos = 0,
       GPIO_PIN_CNF_SENSE_Low = 0, GPIO_PIN_CNF_SENSE_Pos = 0 };
template<typename... Args> void debug(const char*, Args...) {}
#define MESH_DEBUG_PRINTLN(...) debug(__VA_ARGS__)

struct Power { uint8_t GPREGRET2 = 0; uint32_t SYSTEMOFF = 0; } power;
struct Gpio { uint32_t PIN_CNF[64]{}; uint32_t LATCH = 0; } gpio;
auto* NRF_POWER = &power;
auto* NRF_GPIO = &gpio;
auto* NRF_P0 = &gpio;
uint32_t millis() { return model.now; }
void delay(uint32_t ms) { model.now += ms; }
void pinMode(int, int) {}
void digitalWrite(int, int) {}
int digitalRead(int pin) { return pin == RTC_INT_PIN ? model.rtc_level : model.ina_level; }
int digitalPinToInterrupt(int pin) { return pin; }
void attachInterrupt(int, void(*)(), int) {}
void blinkRed(int, int, int, bool) {}
void sd_power_system_off() { model.events.push_back("systemoff"); throw EnteredSystemOff{}; }
void __WFE() { throw std::runtime_error("unexpected fallback WFE"); }

struct FakeWire {
  int available_bytes = 0;
  void setPins(int, int) {}
  void begin() { ++model.wire_begins; model.events.push_back("wire.begin"); }
  void end() { ++model.wire_ends; model.events.push_back("wire.end"); }
  void beginTransmission(int) {}
  void write(int) {}
  int endTransmission(bool = true) { return 0; }
  int requestFrom(int, int count) { available_bytes = count; return count; }
  int available() { return available_bytes; }
  int read() { --available_bytes; return 0; }
} Wire;

struct Ina228Driver {
  static uint16_t readVBATDirect(FakeWire*, uint8_t) { return model.voltage; }
  uint16_t readVoltage_mV() { ++model.voltage_reads; return model.voltage; }
  void enableAlert(bool, bool, bool, bool = false) {}
  bool setUnderVoltageAlert(int) { return true; }
  void shutdown() { ++model.ina_off; model.events.push_back("ina.off"); }
  void clearAlert() {
    ++model.flag_clears;
    if (!model.sticky_ina) model.ina_level = HIGH;
  }
} ina;

struct BoardConfigContainer {
  enum BatteryType { BAT_UNKNOWN, BAT_NA_ION_1S };
  struct BatteryProperties { bool charge_enable; };
  static const BatteryProperties* getBatteryProperties(BatteryType) {
    static BatteryProperties props{true}; return &props;
  }
  static inline Ina228Driver* ina228DriverInstance = &ina;
  static inline bool lowVoltageAlertFired = false;
  static inline uint16_t lowVoltageSleepMv = 2500;
  static inline bool leds_enabled = false;
  static inline struct { int unused; } mpptStats{};
  uint32_t lastMpptMs = 0, lastSocMs = 0, lastLowVoltageMs = 0, lastHourlyMs = 0;
  bool tickInitialized = false, lowVoltageSleepRetryPending = false;
  uint32_t lastLowVoltageSleepAttemptMs = 0;
  BatteryType getBatteryType() { return BAT_NA_ION_1S; }
  bool getMPPTEnabled() { return true; }
  Ina228Driver* getIna228Driver() { return &ina; }
  void begin() { ++model.initialized; model.events.push_back("board.begin"); }
  bool getStationAltitude(float&) { return false; }
  void setLowVoltageRecovery() {}
  static bool setSOCManually(float) { return true; }
  static void setupWatchdog() { ++model.wdt_started; }
  static void feedWatchdog() { ++model.wdt_fed; }
  static void setUsbConnected(bool) {}
  static void stopBackgroundTasks() { ++model.stopped; model.events.push_back("tasks.stop"); }
  void tickPeriodic();
  void runMpptCycle() { ++model.mppt; }
  void updateBatterySOC() { ++model.soc; }
  void updateHourlyStats() { ++model.hourly; }
};
struct NRF52BoardDCDC { void begin() {} };
struct InheroMr2Board : NRF52BoardDCDC {
  static inline volatile bool rtc_irq_pending = false;
  static inline volatile uint32_t ota_dfu_reset_at = 0;
  void begin();
  void loop();
  void initiateShutdown(uint8_t);
  bool configureRTCWake(uint32_t);
  uint16_t getLowVoltageWakeThreshold() { return 2700; }
  uint16_t getLowVoltageSleepThreshold() { return 2500; }
  static void rtcInterruptHandler() { rtc_irq_pending = true; }
  void enterOTADfu() { throw std::runtime_error("unexpected DFU"); }
  void sleep(uint32_t) {}
} board;
static BoardConfigContainer boardConfig;
struct Sensors {
  void setBme280StationAltitude(float) {}
  void setBoardTelemetrySource(BoardConfigContainer&) {}
} sensors;

namespace inhero {
void clearTimerFlag() {}
void recoverI2cBus(int, int) { ++model.recoveries; model.events.push_back("recover"); }
bool configurePeriodicWake(uint16_t ticks) {
  auto index = static_cast<size_t>(model.rtc_calls++);
  model.ticks.push_back(ticks);
  model.events.push_back("rtc.configure");
  model.rtc_level = model.rtc_levels.at(std::min(index, model.rtc_levels.size() - 1));
  return model.rtc_results.at(std::min(index, model.rtc_results.size() - 1));
}
void maintainSolarDuringLowVoltageWake(bool) {}
void prepareIcsForSystemOff() { ++model.ics_off; model.events.push_back("ics.off"); }
void prepareRadioForSystemOff(bool = true) { ++model.radio_off; model.events.push_back("radio.off"); }
void disconnectLeakyPullups() {}
bool isUsbPowered() { return false; }
void serviceUsbAutoManagement() {}
}

#include "production.inc"

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void reset() {
  model = Model{};
  power = Power{};
  gpio = Gpio{};
  boardConfig = BoardConfigContainer{};
  BoardConfigContainer::lowVoltageAlertFired = false;
  BoardConfigContainer::lowVoltageSleepMv = 2500;
  InheroMr2Board::rtc_irq_pending = false;
  InheroMr2Board::ota_dfu_reset_at = 0;
}
template<typename Operation> bool slept(Operation operation) {
  try { operation(); } catch (const EnteredSystemOff&) { return true; }
  return false;
}
void no_shutdown_effects() {
  require(model.stopped == 0 && model.radio_off == 0 && model.ics_off == 0 && model.ina_off == 0,
          "failed wake validation must not shut down tasks, radio or INA");
  require(power.GPREGRET2 == 0, "failed wake validation must not leave a shutdown marker");
}

int main() {
  try {
    reset();
    require(!slept([] { board.initiateShutdown(SHUTDOWN_REASON_LOW_VOLTAGE); }), "failed RTC must not sleep");
    no_shutdown_effects();
    require(model.rtc_calls == 2 && model.recoveries == 1, "RTC setup must have one bounded recovery retry");

    reset(); model.rtc_results = {true, true}; model.rtc_levels = {LOW, LOW};
    require(!slept([] { board.initiateShutdown(SHUTDOWN_REASON_LOW_VOLTAGE); }), "asserted RTC INT must not sleep");
    no_shutdown_effects();
    require(model.rtc_calls == 2 && model.recoveries == 1, "stuck INT should get one bounded recovery retry");

    reset(); model.rtc_results = {true, true}; model.rtc_levels = {LOW, HIGH};
    require(slept([] { board.initiateShutdown(SHUTDOWN_REASON_LOW_VOLTAGE); }), "released RTC INT on retry must allow sleep");
    require(model.rtc_calls == 2 && model.recoveries == 1, "recovered INT needs exactly one recovery retry");

    reset(); model.rtc_results = {false, true};
    require(slept([] { board.initiateShutdown(SHUTDOWN_REASON_LOW_VOLTAGE); }), "recovered RTC must allow sleep");
    require(model.rtc_calls == 2 && model.recoveries == 1 && model.stopped == 1,
            "validated retry must proceed to one shutdown");
    require(power.GPREGRET2 == (GPREGRET2_LOW_VOLTAGE_SLEEP | SHUTDOWN_REASON_LOW_VOLTAGE),
            "successful sleep must retain UVLO reason");
    auto validated = std::find(model.events.begin(), model.events.end(), "tasks.stop");
    require(std::count(model.events.begin(), validated, "rtc.configure") == 2,
            "RTC validation must precede destructive shutdown work");

    reset(); model.rtc_results = {true};
    require(board.configureRTCWake(0), "default interval must validate");
    require(model.ticks.back() == LOW_VOLTAGE_SLEEP_MINUTES, "zero uses configured sleep interval");
    require(board.configureRTCWake(9000) && model.ticks.back() == 4095, "interval must clamp to RTC width");
    require(model.recoveries == 0, "healthy wake configuration needs no bus recovery");

    for (bool wake : {false, true}) {
      reset();
      if (wake) power.GPREGRET2 = GPREGRET2_LOW_VOLTAGE_SLEEP | SHUTDOWN_REASON_LOW_VOLTAGE;
      require(!slept([] { board.begin(); }), "failed early RTC must continue normal boot");
      no_shutdown_effects();
      require(model.initialized == 1 && model.wdt_started == 1, "failed early sleep must reach board init and watchdog");
      require(model.rtc_calls == 2, "early wake failure must not retry through a second boot path");

      reset(); model.rtc_results = {true};
      if (wake) power.GPREGRET2 = GPREGRET2_LOW_VOLTAGE_SLEEP | SHUTDOWN_REASON_LOW_VOLTAGE;
      require(slept([] { board.begin(); }), "validated early low voltage must sleep");
      require(model.initialized == 0 && model.wdt_started == 0, "successful early sleep must keep fast path");
      require(model.rtc_calls == 1, "healthy early path needs exactly one RTC validation");
    }

    reset(); model.voltage = 0;
    power.GPREGRET2 = GPREGRET2_LOW_VOLTAGE_SLEEP | SHUTDOWN_REASON_LOW_VOLTAGE;
    require(!slept([] { board.begin(); }), "unreadable VBAT plus RTC failure must not trap fast wake in sleep");
    no_shutdown_effects();
    require(model.initialized == 1 && model.wdt_started == 1 && model.rtc_calls == 2,
            "unreadable fast-wake voltage must reach board init and WDT after bounded RTC failure");

    reset(); model.voltage = 3250;
    power.GPREGRET2 = GPREGRET2_LOW_VOLTAGE_SLEEP | SHUTDOWN_REASON_LOW_VOLTAGE;
    require(!slept([] { board.begin(); }), "recovered voltage must boot normally");
    require(model.initialized == 1 && model.wdt_started == 1 && model.rtc_calls == 0,
            "voltage recovery must not depend on RTC wake availability");

    reset(); model.now = 3600000;
    BoardConfigContainer::lowVoltageAlertFired = true;
    board.loop();
    no_shutdown_effects();
    require(boardConfig.lowVoltageSleepRetryPending, "failed runtime sleep must enter retry state");
    require(model.wdt_fed == 1 && model.mppt == 1 && model.soc == 1 && model.hourly == 1,
            "failed shutdown must preserve periodic work and watchdog feeds");
    auto retry_at = boardConfig.lastLowVoltageSleepAttemptMs;
    model.now = retry_at + 59999;
    BoardConfigContainer::lowVoltageAlertFired = true;
    model.ina_level = LOW;
    board.loop();
    require(model.rtc_calls == 2 && model.wdt_fed == 2, "ISR and asserted ALERT must respect 60s cooldown");
    model.now = retry_at + 60000;
    model.rtc_results = {true};
    require(slept([] { board.loop(); }), "valid persistent low voltage retries after cooldown");

    for (uint16_t voltage : {uint16_t(0), uint16_t(3250)}) {
      reset(); BoardConfigContainer::lowVoltageAlertFired = true; board.loop();
      model.voltage = voltage;
      model.rtc_results = {true};
      model.ina_level = LOW;
      BoardConfigContainer::lowVoltageAlertFired = true;
      model.now = boardConfig.lastLowVoltageSleepAttemptMs + 60000;
      require(!slept([] { board.loop(); }), "invalid/healed voltage must not retry stale alert");
      no_shutdown_effects();
      require(model.rtc_calls == 2, "invalid/healed voltage must not reconfigure RTC");
      require(boardConfig.lowVoltageSleepRetryPending == (voltage == 0), "valid healed voltage releases retry state");
    }

    reset(); BoardConfigContainer::lowVoltageAlertFired = true; board.loop();
    model.voltage = 3250; model.sticky_ina = true; model.ina_level = LOW;
    model.rtc_results = {true};
    model.now = boardConfig.lastLowVoltageSleepAttemptMs + 60000;
    require(!slept([] { board.loop(); }), "healthy voltage with stuck ALERT must remain awake");
    require(boardConfig.lowVoltageSleepRetryPending, "stuck ALERT must remain guarded against stale retries");
    model.now += 1; board.loop();
    require(model.rtc_calls == 2, "stuck pin must not retrigger shutdown on next loop");

    reset(); model.now = UINT32_MAX - 30000;
    BoardConfigContainer::lowVoltageAlertFired = true; board.loop();
    model.now = boardConfig.lastLowVoltageSleepAttemptMs + uint32_t(60000);
    model.rtc_results = {true};
    require(slept([] { board.loop(); }), "retry cooldown must survive millis wraparound");

    std::cout << "PASS MR2 UVLO flow: RTC/INT failures, bounded recovery, boot/runtime gates, cooldown, voltage recovery, wraparound\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
