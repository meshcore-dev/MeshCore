#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <time.h>
#include <stdlib.h>

class PortduinoBoard : public mesh::MainBoard {
public:
  void begin() { }

  uint16_t getBattMilliVolts() override {
    return 0;  // mains powered, no battery
  }

  const char* getManufacturerName() const override {
    return "Linux/Portduino";
  }

  void reboot() override {
    exit(0);  // let systemd restart the process
  }

  uint8_t getStartupReason() const override {
    return BD_STARTUP_NORMAL;
  }
};

class LinuxRTCClock : public mesh::RTCClock {
public:
  uint32_t getCurrentTime() override {
    return (uint32_t)time(NULL);
  }

  void setCurrentTime(uint32_t time) override {
    // no-op: don't let mesh alter system clock
  }
};
