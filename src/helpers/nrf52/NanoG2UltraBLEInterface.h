#pragma once

#include "SerialBLEInterface.h"

// Nano G2 Ultra specific BLE optimizations for iOS stability
class NanoG2UltraBLEInterface : public SerialBLEInterface {
  unsigned long last_battery_check;
  unsigned long last_power_optimization;
  unsigned long last_display_coordination;
  bool low_power_mode;
  bool display_active;
  bool gps_interference_detected;
  uint16_t battery_voltage_mv;
  
  // Nano G2 Ultra specific power management
  void checkBatteryStatus();
  void optimizePowerConsumption();
  void handleDisplayCoordination();
  void handleGPSInterference();
  void adjustConnectionParametersForBattery();
  
public:
  NanoG2UltraBLEInterface() : SerialBLEInterface() {
    last_battery_check = 0;
    last_power_optimization = 0;
    last_display_coordination = 0;
    low_power_mode = false;
    display_active = false;
    gps_interference_detected = false;
    battery_voltage_mv = 4000; // Default to good battery
  }
  
  // Override begin to add Nano G2 Ultra specific settings
  void begin(const char* device_name, uint32_t pin_code) override;
  
  // Override checkRecvFrame to add power and display coordination
  size_t checkRecvFrame(uint8_t dest[]) override;
  
  // Nano G2 Ultra specific methods
  void setBatteryVoltage(uint16_t voltage_mv);
  void setDisplayActive(bool active);
  void setGPSActive(bool active);
  bool isLowPowerMode() const { return low_power_mode; }
};

// Nano G2 Ultra optimized connection parameters for different battery levels (Apple formulaic compliance)
#define NANO_G2_HIGH_BATTERY_MIN_INTERVAL    24   // 30ms - Apple's minimum multiple of 15ms
#define NANO_G2_HIGH_BATTERY_MAX_INTERVAL    40   // 50ms - satisfies Min + 15ms ≤ Max
#define NANO_G2_LOW_BATTERY_MIN_INTERVAL     48   // 60ms - power saving (multiple of 15ms)
#define NANO_G2_LOW_BATTERY_MAX_INTERVAL     80   // 100ms - satisfies Min + 15ms ≤ Max
#define NANO_G2_CRITICAL_BATTERY_MIN_INTERVAL 120 // 150ms - display off mode (multiple of 15ms)
#define NANO_G2_CRITICAL_BATTERY_MAX_INTERVAL 200 // 250ms - satisfies Min + 15ms ≤ Max

// Battery thresholds for Nano G2 Ultra (similar to T1000-E but adjusted for display power consumption)
#define NANO_G2_LOW_BATTERY_THRESHOLD        3700  // 3.7V (higher due to display)
#define NANO_G2_CRITICAL_BATTERY_THRESHOLD   3500  // 3.5V (higher due to display)

// iOS supervision timeouts for different battery states (Apple formulaic compliance)
#define NANO_G2_HIGH_BATTERY_SUP_TIMEOUT     400   // 4 seconds (satisfies 2s ≤ timeout ≤ 6s)
#define NANO_G2_LOW_BATTERY_SUP_TIMEOUT      500   // 5 seconds 
#define NANO_G2_CRITICAL_BATTERY_SUP_TIMEOUT 600   // 6 seconds (maximum allowed)

// Display coordination parameters
#define NANO_G2_DISPLAY_BLE_COORDINATION_TIME 200   // 200ms coordination window
#define NANO_G2_GPS_BLE_SEPARATION_TIME      1000   // 1 second between GPS and BLE operations

// SoftDevice v6 specific parameters (older version has different capabilities)
#define NANO_G2_MAX_MTU_SIZE                 247    // Same as v7 but may negotiate differently
#define NANO_G2_CONNECTION_EVENT_LENGTH      7500   // Microseconds, conservative for v6
