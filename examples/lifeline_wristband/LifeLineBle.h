#pragma once

#include <Arduino.h>
#include <helpers/nrf52/SerialBLEInterface.h>
#include "MyMesh.h"

// Phone <-> wristband BLE frames. The contract is defined in the LifeLine repo:
// refs/adrs/2026-09-28-wristband-ble-contract-and-simulator.md
// (Dart reference: packages/lifeline_protocol/lib/src/frames.dart)

#define LIFELINE_BLE_NAME_PREFIX  "LifeLine-"

#define LL_CMD_APP_START          0x01
#define LL_CMD_GET_BATT           0x14
#define LL_CMD_SET_LOCATION       0xF0
#define LL_CMD_SOS_ACK            0xF1
#define LL_CMD_SOS_RESULT         0xF2
#define LL_CMD_SOS_CANCEL         0xF3

#define LL_RESP_OK                0x00
#define LL_RESP_ERR               0x01
#define LL_RESP_SELF_INFO         0x05
#define LL_RESP_BATT              0x0C

#define LL_ERR_UNSUPPORTED_CMD    1
#define LL_ERR_ILLEGAL_ARG        6

class LifeLineBle {
  SerialBLEInterface _ble;
  MyMesh& _mesh;
  mesh::MainBoard& _board;
  char _name[16];             // id part of the BLE name, e.g. "A1B2"
  int32_t _phone_lat, _phone_lon;   // last SET_LOCATION, x1e6, RAM only
  uint8_t _in[MAX_FRAME_SIZE];
  uint8_t _out[MAX_FRAME_SIZE];

  void handleFrame(const uint8_t* frame, size_t len);
  void writeOk();
  void writeErr(uint8_t code);
  void writeSelfInfo();
  void writeBattery();

public:
  LifeLineBle(MyMesh& mesh, mesh::MainBoard& board);

  void begin(uint32_t pin_code);
  void loop();
  bool isConnected() const { return _ble.isConnected(); }
};
