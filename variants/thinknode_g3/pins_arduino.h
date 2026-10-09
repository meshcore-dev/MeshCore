#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

static const uint8_t SDA  = 17;
static const uint8_t SCL  = 18;

// Default SPI is the SX1262, not the CH390.
static const uint8_t SS   = 39;
static const uint8_t MOSI = 40;
static const uint8_t MISO = 41;
static const uint8_t SCK  = 42;

#endif
