#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <RadioLib.h>

#if defined(NRF52_PLATFORM)
#include <nrf.h>
#endif

#ifndef RADIOLIB_ERR_SPI_CMD_TIMEOUT
#define RADIOLIB_ERR_SPI_CMD_TIMEOUT -705
#endif

#define RADIO_INIT_FAULT_NONE 0x00
#define RADIO_INIT_FAULT_RADIO_INIT_FAIL 0x52

enum RadioInitBootStage : uint8_t {
  RADIO_BOOT_STAGE_NONE = 0x00,
  RADIO_BOOT_STAGE_RADIO_INIT_ENTERED = 0x30,
  RADIO_BOOT_STAGE_RTC_BEGIN_ENTERED = 0x31,
  RADIO_BOOT_STAGE_RTC_BEGIN_RETURNED = 0x32,
  RADIO_BOOT_STAGE_RADIO_STD_INIT_ENTERED = 0x40,
  RADIO_BOOT_STAGE_RADIO_STD_INIT_SUCCESS = 0x41,
  RADIO_BOOT_STAGE_RADIO_STD_INIT_FAIL = 0x42,
  RADIO_BOOT_STAGE_RADIO_BUSY_TIMEOUT = 0x43,
};

extern volatile int16_t g_last_radio_init_status;
extern volatile uint8_t g_radio_init_attempts;
extern volatile uint8_t g_radio_init_boot_stage;
extern volatile uint8_t g_radio_init_fault;

inline void radioInitSetBootStage(uint8_t stage) {
  g_radio_init_boot_stage = stage;
}

inline void radioInitRecordAttempt(uint8_t attempt) {
  g_radio_init_attempts = attempt;
}

inline void radioInitRecordStatus(int16_t status) {
  g_last_radio_init_status = status;
}

inline void radioInitRecordFault(uint8_t fault) {
  g_radio_init_fault = fault;
#if defined(NRF52_PLATFORM)
  // GPREGRET2 is already used as a reset-persistent one-byte reason store on
  // nRF52. Writing the radio fault before reset lets the next boot report that
  // the previous boot failed before normal application startup.
  NRF_POWER->GPREGRET2 = fault;
#endif
}

inline const char* radioInitFaultString(uint8_t fault) {
  switch (fault) {
    case RADIO_INIT_FAULT_NONE:
      return "None";
    case RADIO_INIT_FAULT_RADIO_INIT_FAIL:
      return "Radio Init Fail";
  }
  return "Unknown";
}

inline bool radioInitWaitBusyLow(uint32_t timeout_ms) {
#if defined(P_LORA_BUSY)
  if (P_LORA_BUSY == RADIOLIB_NC) return true;

  pinMode(P_LORA_BUSY, INPUT);
  uint32_t start = millis();
  while (digitalRead(P_LORA_BUSY) == HIGH) {
    if (millis() - start > timeout_ms) {
      radioInitSetBootStage(RADIO_BOOT_STAGE_RADIO_BUSY_TIMEOUT);
      radioInitRecordStatus(RADIOLIB_ERR_SPI_CMD_TIMEOUT);
      return false;
    }
    delay(1);
  }
#endif
  return true;
}

inline void radioInitResetPulse() {
#if defined(P_LORA_RESET)
  if (P_LORA_RESET == RADIOLIB_NC) return;

  pinMode(P_LORA_RESET, OUTPUT);
  digitalWrite(P_LORA_RESET, LOW);
  delay(10);
  digitalWrite(P_LORA_RESET, HIGH);
  delay(10);
#endif
}

inline void radioInitPowerCycle() {
#if defined(SX126X_POWER_EN)
  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, LOW);
  delay(50);
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(100);
#endif
}

inline void radioInitPrepareAttempt(uint8_t attempt) {
  if (attempt == 2) {
    radioInitResetPulse();
    delay(100);
  } else if (attempt >= 3) {
    radioInitPowerCycle();
    radioInitResetPulse();
    delay(150);
  }
}

inline void radioInitRebootAfterFault(mesh::MainBoard& board, uint8_t fault) {
  radioInitRecordFault(fault);
  Serial.flush();
  delay(250);
  board.reboot();
}
