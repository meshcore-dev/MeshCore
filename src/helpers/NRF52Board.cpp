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

uint32_t NRF52Board::getIRQGpio() {
#if defined(RADIO_SX1276) && defined(P_LORA_DIO_0) // SX1276
  return P_LORA_DIO_0;
#elif defined(P_LORA_DIO_1) // SX1262
  return P_LORA_DIO_1;
#else
  return -1; // Not connected
#endif
}

bool NRF52Board::safeToSleep() {
  // Check for RX status
  uint32_t wakeupPin = getIRQGpio();
  if (digitalRead(wakeupPin) == HIGH) {
    return false;
  }

  // Check if the BLE is powered and looking for/connected to a phone
  uint8_t sd_enabled;
  sd_softdevice_is_enabled(&sd_enabled); // Set sd_enabled to 1 if the BLE stack is active.

  if (sd_enabled) { // BLE is on
    return false;
  }

  // Safe to sleep
  return true;
}

void NRF52Board::sleep(uint32_t secs) {
  // Skip if not safe to sleep
  if (!safeToSleep()) {
    return;
  }

  // Configure GPIO wakeup
  uint32_t wakeupPin = getIRQGpio();

  // Mark the start of the sleep
  uint32_t startTime = millis();
  uint32_t timeoutMs = secs * 1000;

  // Create event when wakeup pin is high
  nrf_gpio_cfg_sense_input(wakeupPin, NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_HIGH);

  while (true) {
    // Do housekeeping tasks for peripherals (UART...) so they do not block sleep
    yield();

    // Wakeup timer
    if ((millis() - startTime) >= timeoutMs) {
      break;
    }

    // Clear event noises from Memory Watch Unit
    NVIC_ClearPendingIRQ(MWU_IRQn);

    // Clear stale events    
    __SEV();
    __WFE();

    // Disable ISR servicing
    noInterrupts();

    if (digitalRead(wakeupPin) == HIGH) {
      interrupts();
      break;
    }

    // Attempt to sleep, wakeup on any events
    __WFE();

    // Enable ISR servicing
    interrupts();
  }

  // Disable sense on wakeup pin
  nrf_gpio_cfg_input(wakeupPin, NRF_GPIO_PIN_NOPULL);

  // Clear the latch so the next sleep is fresh and do not remember old events.
  NRF_GPIO->LATCH = (1 << wakeupPin);
}
#endif
