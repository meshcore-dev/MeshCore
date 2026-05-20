#include <Arduino.h>

#include "esp_log.h"
#include "uiDefines.h"
#include "uiTasks.h"
#include "uiVars.h"

// Tasks
TaskHandle_t t_core0_lvgl;
TaskHandle_t t_core1_clock;
TaskHandle_t t_core1_core;

#define TAG "createTasks"

void createTasks() {
  Serial.println("Creating Tasks...");

  xTaskCreatePinnedToCore(
    lvgl_task,       // Task function.
    "LVGL_Manager",  // Name of task.
    10000,          // Stack size of task
    NULL,           // Parameter of the task
    5,              // Priority of the task
    &t_core0_lvgl,  // Task handle to keep track of created task
    0);             // Pin task to core 0

  xTaskCreatePinnedToCore(
    core_task,       // Task function.
    "MeshCore",     // Name of task.
    10000,          // Stack size of task
    NULL,           // Parameter of the task
    4,              // Priority of the task
    &t_core1_core,  // Task handle to keep track of created task
    1);             // Pin task to core 1

  xTaskCreatePinnedToCore(
    clock_task,       // Task function.
    "CLOCK_Manager",  // Name of task.
    10000,          // Stack size of task
    NULL,           // Parameter of the task
    4,              // Priority of the task
    &t_core1_clock,  // Task handle to keep track of created task
    1);             // Pin task to core 1

  ESP_LOGD(TAG, "All tasks created\nStarting tasks...");

  vTaskResume(t_core0_lvgl);  
  vTaskResume(t_core1_clock);

}
