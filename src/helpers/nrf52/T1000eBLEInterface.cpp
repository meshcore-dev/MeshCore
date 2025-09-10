#include "T1000eBLEInterface.h"

void T1000eBLEInterface::begin(const char* device_name, uint32_t pin_code) {
  // Call parent implementation first
  SerialBLEInterface::begin(device_name, pin_code);
  
  // T1000-E specific BLE optimizations
  
  // Enhanced connection parameters for T1000-E GPS tracker
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  
  // More conservative connection parameters to reduce power consumption
  // and avoid GPS interference
  Bluefruit.configPrphConn(
    247,                                 // MTU max
    BLE_GAP_EVENT_LENGTH_MIN,
    32,                                  // Larger HVN queue for tracker data
    32                                   // Larger write queue for location updates
  );
  
  // T1000-E has better antenna design, can use higher TX power safely
  Bluefruit.setTxPower(8);  // Maximum power for better range
  
  // Enhanced security for wearable tracker device
  Bluefruit.Security.setIOCaps(true, false, false); // Display only, no yes/no, no keyboard
  Bluefruit.Security.setMITM(true);
  
  // Set device appearance as wearable computer for better iOS recognition
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_GENERIC_COMPUTER);
  
  BLE_DEBUG_PRINTLN("T1000-E BLE interface initialized with tracker optimizations");
}

void T1000eBLEInterface::checkBatteryStatus() {
  unsigned long now = millis();
  if (now - last_battery_check < 30000) return; // Check every 30 seconds
  
  last_battery_check = now;
  
  bool was_low_power = low_power_mode;
  
  if (battery_voltage_mv <= T1000E_CRITICAL_BATTERY_THRESHOLD) {
    low_power_mode = true;
    BLE_DEBUG_PRINTLN("T1000-E: Critical battery mode activated");
  } else if (battery_voltage_mv <= T1000E_LOW_BATTERY_THRESHOLD) {
    low_power_mode = true;
    BLE_DEBUG_PRINTLN("T1000-E: Low battery mode activated");
  } else {
    low_power_mode = false;
  }
  
  // If power mode changed, update connection parameters
  if (was_low_power != low_power_mode) {
    adjustConnectionParametersForBattery();
  }
}

void T1000eBLEInterface::adjustConnectionParametersForBattery() {
  if (!_isDeviceConnected) return;
  
  uint16_t conn_handle = Bluefruit.connHandle();
  if (conn_handle == BLE_CONN_HANDLE_INVALID) return;
  
  ble_gap_conn_params_t conn_params;
  
  if (battery_voltage_mv <= T1000E_CRITICAL_BATTERY_THRESHOLD) {
    // Critical battery: wearable emergency mode - Apple formulaic compliance
    conn_params.min_conn_interval = T1000E_CRITICAL_BATTERY_MIN_INTERVAL;
    conn_params.max_conn_interval = T1000E_CRITICAL_BATTERY_MAX_INTERVAL;
    conn_params.slave_latency = T1000E_WEARABLE_LATENCY_TOLERANCE;  // Controlled latency for wearable
    conn_params.conn_sup_timeout = T1000E_CRITICAL_BATTERY_SUP_TIMEOUT; // Apple compliant timeout
    BLE_DEBUG_PRINTLN("T1000-E: Applied critical battery wearable connection parameters (iOS compliant)");
  } else if (battery_voltage_mv <= T1000E_LOW_BATTERY_THRESHOLD) {
    // Low battery: moderate power saving for wearable - Apple formulaic compliance
    conn_params.min_conn_interval = T1000E_LOW_BATTERY_MIN_INTERVAL;
    conn_params.max_conn_interval = T1000E_LOW_BATTERY_MAX_INTERVAL;
    conn_params.slave_latency = T1000E_WEARABLE_LATENCY_TOLERANCE;  // Wearable-appropriate latency
    conn_params.conn_sup_timeout = T1000E_LOW_BATTERY_SUP_TIMEOUT;  // Apple compliant timeout
    BLE_DEBUG_PRINTLN("T1000-E: Applied low battery wearable connection parameters (iOS compliant)");
  } else {
    // Good battery: wearable performance optimized - Apple formulaic compliance
    conn_params.min_conn_interval = T1000E_HIGH_BATTERY_MIN_INTERVAL;
    conn_params.max_conn_interval = T1000E_HIGH_BATTERY_MAX_INTERVAL;
    conn_params.slave_latency = gps_interference_detected ? 1 : 0;  // Minimal latency during GPS for wearable
    conn_params.conn_sup_timeout = T1000E_HIGH_BATTERY_SUP_TIMEOUT;  // Apple compliant timeout
    BLE_DEBUG_PRINTLN("T1000-E: Applied high battery wearable connection parameters (iOS compliant)");
  }
  
  if (sd_ble_gap_conn_param_update(conn_handle, &conn_params) == NRF_SUCCESS) {
    BLE_DEBUG_PRINTLN("T1000-E: Wearable connection parameters updated for battery level");
  } else {
    BLE_DEBUG_PRINTLN("T1000-E: Failed to update wearable connection parameters");
  }
}

void T1000eBLEInterface::optimizePowerConsumption() {
  unsigned long now = millis();
  if (now - last_power_optimization < 60000) return; // Optimize every minute
  
  last_power_optimization = now;
  
  if (low_power_mode && _isDeviceConnected) {
    // In low power mode, reduce advertising power when connected
    // This helps extend battery life significantly
    if (battery_voltage_mv <= T1000E_CRITICAL_BATTERY_THRESHOLD) {
      Bluefruit.setTxPower(-8);  // Reduce power significantly
    } else {
      Bluefruit.setTxPower(0);   // Moderate power reduction
    }
  } else if (!low_power_mode) {
    // Restore full power when battery is good
    Bluefruit.setTxPower(8);
  }
}

void T1000eBLEInterface::handleGPSInterference() {
  // T1000-E GPS can interfere with BLE on 2.4GHz
  // Implement time-division to avoid interference (wearable optimized)
  static unsigned long last_gps_operation = 0;
  unsigned long now = millis();
  
  if (gps_interference_detected) {
    // If GPS is active, delay BLE operations slightly
    if (now - last_gps_operation < T1000E_GPS_BLE_SEPARATION_TIME) {
      // Shorter delay for wearable responsiveness (vs fixed tracker)
      delay(5);
    }
  }
}

size_t T1000eBLEInterface::checkRecvFrame(uint8_t dest[]) {
  // T1000-E specific power management and interference handling
  checkBatteryStatus();
  optimizePowerConsumption();
  handleGPSInterference();
  
  // Call parent implementation
  return SerialBLEInterface::checkRecvFrame(dest);
}

void T1000eBLEInterface::setBatteryVoltage(uint16_t voltage_mv) {
  battery_voltage_mv = voltage_mv;
  BLE_DEBUG_PRINTLN("T1000-E: Battery voltage updated to %u mV", voltage_mv);
}

void T1000eBLEInterface::setGPSActive(bool active) {
  if (gps_interference_detected != active) {
    gps_interference_detected = active;
    BLE_DEBUG_PRINTLN("T1000-E: GPS interference mode %s", active ? "ON" : "OFF");
    
    if (active) {
      // GPS is active, use more conservative BLE timing
      // Slightly increase connection intervals to avoid interference
      if (_isDeviceConnected) {
        adjustConnectionParametersForBattery();
      }
    }
  }
}
