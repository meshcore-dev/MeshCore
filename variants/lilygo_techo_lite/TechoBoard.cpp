#include <Arduino.h>
#include "TechoBoard.h"

#ifdef LILYGO_TECHO

#include <bluefruit.h>
#include <Wire.h>

static BLEDfu bledfu;

static void connect_callback(uint16_t conn_handle) {
  (void)conn_handle;
  MESH_DEBUG_PRINTLN("BLE client connected");
}

static void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void)conn_handle;
  (void)reason;

  MESH_DEBUG_PRINTLN("BLE client disconnected");
}

void TechoBoard::begin() {
  // for future use, sub-classes SHOULD call this from their begin()
  startup_reason = BD_STARTUP_NORMAL;

  Wire.begin();

  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10);   // give sx1262 some time to power up
}

uint16_t TechoBoard::getBattMilliVolts() {
  analogReadResolution(12);

  // Enable control to measure battery
  pinMode(BATTERY_MEASUREMENT_CONTROL, OUTPUT);
  digitalWrite(BATTERY_MEASUREMENT_CONTROL, HIGH);
  delayMicroseconds(50); // To stablize the ADC

  uint16_t adc = 0;
  for (int i = 0; i < BATTERY_SAMPLES; i++) {
    adc += analogRead(PIN_VBAT_READ);
  }
  adc /= BATTERY_SAMPLES; // To average the adc readings

  // Disable control to measure battery
  digitalWrite(BATTERY_MEASUREMENT_CONTROL, LOW);

  // It is not a linear function
  if (adc >= 3430) return 4200;
  else if(adc >= 3420) return 4100;
  else if(adc >= 3410) return 4000;
  else if(adc >= 3400) return 3900;
  else if(adc >= 3390) return 3800;
  else if(adc >= 3380) return 3700;
  else if(adc >= 3360) return 3600;
  else if(adc >= 3355) return 3400;
  else if(adc >= 3350) return 3300;
  else if(adc >= 3250) return 3200;
  else if(adc >= 3130) return 3100;
  else if(adc >= 3020) return 3000;
  else return adc; // From this point, the ADC = mV
}

bool TechoBoard::startOTAUpdate(const char* id, char reply[]) {
  // Config the peripheral connection with maximum bandwidth
  // more SRAM required by SoftDevice
  // Note: All config***() function must be called before begin()
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.configPrphConn(92, BLE_GAP_EVENT_LENGTH_MIN, 16, 16);

  Bluefruit.begin(1, 0);
  // Set max power. Accepted values are: -40, -30, -20, -16, -12, -8, -4, 0, 4
  Bluefruit.setTxPower(4);
  // Set the BLE device name
  Bluefruit.setName("TECHO_OTA");

  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  // To be consistent OTA DFU should be added first if it exists
  bledfu.begin();

  // Set up and start advertising
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();

  /* Start Advertising
    - Enable auto advertising if disconnected
    - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
    - Timeout for fast mode is 30 seconds
    - Start(timeout) with timeout = 0 will advertise forever (until connected)

    For recommended advertising interval
    https://developer.apple.com/library/content/qa/qa1931/_index.html
  */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244); // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);   // number of seconds in fast mode
  Bluefruit.Advertising.start(0);             // 0 = Don't stop advertising after n seconds

  strcpy(reply, "OK - started");
  return true;
}
#endif
