/*
 * Minimal RadioLibHal for Zephyr (XIAO nRF54L15). Maps RadioLib's opaque pin ids
 * to gpio_dt_spec from the `lora0` devicetree node, and SPI to the spi00 controller
 * (no hardware CS, RadioLib drives NSS via digitalWrite). Raw GPIO ops keep
 * RadioLib's Arduino LOW/HIGH == physical-level semantics.
 *
 * Pin ids passed to Module(hal, cs, irq, rst, gpio):  NSS=0 DIO1=1 RESET=2 BUSY=3.
 */
#pragma once
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <RadioLib.h>

#define LR_PIN_NSS   0
#define LR_PIN_DIO1  1
#define LR_PIN_RESET 2
#define LR_PIN_BUSY  3
#define LR_PIN_COUNT 4

#define LORA0_NODE DT_NODELABEL(lora0)
#define SPI0_NODE  DT_NODELABEL(spi00)

class ZephyrHal : public RadioLibHal {
  public:
    /* GPIO modes / levels / edges — opaque tokens RadioLib hands back to us. */
    ZephyrHal()
      : RadioLibHal(/*INPUT*/0, /*OUTPUT*/1, /*LOW*/0, /*HIGH*/1, /*RISING*/1, /*FALLING*/2) {}

    void init() override { spiBegin(); }
    void term() override {}

    void pinMode(uint32_t pin, uint32_t mode) override {
        if (pin >= LR_PIN_COUNT) return;
        const struct gpio_dt_spec *g = &gpios[pin];
        gpio_pin_configure(g->port, g->pin, (mode == 1) ? GPIO_OUTPUT : GPIO_INPUT);
    }
    void digitalWrite(uint32_t pin, uint32_t value) override {
        if (pin >= LR_PIN_COUNT) return;
        const struct gpio_dt_spec *g = &gpios[pin];
        gpio_pin_set_raw(g->port, g->pin, value ? 1 : 0);
    }
    uint32_t digitalRead(uint32_t pin) override {
        if (pin >= LR_PIN_COUNT) return 0;
        const struct gpio_dt_spec *g = &gpios[pin];
        return gpio_pin_get_raw(g->port, g->pin);
    }

    void attachInterrupt(uint32_t interruptNum, void (*cb)(void), uint32_t mode) override {
        if (interruptNum >= LR_PIN_COUNT) return;
        const struct gpio_dt_spec *g = &gpios[interruptNum];
        user_cb = cb;
        gpio_pin_configure(g->port, g->pin, GPIO_INPUT);   /* RadioLib doesn't pinMode the IRQ */
        if (!cb_added) {                                   /* register the callback exactly once */
            gpio_init_callback(&cb_data, gpioIsr, BIT(g->pin));
            gpio_add_callback(g->port, &cb_data);
            cb_added = true;
        }
        gpio_pin_interrupt_configure(g->port, g->pin,
            (mode == 2) ? GPIO_INT_EDGE_FALLING :
            (mode == 1) ? GPIO_INT_EDGE_RISING  : GPIO_INT_EDGE_BOTH);
    }
    void detachInterrupt(uint32_t interruptNum) override {
        if (interruptNum >= LR_PIN_COUNT) return;
        const struct gpio_dt_spec *g = &gpios[interruptNum];
        gpio_pin_interrupt_configure(g->port, g->pin, GPIO_INT_DISABLE);
        gpio_remove_callback(g->port, &cb_data);
        user_cb = nullptr;
    }

    void delay(RadioLibTime_t ms) override { k_msleep((int32_t)ms); }
    void delayMicroseconds(RadioLibTime_t us) override { k_busy_wait((uint32_t)us); }
    RadioLibTime_t millis() override { return (RadioLibTime_t)k_uptime_get(); }
    RadioLibTime_t micros() override { return (RadioLibTime_t)k_ticks_to_us_floor64(k_uptime_ticks()); }
    long pulseIn(uint32_t, uint32_t, RadioLibTime_t) override { return 0; }

    void spiBegin() override {
        spi_dev = DEVICE_DT_GET(SPI0_NODE);
        spi_cfg.frequency = 2000000;     /* 2 MHz, SPI mode 0, MSB first */
        spi_cfg.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB;
        spi_cfg.slave = 0;
        spi_cfg.cs.gpio.port = NULL;     /* no HW CS — RadioLib toggles NSS */
    }
    void spiBeginTransaction() override {}
    void spiEndTransaction() override {}
    void spiEnd() override {}

    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override {
        struct spi_buf txb = { .buf = out, .len = len };
        struct spi_buf rxb = { .buf = in,  .len = len };
        struct spi_buf_set tx = { .buffers = &txb, .count = 1 };
        struct spi_buf_set rx = { .buffers = &rxb, .count = 1 };
        spi_transceive(spi_dev, &spi_cfg, (out ? &tx : NULL), (in ? &rx : NULL));
    }

  private:
    static void (*user_cb)(void);
    static struct gpio_callback cb_data;
    static bool cb_added;
    static void gpioIsr(const struct device *, struct gpio_callback *, uint32_t) {
        if (user_cb) user_cb();
    }

    const struct device *spi_dev = nullptr;
    struct spi_config spi_cfg = {};

    /* index by LR_PIN_*; order must match the #defines above */
    const struct gpio_dt_spec gpios[LR_PIN_COUNT] = {
        GPIO_DT_SPEC_GET(LORA0_NODE, nss_gpios),
        GPIO_DT_SPEC_GET(LORA0_NODE, dio1_gpios),
        GPIO_DT_SPEC_GET(LORA0_NODE, reset_gpios),
        GPIO_DT_SPEC_GET(LORA0_NODE, busy_gpios),
    };
};

void (*ZephyrHal::user_cb)(void) = nullptr;
struct gpio_callback ZephyrHal::cb_data;
bool ZephyrHal::cb_added = false;
