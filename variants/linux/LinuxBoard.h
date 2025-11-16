#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <sys/time.h>

class LinuxConfig {
public:
  float lora_freq = LORA_FREQ;
  float lora_bw = LORA_BW;
  uint8_t lora_sf = LORA_SF;
#ifdef LORA_CR
  uint8_t lora_cr = LORA_CR;
#else
  uint8_t lora_cr = 5;
#endif

  uint32_t lora_irq_pin = -1;
  uint32_t lora_reset_pin = -1;

  char* spidev = "/dev/spidev0.0";

#ifdef SX126X_DIO3_TCXO_VOLTAGE
  float lora_tcxo = SX126X_DIO3_TCXO_VOLTAGE;
#else
  float lora_tcxo = 1.6f;
#endif

  char *advert_name = "Linux Repeater";
  char *admin_password = "password";
  float lat = 0.0f;
  float lon = 0.0f;

  int load(const char *filename);
};

class LinuxBoard : public mesh::MainBoard {
protected:
  uint8_t startup_reason;
  uint8_t btn_prev_state;

public:
  void begin();

  uint16_t getBattMilliVolts() override {
    return 0;
  }

  uint8_t getStartupReason() const override { return startup_reason; }

  const char* getManufacturerName() const override {
    return "Linux";
  }

  int buttonStateChanged() {
    return 0;
  }

  void powerOff() override {
    exit(0);
  }

  void reboot() override {
    exit(0);
  }

  LinuxConfig config;
};

class LinuxRTCClock : public mesh::RTCClock {
public:
  LinuxRTCClock() { }
  void begin() {
  }
  uint32_t getCurrentTime() override {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
  }
  void setCurrentTime(uint32_t time) override {
    struct timeval tv;
    tv.tv_sec = time;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
  }
};
