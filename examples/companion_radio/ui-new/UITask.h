#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#if defined(T_ECHO_LITE_KEYPAD)
  #include <Wire.h>
  #include "MenuModel.h"
#endif

#ifndef LED_STATE_ON
  #define LED_STATE_ON 1
#endif

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  NodePrefs* _node_prefs;
  char _alert[80];
  unsigned long _alert_expiry;
  int _msgcount;
  unsigned long ui_started_at, next_batt_chck;
  int next_backlight_btn_check = 0;
#if defined(T_ECHO_LITE_KEYPAD)
  bool _keypad_found = false;
  bool _keypad_has_event = false;
  uint8_t _keypad_event = 0;
  uint8_t _keypad_code = 0;
  uint8_t _keypad_row = 0;
  uint8_t _keypad_col = 0;
  bool _diagnostic_is_gpio = false;
  bool _button1_state = true;
  bool _button2_state = true;
  uint8_t _diagnostic_gpio = 0;
  static const uint8_t KEY_QUEUE_SIZE = 16;
  char _key_queue[KEY_QUEUE_SIZE] = {0};
  uint8_t _key_queue_head = 0;
  uint8_t _key_queue_tail = 0;
  MenuModel::LedBehaviour _notification_led_settings[2][7];
  MenuModel::LedPattern _notification_led_patterns[2];

  void queueKey(char key);
  bool dequeueKey(char& key);
  bool keypadWrite(uint8_t reg, uint8_t value);
  uint8_t keypadRead(uint8_t reg);
  void beginKeypadDiagnostic();
  void pollKeypadDiagnostic();
  void pollDiagnosticButtons();
  void renderKeypadDiagnostic();
  void triggerNotificationLeds(uint8_t event);
  void notificationLedHandler();
  void writeNotificationLed(uint8_t led, bool on);
#endif
#ifdef PIN_STATUS_LED
  int led_state = 0;
  int next_led_change = 0;
  int last_led_increment = 0;
#endif

#ifdef PIN_USER_BTN_ANA
  unsigned long _analogue_pin_read_millis = millis();
#endif

  UIScreen* splash;
  UIScreen* home;
  UIScreen* msg_preview;
  UIScreen* curr;

  void userLedHandler();

  // Button action handlers
  char checkDisplayOn(char c);
  char handleLongPress(char c);
  char handleDoubleClick(char c);
  char handleTripleClick(char c);

  void setCurrScreen(UIScreen* c);

public:

  UITask(mesh::MainBoard* board, BaseSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    curr = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void gotoHomeScreen() { setCurrScreen(home); }
  void showAlert(const char* text, int duration_millis);
  int  getMsgCount() const { return _msgcount; }
  bool hasDisplay() const { return _display != NULL; }
  bool isButtonPressed() const;

  bool isBuzzerQuiet() { 
#ifdef PIN_BUZZER
    return buzzer.isQuiet();
#else
    return true;
#endif
  }

  void toggleBuzzer();
  bool getGPSState();
  void toggleGPS();

#if defined(T_ECHO_LITE_KEYPAD)
  void configureNotificationLed(uint8_t led, uint8_t event, MenuModel::LedBehaviour behaviour);
#endif

  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  void shutdown(bool restart = false);
};
