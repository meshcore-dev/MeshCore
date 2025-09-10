#pragma once

#include "SerialBLEInterface.h"

// T1000-E specific BLE optimizations for iOS stability
class T1000eBLEInterface : public SerialBLEInterface {
  unsigned long last_battery_check;
  unsigned long last_power_optimization;
  bool low_power_mode;
  bool gps_interference_detected;
  uint16_t battery_voltage_mv;
  
  // T1000-E specific power management
  void checkBatteryStatus();
  void optimizePowerConsumption();
  void handleGPSInterference();
  void adjustConnectionParametersForBattery();
  
public:
  T1000eBLEInterface() : SerialBLEInterface() {
    last_battery_check = 0;
    last_power_optimization = 0;
    low_power_mode = false;
    gps_interference_detected = false;
    battery_voltage_mv = 4000; // Default to good battery
  }
  
  // Override begin to add T1000-E specific settings
  void begin(const char* device_name, uint32_t pin_code) override;
  
  // Override checkRecvFrame to add power management
  size_t checkRecvFrame(uint8_t dest[]) override;
  
  // T1000-E specific methods
  void setBatteryVoltage(uint16_t voltage_mv);
  void setGPSActive(bool active);
  bool isLowPowerMode() const { return low_power_mode; }
};

// T1000-E optimized connection parameters for wearable usage patterns (Apple formulaic compliance)
#define T1000E_HIGH_BATTERY_MIN_INTERVAL    24   // 30ms - Apple's minimum multiple of 15ms
#define T1000E_HIGH_BATTERY_MAX_INTERVAL    40   // 50ms - satisfies Min + 15ms ≤ Max
#define T1000E_LOW_BATTERY_MIN_INTERVAL     48   // 60ms - wearable power saving (multiple of 15ms)
#define T1000E_LOW_BATTERY_MAX_INTERVAL     80   // 100ms - satisfies Min + 15ms ≤ Max  
#define T1000E_CRITICAL_BATTERY_MIN_INTERVAL 120 // 150ms - wearable emergency mode (multiple of 15ms)
#define T1000E_CRITICAL_BATTERY_MAX_INTERVAL 200 // 250ms - still responsive for wearable

// Battery thresholds for T1000-E wearable
#define T1000E_LOW_BATTERY_THRESHOLD        3600  // 3.6V
#define T1000E_CRITICAL_BATTERY_THRESHOLD   3400  // 3.4V

// GPS interference mitigation for wearable
#define T1000E_GPS_BLE_SEPARATION_TIME      800   // 800ms between GPS and BLE for wearable responsiveness
#define T1000E_WEARABLE_LATENCY_TOLERANCE   2     // Allow some latency for power saving in wearable mode

// iOS supervision timeouts for different battery states (Apple formulaic compliance)
#define T1000E_HIGH_BATTERY_SUP_TIMEOUT     400   // 4 seconds (satisfies 2s ≤ timeout ≤ 6s)
#define T1000E_LOW_BATTERY_SUP_TIMEOUT      500   // 5 seconds 
#define T1000E_CRITICAL_BATTERY_SUP_TIMEOUT 600   // 6 seconds (maximum allowed)
