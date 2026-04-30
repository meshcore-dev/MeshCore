#include "TechoCardBoard.h"
#include "variant.h"
#include <Wire.h>

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

  // Initialise I2C — must be done before display.begin() is called from main.cpp
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