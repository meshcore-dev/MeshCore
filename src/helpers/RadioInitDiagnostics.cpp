#include "RadioInitDiagnostics.h"

volatile int16_t g_last_radio_init_status = RADIOLIB_ERR_NONE;
volatile uint8_t g_radio_init_attempts = 0;
volatile uint8_t g_radio_init_boot_stage = RADIO_BOOT_STAGE_NONE;
volatile uint8_t g_radio_init_fault = RADIO_INIT_FAULT_NONE;
