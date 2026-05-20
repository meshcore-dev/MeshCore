#ifndef TASKS_h
#define TASKS_h

#include <Arduino.h>
#include "uiDefines.h"
#include "uiVars.h"

void lvgl_task(void *pvParameters);
void clock_task(void *pvParameters);
void core_task(void *pvParameters);

#endif