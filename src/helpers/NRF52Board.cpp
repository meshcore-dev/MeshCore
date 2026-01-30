#if defined(NRF52_PLATFORM)
#include "NRF52Board.h"

#include <bluefruit.h>
#include <nrf_soc.h>

static BLEDfu bledfu;

static void connect_callback(uint16_t conn_handle) {
  (void)conn_handle;
  MESH_DEBUG_PRINTLN("BLE client connected");
}

static void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void)conn_handle;
  (void)reason;

  MESH_DEBUG_PRINTLN("BLE client disconnected");
}

void NRF52Board::begin() {
  startup_reason = BD_STARTUP_NORMAL;
}

#ifdef NRF52_POWER_MANAGEMENT
#include "nrf.h"

// Power Management global variables
uint32_t g_nrf52_reset_reason = 0;     // Reset/Startup reason
uint8_t g_nrf52_shutdown_reason = 0;   // Shutdown reason

// Early constructor - runs before SystemInit() clears the registers
// Priority 101 ensures this runs before SystemInit (102) and before
// any C++ static constructors (default 65535)
static void __attribute__((constructor(101))) nrf52_early_reset_capture() {
  g_nrf52_reset_reason = NRF_POWER->RESETREAS;
  g_nrf52_shutdown_reason = NRF_POWER->GPREGRET2;
}

void NRF52Board::initPowerMgr() {
  // Copy early-captured register values
  reset_reason = g_nrf52_reset_reason;
  shutdown_reason = g_nrf52_shutdown_reason;
  boot_voltage_mv = 0;  // Will be set by checkBootVoltage()

  // Clear registers for next boot
  // Note: At this point SoftDevice may or may not be enabled
  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) {
    sd_power_reset_reason_clr(0xFFFFFFFF);
    sd_power_gpregret_clr(1, 0xFF);
  } else {
    NRF_POWER->RESETREAS = 0xFFFFFFFF;  // Write 1s to clear
    NRF_POWER->GPREGRET2 = 0;
  }

  // Log reset/shutdown info
  if (shutdown_reason != SHUTDOWN_REASON_NONE) {
    MESH_DEBUG_PRINTLN("PWRMGT: Reset = %s (0x%lX); Shutdown = %s (0x%02X)",
      getResetReasonString(reset_reason), (unsigned long)reset_reason,
      getShutdownReasonString(shutdown_reason), shutdown_reason);
  } else {
    MESH_DEBUG_PRINTLN("PWRMGT: Reset = %s (0x%lX)",
      getResetReasonString(reset_reason), (unsigned long)reset_reason);
  }
}

bool NRF52Board::isExternalPowered() {
  // Check if SoftDevice is enabled before using its API
  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);

  if (sd_enabled) {
    uint32_t usb_status;
    sd_power_usbregstatus_get(&usb_status);
    return (usb_status & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
  } else {
    return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
  }
}

const char* NRF52Board::getResetReasonString(uint32_t reason) {
  if (reason & POWER_RESETREAS_RESETPIN_Msk) return "Reset Pin";
  if (reason & POWER_RESETREAS_DOG_Msk) return "Watchdog";
  if (reason & POWER_RESETREAS_SREQ_Msk) return "Soft Reset";
  if (reason & POWER_RESETREAS_LOCKUP_Msk) return "CPU Lockup";
  #ifdef POWER_RESETREAS_LPCOMP_Msk
    if (reason & POWER_RESETREAS_LPCOMP_Msk) return "Wake from LPCOMP";
  #endif
  #ifdef POWER_RESETREAS_VBUS_Msk
    if (reason & POWER_RESETREAS_VBUS_Msk) return "Wake from VBUS";
  #endif
  #ifdef POWER_RESETREAS_OFF_Msk
    if (reason & POWER_RESETREAS_OFF_Msk) return "Wake from GPIO";
  #endif
  #ifdef POWER_RESETREAS_DIF_Msk
    if (reason & POWER_RESETREAS_DIF_Msk) return "Debug Interface";
  #endif
  return "Cold Boot";
}

const char* NRF52Board::getShutdownReasonString(uint8_t reason) {
  switch (reason) {
    case SHUTDOWN_REASON_LOW_VOLTAGE:  return "Low Voltage";
    case SHUTDOWN_REASON_USER:         return "User Request";
    case SHUTDOWN_REASON_BOOT_PROTECT: return "Boot Protection";
  }
  return "Unknown";
}

bool NRF52Board::checkBootVoltage(const PowerMgtConfig* config) {
  initPowerMgr();

  // Read boot voltage
  boot_voltage_mv = getBattMilliVolts();
  
  if (config->voltage_bootlock == 0) return true;  // Protection disabled

  // Skip check if externally powered
  if (isExternalPowered()) {
    MESH_DEBUG_PRINTLN("PWRMGT: Boot check skipped (external power)");
    boot_voltage_mv = getBattMilliVolts();
    return true;
  }

  MESH_DEBUG_PRINTLN("PWRMGT: Boot voltage = %u mV (threshold = %u mV)",
      boot_voltage_mv, config->voltage_bootlock);

  // Only trigger shutdown if reading is valid (>1000mV) AND below threshold
  // This prevents spurious shutdowns on ADC glitches or uninitialized reads
  if (boot_voltage_mv > 1000 && boot_voltage_mv < config->voltage_bootlock) {
    MESH_DEBUG_PRINTLN("PWRMGT: Boot voltage too low - entering protective shutdown");

    initiateShutdown(SHUTDOWN_REASON_BOOT_PROTECT);
    return false;  // Should never reach this
  }

  return true;
}

void NRF52Board::initiateShutdown(uint8_t reason) {
  enterSystemOff(reason);
}

void NRF52Board::enterSystemOff(uint8_t reason) {
  MESH_DEBUG_PRINTLN("PWRMGT: Entering SYSTEMOFF (%s)", getShutdownReasonString(reason));

  // Record shutdown reason in GPREGRET2
  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) {
    sd_power_gpregret_clr(1, 0xFF);
    sd_power_gpregret_set(1, reason);
  } else {
    NRF_POWER->GPREGRET2 = reason;
  }

  // Flush serial buffers
  Serial.flush();
  delay(100);

  // Enter SYSTEMOFF
  if (sd_enabled) {
    uint32_t err = sd_power_system_off();
    if (err == NRF_ERROR_SOFTDEVICE_NOT_ENABLED) {  //SoftDevice not enabled
      sd_enabled = 0;
    }
  }

  if (!sd_enabled) {
    // SoftDevice not available; write directly to POWER->SYSTEMOFF
    NRF_POWER->SYSTEMOFF = POWER_SYSTEMOFF_SYSTEMOFF_Enter;
  }

  // If we get here, something went wrong. Reset to recover.
  NVIC_SystemReset();
}

void NRF52Board::configureVoltageWake(uint8_t ain_channel, uint8_t refsel) {
  // LPCOMP is not managed by SoftDevice - direct register access required
  // Halt and disable before reconfiguration
  NRF_LPCOMP->TASKS_STOP = 1;
  NRF_LPCOMP->ENABLE = LPCOMP_ENABLE_ENABLE_Disabled;

  // Select analog input (AIN0-7 maps to PSEL 0-7)
  NRF_LPCOMP->PSEL = ((uint32_t)ain_channel << LPCOMP_PSEL_PSEL_Pos) & LPCOMP_PSEL_PSEL_Msk;

  // Reference: REFSEL (0-6=1/8..7/8, 7=ARef, 8-15=1/16..15/16)
  NRF_LPCOMP->REFSEL = ((uint32_t)refsel << LPCOMP_REFSEL_REFSEL_Pos) & LPCOMP_REFSEL_REFSEL_Msk;

  // Detect UP events (voltage rises above threshold for battery recovery)
  NRF_LPCOMP->ANADETECT = LPCOMP_ANADETECT_ANADETECT_Up;

  // Enable 50mV hysteresis for noise immunity
  NRF_LPCOMP->HYST = LPCOMP_HYST_HYST_Hyst50mV;

  // Clear stale events/interrupts before enabling wake
  NRF_LPCOMP->EVENTS_READY = 0;
  NRF_LPCOMP->EVENTS_DOWN = 0;
  NRF_LPCOMP->EVENTS_UP = 0;
  NRF_LPCOMP->EVENTS_CROSS = 0;

  NRF_LPCOMP->INTENCLR = 0xFFFFFFFF;
  NRF_LPCOMP->INTENSET = LPCOMP_INTENSET_UP_Msk;

  // Enable LPCOMP
  NRF_LPCOMP->ENABLE = LPCOMP_ENABLE_ENABLE_Enabled;
  NRF_LPCOMP->TASKS_START = 1;

  // Wait for comparator to settle before entering SYSTEMOFF
  for (uint8_t i = 0; i < 20 && !NRF_LPCOMP->EVENTS_READY; i++) {
    delayMicroseconds(50);
  }

  if (refsel == 7) {
    MESH_DEBUG_PRINTLN("PWRMGT: LPCOMP wake configured (AIN%d, ref=ARef)", ain_channel);
  } else if (refsel <= 6) {
    MESH_DEBUG_PRINTLN("PWRMGT: LPCOMP wake configured (AIN%d, ref=%d/8 VDD)",
      ain_channel, refsel + 1);
  } else {
    uint8_t ref_num = (uint8_t)((refsel - 8) * 2 + 1);
    MESH_DEBUG_PRINTLN("PWRMGT: LPCOMP wake configured (AIN%d, ref=%d/16 VDD)",
      ain_channel, ref_num);
  }

  // Configure VBUS (USB power) wake alongside LPCOMP
  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) {
    sd_power_usbdetected_enable(1);
  } else {
    NRF_POWER->EVENTS_USBDETECTED = 0;
    NRF_POWER->INTENSET = POWER_INTENSET_USBDETECTED_Msk;
  }

  MESH_DEBUG_PRINTLN("PWRMGT: VBUS wake configured");
}
#endif

void NRF52BoardDCDC::begin() {
  NRF52Board::begin();

  // Enable DC/DC converter for improved power efficiency
  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) {
    sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
  } else {
    NRF_POWER->DCDCEN = 1;
  }
}

// Temperature from NRF52 MCU
float NRF52Board::getMCUTemperature() {
  NRF_TEMP->TASKS_START = 1; // Start temperature measurement

  long startTime = millis();  
  while (NRF_TEMP->EVENTS_DATARDY == 0) { // Wait for completion. Should complete in 50us
    if(millis() - startTime > 5) {  // To wait 5ms just in case
      NRF_TEMP->TASKS_STOP = 1;
      return NAN;
    }
  }
  
  NRF_TEMP->EVENTS_DATARDY = 0; // Clear event flag

  int32_t temp = NRF_TEMP->TEMP; // In 0.25 *C units
  NRF_TEMP->TASKS_STOP = 1;

  return temp * 0.25f; // Convert to *C
}

bool NRF52Board::startOTAUpdate(const char *id, char reply[]) {
  // Config the peripheral connection with maximum bandwidth
  // more SRAM required by SoftDevice
  // Note: All config***() function must be called before begin()
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.configPrphConn(92, BLE_GAP_EVENT_LENGTH_MIN, 16, 16);

  Bluefruit.begin(1, 0);
  // Set max power. Accepted values are: -40, -30, -20, -16, -12, -8, -4, 0, 4
  Bluefruit.setTxPower(4);
  // Set the BLE device name
  Bluefruit.setName(ota_name);

  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  // To be consistent OTA DFU should be added first if it exists
  bledfu.begin();

  // Set up and start advertising
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();

  /* Start Advertising
    - Enable auto advertising if disconnected
    - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
    - Timeout for fast mode is 30 seconds
    - Start(timeout) with timeout = 0 will advertise forever (until connected)

    For recommended advertising interval
    https://developer.apple.com/library/content/qa/qa1931/_index.html
  */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244); // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);   // number of seconds in fast mode
  Bluefruit.Advertising.start(0);             // 0 = Don't stop advertising after n seconds

  uint8_t mac_addr[6];
  memset(mac_addr, 0, sizeof(mac_addr));
  Bluefruit.getAddr(mac_addr);
  sprintf(reply, "OK - mac: %02X:%02X:%02X:%02X:%02X:%02X", mac_addr[5], mac_addr[4], mac_addr[3],
          mac_addr[2], mac_addr[1], mac_addr[0]);

  return true;
}
#endif
