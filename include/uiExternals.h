#ifndef EXTERNALS_h
#define EXTERNALS_h

#include <Arduino.h>

extern void createTasks();
extern TaskHandle_t t_core0_lvgl;
extern TaskHandle_t t_core1_clock;  
extern TaskHandle_t t_core1_core;  

#endif