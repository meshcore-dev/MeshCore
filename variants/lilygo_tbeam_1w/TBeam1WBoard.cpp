#include "TBeam1WBoard.h"

void TBeam1WBoard::begin() {
  ESP32Board::begin();

  // Power on radio module (must be done before radio init).
  // Drive LOW first to ensure a clean SX1262 reset — on soft reboot the ESP32
  // GPIO glitch may be too brief to drain module capacitors, leaving the SX1262
  // in a stuck state. Holding LOW for 50ms guarantees a full power cycle.
  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, LOW);
  delay(50);
  digitalWrite(SX126X_POWER_EN, HIGH);
  radio_powered = true;
  delay(10);  // Allow radio to power up

  // Explicitly drive RXEN HIGH (LNA on) from boot, before RadioLib takes over.
  // Without this the pin floats until the first startReceive() call.
  pinMode(SX126X_RXEN, OUTPUT);
  digitalWrite(SX126X_RXEN, HIGH);

  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Fan starts off; onAfterTransmit() enables it with a 30s cooldown timer
  pinMode(FAN_CTRL_PIN, OUTPUT);
  digitalWrite(FAN_CTRL_PIN, LOW);
}

void TBeam1WBoard::onBeforeTransmit() {
  digitalWrite(SX126X_RXEN, LOW);  // disconnect LNA before PA ramps up
  digitalWrite(LED_PIN, HIGH);
}

void TBeam1WBoard::onAfterTransmit() {
  digitalWrite(SX126X_RXEN, HIGH); // re-enable LNA immediately after TX
  digitalWrite(LED_PIN, LOW);
  // Keep fan running for 10s after TX
  setFanEnabled(true);
  _fan_off_millis = millis() + 10000;
}
void TBeam1WBoard::loop() {
  updateFan();
}
void TBeam1WBoard::updateFan() {
  if (_fan_off_millis > 0 && (long)(millis() - _fan_off_millis) >= 0) {
    setFanEnabled(false);
    _fan_off_millis = 0;
  }
}

uint16_t TBeam1WBoard::getBattMilliVolts() {
  // T-Beam 1W uses 7.4V battery with voltage divider
  // ADC reads through divider - adjust multiplier based on actual divider ratio
  analogReadResolution(12);
  uint32_t raw = 0;
  for (int i = 0; i < 8; i++) {
    raw += analogRead(BATTERY_PIN);
  }
  raw = raw / 8;
  // Assuming voltage divider ratio from ADC_MULTIPLIER
  // 3.3V reference, 12-bit ADC (4095 max)
  return static_cast<uint16_t>((raw * 3300 * ADC_MULTIPLIER) / 4095);
}

const char* TBeam1WBoard::getManufacturerName() const {
  return "LilyGo T-Beam 1W";
}

void TBeam1WBoard::powerOff() {
  // Turn off radio LNA (CTRL pin must be LOW when not receiving)
  digitalWrite(SX126X_RXEN, LOW);

  // Turn off radio power
  digitalWrite(SX126X_POWER_EN, LOW);
  radio_powered = false;

  // Turn off LED and fan
  digitalWrite(LED_PIN, LOW);
  digitalWrite(FAN_CTRL_PIN, LOW);

  ESP32Board::powerOff();
}

void TBeam1WBoard::setFanEnabled(bool enabled) {
  digitalWrite(FAN_CTRL_PIN, enabled ? HIGH : LOW);
}

bool TBeam1WBoard::isFanEnabled() const {
  return digitalRead(FAN_CTRL_PIN) == HIGH;
}