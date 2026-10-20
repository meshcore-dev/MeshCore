#include <Arduino.h>
#include <SPI.h>
#include "target.h"

LilyGoT3STM32Board board;
RADIO_CLASS radio = new STM32WLx_Module();
WRAPPER_CLASS radio_driver(radio, board);
VolatileRTCClock rtc_clock;
SensorManager sensors;
DISPLAY_CLASS display;
// PH3 is also BOOT0: the board's button is active high with an external pull-down.
MomentaryButton user_btn(PIN_USER_BTN, 1000, false);

static const uint32_t rfswitch_pins[] = {
  PB2, PB6, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC
};
static const Module::RfSwitchMode_t rfswitch_table[] = {
  {STM32WLx::MODE_IDLE,  {LOW, LOW}},
  {STM32WLx::MODE_RX,    {HIGH, LOW}},
  {STM32WLx::MODE_TX_HP, {LOW, HIGH}},
  END_OF_MODE_TABLE,
};

void LilyGoT3STM32Board::begin() {
  STM32Board::begin();

  // The SD card shares SPI1 with the OLED; keep it deselected.
  digitalWrite(PA4, HIGH);
  pinMode(PA4, OUTPUT);
  digitalWrite(PIN_OLED_CS, HIGH);
  pinMode(PIN_OLED_CS, OUTPUT);
  pinMode(PIN_USER_BTN, INPUT);
  // Both onboard LEDs are active high. Leave them off until explicitly needed.
  digitalWrite(PA0, LOW);
  pinMode(PA0, OUTPUT);
  digitalWrite(PA1, LOW);
  pinMode(PA1, OUTPUT);

  SPI.setSCLK(PIN_SPI_SCK);
  SPI.setMISO(PIN_SPI_MISO);
  SPI.setMOSI(PIN_SPI_MOSI);
  SPI.begin();
  delay(200); // OLED power-on settling time used by the manufacturer's example.
}

#ifndef LORA_CR
  #define LORA_CR 5
#endif

static bool check_radio_status(int status, const char* stage) {
  if (status == RADIOLIB_ERR_NONE) return true;

  Serial.print("ERROR: ");
  Serial.print(stage);
  Serial.print(": ");
  Serial.println(status);
  return false;
}

bool radio_init() {
  radio.setRfSwitchTable(rfswitch_pins, rfswitch_table);
  int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                          RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER,
                          16, STM32WL_TCXO_VOLTAGE, 0);
  if (!check_radio_status(status, "radio.begin")) return false;

  // Match the manufacturer's 3.3 V power-LDO trim (REG_DRV_CTRL).
  const uint8_t power_ldo_3v3 = 0x0E;
  status = radio.writeRegister(0x091F, &power_ldo_3v3, 1);
  if (!check_radio_status(status, "power LDO trim")) return false;

  status = radio.setCurrentLimit(SX126X_CURRENT_LIMIT);
  if (!check_radio_status(status, "current limit")) return false;
  status = radio.setRxBoostedGainMode(RX_BOOSTED_GAIN);
  if (!check_radio_status(status, "RX gain")) return false;
  return check_radio_status(radio.setCRC(1), "CRC");
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
