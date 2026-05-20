#include <Arduino.h>

#include "esp_log.h"
#include "uiDefines.h"
#include "uiVars.h"

#define TAG "clock_task"

void clock_task(void *pvParameters) {

  vTaskSuspend(NULL);

  ESP_LOGI(TAG, "Clock manager: Task running on core %d", xPortGetCoreID());

  uiManager->clearDateTime();

  // TODO: sync clock
  while (1) {
    // uiManager->updateDateTime(
    //   myClock->getTimeStruct()
    // );
    // uiManager->updateValues();
    vTaskDelay(DELAY_CLOCK_TASK / portTICK_PERIOD_MS);
  }
}