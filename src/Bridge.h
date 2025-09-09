#pragma once
#include <Packet.h>

#include <Vector.h>


#define BRIDGE_PACKET_BUFFER_SIZE 5

namespace mesh {

#define BP_PACKET_HEADER_SIZE ( 22 + PUB_KEY_SIZE + 1 + SIGNATURE_SIZE)
#define BP_NODE_PUB_OFFSET ( 22 )
#define BP_PACKET_MAX_SIZE ( MAX_TRANS_UNIT + BP_PACKET_HEADER_SIZE )

struct BridgePacket{
  uint8_t version;  //0
  float frequency;  //1
  uint8_t sf;       //5
  float bw;         //6
  float rssi;       //10
  float snr;        //14
  uint32_t timestamp; //18
  uint8_t node[32];   //22
  uint16_t packetLength; //54
  mesh::Packet* packet;
  uint8_t signature[64];
};

class Bridge {

protected:
  //bool _enabled;
  //bool _bridge_all;
  //bool _is_running;
  
  mesh::BridgePacket _inboundBuffer[BRIDGE_PACKET_BUFFER_SIZE];
  Vector<mesh::BridgePacket> _inboundPackets;

  mesh::BridgePacket _outboundBuffer[0];
  Vector<mesh::BridgePacket> _outboundPackets;

  public:
  //Bridge(bool bridge_all=false);

  virtual void start() = 0;
  virtual void loop() = 0;
  virtual void onMeshPacketRx(mesh::Packet* packet) = 0;
  virtual void onMeshPacketTx(mesh::Packet* packet) = 0;
  //mesh::BridgePacket* createBridgePacket(mesh::Packet);
  //void sendToMesh(mesh::BridgePacket* bridgePacket);
  
};


};
