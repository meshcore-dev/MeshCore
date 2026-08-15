#pragma once
//
// SCAFFOLD / TODO: RadioLib Hardware Abstraction Layer for Zephyr.
//
// MeshCore's radio bindings (src/helpers/radiolib/*) drive RadioLib. On Arduino
// RadioLib uses its built-in Arduino HAL; on Zephyr you must provide a HAL that
// maps RadioLib's GPIO/SPI/timing primitives onto Zephyr device drivers
// (gpio, spi, and the kernel clock).
//
// This header is a starting point only. It is NOT compiled by the baseline
// (CONFIG_MESHCORE_RADIOLIB is off by default). To finish the port:
//
//   1. Add RadioLib to the west manifest so <RadioLib.h> and <hal/RadioLibHal.h>
//      resolve.
//   2. Fill in the methods below using devicetree-declared spi/gpio nodes.
//   3. Set CONFIG_MESHCORE_RADIOLIB=y and select a MESHCORE_RADIO_* driver.
//
// Reference: RadioLib's examples/NonArduino/* HAL implementations.

#if defined(CONFIG_MESHCORE_RADIOLIB)

#include <RadioLib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

class ZephyrHal : public RadioLibHal {
public:
  ZephyrHal(const struct device* spi,
            const struct gpio_dt_spec* nss,
            const struct gpio_dt_spec* rst,
            const struct gpio_dt_spec* busy,
            const struct gpio_dt_spec* dio1);

  // --- RadioLibHal interface (see hal/RadioLibHal.h) -----------------------
  void init() override;
  void term() override;
  void pinMode(uint32_t pin, uint32_t mode) override;
  void digitalWrite(uint32_t pin, uint32_t value) override;
  uint32_t digitalRead(uint32_t pin) override;
  void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;
  void detachInterrupt(uint32_t interruptNum) override;
  void delay(unsigned long ms) override;
  void delayMicroseconds(unsigned long us) override;
  unsigned long millis() override;
  unsigned long micros() override;
  long pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) override;
  void spiBegin() override;
  void spiBeginTransaction() override;
  void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override;
  void spiEndTransaction() override;
  void spiEnd() override;

private:
  const struct device* _spi;
  const struct gpio_dt_spec* _nss;
  const struct gpio_dt_spec* _rst;
  const struct gpio_dt_spec* _busy;
  const struct gpio_dt_spec* _dio1;
};

#endif // CONFIG_MESHCORE_RADIOLIB
