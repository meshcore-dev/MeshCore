#pragma once

#include <lvgl.h>

class UITask;

// On-device node & radio configuration rows for the Settings tab:
// node name, radio params (freq/BW/SF/CR with preset prefills), TX power,
// BLE pin. Applies through the same internals the phone commands use.
void settingsBuildExtras(lv_obj_t* parent, UITask* task);
void settingsRefreshRows();
int  settingsTzOffset();   // UTC offset in hours (persisted)

// exports for the first-boot wizard and about screen
int  settingsPresetCount();
const char* settingsPresetLabel(int idx);
void settingsApplyPreset(int idx);     // sets + saves radio params, no confirm
void settingsSetTzOffset(int hours);
