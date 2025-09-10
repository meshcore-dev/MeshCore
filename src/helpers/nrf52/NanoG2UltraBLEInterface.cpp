#include "NanoG2UltraBLEInterface.h"

void NanoG2UltraBLEInterface::begin(const char* device_name, uint32_t pin_code) {
  // Call parent implementation first
  SerialBLEInterface::begin(device_name, pin_code);
  
  // Nano G2 Ultra specific BLE optimizations
  
  // Enhanced connection parameters for Nano G2 Ultra with display
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  
  // SoftDevice v6 specific configuration - more conservative than v7
  Bluefruit.configPrphConn(
    247,                                 // MTU max
    NANO_G2_CONNECTION_EVENT_LENGTH,     // Conservative event length for v6
    16,                                  // HVN queue size
    16                                   // Write queue size
  );
  
  // Nano G2 Ultra has good antenna design, can use higher TX power
  Bluefruit.setTxPower(4);  // Start with moderate power (can be adjusted based on battery)
  
  // Enhanced security for handheld device with display
  Bluefruit.Security.setIOCaps(true, false, false); // Display only, no yes/no, no keyboard
  Bluefruit.Security.setMITM(true);
  
  // Set device appearance as handheld scanner/computer
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_GENERIC_COMPUTER);
  
  BLE_DEBUG_PRINTLN("Nano G2 Ultra BLE interface initialized with display coordination");
}

void NanoG2UltraBLEInterface::checkBatteryStatus() {
  unsigned long now = millis();
  if (now - last_battery_check < 30000) return; // Check every 30 seconds
  
  last_battery_check = now;
  
  bool was_low_power = low_power_mode;
  
  if (battery_voltage_mv <= NANO_G2_CRITICAL_BATTERY_THRESHOLD) {
    low_power_mode = true;
    BLE_DEBUG_PRINTLN("Nano G2 Ultra: Critical battery mode activated");
  } else if (battery_voltage_mv <= NANO_G2_LOW_BATTERY_THRESHOLD) {
    low_power_mode = true;
    BLE_DEBUG_PRINTLN("Nano G2 Ultra: Low battery mode activated");
  } else {
    low_power_mode = false;
  }
  
  // If power mode changed, update connection parameters
  if (was_low_power != low_power_mode) {
    adjustConnectionParametersForBattery();
  }
}

void NanoG2UltraBLEInterface::adjustConnectionParametersForBattery() {
  if (!_isDeviceConnected) return;
  
  uint16_t conn_handle = Bluefruit.connHandle();
  if (conn_handle == BLE_CONN_HANDLE_INVALID) return;
  
  ble_gap_conn_params_t conn_params;
  
  if (battery_voltage_mv <= NANO_G2_CRITICAL_BATTERY_THRESHOLD) {
    // Critical battery: maximum power saving, display should be off - Apple formulaic compliance
    conn_params.min_conn_interval = NANO_G2_CRITICAL_BATTERY_MIN_INTERVAL;
    conn_params.max_conn_interval = NANO_G2_CRITICAL_BATTERY_MAX_INTERVAL;
    conn_params.slave_latency = 4;  // Controlled latency to save power while maintaining iOS compliance
    conn_params.conn_sup_timeout = NANO_G2_CRITICAL_BATTERY_SUP_TIMEOUT; // Apple compliant timeout
    BLE_DEBUG_PRINTLN("Nano G2 Ultra: Applied critical battery connection parameters (iOS compliant)");
  } else if (battery_voltage_mv <= NANO_G2_LOW_BATTERY_THRESHOLD) {
    // Low battery: moderate power saving - Apple formulaic compliance
    conn_params.min_conn_interval = NANO_G2_LOW_BATTERY_MIN_INTERVAL;
    conn_params.max_conn_interval = NANO_G2_LOW_BATTERY_MAX_INTERVAL;
    conn_params.slave_latency = 3;  // Some latency for power saving
    conn_params.conn_sup_timeout = NANO_G2_LOW_BATTERY_SUP_TIMEOUT;  // Apple compliant timeout
    BLE_DEBUG_PRINTLN("Nano G2 Ultra: Applied low battery connection parameters (iOS compliant)");
  } else {
    // Good battery: normal performance - Apple formulaic compliance
    conn_params.min_conn_interval = NANO_G2_HIGH_BATTERY_MIN_INTERVAL;
    conn_params.max_conn_interval = NANO_G2_HIGH_BATTERY_MAX_INTERVAL;
    conn_params.slave_latency = display_active ? 1 : 0;  // Slight latency if display active
    conn_params.conn_sup_timeout = NANO_G2_HIGH_BATTERY_SUP_TIMEOUT;  // Apple compliant timeout
    BLE_DEBUG_PRINTLN("Nano G2 Ultra: Applied high battery connection parameters (iOS compliant)");
  }
  
  if (sd_ble_gap_conn_param_update(conn_handle, &conn_params) == NRF_SUCCESS) {
    BLE_DEBUG_PRINTLN("Nano G2 Ultra: Connection parameters updated for battery level");
  } else {
    BLE_DEBUG_PRINTLN("Nano G2 Ultra: Failed to update connection parameters");
  }
}

void NanoG2UltraBLEInterface::optimizePowerConsumption() {
  unsigned long now = millis();
  if (now - last_power_optimization < 60000) return; // Optimize every minute
  
  last_power_optimization = now;
  
  if (low_power_mode && _isDeviceConnected) {
    // In low power mode, reduce TX power significantly
    if (battery_voltage_mv <= NANO_G2_CRITICAL_BATTERY_THRESHOLD) {
      Bluefruit.setTxPower(-8);  // Minimum power for emergency operation
    } else {
      Bluefruit.setTxPower(-4);  // Reduced power for low battery
    }
  } else if (!low_power_mode) {
    // Restore moderate power when battery is good
    // Don't use maximum power if display is active (to reduce interference)
    Bluefruit.setTxPower(display_active ? 0 : 4);
  }
}

void NanoG2UltraBLEInterface::handleDisplayCoordination() {
  // Nano G2 Ultra OLED display can cause electrical interference with BLE
  // Coordinate timing to minimize interference
  unsigned long now = millis();
  if (now - last_display_coordination < NANO_G2_DISPLAY_BLE_COORDINATION_TIME) return;
  
  last_display_coordination = now;
  
  if (display_active && _isDeviceConnected) {
    // Display is active, use more conservative BLE timing
    // This helps prevent display refresh from interfering with BLE packets
    static bool coordination_applied = false;
    if (!coordination_applied) {
      // Slightly increase minimum connection interval when display is active
      adjustConnectionParametersForBattery();
      coordination_applied = true;
    }
  }
}

void NanoG2UltraBLEInterface::handleGPSInterference() {
  // Nano G2 Ultra GPS can interfere with BLE on 2.4GHz
  // Use time-division to avoid interference
  static unsigned long last_gps_operation = 0;
  unsigned long now = millis();
  
  if (gps_interference_detected) {
    // If GPS is active, coordinate BLE operations
    if (now - last_gps_operation < NANO_G2_GPS_BLE_SEPARATION_TIME) {
      // Brief delay to avoid GPS/BLE interference
      delay(5);  // Minimal delay for handheld responsiveness
    }
  }
}

size_t NanoG2UltraBLEInterface::checkRecvFrame(uint8_t dest[]) {
  // Nano G2 Ultra specific power management and coordination
  checkBatteryStatus();
  optimizePowerConsumption();
  handleDisplayCoordination();
  handleGPSInterference();
  
  // Call parent implementation
  return SerialBLEInterface::checkRecvFrame(dest);
}

void NanoG2UltraBLEInterface::setBatteryVoltage(uint16_t voltage_mv) {
  battery_voltage_mv = voltage_mv;
  BLE_DEBUG_PRINTLN("Nano G2 Ultra: Battery voltage updated to %u mV", voltage_mv);
}

void NanoG2UltraBLEInterface::setDisplayActive(bool active) {
  if (display_active != active) {
    display_active = active;
    BLE_DEBUG_PRINTLN("Nano G2 Ultra: Display coordination mode %s", active ? "ON" : "OFF");
    
    if (active) {
      // Display is now active, adjust BLE parameters for coordination
      if (_isDeviceConnected) {
        adjustConnectionParametersForBattery();
      }
    }
  }
}

void NanoG2UltraBLEInterface::setGPSActive(bool active) {
  if (gps_interference_detected != active) {
    gps_interference_detected = active;
    BLE_DEBUG_PRINTLN("Nano G2 Ultra: GPS interference mode %s", active ? "ON" : "OFF");
    
    if (active) {
      // GPS is active, use more conservative BLE timing
      if (_isDeviceConnected) {
        adjustConnectionParametersForBattery();
      }
    }
  }
}
