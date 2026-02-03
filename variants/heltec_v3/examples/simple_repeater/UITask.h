#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/MomentaryButton.h>
#include <helpers/CommonCLI.h>

class UITask {
  DisplayDriver* _display;
#ifdef HELTEC_V3_SCREEN_LED_CONTROL
  SensorManager* _sensors;
#endif
  unsigned long _next_read, _next_refresh, _auto_off;
  int _prevBtnState;
  NodePrefs* _node_prefs;
  char _version_info[32];
#ifdef PIN_USER_BTN
  MomentaryButton user_btn;
#endif

  void renderCurrScreen();
  void showAlert(const char* msg, int duration_ms);
public:
  UITask(DisplayDriver& display
#ifdef HELTEC_V3_SCREEN_LED_CONTROL
    , SensorManager* sensors
#endif
  ) : _display(&display)
#ifdef HELTEC_V3_SCREEN_LED_CONTROL
    , _sensors(sensors)
#endif
#ifdef PIN_USER_BTN
    , user_btn(PIN_USER_BTN, 1000, true)
#endif
  { _next_read = _next_refresh = 0; }
  void begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version);

  void loop();
};
