#ifndef VARS_h
#define VARS_h

#include <Arduino.h>
#include "lvgl.h"
#include "uiManager.h"

extern void createTasks();

extern TaskHandle_t t_core1_tft;
extern TaskHandle_t t_core0_lvgl;
extern TaskHandle_t t_core1_clock;
extern TaskHandle_t t_core1_core;

extern SemaphoreHandle_t semaphoreData;

extern UIManager *uiManager;
extern lv_obj_t * ui_MainTabView;

extern volatile bool g_clock_synced;

#endif