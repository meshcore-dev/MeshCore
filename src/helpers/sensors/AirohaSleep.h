#pragma once
#include <helpers/sensors/LocationProvider.h>

// Board must keep the GPS RTC backup pin powered and can cut GPS power once we return.
// Returns true if the command was acked within 3 attempts, otherwise returns false.
static inline bool airohaEnterSleep(LocationProvider* nmea) {
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    nmea->drain();
    nmea->sendSentence("$PAIR650,0");
    if (nmea->waitFor("$PAIR001,650,0", 50)) {  // wait for the command received signal
      #ifdef GPS_NMEA_DEBUG
      Serial.printf("Airoha RTC Backup sleep command accepted by GPS after %u attempts\r\n", attempt + 1);
      #endif
      nmea->waitFor("$PAIR650,0", 50); // give the GPS 50ms grace to signal it is ready for sleep
      return true;
    }
  }
  return false;
}