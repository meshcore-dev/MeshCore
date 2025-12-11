#if defined(NRF52_PLATFORM)
#include "NRF52Board.h"

// Temperature from NRF52 MCU
float NRF52Board::getMCUTemperature() {
  NRF_TEMP->TASKS_START = 1; // Start temperature measurement

  long startTime = millis();  
  while (NRF_TEMP->EVENTS_DATARDY == 0) { // Wait for completion. Should complete in 50us
    if(millis() - startTime > 1) {  // To wait 1ms
      NRF_TEMP->TASKS_STOP = 1;
      return NAN;
    }
  }
  
  NRF_TEMP->EVENTS_DATARDY = 0; // Clear event flag

  int32_t temp = NRF_TEMP->TEMP; // In 0.25 *C units
  NRF_TEMP->TASKS_STOP = 1;

  return temp * 0.25f; // Convert to *C
}

void NRF52Board::enterLightSleep(uint32_t secs) {
  // Enable SoftDevice low-power mode
  sd_power_mode_set(NRF_POWER_MODE_LOWPWR);

  // Enable DC/DC converter- 30% less power conumption
  NRF_POWER->DCDCEN = 1;

  // Wakeup by events
  waitForEvent();
}

void NRF52Board::sleep(uint32_t secs) {
  enterLightSleep(secs); // To wake up after "secs" seconds or when receiving a LoRa packet
}
#endif
