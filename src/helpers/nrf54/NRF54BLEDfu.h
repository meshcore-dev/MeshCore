#pragma once

#include <bluefruit.h>

// Buttonless (legacy) Nordic DFU service, as Adafruit's BLEDfu provides on nRF52 (the nRF54 core's
// BLEDfu is a stub). When a DFU client asks to start, it hands the connection over to the
// nRF54 bootloader and reboots into its BLE DFU, so the client can reconnect and flash the app.
class NRF54BLEDfu : public BLEService {
  BLECharacteristic _chr_packet;
  BLECharacteristic _chr_control;
  BLECharacteristic _chr_revision;

  static void onControlWrite(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data,
                             uint16_t len);

public:
  NRF54BLEDfu();
  err_t begin() override;

  static void loop();  // reboots into the bootloader once a DFU start has been acknowledged

  // the bootloader version, as NRF54Board reads it at startup; the hand-off needs 0.5.0 or later
  static uint32_t bootloader_version;
};
