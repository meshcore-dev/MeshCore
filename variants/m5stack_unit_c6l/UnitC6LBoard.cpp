#include <Arduino.h>
#include "target.h"

// PI4IO GPIO expander: P7=LoRa reset, P6=RF switch, P5=LNA_EN, P0/P1=buttons
#define PI4IO_ADDR         0x43
#define PI4IO_REG_RESET    0x01
#define PI4IO_REG_IO_DIR   0x03
#define PI4IO_REG_OUT_SET  0x05
#define PI4IO_REG_OUT_H_IM 0x07
#define PI4IO_REG_IN_DEF   0x09
#define PI4IO_REG_PULL_EN  0x0B
#define PI4IO_REG_PULL_SEL 0x0D
#define PI4IO_REG_INT_MASK 0x11
#define PI4IO_REG_IRQ_STA  0x13

#define PI4IO_P0  (1 << 0)
#define PI4IO_P1  (1 << 1)
#define PI4IO_P2  (1 << 2)
#define PI4IO_P3  (1 << 3)
#define PI4IO_P4  (1 << 4)
#define PI4IO_P5  (1 << 5)
#define PI4IO_P6  (1 << 6)
#define PI4IO_P7  (1 << 7)

static void pi4ioWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(PI4IO_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static uint8_t pi4ioRead(uint8_t reg) {
  Wire.beginTransmission(PI4IO_ADDR);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom((uint8_t)PI4IO_ADDR, (uint8_t)1);
  return Wire.read();
}

static void pi4ioInit() {
  // reset chip
  pi4ioWrite(PI4IO_REG_RESET,    0xFF);
  // clear reset
  pi4ioRead(PI4IO_REG_RESET);
  // P5=LNA_EN, P6=ANT_SW, P7=NRST as outputs
  pi4ioWrite(PI4IO_REG_IO_DIR,   PI4IO_P5 | PI4IO_P6 | PI4IO_P7);
  // P2-P4 unused, high-Z
  pi4ioWrite(PI4IO_REG_OUT_H_IM, PI4IO_P2 | PI4IO_P3 | PI4IO_P4);
  // pull-up on buttons and output pins
  pi4ioWrite(PI4IO_REG_PULL_SEL, PI4IO_P0 | PI4IO_P1 | PI4IO_P5 | PI4IO_P6 | PI4IO_P7);
  pi4ioWrite(PI4IO_REG_PULL_EN,  PI4IO_P0 | PI4IO_P1 | PI4IO_P5 | PI4IO_P6 | PI4IO_P7);
  // buttons default high
  pi4ioWrite(PI4IO_REG_IN_DEF,   PI4IO_P0 | PI4IO_P1);
  // interrupt on P0, P1 only (mask everything else)
  pi4ioWrite(PI4IO_REG_INT_MASK, PI4IO_P2 | PI4IO_P3 | PI4IO_P4 | PI4IO_P5 | PI4IO_P6 | PI4IO_P7);
  // release LoRa reset, enable ANT_SW and LNA
  pi4ioWrite(PI4IO_REG_OUT_SET,  PI4IO_P5 | PI4IO_P6 | PI4IO_P7);
  // clear IRQ flag
  pi4ioRead(PI4IO_REG_IRQ_STA);
}

void UnitC6LBoard::begin() {
  ESP32Board::begin();  // initializes Wire on SDA=10, SCL=8
  pi4ioInit();
}

UnitC6LBoard board;

#if defined(P_LORA_SCLK)
  static SPIClass spi(0);
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

#if defined(P_LORA_SCLK)
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  return radio.std_init(&spi);
#else
  return radio.std_init();
#endif
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

