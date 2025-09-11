#include "T114Board.h"

#include <Arduino.h>
#include <Wire.h>
#include <bluefruit.h>

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

void T114Board::begin() {
  // for future use, sub-classes SHOULD call this from their begin()
  startup_reason = BD_STARTUP_NORMAL;

  pinMode(PIN_VBAT_READ, INPUT);

  // Enable SoftDevice low-power mode
  sd_power_mode_set(NRF_POWER_MODE_LOWPWR);

  // Enable DC/DC converter for better efficiency (REG1 stage)
  NRF_POWER->DCDCEN = 1;

  // Power down unused communication peripherals
  // UART1 - Not used on T114
  NRF_UARTE1->ENABLE = 0;

  // SPIM2/SPIS2 - Not used (SPI is on SPIM0)
  NRF_SPIM2->ENABLE = 0;
  NRF_SPIS2->ENABLE = 0;

  // TWI1 (I2C1) - Not used (I2C is on TWI0)
  NRF_TWIM1->ENABLE = 0;
  NRF_TWIS1->ENABLE = 0;

  // PWM modules - Not used for standard T114 functions
  NRF_PWM1->ENABLE = 0;
  NRF_PWM2->ENABLE = 0;
  NRF_PWM3->ENABLE = 0;

  // PDM (Digital Microphone Interface) - Not used
  NRF_PDM->ENABLE = 0;

  // I2S - Not used
  NRF_I2S->ENABLE = 0;

  // QSPI - Not used (no external flash)
  NRF_QSPI->ENABLE = 0;

  // Disable unused analog peripherals
  // SAADC channels - only keep what's needed for battery monitoring
  NRF_SAADC->ENABLE = 0; // Re-enable only when needed for measurements

  // COMP - Comparator not used
  NRF_COMP->ENABLE = 0;

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, HIGH);
#endif

  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10); // give sx1262 some time to power up
}

bool T114Board::startOTAUpdate(const char *id, char reply[]) {
  // Config the peripheral connection with maximum bandwidth
  // more SRAM required by SoftDevice
  // Note: All config***() function must be called before begin()
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.configPrphConn(92, BLE_GAP_EVENT_LENGTH_MIN, 16, 16);

  Bluefruit.begin(1, 0);
  // Set max power. Accepted values are: -40, -30, -20, -16, -12, -8, -4, 0, 4
  Bluefruit.setTxPower(4);
  // Set the BLE device name
  Bluefruit.setName("T114_OTA");

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
