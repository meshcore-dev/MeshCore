// Settings implementation - load/save using Preferences
#include "UITask.h"
#include <Preferences.h>
#include <M5Cardputer.h>
#include <helpers/ui/M5CardputerDisplay.h>

void UITask::loadSettings() {
    Preferences prefs;
    
    // Default values
    _brightness = 128;
    _main_color_idx = 0; // White
    _secondary_color_idx = 1; // Black
    
    // Open preferences in read-only mode
    if (!prefs.begin("ui_settings", true)) {
        Serial.println("Failed to open preferences");
        applyTheme();
        M5Cardputer.Display.setBrightness(_brightness);
        return;
    }
    
    // Read settings with defaults
    _brightness = prefs.getUChar("brightness", 128);
    _main_color_idx = prefs.getUChar("main_color", 0);
    _secondary_color_idx = prefs.getUChar("sec_color", 1);
    
    prefs.end();
    
    // Validate
    if (_brightness > 255) _brightness = 255;
    if (_main_color_idx < 0 || _main_color_idx >= NUM_COLORS) _main_color_idx = 0;
    if (_secondary_color_idx < 0 || _secondary_color_idx >= NUM_COLORS) _secondary_color_idx = 1;
    
    Serial.printf("Loaded settings: brightness=%d, main=%d, sec=%d\n", 
                  _brightness, _main_color_idx, _secondary_color_idx);
    
    // Apply loaded settings
    applyTheme();
    M5Cardputer.Display.setBrightness(_brightness);
}

void UITask::saveSettings() {
    Preferences prefs;
    
    // Open preferences in write mode
    if (!prefs.begin("ui_settings", false)) {
        Serial.println("Failed to save settings");
        return;
    }
    
    prefs.putUChar("brightness", _brightness);
    prefs.putUChar("main_color", _main_color_idx);
    prefs.putUChar("sec_color", _secondary_color_idx);
    
    prefs.end();
    
    Serial.printf("Saved settings: brightness=%d, main=%d, sec=%d\n", 
                  _brightness, _main_color_idx, _secondary_color_idx);
}

void UITask::applyTheme() {
    if (_main_color_idx < 0 || _main_color_idx >= NUM_COLORS) {
        _main_color_idx = 0;
    }
    if (_secondary_color_idx < 0 || _secondary_color_idx >= NUM_COLORS) {
        _secondary_color_idx = 1;
    }
    
    const ColorOption& main_color = COLORS[_main_color_idx];
    const ColorOption& secondary_color = COLORS[_secondary_color_idx];
    
    // Update DisplayDriver colors - cast to M5CardputerDisplay
    if (_display) {
        M5CardputerDisplay* m5_display = static_cast<M5CardputerDisplay*>(_display);
        m5_display->setLightColor(main_color.rgb565);
        m5_display->setDarkColor(secondary_color.rgb565);
    }
    
    Serial.printf("Applied colors: %s (0x%04X) on %s (0x%04X)\n", 
                  main_color.name, main_color.rgb565,
                  secondary_color.name, secondary_color.rgb565);
}
