#include "MeshnologyW10Board.h"
#include "target.h"
#include <SPI.h>

void MeshnologyW10Board::begin() {
  // Deselect radio, display and the LCD expansion connector's SD chip select.
  digitalWrite(P_LORA_NSS, HIGH);
  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
  pinMode(PIN_TFT_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_TFT_LEDA_CTL, LOW);
  pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
  pinMode(PIN_USER_BTN, INPUT_PULLUP);

  ESP32Board::begin();
  SPI.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);
  _pmu_ready = _pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, PIN_BOARD_SDA, PIN_BOARD_SCL);
  if (!_pmu_ready) {
    Serial.println("ERROR: W10 AXP2101 not found at 0x34");
    return;
  }
  // DCDC1 supplies MCU, radio, GPS, display and expander, never cycle it during initialization
  _pmu.setProtectedChannel(XPOWERS_DCDC1);
  _pmu.clearIrqStatus();
  // XPowersLib disables TS during begin(), but the W10 has a populated NTC:
  if (!_pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ)
      || !_pmu.enableBattDetection() || !_pmu.enableBattVoltageMeasure()
      || !_pmu.enableVbusVoltageMeasure() || !_pmu.enableTSPinMeasure()
      || !_pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA)
      || !_pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2)
      || !_pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S)) {
    Serial.println("ERROR: W10 AXP2101 power configuration failed");
    return;
  }
  _pmu.setChargingLedMode(XPOWERS_CHG_LED_CTRL_CHG);

  _ready = io.begin()
      && io.digitalWrite(EXIO_LORA_RESET, HIGH) && io.pinMode(EXIO_LORA_RESET, OUTPUT)
      && io.pinMode(EXIO_LORA_DIO1, INPUT) && io.pinMode(EXIO_LORA_BUSY, INPUT)
      && io.digitalWrite(EXIO_AMP_ENABLE, LOW) && io.pinMode(EXIO_AMP_ENABLE, OUTPUT)
#if ENV_INCLUDE_GPS
      && io.digitalWrite(EXIO_GPS_WAKE, HIGH)
#else
      && io.digitalWrite(EXIO_GPS_WAKE, LOW)
#endif
      && io.pinMode(EXIO_GPS_WAKE, OUTPUT)
      && io.digitalWrite(EXIO_LCD_RESET, LOW) && io.pinMode(EXIO_LCD_RESET, OUTPUT);
#ifdef DISPLAY_CLASS
  if (_ready) {
    delay(10);
    _ready = io.digitalWrite(EXIO_LCD_RESET, HIGH);
    delay(20);
  }
#endif
  if (!_ready) Serial.println("ERROR: W10 MCP23017 initialization failed at 0x20");
}

uint16_t MeshnologyW10Board::getBattMilliVolts() {
  return _pmu_ready && _pmu.isBatteryConnect() ? _pmu.getBattVoltage() : 0;
}

void MeshnologyW10Board::powerOff() {
#ifdef DISPLAY_CLASS
  display.turnOff();
#endif
  radio_driver.powerOff();
#if ENV_INCLUDE_GPS
  sensors.getLocationProvider()->stop();
#endif
  Serial.flush();
  if (_pmu_ready) _pmu.shutdown();
  ESP32Board::powerOff();
}
