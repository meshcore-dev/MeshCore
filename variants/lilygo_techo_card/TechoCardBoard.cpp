#include "TechoCardBoard.h"
#include "variant.h"
#include <Wire.h>
#include <nrf_soc.h>
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;

void TechoCardBoard::begin() {
  NRF52BoardDCDC::begin();
  Serial.begin(115200);

  // RT9080 3V3 rail: clean reset cycle (from Meshtastic PR #10267)
  // Toggling EN HIGH→LOW→HIGH forces a clean power-on, preventing
  // brown-out when LoRa TX fires at full power.
  #if PIN_OLED_EN >= 0
    pinMode(PIN_OLED_EN, OUTPUT);
    digitalWrite(PIN_OLED_EN, HIGH);
    delay(100);
    digitalWrite(PIN_OLED_EN, LOW);
    delay(100);
    digitalWrite(PIN_OLED_EN, HIGH);
    delay(100);
  #endif

  // Park peripheral enable pins LOW before setup runs
  #if defined(HAS_GPS) && PIN_GPS_EN >= 0
    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, LOW);
  #endif
  #if defined(HAS_GPS) && PIN_GPS_RF_EN >= 0
    pinMode(PIN_GPS_RF_EN, OUTPUT);
    digitalWrite(PIN_GPS_RF_EN, LOW);
  #endif
  #if defined(HAS_BUZZER) && PIN_BUZZER >= 0
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
  #endif
  #if defined(HAS_SPEAKER)
    pinMode(PIN_SPK_EN, OUTPUT);
    digitalWrite(PIN_SPK_EN, LOW);
    #if PIN_SPK_EN2 >= 0
      pinMode(PIN_SPK_EN2, OUTPUT);
      digitalWrite(PIN_SPK_EN2, LOW);
    #endif
  #endif

  // Enable GPS power after rail stabilises
  #if defined(HAS_GPS) && PIN_GPS_EN >= 0
    delay(10);
    digitalWrite(PIN_GPS_EN, HIGH);
  #endif
  #if defined(HAS_GPS) && PIN_GPS_RF_EN >= 0
    digitalWrite(PIN_GPS_RF_EN, HIGH);
  #endif

  // Initialise GPS UART
  #if defined(HAS_GPS)
    Serial1.setPins(PIN_GPS_RX, PIN_GPS_TX);
    Serial1.begin(GPS_BAUDRATE);
  #endif

  pinMode(PIN_VBAT_READ, INPUT);
  pinMode(PIN_USER_BTN, INPUT);

  // Initialise I2C -- must be done before display.begin() is called from main.cpp
  Wire.begin();
  Wire.setClock(400000);

  // Initialise WS2812 NeoPixel chain (all off at boot)
  // Force data line LOW before init to prevent stray HIGH latching green
  #if defined(HAS_RGB_LED)
    pinMode(PIN_RGB_LED_1, OUTPUT);
    digitalWrite(PIN_RGB_LED_1, LOW);
    delayMicroseconds(300);  // WS2812 reset pulse is ~280µs
    _pixels.begin();
    _pixels.clear();
    _pixels.show();
  #endif
}

void TechoCardBoard::enableGPS(bool enable) {
  #if defined(HAS_GPS) && PIN_GPS_EN >= 0
    digitalWrite(PIN_GPS_EN, enable ? HIGH : LOW);
  #endif
  #if defined(HAS_GPS) && PIN_GPS_RF_EN >= 0
    digitalWrite(PIN_GPS_RF_EN, enable ? HIGH : LOW);
  #endif
}

float TechoCardBoard::getMCUTemperature() {
  // SoftDevice owns the TEMP peripheral -- direct register access hard faults.
  // Use sd_temp_get() when SoftDevice is enabled.
  int32_t temp;
  uint8_t sd_en = 0;
  sd_softdevice_is_enabled(&sd_en);
  if (sd_en) {
    if (sd_temp_get(&temp) == NRF_SUCCESS) {
      return temp * 0.25f;
    }
    return NAN;
  }
  // SoftDevice off -- fall back to parent's direct register access
  return NRF52Board::getMCUTemperature();
}

void TechoCardBoard::enableSpeaker(bool enable) {
  #if defined(HAS_SPEAKER)
    digitalWrite(PIN_SPK_EN, enable ? HIGH : LOW);
    #if PIN_SPK_EN2 >= 0
      digitalWrite(PIN_SPK_EN2, enable ? HIGH : LOW);
    #endif
  #endif
}

void TechoCardBoard::setLED(uint8_t r, uint8_t g, uint8_t b) {
  #if defined(HAS_RGB_LED)
    uint32_t color = Adafruit_NeoPixel::Color(r, g, b);
    for (int i = 0; i < NUM_NEOPIXELS; i++) {
      _pixels.setPixelColor(i, color);
    }
    _pixels.show();
  #else
    (void)r; (void)g; (void)b;
  #endif
}

void TechoCardBoard::ledOff() {
  setLED(0, 0, 0);
}

void TechoCardBoard::setStatusLED(uint8_t led_index, uint32_t color) {
  #if defined(HAS_RGB_LED)
    if (led_index < NUM_NEOPIXELS) {
      _pixels.setPixelColor(led_index, color);
      _pixels.show();
    }
  #else
    (void)led_index; (void)color;
  #endif
}

void TechoCardBoard::buzz(uint16_t freq_hz, uint16_t duration_ms) {
  #if defined(HAS_BUZZER) && PIN_BUZZER >= 0
    if (freq_hz == 0 || duration_ms == 0) {
      noTone(PIN_BUZZER);
      return;
    }
    tone(PIN_BUZZER, freq_hz, duration_ms);
  #else
    (void)freq_hz; (void)duration_ms;
  #endif
}

// =============================================================================
// BQ25896 Charger IC (I2C address 0x6B)
// =============================================================================

#define BQ25896_ADDR 0x6B

bool TechoCardBoard::probeCharger() {
  if (!_chargerProbed) {
    Wire.beginTransmission(BQ25896_ADDR);
    _chargerPresent = (Wire.endTransmission() == 0);
    _chargerProbed = true;
    if (!_chargerPresent) {
      Serial.println("BQ25896: not found at 0x6B");
    }
  }
  return _chargerPresent;
}

uint8_t TechoCardBoard::readChargerReg(uint8_t reg) {
  if (!probeCharger()) return 0;
  Wire.beginTransmission(BQ25896_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  Wire.requestFrom((uint8_t)BQ25896_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

void TechoCardBoard::writeChargerReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(BQ25896_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void TechoCardBoard::enableChargerADC() {
  uint8_t reg02 = readChargerReg(0x02);
  reg02 |= 0xC0; // CONV_RATE=1 (continuous) + CONV_START=1
  writeChargerReg(0x02, reg02);
}

uint8_t TechoCardBoard::getChargeStatus() {
  return (readChargerReg(0x0B) >> 3) & 0x03;
}

uint16_t TechoCardBoard::getChargerBattMV() {
  return 2304 + (readChargerReg(0x0E) & 0x7F) * 20;
}

uint8_t TechoCardBoard::getChargerTSPCT() {
  return 21 + (readChargerReg(0x10) & 0x7F);
}

// =============================================================================
// ICM20948 / AK09916 Compass
//
// Enable I2C bypass on the ICM20948 so the AK09916 magnetometer at 0x0C
// appears directly on Wire. Then set continuous measurement mode.
// =============================================================================

#define ICM20948_ADDR 0x68
#define AK09916_ADDR 0x0C

static uint8_t _i2c_rd(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  Wire.requestFrom(addr, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

static void _i2c_wr(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool TechoCardBoard::initCompass() {
  if (_compassReady) return true;

  // Bank 0
  _i2c_wr(ICM20948_ADDR, 0x7F, 0x00);

  // Check WHO_AM_I (expect 0xEA)
  if (_i2c_rd(ICM20948_ADDR, 0x00) != 0xEA) return false;

  // Wake up: auto clock, not sleep
  _i2c_wr(ICM20948_ADDR, 0x06, 0x01);
  delay(10);

  // Enable I2C bypass so AK09916 is directly accessible
  _i2c_wr(ICM20948_ADDR, 0x0F, 0x02);
  delay(5);

  // Check AK09916 WHO_AM_I (expect 0x09)
  if (_i2c_rd(AK09916_ADDR, 0x01) != 0x09) return false;

  // Leave in power-down -- readMag triggers single measurements on demand
  _i2c_wr(AK09916_ADDR, 0x31, 0x00);

  _compassReady = true;
  return true;
}

bool TechoCardBoard::readMag(int16_t& mx, int16_t& my, int16_t& mz) {
  if (!_compassReady) return false;

  // Single-measurement mode: trigger one fresh measurement per call.
  // Continuous mode gets disrupted by OLED I2C display writes sharing
  // the bus through ICM20948 bypass, causing stale data.
  _i2c_wr(AK09916_ADDR, 0x31, 0x01); // single measurement trigger

  // Wait for data ready (measurement takes ~7.2ms)
  for (int i = 0; i < 20; i++) {
    if (_i2c_rd(AK09916_ADDR, 0x10) & 0x01) break;
    delay(1);
  }

  // Burst read 6 data bytes + ST2 (must read ST2 to complete cycle)
  Wire.beginTransmission(AK09916_ADDR);
  Wire.write(0x11);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((uint8_t)AK09916_ADDR, (uint8_t)7);
  if (Wire.available() < 7) return false;

  uint8_t buf[7];
  for (int i = 0; i < 7; i++) buf[i] = Wire.read();

  mx = (int16_t)(buf[1] << 8 | buf[0]);
  my = (int16_t)(buf[3] << 8 | buf[2]);
  mz = (int16_t)(buf[5] << 8 | buf[4]);
  // buf[6] = ST2, read to unlatch

  return true;
}

// Power down the AK09916 magnetometer and put the ICM20948 itself to sleep.
// Saves ~3-4mA when not actively viewing the compass page.
// Next call to initCompass() will fully re-initialise the chain.
void TechoCardBoard::sleepCompass() {
  if (!_compassReady) return;

  // Bank 0 (in case we drifted)
  _i2c_wr(ICM20948_ADDR, 0x7F, 0x00);

  // AK09916 CNTL2 = 0x00 -- power-down mode (stops continuous measurement)
  _i2c_wr(AK09916_ADDR, 0x31, 0x00);

  // ICM20948 PWR_MGMT_1 = 0x40 -- SLEEP bit set
  _i2c_wr(ICM20948_ADDR, 0x06, 0x40);

  _compassReady = false;
}

// =============================================================================
// Compass calibration persistence
// =============================================================================

#define COMPASS_CAL_FILE "/compass_cal"

bool TechoCardBoard::loadCalibration() {
  // InternalFS must already be initialised (done in main.cpp setup)
  File file = InternalFS.open(COMPASS_CAL_FILE, FILE_O_READ);
  if (file) {
    int n = file.read((uint8_t*)&_cal, sizeof(_cal));
    file.close();
    if (n == (int)sizeof(_cal) && _cal.magic == COMPASS_CAL_MAGIC) {
      return true;
    }
  }
  // No valid calibration -- reset to identity (no correction)
  _cal = { 0, 0, 0, 1.0f, 1.0f, 1.0f, 0 };
  return false;
}

bool TechoCardBoard::saveCalibration(const CompassCalibration& cal) {
  _cal = cal;
  _cal.magic = COMPASS_CAL_MAGIC;
  // Direct-write pattern: remove then create (nRF52 LittleFS compatible)
  InternalFS.remove(COMPASS_CAL_FILE);
  File file = InternalFS.open(COMPASS_CAL_FILE, FILE_O_WRITE);
  if (!file) return false;
  file.write((const uint8_t*)&_cal, sizeof(_cal));
  file.close();
  return true;
}