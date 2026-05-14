#pragma once

#if defined(T_ETH_ELITE_SX1262)

// Define pin mappings BEFORE including ESP32Board.h so sleep() can use P_LORA_DIO_1

// LoRa SX1262 pins (T-ETH Elite LoRa Shield)
#define P_LORA_NSS      40  // CS
#define P_LORA_RESET    46  // RESET
#define P_LORA_BUSY     16  // BUSY
#define P_LORA_DIO_0    -1  // NC
#define P_LORA_DIO_1    8   // IRQ
#define P_LORA_TX_LED   38
#define P_LORA_SCLK     10
#define P_LORA_MISO     9
#define P_LORA_MOSI     11
#define P_LORA_WAKE_DIO P_LORA_DIO_1  // DIO used as deep-sleep wake source

// ETH W5500 pins (T-ETH Elite main board)
#define ETH_MISO       47
#define ETH_MOSI       21
#define ETH_SCLK       48
#define ETH_CS         45
#define ETH_INT        14
#define ETH_RST        -1
#define ETH_ADDR       1

// I2C bus (Wire1)
#define PIN_BOARD_SDA1 17
#define PIN_BOARD_SCL1 18

// GPS pins (LoRa Shield)
#define PIN_GPS_RX     39
#define PIN_GPS_TX     42
#define PIN_GPS_EN     -1

// Analog button
// #define PIN_USER_BTN_ANA 7

// Include headers AFTER pin definitions so ESP32Board::sleep() can use P_LORA_DIO_1
#include <Wire.h>
#include <Arduino.h>
#include "XPowersLib.h"
#include "helpers/ESP32Board.h"
#include <driver/rtc_io.h>

class TEthEliteBoard : public ESP32Board {
  XPowersLibInterface *PMU = NULL;

  enum {
    POWERMANAGE_ONLINE = _BV(0),
    DISPLAY_ONLINE     = _BV(1),
    RADIO_ONLINE       = _BV(2),
    GPS_ONLINE         = _BV(3),
    PSRAM_ONLINE       = _BV(4),
    SDCARD_ONLINE      = _BV(5),
    AXDL345_ONLINE     = _BV(6),
    BME280_ONLINE      = _BV(7),
    BMP280_ONLINE      = _BV(8),
    BME680_ONLINE      = _BV(9),
    QMC6310_ONLINE     = _BV(10),
    QMI8658_ONLINE     = _BV(11),
    PCF8563_ONLINE     = _BV(12),
    OSC32768_ONLINE    = _BV(13),
  };

public:

#ifdef MESH_DEBUG
  void printPMU();
  void scanDevices(TwoWire *w);
#endif
  void begin();
  void startNetwork();
  void startEthernet();
  void startWifi();
  void reconfigureEthernet(uint32_t ip, uint32_t gw, uint32_t subnet, uint32_t dns1 = 0);
  
  void enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    rtc_gpio_set_direction((gpio_num_t)P_LORA_WAKE_DIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)P_LORA_WAKE_DIO);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    uint64_t wake_mask = 1ULL << P_LORA_WAKE_DIO;
    if (pin_wake_btn >= 0) wake_mask |= 1ULL << pin_wake_btn;
    esp_sleep_enable_ext1_wakeup(wake_mask, ESP_EXT1_WAKEUP_ANY_HIGH);

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000);
    }

    esp_deep_sleep_start();
  }

  uint16_t getBattMilliVolts() { return 0; }

  const char* getManufacturerName() const { return "LilyGo T-ETH-Elite"; }
};

#endif
