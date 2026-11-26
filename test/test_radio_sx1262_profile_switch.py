"""Exercise production profile-switch ownership, caching and failure paths."""
from pathlib import Path
from radio_profile_test_support import NativeTest, method
ROOT = Path(__file__).resolve().parents[1]

class FastProfileSwitchTests(NativeTest):
    def test_packet_tuple_requires_matching_acknowledged_owned_rx(self):
        state = (ROOT / "src/helpers/radiolib/SX1262ProfileSwitchState.h").read_text()
        harness = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
@STATE@
int main() {
  SX1262ProfileSwitchState s(true);
  s.packetResult(0, 32, 1, 255, 0, 0);
  assert(!s.matchesPacket(32, 1, 255, 0, 0));
  s.rxResult(0, true);
  s.begin(true, true);
  s.standbyResult(0);
  assert(s.matchesPacket(32, 1, 255, 0, 0));
  assert(!s.matchesPacket(33, 1, 255, 0, 0));
  assert(!s.matchesPacket(32, 0, 255, 0, 0));
  assert(!s.matchesPacket(32, 1, 16, 0, 0));
  assert(!s.matchesPacket(32, 1, 255, 1, 0));
  assert(!s.matchesPacket(32, 1, 255, 0, 1));
  s.invalidatePacket();
  assert(!s.matchesPacket(32, 1, 255, 0, 0));
  s.packetResult(0, 32, 1, 255, 0, 0);
  assert(s.matchesPacket(32, 1, 255, 0, 0));
  s.packetResult(-100, 32, 1, 255, 0, 0);
  assert(s.failed() && !s.packetValid && !s.rxValid);
}
'''
        self.compile_run(harness.replace("@STATE@", state))

    def test_owned_rx_window_commands_errors_and_lifecycle_invalidation(self):
        src = (ROOT / "src/helpers/radiolib/CustomSX1262.h").read_text()
        state = (ROOT / "src/helpers/radiolib/SX1262ProfileSwitchState.h").read_text().replace("#pragma once", "")
        signatures = ["void setProfileSwitchOptimization(", "void beginProfileSwitch(",
                      "void endProfileSwitch(", "bool profileSwitchFailed(", "int16_t restoreTcxoAfterSleep(",
                      "int16_t standby()", "int16_t standby(uint8_t", "int16_t sleep()",
                      "int16_t sleep(bool", "int16_t reset(bool", "int16_t stageMode(",
                      "int16_t setPreambleLength(", "int16_t setCRC(",
                      "int16_t implicitHeader(", "int16_t explicitHeader()",
                      "int16_t invertIQ(", "int16_t startReceive()"]
        methods = "\n".join(method(src, signature).replace(" override", "") for signature in signatures)
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <string>
@STATE@
#define MC_SX1262_FAST_FS @FS@
#define MC_SX1262_FAST_PACKET_CACHE @PACKET@
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_SX126X_STANDBY_XOSC 1
#define RADIOLIB_SX126X_STANDBY_RC 0
#define RADIOLIB_SX126X_XOSC_START_ERR 32
#define RADIOLIB_ERR_SPI_CMD_FAILED -200
#define RADIOLIB_SX126X_RX_TIMEOUT_INF 0xffffff
#define RADIOLIB_SX126X_PACKET_TYPE_LORA 1
#define RADIOLIB_RADIO_MODE_NONE 0
#define RADIOLIB_IRQ_RX_DEFAULT_FLAGS 1
#define RADIOLIB_IRQ_RX_DEFAULT_MASK 2
#define RADIOLIB_IRQ_PREAMBLE_DETECTED 3
using RadioModeType_t=int;
using RadioLibIrqFlags_t=uint32_t;
struct RadioModeConfig_t {};
struct Module {
  enum { MODE_IDLE=0, MODE_RX=1 };std::string* trace;
  void setRfSwitchState(int mode) { assert(mode==MODE_IDLE || mode==MODE_RX);*trace+=mode==MODE_RX?'W':'I'; }
};
struct SX1262 {
  std::string trace;char fail=0;int modem=1;bool standbyXOSC=true;
  size_t preambleLengthLoRa=32;
  uint8_t crcTypeLoRa=1,implicitLen=255,headerType=0,invertIQEnabled=0;
  uint32_t rxTimeout=0;int stagedMode=3;
  float tcxoVoltage=1.8f;uint32_t tcxoDelay=1600;unsigned errors=32;
  Module module{&trace};Module* mod=&module;
  virtual ~SX1262()=default;
  int16_t call(char c) { trace+=c;return fail==c ? -100:0; }
  Module* getMod() { return mod; }
  int16_t setFs() { return call('f'); }
  virtual int16_t standby() { return call('S'); }
  virtual int16_t standby(uint8_t) { return call('U'); }
  int16_t standby(uint8_t mode,bool wake) { assert(mode==1);return call(wake?'S':'s'); }
  virtual int16_t sleep() { return call('L'); }
  int16_t sleep(bool) { return call('l'); }
  int16_t reset(bool) { return call('Z'); }
  unsigned getDeviceErrors() { call('E');return errors; }
  int16_t setTCXO(float voltage,uint32_t delay) {
    assert(voltage==tcxoVoltage && delay==1600);return call('V');
  }
  virtual int16_t stageMode(RadioModeType_t mode,RadioModeConfig_t*) { return call(mode==1?'G':'T'); }
  virtual int16_t setPreambleLength(size_t n) { preambleLengthLoRa=n;return call('M'); }
  int16_t setCRC(uint8_t,uint16_t,uint16_t,bool) { return call('c'); }
  int16_t implicitHeader(size_t) { return call('h'); }
  int16_t explicitHeader() { return call('e'); }
  virtual int16_t invertIQ(bool) { return call('i'); }
  int16_t startReceive(uint32_t timeout,uint32_t flags,uint32_t mask,size_t len) {
    assert(timeout==0xffffff && flags==9 && mask==2 && len==0);
    int16_t rc=stageMode(1,nullptr);if(rc==0) rc=standby();if(rc==0) rc=call('F');return rc;
  }
  int16_t startReceiveDutyCycle(uint32_t,uint32_t,uint32_t,uint32_t) { return call('D'); }
  uint8_t getPacketType() { call('Q');return modem; }
  int16_t clearIrqStatus() { return call('C'); }
  int16_t setPacketParams(size_t p,int crc,int len,int header,int iq) {
    assert(p==preambleLengthLoRa && crc==1 && len==255 && header==0 && iq==0);return call('P');
  }
  int16_t setRx(uint32_t timeout) { assert(timeout==0xffffff);return call('R'); }
};
struct Radio: SX1262 {
  SX1262ProfileSwitchState _profileSwitch{true};bool _rx_ps_rf_rx_disabled=false;uint32_t _profileFastResumes=0;
  bool _coldStandby=true;
  bool _tcxoWakePending=false;
  @METHODS@
};
void prime(Radio& r) {
  const bool cold=r._coldStandby;
  const bool wake=r._tcxoWakePending;
  r.trace.clear();assert(r.startReceive()==0);assert(r.trace==(wake ? "GUEVFQ":cold ? "GUFQ":"GSFQ"));
  assert(!r._coldStandby);
  assert(!r._profileSwitch.modulationValid); // full RX setup does not prime a tuple
  assert(r._profileSwitch.rxValid && r._profileSwitch.packetValid);r.trace.clear();
}
void fast(Radio& r) {
  r.beginProfileSwitch(true);assert(r.standby()==0);
  assert(r.trace==(MC_SX1262_FAST_FS ? "If" : "s"));r.trace.clear();
  r._profileSwitch.modulationResult(0,8,4,1,0);
  assert(r._profileSwitch.matchesModulation(8,4,1,0));
  assert(r.setPreambleLength(120)==0 && r.trace.empty());
  const bool unchanged = r._profileSwitch.packetValid && r._profileSwitch.packetPreamble==120;
  assert(r.startReceive()==0 && r.trace==(MC_SX1262_FAST_PACKET_CACHE && unchanged ? "CWR" : "CPWR"));
  assert(r.stagedMode==0 && r._profileSwitch.rxValid);
  assert(r._profileSwitch.modulationValid); // successful fast resume preserves it
  r.endProfileSwitch(true);assert(!r._profileSwitch.open);r.trace.clear();
}
int main() {
  Radio r;prime(r);fast(r);fast(r);
  // Deferring before standby changes no hardware and retains the RX context.
  r.beginProfileSwitch(true);r.endProfileSwitch(true);fast(r);
  // No shortcut is legal before a successful owned standby.
  r.beginProfileSwitch(true);assert(r.setPreambleLength(88)==0 && r.trace=="M");
  r.endProfileSwitch(true);r.trace.clear();
  // Ordinary standby, mode staging (TX/CAD), sleep and reset revoke RX.
  for(int event=0;event<6;++event) {
    Radio x;prime(x);
    x._profileSwitch.modulationResult(0,8,4,1,0);
    switch(event) {
      case 0:x.standby();break;
      case 1:x.standby(uint8_t(0));break;
      case 2:x.stageMode(2,nullptr);break;
      case 3:x.sleep();break;
      case 4:x.sleep(false);break;
      case 5:x.reset();break;

    }
    assert(!x._profileSwitch.rxValid && !x._profileSwitch.modulationValid
        && !x._profileSwitch.packetValid);
    x.trace.clear();x.beginProfileSwitch(true);
    const bool cold=x._coldStandby;
    const bool wake=x._tcxoWakePending;
    assert(x.standby()==0 && x.trace==(wake ? "UEV":cold ? "U":"S"));x.endProfileSwitch(true);prime(x);fast(x);
  }
  for(int gate=0;gate<3;++gate) {
    Radio x;prime(x);
    if(gate==0) x.setProfileSwitchOptimization(false);
    if(gate==1) x.standbyXOSC=false;
    x.beginProfileSwitch(gate!=2);x.trace.clear();
    assert(x.standby()==0 && x.trace=="S");x.endProfileSwitch(true);
  }
  for(int setter=0;setter<5;++setter) {
    Radio x;prime(x);
    switch(setter) {
      case 0:x.setPreambleLength(120);break;
      case 1:x.setCRC(1);break;
      case 2:x.implicitHeader(16);break;
      case 3:x.explicitHeader();break;
      case 4:x.invertIQ(true);break;
    }
    assert(!x._profileSwitch.packetValid && x._profileSwitch.rxValid);
  }
  const char standbyFail = MC_SX1262_FAST_FS ? 'f' : 's';
  for(char fail:std::string(1,standbyFail)+"CPR") {
    Radio x;prime(x);x.beginProfileSwitch(true);x.fail=fail;
    if(fail==standbyFail) assert(x.standby()==-100);
    else { assert(x.standby()==0);if(fail=='P') x.setPreambleLength(120);assert(x.startReceive()==-100); }
    assert(x.profileSwitchFailed() && !x._profileSwitch.rxValid);
    assert(x.trace.back()==fail);x.endProfileSwitch(false);x.fail=0;prime(x);fast(x);
  }
  Radio x;prime(x);x.beginProfileSwitch(true);x.standby();
  // A modulation/frequency/packet apply failure must revoke the capability
  // before the wrapper rolls back with normal setup.
  x.endProfileSwitch(false);assert(!x._profileSwitch.rxValid);prime(x);fast(x);
  Radio sleeper;prime(sleeper);assert(sleeper.sleep()==0 && sleeper.trace=="Ul");
  prime(sleeper);sleeper.fail='U';assert(sleeper.sleep(true)==-100 && sleeper.trace=="U");
  assert(!sleeper._profileSwitch.rxValid && sleeper._coldStandby);
  Radio fault;prime(fault);fault.sleep();fault.trace.clear();fault.errors=4;
  assert(fault.standby()==-200 && fault.trace=="UE" && fault._tcxoWakePending);
  fault.errors=32;fault.trace.clear();assert(fault.standby()==0 && fault.trace=="UEV");
  assert(!fault._tcxoWakePending);
  Radio wrong;wrong.modem=0;assert(wrong.startReceive()==0 && !wrong._profileSwitch.rxValid);
  Radio off;off.setProfileSwitchOptimization(false);assert(off.startReceive()==0);
  assert(off.trace=="GUF" && !off._profileSwitch.rxValid);
}
'''
        for fs, packet in ((0, 0), (1, 1)):
            with self.subTest(fs=fs, packet=packet):
                self.compile_run(harness.replace("@STATE@", state)
                                 .replace("@METHODS@", methods)
                                 .replace("@FS@", str(fs))
                                 .replace("@PACKET@", str(packet)))
