#pragma once

#include "SerialBLEInterface.h"

// T-Echo specific BLE optimizations for iOS stability
class TEchoBLEInterface : public SerialBLEInterface {
  unsigned long last_battery_check;
  unsigned long last_power_optimization;
  unsigned long last_epaper_coordination;
  bool low_power_mode;
  bool epaper_refreshing;
  bool gps_interference_detected;
  uint16_t battery_voltage_mv;
  
  // T-Echo specific power management
  void checkBatteryStatus();
  void optimizePowerConsumption();
  void handleEPaperCoordination();
  void handleGPSInterference();
  void adjustConnectionParametersForBattery();
  
public:
  TEchoBLEInterface() : SerialBLEInterface() {
    last_battery_check = 0;
    last_power_optimization = 0;
    last_epaper_coordination = 0;
    low_power_mode = false;
    epaper_refreshing = false;
    gps_interference_detected = false;
    battery_voltage_mv = 4000; // Default to good battery
  }
  
  // Override begin to add T-Echo specific settings
  void begin(const char* device_name, uint32_t pin_code) override;
  
  // Override checkRecvFrame to add power and e-paper coordination
  size_t checkRecvFrame(uint8_t dest[]) override;
  
  // T-Echo specific methods
  void setBatteryVoltage(uint16_t voltage_mv);
  void setEPaperRefreshing(bool refreshing);
  void setGPSActive(bool active);
  bool isLowPowerMode() const { return low_power_mode; }
};

// T-Echo optimized connection parameters for different battery levels (Apple formulaic compliance)
#define TECHO_HIGH_BATTERY_MIN_INTERVAL    24   // 30ms - Apple's minimum multiple of 15ms
#define TECHO_HIGH_BATTERY_MAX_INTERVAL    40   // 50ms - satisfies Min + 15ms ≤ Max
#define TECHO_LOW_BATTERY_MIN_INTERVAL     72   // 90ms - e-paper power saving (multiple of 15ms)
#define TECHO_LOW_BATTERY_MAX_INTERVAL     120  // 150ms - satisfies Min + 15ms ≤ Max
#define TECHO_CRITICAL_BATTERY_MIN_INTERVAL 160 // 200ms - maximum e-paper power saving (multiple of 15ms)
#define TECHO_CRITICAL_BATTERY_MAX_INTERVAL 240 // 300ms - satisfies Min + 15ms ≤ Max

// Battery thresholds for T-Echo (optimized for e-paper display power characteristics)
#define TECHO_LOW_BATTERY_THRESHOLD        3600  // 3.6V (e-paper uses very little power)
#define TECHO_CRITICAL_BATTERY_THRESHOLD   3400  // 3.4V (e-paper can run at lower voltages)

// iOS supervision timeouts for different battery states (Apple formulaic compliance)
#define TECHO_HIGH_BATTERY_SUP_TIMEOUT     400   // 4 seconds (satisfies 2s ≤ timeout ≤ 6s)
#define TECHO_LOW_BATTERY_SUP_TIMEOUT      500   // 5 seconds 
#define TECHO_CRITICAL_BATTERY_SUP_TIMEOUT 600   // 6 seconds (maximum allowed)

// E-paper coordination parameters (different from OLED)
#define TECHO_EPAPER_BLE_COORDINATION_TIME 2000  // 2 second coordination window for e-paper refresh
#define TECHO_GPS_BLE_SEPARATION_TIME      1000  // 1 second between GPS and BLE operations

// SoftDevice v6 specific parameters for T-Echo wearable form factor
#define TECHO_MAX_MTU_SIZE                 247   // Same as other devices but may negotiate differently
#define TECHO_CONNECTION_EVENT_LENGTH      6250  // Microseconds, optimized for wearable usage
#define TECHO_WEARABLE_LATENCY_TOLERANCE   2     // Allow some latency for power saving in wearable mode
