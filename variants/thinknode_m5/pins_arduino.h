// Need this file for ESP32-S3
// No need to modify this file, changes to pins imported from variant.h
// Most is similar to https://github.com/espressif/arduino-esp32/blob/master/variants/esp32s3/pins_arduino.h

#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include <variant.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// Serial
static const uint8_t TX = PIN_GPS_TX;
static const uint8_t RX = PIN_GPS_RX;

// Default SPI will be mapped to Radio
static const uint8_t SS = P_LORA_NSS;
static const uint8_t SCK = P_LORA_SCLK;
static const uint8_t MOSI = P_LORA_MISO;
static const uint8_t MISO = P_LORA_MOSI;

// The default Wire will be mapped to PMU and RTC
static const uint8_t SCL = PIN_BOARD_SCL;
static const uint8_t SDA = PIN_BOARD_SDA;

#endif /* Pins_Arduino_h */