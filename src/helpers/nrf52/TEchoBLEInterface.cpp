#include "TEchoBLEInterface.h"

void TEchoBLEInterface::begin(const char* device_name, uint32_t pin_code) {
  // Call parent implementation first
  SerialBLEInterface::begin(device_name, pin_code);
  
  // T-Echo specific BLE optimizations
  
  // Enhanced connection parameters for T-Echo wearable with e-paper display
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  
  // SoftDevice v6 specific configuration - optimized for wearable usage
  Bluefruit.configPrphConn(
    247,                                 // MTU max
    TECHO_CONNECTION_EVENT_LENGTH,       // Optimized event length for wearable
    24,                                  // Larger HVN queue for wearable data bursts
    24                                   // Larger write queue for notification bursts
  );
  
  // T-Echo has decent antenna, use moderate TX power to start
  Bluefruit.setTxPower(4);  // Moderate power for wearable device
  
  // Enhanced security for wearable device
  Bluefruit.Security.setIOCaps(true, false, false); // Display only, no yes/no, no keyboard
  Bluefruit.Security.setMITM(true);
  
  // Set device appearance as wearable computer/watch
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_GENERIC_WATCH);
  
  BLE_DEBUG_PRINTLN("T-Echo BLE interface initialized with wearable and e-paper optimizations");
}

void TEchoBLEInterface::checkBatteryStatus() {
  unsigned long now = millis();
  if (now - last_battery_check < 30000) return; // Check every 30 seconds
  
  last_battery_check = now;
  
  bool was_low_power = low_power_mode;
  
  if (battery_voltage_mv <= TECHO_CRITICAL_BATTERY_THRESHOLD) {
    low_power_mode = true;
    BLE_DEBUG_PRINTLN("T-Echo: Critical battery mode activated");
  } else if (battery_voltage_mv <= TECHO_LOW_BATTERY_THRESHOLD) {
    low_power_mode = true;
    BLE_DEBUG_PRINTLN("T-Echo: Low battery mode activated");
  } else {
    low_power_mode = false;
  }
  
  // If power mode changed, update connection parameters
  if (was_low_power != low_power_mode) {
    adjustConnectionParametersForBattery();
  }
}

void TEchoBLEInterface::adjustConnectionParametersForBattery() {
  if (!_isDeviceConnected) return;
  
  uint16_t conn_handle = Bluefruit.connHandle();
  if (conn_handle == BLE_CONN_HANDLE_INVALID) return;
  
  ble_gap_conn_params_t conn_params;
  
  if (battery_voltage_mv <= TECHO_CRITICAL_BATTERY_THRESHOLD) {
    // Critical battery: maximum power saving for wearable - Apple formulaic compliance
    conn_params.min_conn_interval = TECHO_CRITICAL_BATTERY_MIN_INTERVAL;
    conn_params.max_conn_interval = TECHO_CRITICAL_BATTERY_MAX_INTERVAL;
    conn_params.slave_latency = 4;  // Controlled latency for wearable while maintaining iOS compliance
    conn_params.conn_sup_timeout = TECHO_CRITICAL_BATTERY_SUP_TIMEOUT; // Apple compliant timeout
    BLE_DEBUG_PRINTLN("T-Echo: Applied critical battery connection parameters (iOS compliant)");
  } else if (battery_voltage_mv <= TECHO_LOW_BATTERY_THRESHOLD) {
    // Low battery: moderate power saving for wearable - Apple formulaic compliance
    conn_params.min_conn_interval = TECHO_LOW_BATTERY_MIN_INTERVAL;
    conn_params.max_conn_interval = TECHO_LOW_BATTERY_MAX_INTERVAL;
    conn_params.slave_latency = 3;  // Moderate latency for power saving
    conn_params.conn_sup_timeout = TECHO_LOW_BATTERY_SUP_TIMEOUT;  // Apple compliant timeout
    BLE_DEBUG_PRINTLN("T-Echo: Applied low battery connection parameters (iOS compliant)");
  } else {
    // Good battery: wearable-optimized performance - Apple formulaic compliance
    conn_params.min_conn_interval = TECHO_HIGH_BATTERY_MIN_INTERVAL;
    conn_params.max_conn_interval = TECHO_HIGH_BATTERY_MAX_INTERVAL;
    conn_params.slave_latency = epaper_refreshing ? TECHO_WEARABLE_LATENCY_TOLERANCE : 0;
    conn_params.conn_sup_timeout = TECHO_HIGH_BATTERY_SUP_TIMEOUT;  // Apple compliant timeout
    BLE_DEBUG_PRINTLN("T-Echo: Applied high battery connection parameters (iOS compliant)");
  }
  
  if (sd_ble_gap_conn_param_update(conn_handle, &conn_params) == NRF_SUCCESS) {
    BLE_DEBUG_PRINTLN("T-Echo: Connection parameters updated for battery level");
  } else {
    BLE_DEBUG_PRINTLN("T-Echo: Failed to update connection parameters");
  }
}

void TEchoBLEInterface::optimizePowerConsumption() {
  unsigned long now = millis();
  if (now - last_power_optimization < 60000) return; // Optimize every minute
  
  last_power_optimization = now;
  
  if (low_power_mode && _isDeviceConnected) {
    // In low power mode, reduce TX power for wearable device
    if (battery_voltage_mv <= TECHO_CRITICAL_BATTERY_THRESHOLD) {
      Bluefruit.setTxPower(-8);  // Minimum power for emergency wearable operation
    } else {
      Bluefruit.setTxPower(-4);  // Reduced power for low battery wearable
    }
  } else if (!low_power_mode) {
    // Restore moderate power when battery is good
    // Wearable devices benefit from consistent moderate power rather than maximum
    Bluefruit.setTxPower(epaper_refreshing ? 0 : 4);
  }
}

void TEchoBLEInterface::handleEPaperCoordination() {
  // T-Echo e-paper display has very different characteristics from OLED
  // E-paper refresh takes 2+ seconds but happens infrequently
  // Need to coordinate BLE during these long refresh cycles
  unsigned long now = millis();
  if (now - last_epaper_coordination < TECHO_EPAPER_BLE_COORDINATION_TIME) return;
  
  last_epaper_coordination = now;
  
  if (epaper_refreshing && _isDeviceConnected) {
    // E-paper is refreshing, use conservative BLE timing
    // E-paper refresh can cause power supply fluctuations
    static bool epaper_coordination_applied = false;
    if (!epaper_coordination_applied) {
      // Temporarily increase connection intervals during e-paper refresh
      adjustConnectionParametersForBattery();
      epaper_coordination_applied = true;
      BLE_DEBUG_PRINTLN("T-Echo: E-paper refresh coordination applied");
    }
  } else if (!epaper_refreshing) {
    // E-paper is idle, can use normal BLE parameters
    static bool epaper_coordination_applied = true;
    if (epaper_coordination_applied) {
      adjustConnectionParametersForBattery();
      epaper_coordination_applied = false;
      BLE_DEBUG_PRINTLN("T-Echo: E-paper refresh coordination removed");
    }
  }
}

void TEchoBLEInterface::handleGPSInterference() {
  // T-Echo GPS can interfere with BLE on 2.4GHz
  // Use time-division to avoid interference
  static unsigned long last_gps_operation = 0;
  unsigned long now = millis();
  
  if (gps_interference_detected) {
    // If GPS is active, coordinate BLE operations
    if (now - last_gps_operation < TECHO_GPS_BLE_SEPARATION_TIME) {
      // Brief delay to avoid GPS/BLE interference
      // Shorter delay than other devices since T-Echo is wearable and needs responsiveness
      delay(3);
    }
  }
}

size_t TEchoBLEInterface::checkRecvFrame(uint8_t dest[]) {
  // T-Echo specific power management and coordination
  checkBatteryStatus();
  optimizePowerConsumption();
  handleEPaperCoordination();
  handleGPSInterference();
  
  // Call parent implementation
  return SerialBLEInterface::checkRecvFrame(dest);
}

void TEchoBLEInterface::setBatteryVoltage(uint16_t voltage_mv) {
  battery_voltage_mv = voltage_mv;
  BLE_DEBUG_PRINTLN("T-Echo: Battery voltage updated to %u mV", voltage_mv);
}

void TEchoBLEInterface::setEPaperRefreshing(bool refreshing) {
  if (epaper_refreshing != refreshing) {
    epaper_refreshing = refreshing;
    BLE_DEBUG_PRINTLN("T-Echo: E-paper refresh coordination mode %s", refreshing ? "ON" : "OFF");
    
    if (refreshing) {
      // E-paper refresh starting, prepare BLE for power supply fluctuations
      if (_isDeviceConnected) {
        adjustConnectionParametersForBattery();
      }
    } else {
      // E-paper refresh complete, can return to normal BLE operation
      if (_isDeviceConnected) {
        adjustConnectionParametersForBattery();
      }
    }
  }
}

void TEchoBLEInterface::setGPSActive(bool active) {
  if (gps_interference_detected != active) {
    gps_interference_detected = active;
    BLE_DEBUG_PRINTLN("T-Echo: GPS interference mode %s", active ? "ON" : "OFF");
    
    if (active) {
      // GPS is active, use more conservative BLE timing
      if (_isDeviceConnected) {
        adjustConnectionParametersForBattery();
      }
    }
  }
}
