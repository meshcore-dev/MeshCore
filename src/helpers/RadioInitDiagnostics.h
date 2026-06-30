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
#define RADIO_INIT_BOOT_DIAG_MAGIC 0x4D435244UL  // "MCRD"
#define RADIO_INIT_BOOT_DIAG_VERSION 1

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

struct RadioInitBootRecord {
  uint32_t reset_reason;
  int16_t radio_status;
  uint8_t shutdown_reason;
  uint8_t fault;
  uint8_t boot_stage;
  uint8_t attempts;
};

struct RadioInitBootHistory {
  uint32_t magic;
  uint8_t version;
  RadioInitBootRecord current;
  RadioInitBootRecord previous;
};

extern volatile int16_t g_last_radio_init_status;
extern volatile uint8_t g_radio_init_attempts;
extern volatile uint8_t g_radio_init_boot_stage;
extern volatile uint8_t g_radio_init_fault;
extern RadioInitBootHistory g_radio_init_boot_history;

void radioInitCaptureBoot(uint32_t reset_reason, uint8_t shutdown_reason);
RadioInitBootRecord radioInitCurrentBootRecord();
RadioInitBootRecord radioInitPreviousBootRecord();
void radioInitHistorySetStage(uint8_t stage);
void radioInitHistorySetAttempt(uint8_t attempt);
void radioInitHistorySetStatus(int16_t status);
void radioInitHistorySetFault(uint8_t fault);

inline void radioInitSetBootStage(uint8_t stage) {
  g_radio_init_boot_stage = stage;
  radioInitHistorySetStage(stage);
}

inline void radioInitRecordAttempt(uint8_t attempt) {
  g_radio_init_attempts = attempt;
  radioInitHistorySetAttempt(attempt);
}

inline void radioInitRecordStatus(int16_t status) {
  g_last_radio_init_status = status;
  radioInitHistorySetStatus(status);
}

inline void radioInitRecordFault(uint8_t fault) {
  g_radio_init_fault = fault;
  radioInitHistorySetFault(fault);
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
