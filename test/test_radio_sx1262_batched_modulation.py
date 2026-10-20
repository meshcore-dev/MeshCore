"""Exercise production profile-switch ownership, caching and failure paths."""
from pathlib import Path
from radio_profile_test_support import NativeTest, method
ROOT = Path(__file__).resolve().parents[1]

class BatchedModulationTests(NativeTest):
    def test_tuple_validation_ldro_and_failure_cache_rollback(self):
        production = method((ROOT / "src/helpers/radiolib/CustomSX1262.h").read_text(),
                            "int16_t setLoRaModulationParams(")
        harness = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <initializer_list>
@STATE@
using std::isfinite;
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_ERR_INVALID_SPREADING_FACTOR -1
#define RADIOLIB_ERR_INVALID_CODING_RATE -2
#define RADIOLIB_ERR_INVALID_BANDWIDTH -3
#define RADIOLIB_ERR_WRONG_MODEM -4
#define RADIOLIB_SX126X_PACKET_TYPE_LORA 1
#define RADIOLIB_SX126X_LORA_BW_7_8 0x00
#define RADIOLIB_SX126X_LORA_BW_10_4 0x08
#define RADIOLIB_SX126X_LORA_BW_15_6 0x01
#define RADIOLIB_SX126X_LORA_BW_20_8 0x09
#define RADIOLIB_SX126X_LORA_BW_31_25 0x02
#define RADIOLIB_SX126X_LORA_BW_41_7 0x0a
#define RADIOLIB_SX126X_LORA_BW_62_5 0x03
#define RADIOLIB_SX126X_LORA_BW_125_0 0x04
#define RADIOLIB_SX126X_LORA_BW_250_0 0x05
#define RADIOLIB_SX126X_LORA_BW_500_0 0x06
struct Radio {
  SX1262ProfileSwitchState _profileSwitch{true};
  uint8_t spreadingFactor=9, bandwidth=4, codingRate=3, ldrOptimize=0;
  float bandwidthKhz=125;
  bool ldroAuto=true;
  unsigned queries=0, writes=0;
  uint8_t packetType=1, payload[4]={};
  int16_t result=0;
  uint8_t getPacketType() { ++queries; return packetType; }
  int16_t setModulationParams(uint8_t sf,uint8_t bw,uint8_t cr,uint8_t ldro) {
    // The pinned RadioLib reads these caches, not just the command arguments,
    // when deciding LDRO. Assert they are synchronized before that call.
    assert(spreadingFactor==sf && bandwidth==bw && codingRate==cr);
    if (ldroAuto) ldrOptimize=((1U<<spreadingFactor)/bandwidthKhz)>=16.0f;
    else ldrOptimize=ldro;
    payload[0]=sf;payload[1]=bw;payload[2]=cr;payload[3]=ldrOptimize;
    ++writes; return result;
  }
  @PRODUCTION@
};
bool sameCache(const Radio& a,const Radio& b) {
  return a.spreadingFactor==b.spreadingFactor && a.bandwidth==b.bandwidth
    && a.bandwidthKhz==b.bandwidthKhz && a.codingRate==b.codingRate
    && a.ldrOptimize==b.ldrOptimize && a.ldroAuto==b.ldroAuto;
}
void beginHop(Radio& r) {
  r._profileSwitch.end(true);
  r._profileSwitch.rxResult(0,true);
  r._profileSwitch.begin(true,true);
  r._profileSwitch.standbyResult(0);
}
int main() {
  const float bandwidths[]={7.8f,10.4f,15.6f,20.8f,31.25f,41.7f,62.5f,125,250,500};
  const uint8_t codes[]={0,8,1,9,2,10,3,4,5,6};
  for (unsigned i=0;i<10;++i) for(uint8_t sf=5;sf<=12;++sf)
    for(uint8_t cr=4;cr<=8;++cr) for(int mode=0;mode<3;++mode) {
      Radio r;r.ldroAuto=mode==0;r.ldrOptimize=mode==2;
      assert(r.setLoRaModulationParams(bandwidths[i],sf,cr)==0);
      assert(r.queries==1 && r.writes==1);
      assert(r.spreadingFactor==sf && r.bandwidth==codes[i]);
      assert(r.bandwidthKhz==bandwidths[i] && r.codingRate==cr-4);
      const uint8_t ldro=mode==0 ? ((1U<<sf)/bandwidths[i]>=16.0f) : mode==2;
      assert(r.ldrOptimize==ldro && r.payload[0]==sf && r.payload[1]==codes[i]
          && r.payload[2]==cr-4 && r.payload[3]==ldro);
      // Only a subsequent owned RX hop may reuse that acknowledged tuple.
      beginHop(r);
      assert(r.setLoRaModulationParams(bandwidths[i],sf,cr)==0);
      assert(r.queries==2 && r.writes==1);
      assert(r._profileSwitch.matchesModulation(sf,codes[i],cr-4,ldro));
    }
  {
    Radio r;beginHop(r);
    // Initially matching software fields are not proof of programmed hardware.
    assert(r.setLoRaModulationParams(125,9,7)==0 && r.writes==1);
    beginHop(r);assert(r.setLoRaModulationParams(125,9,7)==0 && r.writes==1);
    // Change each effective field independently, then verify repeat is skipped.
    assert(r.setLoRaModulationParams(250,9,7)==0 && r.writes==2);
    assert(r.setLoRaModulationParams(250,10,7)==0 && r.writes==3);
    assert(r.setLoRaModulationParams(250,10,5)==0 && r.writes==4);
    assert(r.setLoRaModulationParams(250,10,5)==0 && r.writes==4);
    r.ldroAuto=false;r.ldrOptimize=1;
    assert(r.setLoRaModulationParams(250,10,5)==0 && r.writes==5);
    assert(r.setLoRaModulationParams(250,10,5)==0 && r.writes==5);
    // autoLDRO() itself does not write: the next hop must apply its new value.
    r.ldroAuto=true;
    assert(r.setLoRaModulationParams(250,10,5)==0 && r.writes==6);
    r.ldroAuto=false;
    assert(r.setLoRaModulationParams(250,10,5)==0 && r.writes==6);
    r.ldroAuto=true;
    assert(r.setLoRaModulationParams(125,11,5)==0 && r.writes==7 && r.ldrOptimize==1);
    assert(r.setLoRaModulationParams(125,11,5)==0 && r.writes==7);
    // A failed changed tuple revokes reuse before physical rollback.
    r.result=-707;assert(r.setLoRaModulationParams(500,8,5)==-707);
    assert(!r._profileSwitch.modulationValid && r._profileSwitch.failed());
    r.result=0;beginHop(r);
    assert(r.setLoRaModulationParams(125,11,5)==0 && r.writes==9);
    assert(r.setLoRaModulationParams(125,11,5)==0 && r.writes==9);
    r.packetType=255;assert(r.setLoRaModulationParams(125,11,5)==-4);
    assert(!r._profileSwitch.modulationValid);
    r.packetType=1;assert(r.setLoRaModulationParams(125,11,5)==0 && r.writes==10);
  }
  for(int gate=0;gate<6;++gate) {
    Radio r;beginHop(r);assert(r.setLoRaModulationParams(125,9,7)==0);
    switch(gate) {
      case 0:r._profileSwitch.end(true);break; // no owned window
      case 1:r._profileSwitch.begin(true,true);break; // no standby yet
      case 2:r._profileSwitch.begin(false,true);break; // not continuous RX
      case 3:r._profileSwitch.begin(true,false);break; // cold oscillator
      case 4:r._profileSwitch.enabled=false;r._profileSwitch.begin(true,true);break;
      case 5:r._profileSwitch.invalidate();beginHop(r);break; // state lost
    }
    assert(r.setLoRaModulationParams(125,9,7)==0 && r.writes==2);
  }
  // Invalid values cannot issue SPI, mutate caches, or enter float->int UB.
  for(float bw: {NAN,INFINITY,-INFINITY,-1.0f,0.0f,50.0f,62.6f,1000.0f}) {
    Radio r,old=r;assert(r.setLoRaModulationParams(bw,7,5)==-3);
    assert(!r.queries && !r.writes && sameCache(r,old));
  }
  for(uint8_t sf: {0,4,13,255}) {
    Radio r,old=r;assert(r.setLoRaModulationParams(500,sf,5)==-1);
    assert(!r.queries && !r.writes && sameCache(r,old));
  }
  for(uint8_t cr: {0,3,9,255}) {
    Radio r,old=r;assert(r.setLoRaModulationParams(500,7,cr)==-2);
    assert(!r.queries && !r.writes && sameCache(r,old));
  }
  for(uint8_t packet: {0,2,255}) {
    Radio r;r.packetType=packet;Radio old=r;
    assert(r.setLoRaModulationParams(500,7,5)==-4);
    assert(r.queries==1 && !r.writes && sameCache(r,old));
  }
  for (int mode=0;mode<3;++mode) for(int error: {-10,-705,-707}) {
    Radio r;r.ldroAuto=mode==0;r.ldrOptimize=mode==2;Radio old=r;r.result=error;
    assert(r.setLoRaModulationParams(7.8f,12,8)==error);
    assert(r.queries==1 && r.writes==1 && sameCache(r,old));
    // A following restoration/retry must still work.
    r.result=0;assert(r.setLoRaModulationParams(125,9,7)==0);
    assert(sameCache(r,old));
  }
}
'''
        state = (ROOT / "src/helpers/radiolib/SX1262ProfileSwitchState.h").read_text().replace("#pragma once", "")
        self.compile_run(harness.replace("@PRODUCTION@", production).replace("@STATE@", state))

    def test_ordinary_setters_invalidate_acknowledged_tuple_even_on_failure(self):
        source = (ROOT / "src/helpers/radiolib/CustomSX1262.h").read_text()
        signatures = ("int16_t setBandwidth(", "int16_t setSpreadingFactor(",
                      "int16_t setCodingRate(", "int16_t forceLDRO(")
        methods = "\n".join(method(source, signature) for signature in signatures)
        state = (ROOT / "src/helpers/radiolib/SX1262ProfileSwitchState.h").read_text().replace("#pragma once", "")
        harness = r'''
#include <cassert>
#include <cstdint>
@STATE@
struct SX1262 {
  int16_t result=0;unsigned calls=0;bool interleave=false;
  virtual ~SX1262()=default;
  virtual int16_t setBandwidth(float bw) { assert(bw==125);++calls;return result; }
  virtual int16_t setSpreadingFactor(uint8_t sf) { assert(sf==9);++calls;return result; }
  int16_t setCodingRate(uint8_t cr,bool li) { assert(cr==7);interleave=li;++calls;return result; }
  int16_t forceLDRO(bool enabled) { assert(enabled);++calls;return result; }
};
struct Radio: SX1262 {
  SX1262ProfileSwitchState _profileSwitch{true};
  @METHODS@
};
int main() {
  for(int setter=0;setter<5;++setter) for(int failure=0;failure<2;++failure) {
    Radio r;r.result=failure ? -707:0;
    r._profileSwitch.rxResult(0,true);r._profileSwitch.begin(true,true);
    r._profileSwitch.standbyResult(0);r._profileSwitch.modulationResult(0,9,4,3,0);
    assert(r._profileSwitch.matchesModulation(9,4,3,0));
    SX1262& base=r;int16_t rc=0;
    switch(setter) {
      case 0:rc=base.setBandwidth(125);break;
      case 1:rc=base.setSpreadingFactor(9);break;
      case 2:rc=r.setCodingRate(7);assert(!r.interleave);break;
      case 3:rc=r.setCodingRate(7,true);assert(r.interleave);break;
      case 4:rc=r.forceLDRO(true);break;
    }
    assert(rc==r.result && r.calls==1 && !r._profileSwitch.modulationValid);
    assert(!r._profileSwitch.matchesModulation(9,4,3,0));
  }
}
'''
        self.compile_run(harness.replace("@STATE@", state).replace("@METHODS@", methods))
