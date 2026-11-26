"""Exercise production profile-switch ownership, caching and failure paths."""
from pathlib import Path
from radio_profile_test_support import NativeTest, method
ROOT = Path(__file__).resolve().parents[1]

class LR1110ProfileSwitchTests(NativeTest):
    def test_checked_irq_snapshot_restores_spi_width_and_propagates_error(self):
        source = (ROOT / "src/helpers/radiolib/CustomLR1110.h").read_text()
        implementation = method(source, "int16_t snapshotIrqAfterRxExit(")
        harness = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_MODULE_SPI_WIDTH_STATUS 2
struct Module {
  enum { BITS_0=0 };
  struct { uint8_t widths[3]={0,0,8}; } spiConfig;
  uint8_t response[6]={0,0,0x12,0x34,0x56,0x78};
  int16_t result=0;
  int16_t SPItransferStream(const uint8_t*,uint8_t,bool,const uint8_t*,
                            uint8_t* out,size_t length,bool wait) {
    assert(spiConfig.widths[2]==0 && length==6 && wait);
    std::memcpy(out,response,length);return result;
  }
};
struct Radio {
  Module* mod;
  @METHOD@
};
int main() {
  Module m; Radio r; r.mod=&m; uint32_t irq=0;
  assert(r.snapshotIrqAfterRxExit(&irq)==0 && irq==0x12345678);
  assert(m.spiConfig.widths[2]==8);
  m.result=-7;irq=0xAABBCCDD;
  assert(r.snapshotIrqAfterRxExit(&irq)==-7 && irq==0xAABBCCDD);
  assert(m.spiConfig.widths[2]==8);
}
'''
        self.compile_run(harness.replace("@METHOD@", implementation))

    def test_fast_rx_skips_only_unchanged_packet_tuple_and_fails_closed(self):
        source = (ROOT / "src/helpers/radiolib/CustomLR1110.h").read_text()
        signatures = (
            "void setProfileSwitchOptimization(", "void setProfileStandbyWarm(",
            "void beginProfileSwitch(", "void endProfileSwitch(",
            "bool profileSwitchFailed(", "int16_t standby()", "int16_t standby(uint8_t",
            "int16_t sleep()", "int16_t reset()", "int16_t stageMode(",
            "int16_t setPreambleLength(", "int16_t startReceive()",
        )
        methods = "\n".join(method(source, signature).replace(" override", "")
                            for signature in signatures)
        state = (ROOT / "src/helpers/radiolib/LR1110ProfileSwitchState.h").read_text().replace(
            "#pragma once", ""
        )
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <string>
@STATE@
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_LR11X0_STANDBY_XOSC 0
#define RADIOLIB_LR11X0_RX_TIMEOUT_INF 0xffffff
#define RADIOLIB_LR11X0_IRQ_ALL 0xffffffff
#define RADIOLIB_LR11X0_MAX_PACKET_LENGTH 255
#define RADIOLIB_RADIO_MODE_NONE 0
#define RADIOLIB_IRQ_RX_DEFAULT_FLAGS 1
#define RADIOLIB_IRQ_RX_DEFAULT_MASK 2
#define RADIOLIB_IRQ_PREAMBLE_DETECTED 3
#define RADIOLIB_IRQ_HEADER_ERR 6
using RadioModeType_t=int;
struct RadioModeConfig_t {};
struct Module {
  enum { MODE_IDLE=0, MODE_RX=1 };
  std::string* trace;
  void setRfSwitchState(int mode) {
    assert(mode==MODE_RX || mode==MODE_IDLE);
    *trace+=mode==MODE_RX ? 'W' : 'I';
  }
};
struct LR1110 {
  std::string trace;
  char fail=0;
  size_t preambleLengthLoRa=32;
  uint8_t headerType=0,crcTypeLoRa=1,invertIQEnabled=0;
  uint32_t rxTimeout=0;
  int stagedMode=7;
  Module module{&trace};
  Module* mod=&module;
  int16_t call(char c) { trace+=c;return fail==c ? -7:0; }
  int16_t standby() { return call('S'); }
  int16_t standby(uint8_t) { return call('U'); }
  int16_t standby(uint8_t mode, bool wake) { assert(mode==1 && !wake);return call('s'); }
  int16_t setFs() { return call('F'); }
  int16_t sleep() { return call('L'); }
  int16_t reset() { return call('Z'); }
  int16_t stageMode(RadioModeType_t,RadioModeConfig_t*) { return call('G'); }
  int16_t setPreambleLength(size_t n) { preambleLengthLoRa=n;return call('P'); }
  int16_t startReceive(uint32_t,uint32_t,uint32_t,size_t) { return call('N'); }
  int16_t clearIrqState(uint32_t flags) { assert(flags==0xffffffff);return call('C'); }
  int16_t setPacketParamsLoRa(size_t p,uint8_t,uint8_t len,uint8_t,uint8_t) {
    assert(p==preambleLengthLoRa && len==255);return call('P');
  }
  int16_t setRx(uint32_t timeout) { assert(timeout==0xffffff);return call('R'); }
};
struct Radio:LR1110 {
  LR1110ProfileSwitchState _profileSwitch{true};
  bool _profileStandbyWarm=false,_profilePacketDirty=false;
  uint32_t _profileFastResumes=0;
  uint32_t pendingIrq=0;
  int16_t snapshotIrqAfterRxExit(uint32_t* irq) {
    const int16_t rc=call('Q');
    if (rc==0) *irq=pendingIrq;
    return rc;
  }
  @METHODS@
};
void prime(Radio& r) {
  r.trace.clear();assert(r.startReceive()==0 && r.trace=="N");
  assert(r._profileSwitch.rxValid);r.trace.clear();
  r.setProfileStandbyWarm(true);
}
void hop(Radio& r,size_t preamble,const std::string& expected) {
  r.beginProfileSwitch(true);assert(r.standby()==0 && r.trace=="IF");r.trace.clear();
  assert(r.setPreambleLength(preamble)==0 && r.trace.empty());
  assert(r.startReceive()==0 && r.trace==expected);
  assert(r._profileSwitch.rxValid && !r._profilePacketDirty);
  r.endProfileSwitch(true);r.trace.clear();
}
int main() {
  Radio r;prime(r);hop(r,32,"QWR");hop(r,48,"QPWR");hop(r,48,"QWR");
  r.pendingIrq=1;hop(r,32,"QCPWR");r.pendingIrq=0;
  assert(r._profileFastResumes==4);
  r.beginProfileSwitch(true);r.fail='F';
  assert(r.standby()==-7 && r.trace=="IF");
  assert(r.profileSwitchFailed() && !r._profileSwitch.rxValid);
  r.endProfileSwitch(false);r.fail=0;prime(r);
  r.setProfileStandbyWarm(false);
  assert(!r._profileSwitch.rxValid);r.beginProfileSwitch(true);
  assert(r.standby()==0 && r.trace=="S");r.endProfileSwitch(true);
  prime(r);
  r.beginProfileSwitch(true);r.standby();r.trace.clear();
  r.pendingIrq=1;r.fail='C';assert(r.startReceive()==-7 && r.trace=="QC");
  assert(r.profileSwitchFailed() && !r._profileSwitch.rxValid);
  r.endProfileSwitch(false);r.fail=0;r.pendingIrq=0;prime(r);hop(r,32,"QWR");
  r.beginProfileSwitch(true);r.standby();r.trace.clear();
  r.fail='Q';assert(r.startReceive()==-7 && r.trace=="Q");
  assert(r.profileSwitchFailed() && !r._profileSwitch.rxValid);
  r.endProfileSwitch(false);r.fail=0;prime(r);
  r.beginProfileSwitch(true);r.standby();r.trace.clear();
  r.setPreambleLength(64);r.fail='P';
  assert(r.startReceive()==-7 && r.trace=="QP");
  assert(r.profileSwitchFailed() && !r._profileSwitch.rxValid);
  r.endProfileSwitch(false);r.fail=0;prime(r);hop(r,32,"QPWR");
  r.beginProfileSwitch(true);r.standby();r.trace.clear();
  r.fail='R';assert(r.startReceive()==-7 && r.trace=="QWR");
  assert(r.profileSwitchFailed() && !r._profileSwitch.rxValid);
  r.endProfileSwitch(false);r.fail=0;prime(r);
  r.standby(uint8_t(0));assert(!r._profileSwitch.rxValid);
  prime(r);r.stageMode(2,nullptr);assert(!r._profileSwitch.rxValid);
  prime(r);r.sleep();assert(!r._profileSwitch.rxValid);
  prime(r);r.reset();assert(!r._profileSwitch.rxValid);
}
'''
        self.compile_run(harness.replace("@STATE@", state).replace("@METHODS@", methods))

    def test_owned_rx_context_invalidates_on_errors_and_unowned_operations(self):
        state = (ROOT / "src/helpers/radiolib/LR1110ProfileSwitchState.h").read_text().replace(
            "#pragma once", ""
        )
        harness = r'''
#include <cassert>
#include <cstdint>
@STATE@
int main() {
  LR1110ProfileSwitchState s(true);
  s.begin(true, true);
  assert(!s.canResumeFast());
  s.end(true);
  s.rxResult(0, true);
  for (int continuous = 0; continuous < 2; ++continuous)
    for (int warm = 0; warm < 2; ++warm) {
      s.begin(continuous, warm);
      s.standbyResult(0);
      assert(s.canResumeFast() == bool(continuous && warm));
      s.end(true);
    }
  s.begin(true, true);
  assert(!s.canResumeFast());
  s.standbyResult(0);
  assert(s.canResumeFast());
  s.modulationResult(0, 8, 4, 1, 0);
  assert(s.matchesModulation(8, 4, 1, 0));
  assert(!s.matchesModulation(9, 4, 1, 0));
  s.rxResult(0, true);
  assert(!s.canResumeFast());
  s.end(true);
  s.begin(true, true);
  s.standbyResult(-7);
  assert(s.failed() && !s.rxValid && !s.modulationValid);
  s.end(false);
  assert(!s.failed());
  s.rxResult(0, true);
  s.begin(true, true);
  s.standbyResult(0);
  s.modulationResult(-8, 8, 4, 1, 0);
  assert(s.failed() && !s.rxValid && !s.modulationValid);
  s.end(false);
  s.rxResult(0, true);
  s.begin(true, true);
  s.standbyResult(0);
  s.invalidate();
  assert(!s.canResumeFast() && !s.rxValid);
}
'''
        self.compile_run(harness.replace("@STATE@", state))

    def test_batched_modulation_validation_cache_and_failed_write_rollback(self):
        source = (ROOT / "src/helpers/radiolib/CustomLR1110.h").read_text()
        production = method(source, "int16_t setLoRaModulationParams(")
        state = (ROOT / "src/helpers/radiolib/LR1110ProfileSwitchState.h").read_text().replace(
            "#pragma once", ""
        )
        harness = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
@STATE@
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_ERR_INVALID_SPREADING_FACTOR -1
#define RADIOLIB_ERR_INVALID_CODING_RATE -2
#define RADIOLIB_ERR_INVALID_BANDWIDTH -3
#define RADIOLIB_ERR_WRONG_MODEM -4
#define RADIOLIB_LR11X0_PACKET_TYPE_NONE 0
#define RADIOLIB_LR11X0_PACKET_TYPE_LORA 1
#define RADIOLIB_LR11X0_LORA_BW_62_5 3
#define RADIOLIB_LR11X0_LORA_BW_125_0 4
#define RADIOLIB_LR11X0_LORA_BW_250_0 5
#define RADIOLIB_LR11X0_LORA_BW_500_0 6
#define RADIOLIB_LR11X0_LORA_LDRO_ENABLED 1
#define RADIOLIB_LR11X0_LORA_LDRO_DISABLED 0
struct Radio {
  LR1110ProfileSwitchState _profileSwitch{true};
  uint8_t spreadingFactor=7, bandwidth=4, codingRate=1, ldrOptimize=0;
  float bandwidthKhz=125;
  bool ldroAuto=true;
  uint8_t packetType=1;
  int16_t result=0;
  unsigned queries=0, writes=0;
  uint8_t payload[4]={};
  int16_t getPacketType(uint8_t* type) { ++queries;*type=packetType;return 0; }
  int16_t setModulationParamsLoRa(uint8_t sf,uint8_t bw,uint8_t cr,uint8_t ldro) {
    assert(spreadingFactor==sf && bandwidth==bw && codingRate==cr);
    ldrOptimize=ldroAuto ? ((float(1U<<sf)/bandwidthKhz)>=16.0f) : ldro;
    payload[0]=sf;payload[1]=bw;payload[2]=cr;payload[3]=ldrOptimize;
    ++writes;return result;
  }
  @PRODUCTION@
};
int main() {
  Radio r;
  assert(r.setLoRaModulationParams(125,4,5)==RADIOLIB_ERR_INVALID_SPREADING_FACTOR);
  assert(r.setLoRaModulationParams(125,7,9)==RADIOLIB_ERR_INVALID_CODING_RATE);
  assert(r.setLoRaModulationParams(63,7,5)==RADIOLIB_ERR_INVALID_BANDWIDTH);
  assert(r.writes==0);
  r.packetType=0;
  assert(r.setLoRaModulationParams(125,7,5)==RADIOLIB_ERR_WRONG_MODEM);
  r.packetType=1;
  for(float bw: {62.5f,125.0f,250.0f,500.0f}) {
    for(uint8_t sf=5;sf<=12;++sf) {
      for(uint8_t cr=4;cr<=8;++cr) {
        assert(r.setLoRaModulationParams(bw,sf,cr)==0);
        assert(r.spreadingFactor==sf && r.bandwidthKhz==bw && r.codingRate==cr-4);
        assert(r.payload[0]==sf && r.payload[2]==cr-4);
        assert(r.payload[3]==((float(1U<<sf)/bw)>=16.0f));
        r._profileSwitch.rxResult(0,true);
        r._profileSwitch.begin(true,true);
        r._profileSwitch.standbyResult(0);
        const unsigned writes=r.writes;
        assert(r.setLoRaModulationParams(bw,sf,cr)==0 && r.writes==writes);
        r._profileSwitch.end(true);
      }
    }
  }
  r._profileSwitch.rxResult(0,true);
  r._profileSwitch.begin(true,true);
  r._profileSwitch.standbyResult(0);
  const auto oldSf=r.spreadingFactor, oldBw=r.bandwidth, oldCr=r.codingRate;
  const auto oldKhz=r.bandwidthKhz;
  const auto oldLdro=r.ldrOptimize;
  r.result=-99;
  assert(r.setLoRaModulationParams(125,8,5)==-99);
  assert(r.spreadingFactor==oldSf && r.bandwidth==oldBw && r.codingRate==oldCr);
  assert(r.bandwidthKhz==oldKhz && r.ldrOptimize==oldLdro);
  assert(r._profileSwitch.failed() && !r._profileSwitch.rxValid);
}
'''
        self.compile_run(harness.replace("@STATE@", state).replace("@PRODUCTION@", production))

    def test_ordinary_packet_mutations_revoke_owned_rx_even_on_failure(self):
        source = (ROOT / "src/helpers/radiolib/CustomLR1110.h").read_text()
        signatures = ("int16_t setCRC(", "int16_t implicitHeader(",
                      "int16_t explicitHeader(", "int16_t invertIQ(", "int16_t setModem(")
        methods = "\n".join(method(source, s).replace(" override", "") for s in signatures)
        state = (ROOT / "src/helpers/radiolib/LR1110ProfileSwitchState.h").read_text().replace("#pragma once", "")
        harness = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
@STATE@
using ModemType_t = int;
struct LR1110 {
  int16_t result=0;
  int16_t setCRC(uint8_t,uint32_t,uint32_t,bool) { return result; }
  int16_t implicitHeader(size_t) { return result; }
  int16_t explicitHeader() { return result; }
  int16_t invertIQ(bool) { return result; }
  int16_t setModem(ModemType_t) { return result; }
};
struct Radio : LR1110 {
  LR1110ProfileSwitchState _profileSwitch{true};
  @METHODS@
};
int main() {
  for(int failure=0;failure<2;++failure) for(int setter=0;setter<5;++setter) {
    Radio r;r.result=failure ? -7 : 0;
    r._profileSwitch.rxResult(0,true);r._profileSwitch.begin(true,true);
    r._profileSwitch.standbyResult(0);r._profileSwitch.modulationResult(0,7,4,1,0);
    assert(r._profileSwitch.canResumeFast());int16_t rc=0;
    switch(setter) {
      case 0:rc=r.setCRC(2);break;
      case 1:rc=r.implicitHeader(16);break;
      case 2:rc=r.explicitHeader();break;
      case 3:rc=r.invertIQ(true);break;
      case 4:rc=r.setModem(1);break;
    }
    assert(rc==r.result && !r._profileSwitch.rxValid && !r._profileSwitch.modulationValid);
    assert(!r._profileSwitch.canResumeFast());
  }
}
'''
        self.compile_run(harness.replace("@STATE@", state).replace("@METHODS@", methods))
