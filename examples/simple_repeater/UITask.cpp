#include "UITask.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>

#define AUTO_OFF_MILLIS      20000  // 20 seconds
#define BOOT_SCREEN_MILLIS   4000   // 4 seconds

// 'meshcore', 128x13px
static const int LOGO_W = 128;
static const int LOGO_H = 13;
static const uint8_t meshcore_logo [] PROGMEM = {
    0x3c, 0x01, 0xe3, 0xff, 0xc7, 0xff, 0x8f, 0x03, 0x87, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 
    0x3c, 0x03, 0xe3, 0xff, 0xc7, 0xff, 0x8e, 0x03, 0x8f, 0xfe, 0x3f, 0xfe, 0x1f, 0xff, 0x1f, 0xfe, 
    0x3e, 0x03, 0xc3, 0xff, 0x8f, 0xff, 0x0e, 0x07, 0x8f, 0xfe, 0x7f, 0xfe, 0x1f, 0xff, 0x1f, 0xfc, 
    0x3e, 0x07, 0xc7, 0x80, 0x0e, 0x00, 0x0e, 0x07, 0x9e, 0x00, 0x78, 0x0e, 0x3c, 0x0f, 0x1c, 0x00, 
    0x3e, 0x0f, 0xc7, 0x80, 0x1e, 0x00, 0x0e, 0x07, 0x1e, 0x00, 0x70, 0x0e, 0x38, 0x0f, 0x3c, 0x00, 
    0x7f, 0x0f, 0xc7, 0xfe, 0x1f, 0xfc, 0x1f, 0xff, 0x1c, 0x00, 0x70, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x1f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x3f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x1e, 0x3f, 0xfe, 0x3f, 0xf0, 
    0x77, 0x3b, 0x87, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xfc, 0x38, 0x00, 
    0x77, 0xfb, 0x8f, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xf8, 0x38, 0x00, 
    0x73, 0xf3, 0x8f, 0xff, 0x0f, 0xff, 0x1c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x78, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfe, 0x3c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x3c, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfc, 0x3c, 0x0e, 0x1f, 0xf8, 0xff, 0xf8, 0x70, 0x3c, 0x7f, 0xf8, 
};

void UITask::updateCachedStrings() {
  if (_node_prefs == nullptr) return;
  
  // Update frequency and spreading factor string if changed
  if (_last_freq != _node_prefs->freq || _last_sf != _node_prefs->sf) {
    snprintf(_line_freq, sizeof(_line_freq), "FREQ: %06.3f SF%d", _node_prefs->freq, _node_prefs->sf);
    _last_freq = _node_prefs->freq;
    _last_sf = _node_prefs->sf;
  }
  
  // Update bandwidth and coding rate string if changed
  if (_last_bw != _node_prefs->bw || _last_cr != _node_prefs->cr) {
    snprintf(_line_bw, sizeof(_line_bw), "BW: %03.2f CR: %d", _node_prefs->bw, _node_prefs->cr);
    _last_bw = _node_prefs->bw;
    _last_cr = _node_prefs->cr;
  }
}

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version) {
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _node_prefs = node_prefs;
  _display->turnOn();
  _press_started = 0;

  // Copy version up to '-' without heap use
  char ver[16] = {0};  // fits "vX.Y.Z" comfortably
  size_t n = 0;
  for (; firmware_version[n] && firmware_version[n] != '-' && n < sizeof(ver)-1; ++n) {
    ver[n] = firmware_version[n];
  }
  ver[n] = '\0';

  // Safe, bounded formatting
  snprintf(_version_info, sizeof(_version_info), "%s (%s)", ver, build_date);
  
  // Cache the version width for centering
  _version_width = _display->getTextWidth(_version_info);
  
  // Initialize cached strings
  updateCachedStrings();
}

void UITask::renderCurrScreen() {
  if (!timePassed(BOOT_SCREEN_MILLIS)) { // boot screen until timeout passes
    // meshcore logo
    _display->setColor(DisplayDriver::BLUE);
    _display->drawXbm((_display->width() - LOGO_W) / 2, 3, meshcore_logo, LOGO_W, LOGO_H);

    // version info
    _display->setColor(DisplayDriver::LIGHT);
    _display->setTextSize(1);
    _display->setCursor((_display->width() - _version_width) / 2, 22);
    _display->print(_version_info);

    // node type
    const char* node_type = "< Repeater >";
    uint16_t typeWidth = _display->getTextWidth(node_type);
    _display->setCursor((_display->width() - typeWidth) / 2, 35);
    _display->print(node_type);
  } else {  // home screen
    // Make sure cached strings are up to date
    updateCachedStrings();
    
    // node name
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(DisplayDriver::GREEN);
    _display->print(_node_prefs->node_name);

    // freq / sf - use cached string
    _display->setCursor(0, 20);
    _display->setColor(DisplayDriver::YELLOW);
    _display->print(_line_freq);

    // bw / cr - use cached string
    _display->setCursor(0, 30);
    _display->print(_line_bw);
  }
}

// Display state tracking to minimize updates
unsigned long _last_display_update = 0;
bool _display_needs_update = true;

void UITask::loop() {
#ifdef PIN_USER_BTN
  if (timePassed(_next_read)) {
    int btnState = digitalRead(PIN_USER_BTN);
    
    // Track button state changes
    if (btnState != _prevBtnState) {
      if (btnState == LOW) {  // Button pressed
        if (_press_started == 0) _press_started = millis(); // Start timing the press
        
        if (!_display->isOn()) {
          _display->turnOn();
          _display_needs_update = true; // Force redraw when turning back on
        }
      } else if (_prevBtnState == LOW) { // Button just released
        unsigned long held = millis() - _press_started;
        _press_started = 0;
        
        if (_display->isOn()) {
          if (held > 1200) {  // Long press (>1.2 sec)
            // TODO: Long press action (e.g., toggle logging)
          } else {
            // TODO: Short press action (e.g., cycle display mode)
          }
        }
      }
      _auto_off = millis() + AUTO_OFF_MILLIS;  // Extend auto-off timer on any button activity
      _prevBtnState = btnState;
    }
    _next_read = millis() + 200;  // 5 reads per second
  }
#endif

  if (_display->isOn()) {
    // Update display if needed and at least 1 second has passed since last update
    // or 5 seconds have passed regardless (to ensure clock updates)
    bool time_to_update = ((millis() - _last_display_update) >= 5000) || 
                         (_display_needs_update && (millis() - _last_display_update) >= 1000);
    
    if (time_to_update && timePassed(_next_refresh)) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();
      
      _last_display_update = millis();
      _display_needs_update = false;
      _next_refresh = millis() + 200; // Minimum time between refreshes
    }
    
    if (timePassed(_auto_off)) {
      _display->turnOff();
    }
  }
}
