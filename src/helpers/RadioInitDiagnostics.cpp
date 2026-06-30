#include "RadioInitDiagnostics.h"

volatile int16_t g_last_radio_init_status = RADIOLIB_ERR_NONE;
volatile uint8_t g_radio_init_attempts = 0;
volatile uint8_t g_radio_init_boot_stage = RADIO_BOOT_STAGE_NONE;
volatile uint8_t g_radio_init_fault = RADIO_INIT_FAULT_NONE;

#if defined(NRF52_PLATFORM)
RadioInitBootHistory g_radio_init_boot_history __attribute__((section(".noinit")));
#else
RadioInitBootHistory g_radio_init_boot_history = {};
#endif

static void resetRecord(volatile RadioInitBootRecord& record) {
  record.reset_reason = 0;
  record.radio_status = RADIOLIB_ERR_NONE;
  record.shutdown_reason = RADIO_INIT_FAULT_NONE;
  record.fault = RADIO_INIT_FAULT_NONE;
  record.boot_stage = RADIO_BOOT_STAGE_NONE;
  record.attempts = 0;
}

static void ensureBootHistory() {
  if (g_radio_init_boot_history.magic == RADIO_INIT_BOOT_DIAG_MAGIC &&
      g_radio_init_boot_history.version == RADIO_INIT_BOOT_DIAG_VERSION) {
    return;
  }

  g_radio_init_boot_history.magic = RADIO_INIT_BOOT_DIAG_MAGIC;
  g_radio_init_boot_history.version = RADIO_INIT_BOOT_DIAG_VERSION;
  resetRecord(g_radio_init_boot_history.current);
  resetRecord(g_radio_init_boot_history.previous);
}

void radioInitCaptureBoot(uint32_t reset_reason, uint8_t shutdown_reason) {
  ensureBootHistory();

  g_radio_init_boot_history.previous = g_radio_init_boot_history.current;
  resetRecord(g_radio_init_boot_history.current);

  g_radio_init_boot_history.current.reset_reason = reset_reason;
  g_radio_init_boot_history.current.shutdown_reason = shutdown_reason;
  g_radio_init_boot_history.current.fault = shutdown_reason;

  g_radio_init_fault = shutdown_reason;
  g_radio_init_boot_stage = RADIO_BOOT_STAGE_NONE;
  g_radio_init_attempts = 0;
  g_last_radio_init_status = RADIOLIB_ERR_NONE;
}

RadioInitBootRecord radioInitCurrentBootRecord() {
  ensureBootHistory();
  return g_radio_init_boot_history.current;
}

RadioInitBootRecord radioInitPreviousBootRecord() {
  ensureBootHistory();
  return g_radio_init_boot_history.previous;
}

void radioInitHistorySetStage(uint8_t stage) {
  ensureBootHistory();
  g_radio_init_boot_history.current.boot_stage = stage;
}

void radioInitHistorySetAttempt(uint8_t attempt) {
  ensureBootHistory();
  g_radio_init_boot_history.current.attempts = attempt;
}

void radioInitHistorySetStatus(int16_t status) {
  ensureBootHistory();
  g_radio_init_boot_history.current.radio_status = status;
}

void radioInitHistorySetFault(uint8_t fault) {
  ensureBootHistory();
  g_radio_init_boot_history.current.fault = fault;
}
