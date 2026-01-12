#if defined(NRF52_PLATFORM)
#include "NRF52Board.h"
#include "nrf_sdm.h"

// For PowerSaving
static SoftwareTimer wakeupTimer;

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

static void wakeUpCallback(TimerHandle_t xTimer) {
  // To wake up based on timer
  resumeLoop();
  wakeupTimer.stop();
}

void NRF52Board::enterLightSleep(uint32_t secs) {
//  uint32_t realFreeHeap = dbgHeapFree();
//  Serial.printf("Actual Free Heap: %u bytes\n", realFreeHeap);
//  delay(100);

#if defined(P_LORA_DIO_1)
  // To set wakeup timer
  if (wakeupTimer.getHandle() == NULL) {
    // This block runs ONLY if .begin() has never been called
    wakeupTimer.begin(secs * 1000, wakeUpCallback, nullptr, false);
    wakeupTimer.start();
  } else {
    // The timer already exists in the Heap, to reset the timer
    wakeupTimer.reset();
  }

  // Extra check to prevent to enter suspendLoop when the loop has not processed the pending RX
  if(digitalRead(P_LORA_DIO_1) == HIGH) {
    return;
  }

  // To pause MCU to sleep
  suspendLoop();
#endif
}

void NRF52Board::sleep(uint32_t secs) {
  // To check if the BLE is powered and looking for/connected to a phone
  uint8_t sd_enabled;
  sd_softdevice_is_enabled(&sd_enabled); // To set sd_enabled to 1 if the BLE stack is active.

  if (!sd_enabled) { // BLE is off ~ No active OTA, safe to go to sleep
    enterLightSleep(secs); // To wake up after "secs" seconds or when receiving a LoRa packet
  }
}
#endif
