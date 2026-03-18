#pragma once

// SenseCapHAL - Custom RadioLib 7.x HAL for the Seeed SenseCAP Indicator
//
// The SenseCAP Indicator routes SX1262 control pins through a TCA9535
// 16-bit I2C IO expander (address 0x20) because GPIOs 0-15 are used by
// the 16-bit RGB display bus.
//
// Pin encoding matches LovyanGFX convention: (pin_index | IO_EXPANDER)
//   IO_EXPANDER = 0x40  (defined as build flag in platformio.ini)
//
// SX1262 expander pin assignments (TCA9535 Port 0):
//   pin 0 = NSS   (EXPANDER_IO_RADIO_NSS)
//   pin 1 = RESET (EXPANDER_IO_RADIO_RST)
//   pin 2 = BUSY  (EXPANDER_IO_RADIO_BUSY)
//   pin 3 = DIO1  (EXPANDER_IO_RADIO_DIO_1)
//
// DIO1 interrupt: TCA9535 /INT output → GPIO 42 (IO_EXPANDER_IRQ)
//   The /INT pin fires FALLING when any input changes.
//
// TCA9535 register map (Port 0 only):
//   0x00 Input  Port 0  (read)
//   0x02 Output Port 0  (write; 1=HIGH, 0=LOW)
//   0x06 Config Port 0  (0=output, 1=input)

#include <RadioLib.h>
#include <Wire.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>

class SenseCapHAL : public ArduinoHal {
  TwoWire*          _wire;
  uint8_t           _addr;   // 7-bit I2C address of TCA9535 (0x20)
  uint8_t           _out0;   // cached Output Port 0 latch  (all HIGH = de-asserted)
  uint8_t           _cfg0;   // cached Config Port 0        (all 1 = input initially)
  int               _sclk, _miso, _mosi;  // SPI data pins
  SemaphoreHandle_t _mutex;  // shared Wire mutex (set via setMutex() after creation)

  // A pin is on the expander when its upper nibble equals IO_EXPANDER
  static bool isExp(uint32_t pin) {
    return (pin & ~0x0Fu) == (uint32_t)IO_EXPANDER;
  }
  // Bit mask within TCA9535 port register
  static uint8_t expBit(uint32_t pin) {
    return (uint8_t)(1u << (pin & 0x07u));
  }
  // Pins 0-7 are Port 0; pins 8-15 are Port 1
  static bool isPort0(uint32_t pin) {
    return (pin & 0x08u) == 0;
  }

  void writeReg(uint8_t reg, uint8_t val) {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->write(val);
    _wire->endTransmission();
    if (_mutex) xSemaphoreGive(_mutex);
  }
  uint8_t readReg(uint8_t reg) {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->endTransmission(false);
    _wire->requestFrom(_addr, (uint8_t)1);
    uint8_t val = _wire->available() ? _wire->read() : 0xFF;
    if (_mutex) xSemaphoreGive(_mutex);
    return val;
  }

public:
  SenseCapHAL(SPIClass& spi, int sclk, int miso, int mosi,
              uint8_t i2c_addr = 0x20, TwoWire* wire = &Wire)
    : ArduinoHal(spi, SPISettings(2000000, MSBFIRST, SPI_MODE0))
    , _wire(wire), _addr(i2c_addr)
    , _out0(0xFF)   // all HIGH (NSS, RESET de-asserted)
    , _cfg0(0xFF)   // all inputs initially
    , _sclk(sclk), _miso(miso), _mosi(mosi)
    , _mutex(nullptr)
  {}

  // Set the shared I2C mutex.  Must be called before any Wire operations.
  // Both this HAL and the touch-read callback must use the same handle.
  void setMutex(SemaphoreHandle_t m) { _mutex = m; }

  // Call once Wire is running.
  // Configures TCA9535 Port 0 radio pins (bits 0-3) only.
  // Bits 4-7 are preserved exactly as LovyanGFX left them
  // (bit 4 = display SPI CS, must stay OUTPUT HIGH so the
  //  display ignores any SPI clock pulses from the radio).
  void initExpander() {
    // Read the current register values set by LovyanGFX
    uint8_t cur_out = readReg(0x02);
    uint8_t cur_cfg = readReg(0x06);

    // Bits 0-3  (radio pins):     inputs initially, output latch HIGH (de-asserted)
    //                              RadioLib will reconfigure them as needed.
    // Bit  4    (display SPI CS):  OUTPUT HIGH — permanently de-asserted.
    //                              LovyanGFX leaves this pin as INPUT after lcd.begin(),
    //                              which lets it float. When radio SPI transactions drive
    //                              GPIO 41/48, a floating CS can corrupt the ST7701 display.
    // Bits 5-7  (other):           preserved exactly as LovyanGFX left them.
    _out0 = (cur_out & 0xE0) | 0x1F;  // bits 0-4 = HIGH,   bits 5-7 = preserved
    _cfg0 = (cur_cfg & 0xE0) | 0x0F;  // bits 0-3 = input,  bit 4 = OUTPUT, bits 5-7 = preserved

    writeReg(0x02, _out0);  // Output Port 0
    writeReg(0x06, _cfg0);  // Config Port 0
    Serial.printf("[SenseCapHAL] TCA9535@0x%02X init: out=0x%02X cfg=0x%02X\n",
                  _addr, _out0, _cfg0);
  }

  // ── Overrides ──────────────────────────────────────────────────────────
  //
  // SOFTWARE SPI — we bit-bang GPIO 41/47/48 directly instead of using
  // the hardware FSPI peripheral.  Calling spi.begin(41,47,48) routes
  // those pins through the ESP32-S3 GPIO matrix to FSPI, which silently
  // disables LovyanGFX's LCD_CAM framebuffer output (blank display).
  // Software SPI avoids that conflict entirely; the SX1262 works fine
  // at the ~500 kHz rate the bit-bang produces.

  // ArduinoHal::init() only calls spiBegin() when initInterface==true,
  // which is false when an explicit SPIClass& is supplied.  Override to
  // always call our spiBegin().
  void init() override { spiBegin(); }

  void spiBegin() override {
    // ── IMPORTANT ──────────────────────────────────────────────────────────
    // We must NOT call ::pinMode() here.  ::pinMode() calls gpio_reset_pin()
    // which disrupts the GPIO matrix signal routing used by LovyanGFX's
    // Bus_RGB LCD_CAM driver — blanking the display permanently.
    //
    // We also must NOT use gpio_set_direction() alone: it does not call
    // PIN_FUNC_SELECT, so the IO_MUX for GPIO 41/48 may remain pointed at
    // the JTAG MTDI/MTCK function (restored by LovyanGFX's pin_backup_t
    // after lcd.begin()).  With IO_MUX in JTAG function the physical pins
    // bypass the GPIO matrix entirely, making soft-SPI write nothing.
    //
    // Fix: gpio_config() sets IO_MUX → PIN_FUNC_GPIO (via PIN_FUNC_SELECT)
    // AND enables GPIO output via the GPIO matrix (SIG_GPIO_OUT_IDX), all
    // without ever calling gpio_reset_pin().
    // ───────────────────────────────────────────────────────────────────────
    gpio_config_t out_conf = {};
    out_conf.pin_bit_mask = (1ULL << _sclk) | (1ULL << _mosi);
    out_conf.mode         = GPIO_MODE_OUTPUT;
    out_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
    out_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    out_conf.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&out_conf);

    gpio_config_t in_conf = {};
    in_conf.pin_bit_mask = (1ULL << _miso);
    in_conf.mode         = GPIO_MODE_INPUT;
    in_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
    in_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    in_conf.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&in_conf);

    gpio_set_level((gpio_num_t)_sclk, 0);
    gpio_set_level((gpio_num_t)_mosi, 0);
    Serial.printf("[SenseCapHAL] SPI (soft) SCLK=%d MOSI=%d MISO=%d\n",
                  _sclk, _mosi, _miso);
  }

  void spiBeginTransaction() override { /* no-op for soft SPI */ }
  void spiEndTransaction()   override { /* no-op for soft SPI */ }
  void spiEnd()              override { /* no-op for soft SPI */ }

  // SPI Mode 0: CPOL=0 (CLK idles LOW), CPHA=0 (sample on rising edge)
  void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override {
    for (size_t i = 0; i < len; i++) {
      uint8_t txByte = out ? out[i] : 0xFF;
      uint8_t rxByte = 0;
      for (int b = 7; b >= 0; b--) {
        ::digitalWrite(_mosi, (txByte >> b) & 1);
        ::delayMicroseconds(1);                          // MOSI setup time
        ::digitalWrite(_sclk, HIGH);
        rxByte |= (uint8_t)(::digitalRead(_miso) << b); // sample on rising edge
        ::delayMicroseconds(1);                          // hold time
        ::digitalWrite(_sclk, LOW);
      }
      if (in) in[i] = rxByte;
    }
  }

  void pinMode(uint32_t pin, uint32_t mode) override {
    if (isExp(pin) && isPort0(pin)) {
      if (mode == OUTPUT) _cfg0 &= ~expBit(pin);
      else                _cfg0 |=  expBit(pin);
      writeReg(0x06, _cfg0);
    } else {
      ArduinoHal::pinMode(pin, mode);
    }
  }

  void digitalWrite(uint32_t pin, uint32_t value) override {
    if (isExp(pin) && isPort0(pin)) {
      if (value) _out0 |=  expBit(pin);
      else       _out0 &= ~expBit(pin);
      writeReg(0x02, _out0);
    } else {
      ArduinoHal::digitalWrite(pin, value);
    }
  }

  uint32_t digitalRead(uint32_t pin) override {
    if (isExp(pin) && isPort0(pin)) {
      return (readReg(0x00) & expBit(pin)) ? 1 : 0;
    }
    return ArduinoHal::digitalRead(pin);
  }

  // DIO1 is expander pin 3; redirect the interrupt to IO_EXPANDER_IRQ (GPIO 42).
  // TCA9535 /INT fires FALLING when any input changes (DIO1 going HIGH = packet ready).
  void attachInterrupt(uint32_t pin, void (*cb)(void), uint32_t mode) override {
    if (isExp(pin)) {
      ::pinMode(IO_EXPANDER_IRQ, INPUT_PULLUP);
      ::attachInterrupt(digitalPinToInterrupt(IO_EXPANDER_IRQ), cb, FALLING);
      Serial.printf("[SenseCapHAL] DIO1 interrupt → GPIO %d (FALLING)\n", IO_EXPANDER_IRQ);
    } else {
      ArduinoHal::attachInterrupt(pin, cb, mode);
    }
  }

  void detachInterrupt(uint32_t pin) override {
    if (isExp(pin)) {
      ::detachInterrupt(digitalPinToInterrupt(IO_EXPANDER_IRQ));
    } else {
      ArduinoHal::detachInterrupt(pin);
    }
  }

  // Scan for the TCA9535 on I2C and log the result
  bool scanExpander() {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    _wire->beginTransmission(_addr);
    bool found = (_wire->endTransmission() == 0);
    if (_mutex) xSemaphoreGive(_mutex);
    return found;
  }
};
