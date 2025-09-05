#pragma once

#include <Bridge.h>
#include <Dispatcher.h>
#include <helpers/CommonCLI.h>

#include <AsyncUDP.h>

#include "UdpBridgeDetails.h"

namespace mesh {


class UdpBridge : public mesh::Bridge {

protected:
  //bool _listening;
  bool _waitingForNetwork;
  AsyncUDP _udp;
  NodePrefs* _nodePrefs;
  mesh::UDPBridgePrefs* _prefs;
  mesh::Dispatcher* _dispatcher;
  mesh::LocalIdentity* _identity;
  mesh::RTCClock* _clock;

  bool setupListener();

public:
  UdpBridge(NodePrefs* prefs, mesh::LocalIdentity* identiy, mesh::Dispatcher* dispatcher, mesh::RTCClock* clock);
  void start() override;
  void loop() override;
  void stop();
  void onMeshPacketRx(mesh::Packet* packet) override;
  void onMeshPacketTx(mesh::Packet* packet) override;


  void bufferRawBridgePacket(const uint8_t* data, const uint8_t length, uint8_t source);
  void bridgeMeshPacket(mesh::Packet* packet, uint8_t source);
};


};