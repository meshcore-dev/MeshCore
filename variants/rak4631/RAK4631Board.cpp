#include <Arduino.h>
#include <Wire.h>
#include "nrf_gpio.h"

#ifdef WATCHDOG_TIMEOUT_SECS
#include "nrf_wdt.h"

static_assert(WATCHDOG_TIMEOUT_SECS > 0, "WATCHDOG_TIMEOUT_SECS must be positive");
static_assert((uint64_t)WATCHDOG_TIMEOUT_SECS * 32768ULL <= UINT32_MAX,
              "WATCHDOG_TIMEOUT_SECS exceeds the nRF52840 WDT limit");
#endif

#include "RAK4631Board.h"

#ifdef ETHERNET_ENABLED
// Drive WB_IO2 HIGH as early as possible using direct register access.
// WB_IO2 (P1.02, Arduino pin 34) controls the WisBlock slot power switch.
// With POE through RAK13800, this must be enabled before the framework
// initializes or the board will brownout from insufficient power delivery.
// Priority 102 runs just after SystemInit.
static void __attribute__((constructor(102))) rak4631_early_poe_power() {
  nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(1, 2));  // WB_IO2 = P1.02
  nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(1, 2));
}
#endif

#ifdef NRF52_POWER_MANAGEMENT
// Static configuration for power management
// Values set in variant.h defines
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void RAK4631Board::initiateShutdown(uint8_t reason) {
  // Disable LoRa module power before shutdown
  digitalWrite(SX126X_POWER_EN, LOW);

  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
      reason == SHUTDOWN_REASON_BOOT_PROTECT) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

void RAK4631Board::begin() {
  NRF52BoardDCDC::begin();
  pinMode(PIN_VBAT_READ, INPUT);
#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif

#ifdef PIN_USER_BTN_ANA
  pinMode(PIN_USER_BTN_ANA, INPUT_PULLUP);
#endif


#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();

  pinMode(SX126X_POWER_EN, OUTPUT);
#ifdef NRF52_POWER_MANAGEMENT
  // Boot voltage protection check (may not return if voltage too low)
  // We need to call this after we configure SX126X_POWER_EN as output but before we pull high
  checkBootVoltage(&power_config);
#endif
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10);   // give sx1262 some time to power up
}

#ifdef WATCHDOG_TIMEOUT_SECS
void RAK4631Board::onBootComplete() {
  if (nrf_wdt_started(NRF_WDT)) return;

  nrf_wdt_behaviour_set(NRF_WDT, NRF_WDT_BEHAVIOUR_RUN_SLEEP);
  nrf_wdt_reload_value_set(NRF_WDT, (uint32_t)((uint64_t)WATCHDOG_TIMEOUT_SECS * 32768ULL));
  nrf_wdt_reload_request_enable(NRF_WDT, NRF_WDT_RR0);
  nrf_wdt_task_trigger(NRF_WDT, NRF_WDT_TASK_START);
  MESH_DEBUG_PRINTLN("Watchdog started with a %lu-second timeout", (unsigned long)WATCHDOG_TIMEOUT_SECS);
}

void RAK4631Board::resetWatchdog() {
  if (nrf_wdt_started(NRF_WDT)) {
    nrf_wdt_reload_request_set(NRF_WDT, NRF_WDT_RR0);
  }
}
#endif