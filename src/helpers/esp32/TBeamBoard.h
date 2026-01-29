#pragma once

#if defined(TBEAM_SUPREME_SX1262) || defined(TBEAM_SX1262) || defined(TBEAM_SX1276)

// Define pin mappings BEFORE including ESP32Board.h so sleep() can use P_LORA_DIO_1
#ifdef TBEAM_SUPREME_SX1262
  // LoRa radio module pins for TBeam S3 Supreme SX1262
  #define  P_LORA_DIO_0   -1   //NC
  #define  P_LORA_DIO_1    1   //SX1262 IRQ pin
  #define  P_LORA_NSS      10  //SX1262 SS pin
  #define  P_LORA_RESET    5   //SX1262 Rest pin
  #define  P_LORA_BUSY     4   //SX1262 Busy pin
  #define  P_LORA_SCLK     12   //SX1262 SCLK pin
  #define  P_LORA_MISO     13   //SX1262 MISO pin
  #define  P_LORA_MOSI     11   //SX1262 MOSI pin

  #define PIN_BOARD_SDA1  42  //SDA for PMU and PFC8563 (RTC)
  #define PIN_BOARD_SCL1  41  //SCL for PMU and PFC8563 (RTC)

  #define PIN_PMU_IRQ  40     //IRQ pin for PMU

  // #define PIN_GPS_RX      9
  // #define PIN_GPS_TX      8
  // #define PIN_GPS_EN      7

  #define P_BOARD_SPI_MOSI  35  //SPI for SD Card and QMI8653 (IMU)
  #define P_BOARD_SPI_MISO  37  //SPI for SD Card and QMI8653 (IMU)
  #define P_BOARD_SPI_SCK   36  //SPI for SD Card and QMI8653 (IMU)
  #define P_BPARD_SPI_CS    47  //Pin for SD Card CS
  #define P_BOARD_IMU_CS    34  //Pin for QMI8653 (IMU) CS

  #define P_BOARD_IMU_INT  33  //IMU Int pin
  #define P_BOARD_RTC_INT  14  //RTC Int pin

  //I2C Wire addresses
  #define I2C_BME280_ADD     0x76  //BME280 sensor I2C address on Wire
  #define I2C_OLED_ADD       0x3C  //SH1106 OLED I2C address on Wire
  #define I2C_QMC6310U_ADD   0x1C  //QMC6310U mag sensor I2C address on Wire

  //I2C Wire1 addresses
  #define I2C_RTC_ADD  0x51  //RTC I2C address on Wire1
  #define I2C_PMU_ADD  0x34  //AXP2101 I2C address on Wire1
  
  #define PMU_WIRE_PORT  Wire1
  #define RTC_WIRE_PORT  Wire1
#endif

#ifdef TBEAM_SX1262
  #define  P_LORA_BUSY    32
#endif

#ifdef TBEAM_SX1276
  #define  P_LORA_DIO_2    32
  #define  P_LORA_BUSY    RADIOLIB_NC
#endif

#if defined(TBEAM_SX1262) || defined(TBEAM_SX1276)
  // LoRa radio module pins for TBeam
  // uint32_t  P_LORA_BUSY  = 0;   //shared, so define at run
  // uint32_t  P_LORA_DIO_2 = 0;   //SX1276 only, so define at run

  #define  P_LORA_DIO_0  26
  #define  P_LORA_DIO_1  33
  #define  P_LORA_NSS    18
  #define  P_LORA_RESET  23
  #define  P_LORA_SCLK    5
  #define  P_LORA_MISO   19
  #define  P_LORA_MOSI   27

  // #define PIN_GPS_RX      34
  // #define PIN_GPS_TX      12

  #define PIN_PMU_IRQ   35
  #define PMU_WIRE_PORT  Wire
  #define RTC_WIRE_PORT  Wire
  #define I2C_PMU_ADD    0x34
#endif

// enum RadioType {
//   SX1262, 
//   SX1276
// };

// Include headers AFTER pin definitions so ESP32Board::sleep() can use P_LORA_DIO_1
#include <Wire.h>
#include <Arduino.h>
#include "XPowersLib.h"
#include "helpers/ESP32Board.h"
#include <driver/rtc_io.h>

class TBeamBoard : public ESP32Board {
XPowersLibInterface *PMU = NULL;
//PhysicalLayer * pl;
//RadioType * radio = NULL;
// int radioVersions = 2;

enum {
  POWERMANAGE_ONLINE  = _BV(0),
  DISPLAY_ONLINE      = _BV(1),
  RADIO_ONLINE        = _BV(2),
  GPS_ONLINE          = _BV(3),
  PSRAM_ONLINE        = _BV(4),
  SDCARD_ONLINE       = _BV(5),
  AXDL345_ONLINE      = _BV(6),
  BME280_ONLINE       = _BV(7),
  BMP280_ONLINE       = _BV(8),
  BME680_ONLINE       = _BV(9),
  QMC6310_ONLINE      = _BV(10),
  QMI8658_ONLINE      = _BV(11),
  PCF8563_ONLINE      = _BV(12),
  OSC32768_ONLINE      = _BV(13),
};

bool power_init();
//void radiotype_detect();

public:

#ifdef MESH_DEBUG
  void printPMU();
  void scanDevices(TwoWire *w);
#endif
  void begin();

  #ifndef TBEAM_SUPREME_SX1262
  void onBeforeTransmit() override{
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED on - invert pin for SX1276
  }
  void onAfterTransmit() override{
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED off - invert pin for SX1276
  }
  #endif

  void enterDeepSleep(uint32_t secs, int pin_wake_btn) {
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  // Make sure the DIO1 and NSS GPIOs are hold on required levels during deep sleep
  rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);

  rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

  if (pin_wake_btn < 0) {
    esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet
  } else {
    esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1) | (1L << pin_wake_btn), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet OR wake btn
  }

  if (secs > 0) {
    esp_sleep_enable_timer_wakeup(secs * 1000000);
  }

  // Finally set ESP32 into sleep
  esp_deep_sleep_start();   // CPU halts here and never returns!
}

  uint16_t getBattMilliVolts(){
    return PMU->getBattVoltage();
  }

  const char* getManufacturerName() const{
    return "LilyGo T-Beam";
  }
};

#endif
