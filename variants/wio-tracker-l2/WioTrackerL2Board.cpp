#include "WioTrackerL2Board.h"

// TCA9535 register map
static const uint8_t TCA_REG_OUTPUT0 = 0x02;
static const uint8_t TCA_REG_OUTPUT1 = 0x03;
static const uint8_t TCA_REG_CONFIG0 = 0x06;  // 1 = input, 0 = output
static const uint8_t TCA_REG_CONFIG1 = 0x07;

// ADS1115 registers / config bits
static const uint8_t ADS_REG_CONVERSION = 0x00;
static const uint8_t ADS_REG_CONFIG     = 0x01;
// OS=1 (start single shot) | MUX=100 (AIN0 vs GND) | PGA=001 (+/-4.096V)
// | MODE=1 (single shot) | DR=100 (128 SPS) | COMP_QUE=11 (disabled)
static const uint16_t ADS_CFG_BATT = 0x8000 | 0x4000 | 0x0200 | 0x0100 | 0x0080 | 0x0003;
// +/-4.096V FSR -> 125uV/LSB; battery is behind a x2 divider -> 0.25 mV/LSB
static const float ADS_MV_PER_LSB = 0.125f * 2.0f;

bool WioTrackerL2Board::expWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(TCA9535_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

void WioTrackerL2Board::expSetOutput(uint8_t pin, bool initial_level) {
  uint8_t port = pin >> 3, bit = pin & 7;
  // set desired level first so the pin doesn't glitch when direction flips
  if (initial_level) out_shadow[port] |= (1 << bit);
  else out_shadow[port] &= ~(1 << bit);
  expWriteReg(port ? TCA_REG_OUTPUT1 : TCA_REG_OUTPUT0, out_shadow[port]);

  cfg_shadow[port] &= ~(1 << bit);  // 0 = output
  expWriteReg(port ? TCA_REG_CONFIG1 : TCA_REG_CONFIG0, cfg_shadow[port]);
}

void WioTrackerL2Board::expSetInput(uint8_t pin) {
  uint8_t port = pin >> 3, bit = pin & 7;
  cfg_shadow[port] |= (1 << bit);
  expWriteReg(port ? TCA_REG_CONFIG1 : TCA_REG_CONFIG0, cfg_shadow[port]);
}

void WioTrackerL2Board::expWritePin(uint8_t pin, bool level) {
  uint8_t port = pin >> 3, bit = pin & 7;
  if (level) out_shadow[port] |= (1 << bit);
  else out_shadow[port] &= ~(1 << bit);
  expWriteReg(port ? TCA_REG_OUTPUT1 : TCA_REG_OUTPUT0, out_shadow[port]);
}

// Power-up sequence mirrors Seeed's reference firmware for this board
// (order and delays matter, especially the LCD reset settle time).
bool WioTrackerL2Board::initExpander() {
  Wire.beginTransmission(TCA9535_ADDR);
  if (Wire.endTransmission() != 0) {
    return false;  // expander not responding
  }

  expSetInput(EXP_PIN_WAKE_BTN);
  expSetInput(EXP_PIN_I2C_IRQ);
  expSetInput(EXP_PIN_SD_DETECT);

  expSetOutput(EXP_PIN_OTG_EN, LOW);
  delay(10);
  expSetOutput(EXP_PIN_PA_EN, LOW);   // amp only powered while a tone plays
  delay(10);
  expSetOutput(EXP_PIN_TF_EN, HIGH);
  delay(10);
  expSetOutput(EXP_PIN_BAT_ADC_EN, HIGH);
  delay(10);
  expSetOutput(EXP_PIN_GNSS_EN, HIGH);
  delay(10);
  // GNSS reset is active HIGH; hold 10ms then release LOW so the L76K runs
  expSetOutput(EXP_PIN_GNSS_RST, HIGH);
  delay(10);
  expWritePin(EXP_PIN_GNSS_RST, LOW);
  // user LED is driven active-low through the expander; idle level HIGH = off
  expSetOutput(EXP_PIN_USER_LED, HIGH);
  delay(10);
  expSetOutput(EXP_PIN_GROVE_EN, HIGH);
  delay(10);

  expSetOutput(EXP_PIN_LCD_EN, HIGH);
  delay(50);
  expSetOutput(EXP_PIN_LCD_RST, HIGH);
  delay(5);
  expWritePin(EXP_PIN_LCD_RST, LOW);
  delay(10);
  expWritePin(EXP_PIN_LCD_RST, HIGH);
  delay(500);  // NV3031B needs a long settle after reset before init commands
  expSetOutput(EXP_PIN_LCD_CS, HIGH);
  delay(10);

  // GT911 touch reset sequence: INT low during reset selects I2C addr 0x5D
  expSetOutput(EXP_PIN_TP_RST, LOW);
  expSetOutput(EXP_PIN_TP_INT, LOW);
  delay(10);
  expWritePin(EXP_PIN_TP_RST, HIGH);
  delay(60);

  // capture WAKE button idle level (polarity unknown on alpha hardware)
  int inputs = expReadInputs();
  wake_btn_baseline = inputs >= 0 ? (uint8_t)(inputs & 1) : 0;

  return true;
}

void WioTrackerL2Board::begin() {
  // GNSS UART: NMEA arrives at ~500 B/s and the main loop can stall for
  // hundreds of ms on SD tile decodes; the 256-byte default overflowed
  Serial1.setRxBufferSize(1024);

  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) { delay(50); }
  MESH_DEBUG_PRINTLN("WioTrackerL2Board: ESP32Board init");

  ESP32Board::begin();  // starts Wire on PIN_BOARD_SDA/PIN_BOARD_SCL (47/48)

  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  pinMode(P_LORA_MISO, INPUT_PULLUP);

  MESH_DEBUG_PRINTLN("WioTrackerL2Board: TCA9535 expander init");
  expander_ok = initExpander();
  if (!expander_ok) {
    Serial.println("ERROR: TCA9535 IO expander not found - peripherals unpowered!");
  }

  Wire.beginTransmission(0x22);
  aw_ok = Wire.endTransmission() == 0;
  MESH_DEBUG_PRINTLN("WioTrackerL2Board: init done");

  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_DEEPSLEEP) {
    long wakeup_source = esp_sleep_get_ext1_wakeup_status();
    if (wakeup_source & (1 << P_LORA_DIO_1)) {
      startup_reason = BD_STARTUP_RX_PACKET;  // LoRa packet woke us from deep sleep
    }
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
  }
}

void WioTrackerL2Board::setLed(bool on) {
  // P10 doubles as GNSS wakeup and idles HIGH; blink = brief LOW pulses so
  // the GPS never sees a sustained low level
  if (expander_ok) {
    expWritePin(EXP_PIN_USER_LED, !on);
  }
}

int WioTrackerL2Board::expReadInputs() {
  Wire.beginTransmission(TCA9535_ADDR);
  Wire.write((uint8_t)0x00);   // input port 0 register
  if (Wire.endTransmission() != 0) return -1;
  if (Wire.requestFrom((int)TCA9535_ADDR, 2) != 2) return -1;
  int lo = Wire.read();
  int hi = Wire.read();
  return lo | (hi << 8);
}

bool WioTrackerL2Board::isExternalPowered() {
  if (!aw_ok) return false;
  Wire.beginTransmission(0x22);
  Wire.write((uint8_t)0x40);   // STATUS0
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(0x22, 1) != 1) return false;
  return (Wire.read() & 0x80) != 0;   // VBUSOK bit
}

bool WioTrackerL2Board::readWakeButton() {
  if (!expander_ok) return false;
  int v = expReadInputs();
  if (v < 0) return false;
  return ((uint8_t)(v & 1)) != wake_btn_baseline;
}

int16_t WioTrackerL2Board::adsReadRaw() {
  Wire.beginTransmission(ADS1115_ADDR);
  Wire.write(ADS_REG_CONFIG);
  Wire.write((uint8_t)(ADS_CFG_BATT >> 8));
  Wire.write((uint8_t)(ADS_CFG_BATT & 0xFF));
  if (Wire.endTransmission() != 0) return -1;

  delay(10);  // 128 SPS -> ~8ms conversion time

  Wire.beginTransmission(ADS1115_ADDR);
  Wire.write(ADS_REG_CONVERSION);
  if (Wire.endTransmission() != 0) return -1;
  if (Wire.requestFrom((int)ADS1115_ADDR, 2) != 2) return -1;

  // sequence the two reads explicitly: operand evaluation order of <<|
  // is unspecified and each read dequeues a byte
  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  int16_t raw = ((int16_t)hi << 8) | lo;
  return raw < 0 ? 0 : raw;
}

uint16_t WioTrackerL2Board::getBattMilliVolts() {
  if (!expander_ok) return 0;  // BAT_ADC_EN rail never came up

  int16_t raw = adsReadRaw();
  if (raw < 0) return 0;

  return (uint16_t)(raw * ADS_MV_PER_LSB);
}
