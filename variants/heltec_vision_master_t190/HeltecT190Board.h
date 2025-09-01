#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>
#include <driver/rtc_io.h>

// LoRa radio module pins for heltec_vision_master_e290 
#define  P_LORA_DIO_1   14
#define  P_LORA_NSS      8
#define  P_LORA_RESET   12
#define  P_LORA_BUSY    13
#define  P_LORA_SCLK     9
#define  P_LORA_MISO    11
#define  P_LORA_MOSI    10

class HeltecT190Board : public ESP32Board {

public:
  RefCountedDigitalPin periph_power;

  HeltecT190Board() : periph_power(PIN_VEXT_EN,PIN_VEXT_EN_ACTIVE) { }

  void begin();
  void enterDeepSleep(uint32_t secs, int pin_wake_btn = -1);
  void powerOff() override;
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override ;

};
