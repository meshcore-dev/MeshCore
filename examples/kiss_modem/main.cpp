#include <Arduino.h>
#include <target.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/IdentityStore.h>
#include "KissModem.h"

#if defined(NRF52_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
#endif

StdRNG rng;
mesh::LocalIdentity identity;
KissModem* modem_usb;
KissModem* tx_pending_modem = nullptr;

#if defined(KISS_UART_RX) && defined(KISS_UART_TX)
  #define KISS_UART_ENABLED
  #if defined(ESP32)
    HardwareSerial kiss_uart(1);
  #elif defined(NRF52_PLATFORM)
    Uart kiss_uart(NRF_UARTE1, NRF_UARTE1_IRQn, KISS_UART_RX, KISS_UART_TX);
  #elif defined(RP2040_PLATFORM)
    SerialUART kiss_uart(uart1, KISS_UART_TX, KISS_UART_RX);
  #elif defined(STM32_PLATFORM)
    HardwareSerial kiss_uart(KISS_UART_RX, KISS_UART_TX);
  #endif
  KissModem* modem_uart;
#endif

#ifndef KISS_UART_BAUD
  #define KISS_UART_BAUD 115200
#endif

void halt() {
  while (1) ;
}

void loadOrCreateIdentity() {
#if defined(NRF52_PLATFORM)
  InternalFS.begin();
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "Filesystem not defined"
#endif

  if (!store.load("_main", identity)) {
    identity = radio_new_identity();
    while (identity.pub_key[0] == 0x00 || identity.pub_key[0] == 0xFF) {
      identity = radio_new_identity();
    }
    store.save("_main", identity);
  }
}

void onSetRadio(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio_set_params(freq, bw, sf, cr);
}

void onSetTxPower(uint8_t power) {
  radio_set_tx_power(power);
}

float onGetCurrentRssi() {
  return radio_driver.getCurrentRSSI();
}

void onGetStats(uint32_t* rx, uint32_t* tx, uint32_t* errors) {
  *rx = radio_driver.getPacketsRecv();
  *tx = radio_driver.getPacketsSent();
  *errors = radio_driver.getPacketsRecvErrors();
}

void setup() {
  board.begin();

  if (!radio_init()) {
    halt();
  }

  radio_driver.begin();

  rng.begin(radio_get_rng_seed());
  loadOrCreateIdentity();

  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  delay(100);

  sensors.begin();

  modem_usb = new KissModem(Serial, identity, rng, radio_driver, board, sensors);
  modem_usb->setRadioCallback(onSetRadio);
  modem_usb->setTxPowerCallback(onSetTxPower);
  modem_usb->setGetCurrentRssiCallback(onGetCurrentRssi);
  modem_usb->setGetStatsCallback(onGetStats);
  modem_usb->begin();

#ifdef KISS_UART_ENABLED
  #if defined(ESP32)
    kiss_uart.setPins(KISS_UART_RX, KISS_UART_TX);
  #endif
  kiss_uart.begin(KISS_UART_BAUD);

  modem_uart = new KissModem(kiss_uart, identity, rng, radio_driver, board, sensors);
  modem_uart->setRadioCallback(onSetRadio);
  modem_uart->setTxPowerCallback(onSetTxPower);
  modem_uart->setGetCurrentRssiCallback(onGetCurrentRssi);
  modem_uart->setGetStatsCallback(onGetStats);
  modem_uart->begin();
#endif
}

void loop() {
  modem_usb->loop();
#ifdef KISS_UART_ENABLED
  modem_uart->loop();
#endif

  uint8_t packet[KISS_MAX_PACKET_SIZE];
  uint16_t len;
  
  if (tx_pending_modem == nullptr && modem_usb->getPacketToSend(packet, &len)) {
    tx_pending_modem = modem_usb;
    radio_driver.startSendRaw(packet, len);
  }

#ifdef KISS_UART_ENABLED
  if (tx_pending_modem == nullptr && modem_uart->getPacketToSend(packet, &len)) {
    tx_pending_modem = modem_uart;
    radio_driver.startSendRaw(packet, len);
  }
#endif

  if (tx_pending_modem != nullptr && radio_driver.isSendComplete()) {
    radio_driver.onSendFinished();
    tx_pending_modem->onTxComplete(true);
    tx_pending_modem = nullptr;
  }

  uint8_t rx_buf[256];
  int rx_len = radio_driver.recvRaw(rx_buf, sizeof(rx_buf));
  
  if (rx_len > 0) {
    int8_t snr = (int8_t)(radio_driver.getLastSNR() * 4);
    int8_t rssi = (int8_t)radio_driver.getLastRSSI();
    modem_usb->onPacketReceived(snr, rssi, rx_buf, rx_len);
#ifdef KISS_UART_ENABLED
    modem_uart->onPacketReceived(snr, rssi, rx_buf, rx_len);
#endif
  }

  radio_driver.loop();
}
