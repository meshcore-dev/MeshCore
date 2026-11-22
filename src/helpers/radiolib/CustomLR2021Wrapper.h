#pragma once

#include "CustomLR2021.h"
#include "RadioLibWrappers.h"

#ifndef USE_LR2021
#define USE_LR2021
#endif

#ifndef LR2021_RX_BOOST_LEVEL
#define LR2021_RX_BOOST_LEVEL 7
#endif

class CustomLR2021Wrapper : public RadioLibWrapper {
  float snr, rssi;
  uint8_t last_rx_packet_sf;

public:
  CustomLR2021Wrapper(CustomLR2021& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    auto r = (CustomLR2021 *)_radio;
    // LR2021 only accepts RF/modulation config commands while in STANDBY mode.
    // Re-configuring while in continuous RX gets silently rejected (-706),
    // which broke hot frequency changes over BLE.
    int16_t status = r->standby();
    if (status != RADIOLIB_ERR_NONE) { MESH_DEBUG_PRINTLN("CustomLR2021Wrapper: standby() -> %d", status); }
    if ((status = r->setFrequency(freq)) != RADIOLIB_ERR_NONE) { MESH_DEBUG_PRINTLN("CustomLR2021Wrapper: setFrequency() -> %d", status); }
    if ((status = r->setSpreadingFactor(sf)) != RADIOLIB_ERR_NONE) { MESH_DEBUG_PRINTLN("CustomLR2021Wrapper: setSpreadingFactor() -> %d", status); }
    if ((status = r->setBandwidth(bw)) != RADIOLIB_ERR_NONE) { MESH_DEBUG_PRINTLN("CustomLR2021Wrapper: setBandwidth() -> %d", status); }
    if ((status = r->setCodingRate(cr)) != RADIOLIB_ERR_NONE) { MESH_DEBUG_PRINTLN("CustomLR2021Wrapper: setCodingRate() -> %d", status); }
    updatePreamble(sf);
    // internal idle() sets STATE_IDLE, which triggers a startReceive() on the next loop
    status = applySideDetectorConfig();
    if (status != RADIOLIB_ERR_NONE) { MESH_DEBUG_PRINTLN("CustomLR2021Wrapper: applySideDetectorConfig() -> %d", status); }
    PacketMillis pm = calcMaxPacketMillis(sf, bw, cr, preambleLengthForSF(sf));
    r->setPreambleMillis(pm.preambleMillis);      // pure in-memory setters, no standby needed
    r->setMaxPayloadMillis(pm.payloadMillis);

  }

  int recvRaw(uint8_t* bytes, int sz) override {
    int len = 0;
    if (state & STATE_INT_READY) {
      uint8_t cr;
      bool crc = false;;
      uint8_t packetLen = 0;
      uint8_t detector = 0;
      ((CustomLR2021 *)_radio)->getLoRaPacketStatus(&cr, &crc, &packetLen, &snr, &rssi, NULL, &detector);
      if (packetLen > 0) {
        len = (packetLen > sz) ? sz : (int) packetLen;
        int err = _radio->readData(bytes, len);
        if (err == RADIOLIB_ERR_NONE && !crc) {
          err = RADIOLIB_ERR_LORA_HEADER_DAMAGED;  // header says no CRC, so the payload was never checked
        }
        if (err != RADIOLIB_ERR_NONE) {
          MESH_DEBUG_PRINTLN("RadioLibWrapper: error: readData(%d)", err);
          len = 0;
          n_recv_errors++;
        } else {
          //  Serial.print("  readData() -> "); Serial.println(len);
          n_recv++;
          if (detector == 0) {
            last_rx_packet_sf = getSpreadingFactor();
          } else {
            last_rx_packet_sf = _sideDet[detector - 1].sf;
          }
        }
      }
      state = STATE_RX;     // LR2021 stays in Rx after readData, calling startReceive while still in Rx throws -706 errors
    }

    if (state != STATE_RX) {
      int err = _radio->startReceive();
      if (err == RADIOLIB_ERR_NONE) {
        state = STATE_RX;
      } else {
        MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
      }
    }
    return len;
  }

  bool configSideDetectors(const uint8_t* sideDetSFs, uint8_t num, float bw) override {
    LR2021LoRaSideDetector_t tmp[3];
    uint8_t sf = getSpreadingFactor();

    if (sf >= 10 && num > 1) { return false; }  // only 1 side detector allowed when primary SF >= 10
    for (int i = 0; i < num; i++) {
      if (sideDetSFs[i] > 12 || sideDetSFs[i] < 5) { return false; }  // must be valid SF
      if (sideDetSFs[i] <= sf) { return false; }  // must be < primary SF
      if (sideDetSFs[i] > sf + 4) { return false; }  // span must not be > 4

      tmp[i].sf = sideDetSFs[i];
      float tSym = calcTsym(tmp[i].sf, bw);
      if (tSym >= 16.0f) {
        tmp[i].ldro = true;
      } else {
        tmp[i].ldro = false; 
      }
      tmp[i].invertIQ = false;
      tmp[i].syncWord = 0x12;
    }
    int16_t status = ((CustomLR2021 *)_radio)->setSideDetector(tmp, num);
    RadioLibWrapper::idle(); // trigger startReceive()
    MESH_DEBUG_PRINTLN("setSideDetector() returned %d", status);

    if (status == RADIOLIB_ERR_NONE) {
      for (int i = 0; i < num; i++) { _sideDet[i] = tmp[i]; }
      _numSideDet = num;
    } else {
      return false;
    }
    
    return true;
  }

  int16_t applySideDetectorConfig() {
    int16_t status = ((CustomLR2021 *)_radio)->setSideDetector(_sideDet, _numSideDet);
    RadioLibWrapper::idle(); // trigger startReceive()
    return status;
  }

  float calcTsym(uint8_t sf, float bw) {
    float tSym = (float)(uint32_t(1) << sf) / (float)bw;
    return tSym;
  }

  bool lastRxHadCRC() override {
    bool crc = true;
    ((CustomLR2021 *)_radio)->getLoRaRxHeaderInfo(NULL, &crc);
    return crc;
  }
  bool isReceivingPacket() override {
    return ((CustomLR2021 *)_radio)->isReceiving();
  }

  float getCurrentRSSI() override {
    float rssi = -110;
    ((CustomLR2021 *)_radio)->getRssiInst(&rssi);
    return rssi;
  }

  void onSendFinished() override {
    RadioLibWrapper::onSendFinished();
    _radio->setPreambleLength(preambleLengthForSF(getSpreadingFactor())); // overcomes weird issues with small and big pkts
  }

  float getLastRSSI() const override { return rssi; }
  float getLastSNR() const override { return snr; }

  uint8_t getLastRxSpreadingFactor() const { return last_rx_packet_sf; }

  uint8_t getSpreadingFactor() const override { return ((CustomLR2021 *)_radio)->getSpreadingFactor(); }
  
  bool supportsRxBoostedGain() const override { return true; }
  bool setRxBoostedGainMode(bool en) override {
    ((CustomLR2021 *)_radio)->standby(); // LR2021 must be in standby to accept setRxBoostedGainMode
    int16_t status = ((CustomLR2021 *)_radio)->setRxBoostedGainMode(en ? LR2021_RX_BOOST_LEVEL: 0);
    RadioLibWrapper::idle(); // trigger startReceive()
    return status == RADIOLIB_ERR_NONE;
  }

  bool getRxBoostedGainMode() const override {
    return ((CustomLR2021 *)_radio)->getRxBoostedGainMode();
  }

  protected:
    LR2021LoRaSideDetector_t _sideDet[3];
    size_t _numSideDet = 0;

};
