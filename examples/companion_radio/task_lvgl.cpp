#include <Arduino.h>

#include "esp_log.h"
#include "uiDefines.h"
#include "uiVars.h"

#define TAG "lvgl_task"

void lvgl_task(void *pvParameters) {

  vTaskSuspend(NULL);

  ESP_LOGI(TAG, "UI manager: Task running on core %d", xPortGetCoreID());

  while (1) {    
    lv_timer_handler();
    vTaskDelay(DELAY_LVGL_TASK / portTICK_PERIOD_MS);
  }
}