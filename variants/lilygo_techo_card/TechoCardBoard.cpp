#include <Arduino.h>
#include <Wire.h>


#include "TechoCardBoard.h"

#ifdef LILYGO_TECHO_CARD

Adafruit_NeoPixel Led_A(1, WS2812_DATA_2, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel Led_B(1, WS2812_DATA_3, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel Led_C(1, WS2812_DATA_1, NEO_GRB + NEO_KHZ800);

Adafruit_NeoPixel *Led[] =
    {
        &Led_A,
        &Led_B,
        &Led_C,
};


void TechoCardBoard::begin() {
    NRF52BoardDCDC::begin();
    Wire.begin();

    for (uint8_t i = 0; i < sizeof(Led) / sizeof(Led[0]); i++)
    {
        Led[i]->begin();
        delay(3); // allow the LEDs to initialise, otherwise they can get stuck
        Led[i]->setPixelColor(0, Led[i]->Color(0, 0, 0));
        Led[i]->show();
    }
    
    // put IMU20948 to sleep
    // see https://product.tdk.com/system/files/dam/doc/product/sensor/mortion-inertial/imu/data_sheet/ds-000189-icm-20948-v1.5.pdf
    Wire.beginTransmission(0x68);
    Wire.write(0x06);   // PWR_MGMT_1 register
    Wire.write(0x40);   // set SLEEP bit
    Wire.endTransmission();

}

uint16_t TechoCardBoard::getBattMilliVolts() {
  int adcvalue = 0;

  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  
  digitalWrite(PIN_BAT_CTL, HIGH); // enable vbat vdiv
  delay(10);

  // ADC range is 0..3000mV and resolution is 12-bit (0..4095)
  adcvalue = analogRead(PIN_VBAT_READ);
  digitalWrite(PIN_BAT_CTL, LOW);

  // Convert the raw value to compensated mv, taking the resistor-
  // divider into account (providing the actual LIPO voltage)
  return (uint16_t)((float)adcvalue * REAL_VBAT_MV_PER_LSB);
}

void TechoCardBoard::onBeforeTransmit() {
    Led_A.setPixelColor(0, 20, 20, 20);   // turn TX LED on
    Led_A.show();
}

void TechoCardBoard::onAfterTransmit() {
    Led_A.setPixelColor(0, 0, 0, 0);   // turn TX LED off
    Led_A.show();
}

void TechoCardBoard::toggleTorch() {
    if (!_torchStatus) {
        Led_C.setPixelColor(0, 255, 255, 255);
        Led_C.show();
        _torchStatus = true;
    } else {
        Led_C.setPixelColor(0, 0, 0, 0);
        Led_C.show();
        _torchStatus = false;
        }
}

void TechoCardBoard::turnOffLeds() {
    for (uint8_t i = 0; i < sizeof(Led) / sizeof(*Led); i++)
    {
        Led[i]->setPixelColor(0, 0, 0, 0);
        Led[i]->show();
    }
}

void TechoCardBoard::powerOff() {
  nrf_gpio_cfg_sense_input(BUTTON_PIN, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  turnOffLeds();
  digitalWrite(PIN_PWR_EN, LOW);
  NRF52Board::powerOff();
}

#endif
