#include <Arduino.h>
#include <Wire.h>
#include "nrf_gpio.h"

#include "RAK4631Board.h"

#if __has_include(<bluefruit.h>)
#include <bluefruit.h>
#endif

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

#if defined(PIN_USER_BTN_ANA) && (PIN_USER_BTN_ANA >= 0)
  #define BOARD_BUTTON_PIN PIN_USER_BTN_ANA
#elif defined(WB_A1)
  #define BOARD_BUTTON_PIN WB_A1
#elif defined(PIN_USER_BTN) && (PIN_USER_BTN >= 0)
  #define BOARD_BUTTON_PIN PIN_USER_BTN
#endif

static bool isButtonPressed() {
#if defined(PIN_USER_BTN_ANA) && (PIN_USER_BTN_ANA >= 0)
  return analogRead(PIN_USER_BTN_ANA) < 20;
#elif defined(BOARD_BUTTON_PIN)
  return digitalRead(BOARD_BUTTON_PIN) == LOW;
#else
  return false;
#endif
}

void RAK4631Board::powerOff() {
#if defined(BOARD_BUTTON_PIN)
  // Turn on LED to indicate long press was recognized and signal to release
  #if defined(PIN_LED1)
  digitalWrite(PIN_LED1, HIGH);
  #elif defined(PIN_LED2)
  digitalWrite(PIN_LED2, HIGH);
  #endif

  // Wait until the user physically releases the button and it remains stably released
  uint32_t stable_start = millis();
  while (millis() - stable_start < 100) {
    if (isButtonPressed()) {
      stable_start = millis();
    }
    delay(5);
  }

  // Turn off LEDs before shutdown
  #if defined(PIN_LED1)
  digitalWrite(PIN_LED1, LOW);
  #endif
  #if defined(PIN_LED2)
  digitalWrite(PIN_LED2, LOW);
  #endif
#endif

#ifdef NRF52_POWER_MANAGEMENT
  initiateShutdown(SHUTDOWN_REASON_USER);
#else
  NRF52BoardDCDC::powerOff();
#endif
}

void RAK4631Board::initiateShutdown(uint8_t reason) {
#if __has_include(<bluefruit.h>)
  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) {
    Bluefruit.autoConnLed(false);
    uint16_t conn_hdl = Bluefruit.connHandle();
    if (conn_hdl != BLE_CONN_HANDLE_INVALID) {
      Bluefruit.disconnect(conn_hdl);
      delay(50);
    }
    Bluefruit.Advertising.stop();
  }
#endif

  // Disconnect LoRa pins and detach RadioLib interrupts before powering down
  // Otherwise, the collapsing power rail on the SX1262 triggers a false DETECT wakeup
  detachInterrupt(digitalPinToInterrupt(P_LORA_DIO_1));
#ifdef P_LORA_BUSY
  detachInterrupt(digitalPinToInterrupt(P_LORA_BUSY));
  nrf_gpio_cfg_default(g_ADigitalPinMap[P_LORA_BUSY]);
#endif
#ifdef P_LORA_RESET
  nrf_gpio_cfg_default(g_ADigitalPinMap[P_LORA_RESET]);
#endif
#ifdef P_LORA_NSS
  nrf_gpio_cfg_default(g_ADigitalPinMap[P_LORA_NSS]);
#endif
  nrf_gpio_cfg_default(g_ADigitalPinMap[P_LORA_DIO_1]);

  // Disable LoRa module power before shutdown
  digitalWrite(SX126X_POWER_EN, LOW);

#if defined(BOARD_BUTTON_PIN)
  // Ensure pullup is engaged and line is stably HIGH before arming SENSE
  pinMode(BOARD_BUTTON_PIN, INPUT_PULLUP);
  uint32_t settle_start = millis();
  while (millis() - settle_start < 50) {
    if (digitalRead(BOARD_BUTTON_PIN) == LOW) {
      settle_start = millis();
    }
    delay(5);
  }

  // Ensure no other pin has SENSE enabled
  for (uint32_t p = 0; p < 32; p++) {
    if (p != g_ADigitalPinMap[BOARD_BUTTON_PIN]) {
      nrf_gpio_cfg_sense_set(p, NRF_GPIO_PIN_NOSENSE);
    }
  }
#if defined(NRF_P1)
  for (uint32_t p = 32; p < 48; p++) {
    if (p != g_ADigitalPinMap[BOARD_BUTTON_PIN]) {
      nrf_gpio_cfg_sense_set(p, NRF_GPIO_PIN_NOSENSE);
    }
  }
#endif

  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[BOARD_BUTTON_PIN], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#endif

  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
      reason == SHUTDOWN_REASON_BOOT_PROTECT) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  // Record shutdown reason in GPREGRET2
  uint8_t sd_on = 0;
  sd_softdevice_is_enabled(&sd_on);
  if (sd_on) {
    sd_power_gpregret_clr(1, 0xFF);
    sd_power_gpregret_set(1, reason);
  } else {
    NRF_POWER->GPREGRET2 = reason;
  }

  Serial.flush();
  delay(50);

  // Ensure all LEDs are explicitly powered off and disconnected right before entering sleep
#if defined(PIN_LED1)
  digitalWrite(PIN_LED1, LOW);
  pinMode(PIN_LED1, INPUT);
  nrf_gpio_pin_clear(g_ADigitalPinMap[PIN_LED1]);
  nrf_gpio_cfg_default(g_ADigitalPinMap[PIN_LED1]);
#endif
#if defined(PIN_LED2)
  digitalWrite(PIN_LED2, LOW);
  pinMode(PIN_LED2, INPUT);
  nrf_gpio_pin_clear(g_ADigitalPinMap[PIN_LED2]);
  nrf_gpio_cfg_default(g_ADigitalPinMap[PIN_LED2]);
#endif

#if defined(BOARD_BUTTON_PIN)
  // Ensure the button is unpressed before clearing latch
  while (digitalRead(BOARD_BUTTON_PIN) == LOW) {
    delay(10);
  }
#endif

  // Clear any latched GPIO DETECT events right before sleep
  NRF_P0->LATCH = 0xFFFFFFFF;
#if defined(NRF_P1)
  NRF_P1->LATCH = 0xFFFFFFFF;
#endif

  if (sd_on) {
    sd_power_system_off();
  }
  // Force hardware system off directly
  NRF_POWER->SYSTEMOFF = POWER_SYSTEMOFF_SYSTEMOFF_Enter;
  while (1) {
    NRF_POWER->SYSTEMOFF = POWER_SYSTEMOFF_SYSTEMOFF_Enter;
  }
}
#endif // NRF52_POWER_MANAGEMENT

void RAK4631Board::begin() {
  NRF52BoardDCDC::begin();
  pinMode(PIN_VBAT_READ, INPUT);
#if defined(BOARD_BUTTON_PIN)
  pinMode(BOARD_BUTTON_PIN, INPUT_PULLUP);
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

void RAK4631Board::loop() {
  NRF52BoardDCDC::loop();

#if defined(BOARD_BUTTON_PIN)
  if (isButtonPressed()) {
    if (btn_press_start == 0) {
      btn_press_start = millis();
    } else if (millis() - btn_press_start >= 3000) {
      powerOff();
    }
  } else {
    btn_press_start = 0;
  }
#endif
}