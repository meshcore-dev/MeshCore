#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

#include "NodePrefs.h"

enum class UIEventType {
    none,
    contactMessage,
    channelMessage,
    roomMessage,
    newContactMessage,
    ack
};

class AbstractUITask {
protected:
  mesh::MainBoard* _board;
  MultiSerialInterface* _interfaceManager;
  bool _connected;

  AbstractUITask(mesh::MainBoard* board, MultiSerialInterface* interfaceManager) : _board(board), _interfaceManager(interfaceManager) {
    _connected = false;
  }

public:
  void setHasConnection(bool connected) { _connected = connected; }
  bool hasConnection() const { return _connected; }
  uint16_t getBattMilliVolts() const { return _board->getBattMilliVolts(); }
  bool isBluetoothEnabled() const { return _interfaceManager->isBluetoothEnabled(); }
  void enableBluetooth() { _interfaceManager->enableBluetooth(); }
  void disableBluetooth() { _interfaceManager->disableBluetooth(); }
  virtual void msgRead(int msgcount) = 0;
  virtual void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) = 0;
  virtual void msgAck(uint32_t ack_crc) { }   // delivery ACK received (any origin)
  virtual void msgEchoHeard() { }   // our last sent message heard being retransmitted
  virtual void loginResult(const uint8_t* pub_key, bool success) { }   // repeater/room login outcome
  virtual void statusResponse(const uint8_t* pub_key, const uint8_t* data, int len) { }   // REQ_TYPE_GET_STATUS reply
  virtual void cliResponse(const char* from_name, const char* text) { }   // repeater CLI reply text
  virtual void traceResponse(uint32_t tag, const uint8_t* path_hashes, const uint8_t* path_snrs, uint8_t hop_count, int8_t final_snr) { }   // TRACE round trip returned
  virtual void notify(UIEventType t = UIEventType::none) = 0;
  virtual void loop() = 0;
};
