#include "UdpBridge.h"
#include <WiFi.h>

namespace mesh {

UdpBridge::UdpBridge(NodePrefs* prefs, mesh::LocalIdentity* identity, mesh::Dispatcher* dispatcher, mesh::RTCClock* clock){
    //_listening = false;
    _nodePrefs = prefs;
    _prefs = &prefs->udpBridge;
    _identity = identity;
    _dispatcher = dispatcher;
    _clock = clock;
    _waitingForNetwork = false;
}

void UdpBridge::start(){

    if(this->_prefs->flags.network_bridge){

        if(WiFi.status() == WL_CONNECTED){
            Serial.println("udp can start, connected");
            if(setupListener()){
                _waitingForNetwork = false;
            } else {
                Serial.println("udp failed to start");
                _waitingForNetwork = true;
            }

        } else {
            Serial.println("udp can't start, wait for network");
            _waitingForNetwork = true;
        }
    }

    /*if(this->_prefs->flags.tx_bridge || this->_prefs->flags.rx_bridge){
        this->_outboundPackets = Vector<mesh::BridgePacket>(this->_outboundBuffer);
    }*/
}

void UdpBridge::loop(){

    if(_waitingForNetwork){
        if(this->_prefs->flags.network_bridge){
            if(WiFi.status() == WL_CONNECTED){
                Serial.println("udp can start now");
                _waitingForNetwork = false;
                if(setupListener()){
                    Serial.println("udp listener success");
                    _waitingForNetwork = false;
                } else {
                    _waitingForNetwork = true;
                    Serial.println("udp failed to start");
                }

            } else {
                //Serial.println("Still waiting");
                //_waitingForNetwork = true;
            }
        }
    } else if(!_inboundPackets.empty()) {
        // Network is connected

        Serial.printf("udp bridge has packets %i \r\n", _inboundPackets.size());

        while(!_inboundPackets.empty()) {
            mesh::BridgePacket bpacket = _inboundPackets.back();

            _dispatcher->processBridgePacket( bpacket.packet );
            _dispatcher->releasePacket( bpacket.packet );
            _inboundPackets.pop_back();

        }
    }

}

void UdpBridge::stop(){
    if(this->_udp.connected()){
        this->_udp.close();
    }
}

void UdpBridge::onMeshPacketRx(mesh::Packet* packet){

    if(!_waitingForNetwork && this->_prefs->flags.rx_bridge){
        Serial.println("onMeshPacketRx - bridging to udp");
        bridgeMeshPacket(packet, PACKET_SOURCE_UDP_BRIDGE);
    }
}

void UdpBridge::onMeshPacketTx(mesh::Packet* packet){
    
    if(!_waitingForNetwork && this->_prefs->flags.tx_bridge){
        Serial.println("onMeshPacketTx - bridging to udp");

        bridgeMeshPacket(packet, PACKET_SOURCE_UDP_BRIDGE);
    }
}


bool UdpBridge::setupListener(){

    this->_inboundPackets = Vector<mesh::BridgePacket>(this->_inboundBuffer);

    Serial.println("UDP starting network listner");

    if(_prefs->flags.mode == UDP_BRIDGE_MODE_BROADCAST || _prefs->flags.mode == UDP_BRIDGE_MODE_DIRECT){

        Serial.println("    mode = broadcast || direct");
        this->_udp.listen( this->_prefs->port );
    
    } else if( _prefs->flags.mode == UDP_BRIDGE_MODE_MULTICAST ){

        if(_prefs->flags.ip_version == UDP_BRIDGE_IPV4){

            // ipv4
            IPAddress addr4( _prefs->ipv4 );
            _udp.listenMulticast( addr4, _prefs->port );

            Serial.println("    mode = multicast(ipv4)");

        } else {
            // ipv6
            IPv6Address addr6( _prefs->ipv6 );
            _udp.listenMulticast( addr6, _prefs->port );
            Serial.println("    mode = multicast(ipv6)");
        }

    }

    if(this->_udp.connected()){
        //setup udp handler
        Serial.println("UDP started network listener");

        this->_udp.onPacket([this](AsyncUDPPacket packet) {
            Serial.print("UDP Packet Type: ");
            Serial.print(packet.isBroadcast() ? "Broadcast" : packet.isMulticast() ? "Multicast" : "Unicast");
            Serial.print(", From: ");
            Serial.print(packet.remoteIP());
            Serial.print(":");
            Serial.print(packet.remotePort());
            Serial.print(", To: ");
            Serial.print(packet.localIP());
            Serial.print(":");
            Serial.print(packet.localPort());
            Serial.print(", Length: ");
            Serial.print(packet.length());
            Serial.printf("\r\n");

            bufferRawBridgePacket( packet.data(), packet.length(), PACKET_SOURCE_UDP_BRIDGE);

            Serial.printf("UDP queue Size: %d max: %d\r\n", _inboundPackets.size(), _inboundPackets.max_size());
        });

        return true;
    } else {
        Serial.println("UDP failed to start network listener");

        return false;
    }

}



void UdpBridge::bufferRawBridgePacket(const uint8_t* data, const uint8_t length, uint8_t source){

    if(_inboundPackets.full()){ Serial.println("\tDROPPING - inbound udp queue full"); return; }

    Serial.printf("Got udp packet len = %i\n\r", length);

    Serial.print("0x");
    mesh::Utils::printHex(Serial, data, length);
    Serial.println();
    

    mesh::Packet* pkt = this->_dispatcher->obtainNewPacket();

    if(pkt){

        pkt->_source = source;
    
        mesh::Identity bsender(data+BP_NODE_PUB_OFFSET);
        bool verified = bsender.verify( data+(length-SIGNATURE_SIZE), data, length-SIGNATURE_SIZE );

        if(!verified){
            _dispatcher->releasePacket(pkt);
            Serial.println("\tDROPPING - DANGER inbound udp packet is forged or corrupt");
            
            return;
        }
        

        mesh::BridgePacket bpacket;
    
        uint8_t* idx = (uint8_t*) data;
        bpacket.version = *idx; idx++;
        bpacket.frequency = *((float*) idx); idx+=sizeof(float);
        bpacket.sf = *idx; idx++;
        bpacket.bw = *((float*) idx); idx+=sizeof(float);
        bpacket.rssi = *((float*) idx); idx+=sizeof(float);
        bpacket.snr = *((float*) idx); idx+=sizeof(float);
        bpacket.timestamp = *((uint32_t*) idx); idx+=sizeof(uint32_t);
        
        memcpy( bpacket.node, bsender.pub_key, PUB_KEY_SIZE );
        idx+=PUB_KEY_SIZE;

        bpacket.packetLength = *((uint16_t*) idx); idx+=sizeof(uint16_t);
    
        pkt->readFrom( idx, bpacket.packetLength);
        bpacket.packet = pkt;
        idx+=bpacket.packetLength;
    
        memcpy( bpacket.signature, idx, SIGNATURE_SIZE );

        
        if(!_inboundPackets.full()){
            _inboundPackets.push_back(bpacket);
            Serial.println("ACCEPTING inbound udp packet");
        } else {
            Serial.println("\tDROPPING - inbound udp queue full");
            _dispatcher->releasePacket(pkt);
        }
    } else {
        Serial.println("\tDROPPING inbound udp packet. mesh packet queue full");
    }

}



void UdpBridge::bridgeMeshPacket(mesh::Packet* packet, uint8_t source){

    if(packet->_source == source){
        Serial.println(" dropping from this bridge");
        return;
    }

    //if(!_outboundPackets.full()){ return; }

    //Serial.printf("free heap %i and max alloc %i\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    mesh::BridgePacket bpacket;


    Serial.println(" mesh packet sent to udp network");
    uint8_t* pktBuffer = (uint8_t*) malloc(BP_PACKET_MAX_SIZE);
    uint8_t* idx = pktBuffer;

    //size_t idx = 0;
    *(idx) = 0x0;                                 // version
    idx++;

    //Serial.printf("2   idx = %i\n", idx);

    *((float*) idx) = _nodePrefs->freq;
    idx+=sizeof(float);
    
    ///Serial.printf("3   idx = %i\n", idx);

    *idx = _nodePrefs->sf;
    idx+=sizeof(uint8_t);
    
    ///Serial.printf("4   idx = %i\n", idx);

    // *((float*) (&pktBuffer[idx+=sizeof(float)])) = _nodePrefs->bw;

    *((float*) idx) = _nodePrefs->bw;
    idx+=sizeof(float);
    
    //Serial.printf("5   idx = %i\n", idx);

    float rssi = 0.0f;
    *((float*) idx) = rssi;
    idx+=sizeof(float);
    
    //Serial.printf("6   idx = %i\n", idx);

    float snr = packet->getSNR();

    // *((float*) (&pktBuffer[idx+=sizeof(float)])) = packet->getSNR();
    *((float*) idx) = snr;
    idx+=sizeof(float);
    
    //Serial.printf("7   idx = %i\n", idx);

    uint32_t time = _clock->getCurrentTime();
    *((uint32_t*) idx) = time;
    idx+=sizeof(uint32_t);
    

    memcpy( idx, _identity->pub_key, PUB_KEY_SIZE );
    idx += PUB_KEY_SIZE;
    
    //Serial.printf("9   idx = %i\n", idx);


    uint16_t packetLen = (uint16_t) packet->getRawLength();
    *((uint16_t*) idx) = packetLen;
    idx+=sizeof(uint16_t);

    //Serial.printf("10   idx = %i\n", idx);

    uint8_t written = packet->writeTo( idx);
    idx+=written;

    //Serial.printf("   wrote = %i vs expected = %i\n", written, packetLen);
    
    //Serial.printf("11   idx = %i\n", idx);


    _identity->sign( idx, pktBuffer, idx - pktBuffer );
    idx+=SIGNATURE_SIZE;
    
    Serial.println("got mesh packet");

    Serial.print("0x");
    mesh::Utils::printHex(Serial, pktBuffer, idx-pktBuffer);
    Serial.println();

    
    if(_prefs->flags.mode == UDP_BRIDGE_MODE_BROADCAST){
        _udp.broadcastTo( pktBuffer, idx-pktBuffer, _prefs->port);
    } else {

        AsyncUDPMessage msg(idx-pktBuffer);

        msg.write( pktBuffer, idx-pktBuffer );

        if(_prefs->flags.ip_version == UDP_BRIDGE_IPV4){

            // ipv4
            IPAddress addr4( _prefs->ipv4 );
            _udp.sendTo(msg, addr4, _prefs->port);

        } else {

            // ipv6
            IPv6Address addr6( _prefs->ipv6 );
            _udp.sendTo(msg, addr6, _prefs->port);

        }
    }

    free( (void*) pktBuffer);
}

}