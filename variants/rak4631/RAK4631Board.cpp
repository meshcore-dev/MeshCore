#include <Arduino.h>
#include <Wire.h>
#include "nrf_gpio.h"

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

#ifdef WITH_W5100S_POE
  #include "W5100SPoE.h"
#endif

#ifdef NRF52_POWER_MANAGEMENT
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel     = PWRMGT_LPCOMP_REFSEL,
  // WITH_W5100S_POE = PoE operation without a battery.
  // isExternalPowered() only detects USB VBUS, not PoE power.
  // Without this exception checkBootVoltage() reads the floating
  // battery ADC pin (no battery) and triggers the protection
  // shutdown -> SYSTEMOFF loop -> red LED flicker.
#ifdef WITH_W5100S_POE
  .voltage_bootlock  = 0
#else
  .voltage_bootlock  = PWRMGT_VOLTAGE_BOOTLOCK
#endif
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

#ifdef WITH_W5100S_POE
  uint8_t w5100s_ver = w5100s_poe_init();
  (void)w5100s_ver;
  #ifdef MESH_DEBUG
    // Wait for USB-CDC to re-enumerate so this line isn't eaten during the
    // reconnect, then print it repeatedly to be sure it is seen. Debug only —
    // the PoE production build skips this and boots fast.
    delay(3000);
    for (int i = 0; i < 5; i++) {
      MESH_DEBUG_PRINTLN(">>> W5100S VERSIONR = 0x%02X (expect 0x51) <<<", w5100s_ver);
      delay(200);
    }
  #endif
#endif
}
