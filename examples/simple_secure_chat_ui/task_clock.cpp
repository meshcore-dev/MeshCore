#include <Arduino.h>
#include <time.h>

#include "esp_log.h"
#include "uiDefines.h"
#include "uiVars.h"

#define TAG "clock_task"

void clock_task(void *pvParameters) {

  ESP_LOGI(TAG, "Clock manager: Task running on core %d", xPortGetCoreID());

  uiManager->clearDateTime();

  while (1) {
    if (g_clock_synced) {
      time_t now = time(nullptr);
      struct tm t;
      localtime_r(&now, &t);
      uiManager->updateDateTime(t);
    }
    vTaskDelay(DELAY_CLOCK_TASK / portTICK_PERIOD_MS);
  }
}