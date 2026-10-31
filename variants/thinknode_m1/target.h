#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <ThinkNodeM1Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#include <m1_leds.h>
#include <m1_gps.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/GxEPDDisplay.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

/* 2026-08-21 评审 序号10：功能键（长按 3s 进入休眠 / 单击唤醒）
   PIN_FUNC_BTN / FUNC_BTN_LONG_MILLIS 定义在 ThinkNodeM1Board.h（板级单一来源），
   依据 V2.0 原理图：功能键 = 触摸键 K3 = BUTTON_TOUCH = P1.07 = Arduino 39。 */

/* 2026-08-21 评审 序号8：低电量指示灯闪烁阈值（mV） */
#ifndef M1_LOW_BATT_MILLIVOLTS
  #define M1_LOW_BATT_MILLIVOLTS 3500
#endif

class ThinkNodeM1SensorManager : public SensorManager {
  bool gps_active = false;
  bool last_gps_switch_state = false;
  bool gps_start_pending = false;   // 开机首次启动延后到主循环（避免波特率扫描拖慢启动）
  LocationProvider* _location;

  void start_gps();
  void stop_gps();
public:
  ThinkNodeM1SensorManager(LocationProvider &location): _location(&location) { }
  LocationProvider* getLocationProvider() override { return _location; }
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  void loop() override;
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};

extern ThinkNodeM1Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern ThinkNodeM1SensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
  #ifdef PIN_FUNC_BTN
    extern MomentaryButton func_btn;
  #endif
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();

