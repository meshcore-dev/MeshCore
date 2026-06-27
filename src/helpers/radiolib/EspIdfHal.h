#pragma once

#include <RadioLib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"

#define HAL_LOW (0x0)
#define HAL_HIGH (0x1)
#define HAL_INPUT (0x01)
#define HAL_OUTPUT (0x03)
#define HAL_RISING (0x01)
#define HAL_FALLING (0x02)
#define NOP() asm volatile("nop")

class EspIdfHal : public RadioLibHal {
public:
  EspIdfHal(int8_t sck, int8_t miso, int8_t mosi)
      : RadioLibHal(HAL_INPUT, HAL_OUTPUT, HAL_LOW, HAL_HIGH, HAL_RISING,
                    HAL_FALLING),
        spiSCK(sck), spiMISO(miso), spiMOSI(mosi), spiDev(nullptr),
        spiInitialized(false) {}

  void init() override { spiBegin(); }

  void term() override { spiEnd(); }

  void pinMode(uint32_t pin, uint32_t mode) override {
    if (pin == RADIOLIB_NC)
      return;
    gpio_config_t conf = {};
    conf.pin_bit_mask = (1ULL << pin);
    conf.mode = (gpio_mode_t)mode;
    conf.pull_up_en = GPIO_PULLUP_DISABLE;
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&conf);
  }

  void digitalWrite(uint32_t pin, uint32_t value) override {
    if (pin == RADIOLIB_NC)
      return;
    gpio_set_level((gpio_num_t)pin, value);
  }

  uint32_t digitalRead(uint32_t pin) override {
    if (pin == RADIOLIB_NC)
      return (0);
    return (gpio_get_level((gpio_num_t)pin));
  }

  void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void),
                       uint32_t mode) override {
    if (interruptNum == RADIOLIB_NC)
      return;
    gpio_install_isr_service((int)ESP_INTR_FLAG_IRAM);
    gpio_set_intr_type((gpio_num_t)interruptNum,
                       (gpio_int_type_t)(mode & 0x7));
    gpio_isr_handler_add((gpio_num_t)interruptNum,
                         (void (*)(void *))interruptCb, NULL);
  }

  void detachInterrupt(uint32_t interruptNum) override {
    if (interruptNum == RADIOLIB_NC)
      return;
    gpio_isr_handler_remove((gpio_num_t)interruptNum);
    gpio_set_intr_type((gpio_num_t)interruptNum, GPIO_INTR_DISABLE);
  }

  void delay(unsigned long ms) override {
    vTaskDelay(ms / portTICK_PERIOD_MS);
  }

  void delayMicroseconds(unsigned long us) override {
    uint64_t m = (uint64_t)esp_timer_get_time();
    if (us) {
      uint64_t e = (m + us);
      if (m > e) {
        while ((uint64_t)esp_timer_get_time() > e) {
          NOP();
        }
      }
      while ((uint64_t)esp_timer_get_time() < e) {
        NOP();
      }
    }
  }

  unsigned long millis() override {
    return ((unsigned long)(esp_timer_get_time() / 1000ULL));
  }

  unsigned long micros() override {
    return ((unsigned long)(esp_timer_get_time()));
  }

  long pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) override {
    if (pin == RADIOLIB_NC)
      return (0);
    this->pinMode(pin, HAL_INPUT);
    uint32_t start = this->micros();
    uint32_t curtick = this->micros();
    while (this->digitalRead(pin) == state) {
      if ((this->micros() - curtick) > timeout)
        return (0);
    }
    return (this->micros() - start);
  }

  void spiBegin() override {
    if (spiInitialized)
      return;
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = this->spiMOSI;
    bus_cfg.miso_io_num = this->spiMISO;
    bus_cfg.sclk_io_num = this->spiSCK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 256;
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
      return;
    }

    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.mode = 0;
    dev_cfg.clock_speed_hz = 2000000;
    dev_cfg.spics_io_num = -1;
    dev_cfg.queue_size = 1;
    ret = spi_bus_add_device(SPI2_HOST, &dev_cfg, &this->spiDev);
    if (ret != ESP_OK) {
      return;
    }
    this->spiInitialized = true;
  }

  void spiBeginTransaction() override {}

  void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override {
    uint8_t *buf = out;
    for (size_t i = 0; i < len; i++) {
      spi_transaction_t trans = {};
      trans.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
      trans.length = 8;
      trans.tx_data[0] = buf[i];
      esp_err_t ret = spi_device_polling_transmit(this->spiDev, &trans);
      if (ret != ESP_OK) {
        in[i] = 0xFF;
      } else {
        in[i] = trans.rx_data[0];
      }
    }
  }

  void spiEndTransaction() override {}

  void spiEnd() override {
    if (this->spiDev) {
      spi_bus_remove_device(this->spiDev);
      this->spiDev = nullptr;
    }
    spi_bus_free(SPI2_HOST);
    this->spiInitialized = false;
  }

private:
  int8_t spiSCK;
  int8_t spiMISO;
  int8_t spiMOSI;
  spi_device_handle_t spiDev;
  bool spiInitialized;
};
