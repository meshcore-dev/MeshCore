#pragma once

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <helpers/ESP32Board.h>
#include <helpers/ui/AnalogJoystick.h>

#define RADIOMASTER_900_BANDIT
#define DISPLAY_CLASS SH1115Display
#define PIN_NEOPIXEL  15
#define NEOPIXEL_NUM  6

extern Adafruit_NeoPixel pixels;

/*
  6 x Neopixels, GRB
  GPIO 15
  Backgroundlight button 1 at index 0
  Backgroundlight button 2 at index 1

  Button 1 at GPIO 34
  Button 2 at GPIO 35

  STK8XXX Accelerometer I2C address 0x18 and Interrupt at GPIO 37
*/

/*
  Pin connections from ESP32-D0WDQ6 to SX1276.
*/
#define P_LORA_DIO_0     22
#define P_LORA_DIO_1     21
#define P_LORA_NSS       4
#define P_LORA_RESET     5
#define P_LORA_SCLK      18
#define P_LORA_MISO      19
#define P_LORA_MOSI      23
#define SX176X_TXEN      33

/*
  I2C SDA and SCL.
*/
#define PIN_BOARD_SDA    14
#define PIN_BOARD_SCL    12

/*
  This unit has a FAN built-in.
  FAN is active at 250mW on it's ExpressLRS Firmware.
  Always ON
*/
#define PA_FAN_EN        2 // FAN on GPIO 2

/*
  This module has Skyworks SKY66122 controlled by dacWrite
  power rangeing from 100mW to 1000mW.

  Mapping of PA_LEVEL to Power output: GPIO26/dacWrite
  168 -> 100mW  -> 2.11v
  148 -> 250mW  -> 1.87v
  128 -> 500mW  -> 1.63v
  90  -> 1000mW -> 1.16v
*/
#define DAC_PA_PIN       26 // GPIO 26 enables the PA

// Configuration - adjust these for your hardware
#define PA_CONSTANT_GAIN 18 // SKY66122 operates at constant 18dB gain
#define MIN_OUTPUT_DBM   20 // 100mW minimum
#define MAX_OUTPUT_DBM   30 // 1000mW maximum

// Calibration points from manufacturer
struct PowerCalibration {
  uint8_t output_dbm;
  int8_t sx1278_dbm;
  uint8_t dac_value;
};

// Values are from Radiomaster.
const PowerCalibration calibration[] = {
  { 20, 2, 165 }, // 100mW
  { 24, 6, 155 }, // 250mW
  { 27, 9, 142 }, // 500mW
  { 30, 10, 110 } // 1000mW
};

inline Adafruit_NeoPixel pixels(NEOPIXEL_NUM, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

const int NUM_CAL_POINTS = sizeof(calibration) / sizeof(calibration[0]);

class BanditBoard : public ESP32Board {
private:
public:
  void begin() {
    ESP32Board::begin();
    pixels.begin();
    pixels.clear();
    pixels.show();
  }
  // Return fake battery status, battery/fixed power is not monitored.
  uint16_t getBattMilliVolts() override { return (5.42 * (3.3 / 1024.0) * 250) * 1000; }

  const char *getManufacturerName() const override { return "RadioMaster Bandit"; }

  void onBeforeTransmit() override {
    pixels.setPixelColor(0, pixels.Color(0, 150, 0));
    pixels.setPixelColor(1, pixels.Color(0, 150, 0));
    pixels.setPixelColor(2, pixels.Color(0, 150, 0));
    pixels.setPixelColor(3, pixels.Color(0, 150, 0));
    pixels.setPixelColor(4, pixels.Color(0, 150, 0));
    pixels.setPixelColor(5, pixels.Color(0, 150, 0));
    pixels.show();
  }

  void onAfterTransmit() override {
    pixels.clear();
    pixels.show();
  }
};
