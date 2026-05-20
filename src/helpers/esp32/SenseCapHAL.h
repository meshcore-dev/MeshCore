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
// TCA9535 Port 0 pin assignments:
//   pin 0 = NSS   (EXPANDER_IO_RADIO_NSS)       — SX1262 chip-select
//   pin 1 = RESET (EXPANDER_IO_RADIO_RST)        — SX1262 hardware reset
//   pin 2 = BUSY  (EXPANDER_IO_RADIO_BUSY)       — SX1262 busy signal
//   pin 3 = DIO1  (EXPANDER_IO_RADIO_DIO_1)      — SX1262 IRQ
//   pin 4 =        display SPI CS                — ST7701 chip-select (must stay OUTPUT HIGH)
//   pin 5 =        display RESX                  — ST7701 hardware reset (LovyanGFX cfg.pin_rst)
//   pin 6 =        touch INT                     — FT5x06 interrupt (input, not used in SW)
//   pin 7 =        touch RST                     — FT5x06 reset (not used in SW)
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

  // ── Deferred IRQ dispatch ──────────────────────────────────────────────
  // The TCA9535 /INT fires for ANY input change on Port 0, not just DIO1.
  // BUSY transitions, touch-INT (pin 6), and other inputs all trigger GPIO 42.
  // We cannot do I2C inside an ISR, so the raw ISR just sets a flag.
  // dispatchPendingIrq() (called from the main loop via recvRaw) reads Port 0
  // to verify DIO1 is actually HIGH before forwarding the event to RadioLib.
  inline static volatile bool    _s_pending = false;
  inline static void (*_s_cb)(void) = nullptr;
  uint8_t _irqBit = 0;  // Port 0 bit-mask for DIO1, set in attachInterrupt()

  // NOTE: no IRAM_ATTR — class-static IRAM functions cause Xtensa literal-pool
  // linker errors when defined inline in a header.  Running from flash is fine
  // here: the SenseCAP uses ESP32-S3 with no mid-operation flash cache disables,
  // and LoRa packet timescales (tens of ms) dwarf any flash-cache miss latency.
  static void _rawIsr() { _s_pending = true; }

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

  // Call once Wire is running, AFTER lcd.begin() has completed.
  // Configures TCA9535 Port 0 radio pins (bits 0-3) and locks bit 4 (display CS).
  // Bits 5-7 are preserved exactly as LovyanGFX left them.
  void initExpander() {
    // Read the current register values set by LovyanGFX
    uint8_t cur_out = readReg(0x02);
    uint8_t cur_cfg = readReg(0x06);

    // Bits 0-3  (radio):           inputs initially; RadioLib reconfigures as needed.
    //                              Output latch HIGH = de-asserted.
    // Bit  4    (display CS):      OUTPUT HIGH — permanently de-asserted.
    //                              LovyanGFX leaves this as INPUT after lcd.begin();
    //                              a floating CS allows radio SPI to corrupt the ST7701.
    // Bit  5    (display RESX):    preserved as OUTPUT HIGH — lcd.begin() already pulsed
    //                              it LOW→HIGH to reset the ST7701 before init commands.
    // Bits 6-7  (touch INT/RST):   preserved as LovyanGFX left them (inputs).
    _out0 = (cur_out & 0xE0) | 0x1F;  // bits 0-4 = HIGH,   bits 5-7 = preserved
    _cfg0 = (cur_cfg & 0xE0) | 0x0F;  // bits 0-3 = input,  bit 4 = OUTPUT, bits 5-7 = preserved

    writeReg(0x02, _out0);  // Output Port 0
    writeReg(0x06, _cfg0);  // Config Port 0

    // Port 1 (pins 8-15) — appears unused on the SenseCAP Indicator.
    // TCA9535 /INT fires on ANY input change on either port and only clears
    // when the triggering port register is read.  Floating Port 1 inputs
    // continuously re-assert /INT, preventing DIO1 edges from ever firing.
    // Fix: make all Port 1 pins OUTPUT HIGH so they can never float.
    writeReg(0x03, 0xFF);  // Output Port 1: all HIGH
    writeReg(0x07, 0x00);  // Config  Port 1: all OUTPUTS

    // Read both input ports now to establish a clean /INT baseline.
    readReg(0x00);
    readReg(0x01);

    Serial.printf("[SenseCapHAL] TCA9535@0x%02X init: out0=0x%02X cfg0=0x%02X\n",
                  _addr, _out0, _cfg0);
  }

  // ── Overrides ──────────────────────────────────────────────────────────
  //
  // SOFTWARE (bit-bang) SPI — intentionally avoids the hardware FSPI peripheral.
  //
  // Calling SPI.begin() (hardware FSPI) AFTER lcd.begin() disrupts the
  // LCD_CAM peripheral that is already running the RGB parallel bus,
  // causing the display to go blank.  Since the LCD_CAM uses GPIOs 0–21
  // and the SX1262 SPI uses GPIOs 41/47/48, there is no pin conflict —
  // the problem is at the peripheral / DMA level when FSPI is initialised
  // while LCD_CAM is active.
  //
  // Bit-bang SPI on the same GPIOs avoids hardware SPI entirely.
  // The SX1262 requires at most ~16 MHz SPI; bit-bang on ESP32-S3 gives
  // ~1–2 MHz which is more than adequate for LoRa configuration and DIO
  // interrupt latency is unaffected.

  // ArduinoHal::init() only calls spiBegin() when initInterface==true,
  // which is false when an explicit SPIClass& is supplied.  Override to
  // always call our spiBegin().
  void init() override { spiBegin(); }

  void spiBegin() override {
    // Configure GPIO pins for direct CPU control (disconnects any
    // peripheral / GPIO-matrix function, including leftover FSPI routing).
    ::pinMode(_sclk, OUTPUT);
    ::pinMode(_mosi, OUTPUT);
    ::pinMode(_miso, INPUT);
    ::digitalWrite(_sclk, LOW);
    Serial.printf("[SenseCapHAL] SPI (bit-bang) SCLK=%d MOSI=%d MISO=%d\n",
                  _sclk, _mosi, _miso);
  }

  void spiEnd() override {
    // nothing to tear down for bit-bang
  }

  void spiBeginTransaction() override {
    // nothing needed — CS is managed via the expander in digitalWrite()
  }

  void spiEndTransaction() override {
    // nothing needed
  }

  // Bit-bang SPI Mode 0 (CPOL=0, CPHA=0): data sampled on rising edge.
  void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override {
    for (size_t i = 0; i < len; i++) {
      uint8_t tx = out ? out[i] : 0x00;
      uint8_t rx = 0;
      for (int b = 7; b >= 0; b--) {
        ::digitalWrite(_mosi, (tx >> b) & 1);
        ::digitalWrite(_sclk, HIGH);
        rx = (rx << 1) | (::digitalRead(_miso) ? 1 : 0);
        ::digitalWrite(_sclk, LOW);
      }
      if (in) in[i] = rx;
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
  // RadioLib 7.x calls pinToInterrupt(irq_pin) BEFORE attachInterrupt().
  // Without this override, pinToInterrupt(0x43) → digitalPinToInterrupt(67)
  // which is invalid on ESP32-S3, so attachInterrupt() silently fails.
  // Return IO_EXPANDER_IRQ (GPIO 42) directly for any expander pin.
  uint32_t pinToInterrupt(uint32_t pin) override {
    if (isExp(pin)) {
      return (uint32_t)IO_EXPANDER_IRQ;
    }
    return ArduinoHal::pinToInterrupt(pin);
  }

  // Called with either:
  //   a) raw expander pin (e.g. 0x43) — direct call path
  //   b) IO_EXPANDER_IRQ (42)         — via pinToInterrupt() path (RadioLib 7.x)
  //
  // DEFERRED DISPATCH: instead of calling `cb` (= RadioLib's setFlag) directly
  // from the ISR, we attach a lightweight raw ISR that only sets _s_pending.
  // The real dispatch (reading Port 0 to verify DIO1 is HIGH) happens later
  // in dispatchPendingIrq(), called from the main loop via recvRaw().
  // This prevents touch-INT (pin 6), BUSY, and other Port 0 input changes
  // from triggering false radio-interrupt events.
  void attachInterrupt(uint32_t pin, void (*cb)(void), uint32_t mode) override {
    if (isExp(pin) || pin == (uint32_t)IO_EXPANDER_IRQ) {
      _s_cb  = cb;                          // save RadioLib's setFlag callback
      _irqBit = isExp(pin) ? expBit(pin)    // direct expander pin → derive bit
                           : (uint8_t)(1u << 3u); // pin=42 path → DIO1 is bit 3
      // Clear any pending TCA9535 /INT by reading BOTH port registers.
      readReg(0x00);
      readReg(0x01);
      ::pinMode(IO_EXPANDER_IRQ, INPUT_PULLUP);
      int gpio42 = ::digitalRead(IO_EXPANDER_IRQ);
      ::attachInterrupt(digitalPinToInterrupt(IO_EXPANDER_IRQ), _rawIsr, FALLING);
      Serial.printf("[SenseCapHAL] DIO1 interrupt → GPIO %d  state=%d (FALLING, deferred)\n",
                    IO_EXPANDER_IRQ, gpio42);
    } else {
      ArduinoHal::attachInterrupt(pin, cb, mode);
    }
  }

  // ── dispatchPendingIrq ────────────────────────────────────────────────
  // Call from the main loop (NOT from ISR).  Reads Port 0 to check whether
  // DIO1 is actually HIGH.  If so, forwards the event to RadioLib (setFlag).
  // Ignores spurious triggers from BUSY, touch-INT, or any other Port 0 input.
  void dispatchPendingIrq() {
    if (!_s_pending) return;
    _s_pending = false;

    // Reading Port 0 also acknowledges the TCA9535 /INT for ALL inputs,
    // including touch INT and BUSY — preventing /INT from staying stuck LOW.
    uint8_t port0 = readReg(0x00);
    if (_irqBit && (port0 & _irqBit)) {
      // DIO1 is HIGH → this is a real radio interrupt (RX_DONE or TX_DONE)
      if (_s_cb) _s_cb();
    }
    // else: spurious — BUSY transition, touch INT, etc.  Do nothing.
  }

  void detachInterrupt(uint32_t pin) override {
    if (isExp(pin) || pin == (uint32_t)IO_EXPANDER_IRQ) {
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
