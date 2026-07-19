#pragma once

#include <Arduino.h>

static inline void generateEthernetMac(uint8_t mac[6]) {
  uint32_t device_id = NRF_FICR->DEVICEID[0];
  mac[0] = 0x02;
  mac[1] = 0x92;
  mac[2] = 0x1F;
  mac[3] = (device_id >> 16) & 0xFF;
  mac[4] = (device_id >> 8) & 0xFF;
  mac[5] = device_id & 0xFF;
}
