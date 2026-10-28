#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"
#include "Adafruit_NeoPixel.h"

const int MISO = PIN_SPI_MISO;
const int MOSI = PIN_SPI_MOSI;
const int SCK = PIN_SPI_SCK;



const uint32_t g_ADigitalPinMap[] = {
  0xff, 0xff, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
  14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
  27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
  40, 41, 42, 43, 44, 45, 46, 47
};

void initVariant() {
  // turn on 3v3 rail
  pinMode(PIN_PWR_EN, OUTPUT);
  digitalWrite(PIN_PWR_EN, HIGH);

  // VDIV enable
  pinMode(PIN_BAT_CTL, OUTPUT);

  // buttons
  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_BUTTON2, INPUT_PULLUP);

  // speaker
  pinMode(SPEAKER_EN, OUTPUT);
  digitalWrite(SPEAKER_EN, LOW);
  pinMode(SPEAKER_EN_2, OUTPUT);
  digitalWrite(SPEAKER_EN_2, LOW);

  // gps
  pinMode(PIN_GPS_STANDBY, OUTPUT);
  digitalWrite(PIN_GPS_STANDBY, HIGH);
  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(PIN_GPS_EN, HIGH);
  pinMode(PIN_GPS_RESET, OUTPUT);
  digitalWrite(PIN_GPS_RESET, HIGH);

}
