"""Execute production scanner transactions against a controllable transceiver."""
from pathlib import Path
from radio_profile_test_support import NativeTest, method
ROOT = Path(__file__).resolve().parents[1]

class ProfileDriverTests(NativeTest):
    def test_busy_fifo_rollback_scanner_and_primary_transactions(self):
        source = (ROOT / "src/helpers/radiolib/RadioLibWrappers.cpp").read_text()
        signatures = ("uint8_t RadioLibWrapper::beginReconfigure(",
                      "int RadioLibWrapper::recvRaw(",
                      "mesh::RadioParamApplyResult RadioLibWrapper::tuneProfile(",
                      "void RadioLibWrapper::serviceProfiles(",
                      "mesh::RadioParamApplyResult RadioLibWrapper::trySetPrimaryParams(",
                      "mesh::RadioParamApplyResult RadioLibWrapper::trySetParams(")
        production = "\n".join(method(source, s).replace("RadioLibWrapper::", "") for s in signatures)
        harness = r'''
#include <cassert>
#include <RadioProfiles.h>
#include <helpers/radiolib/RadioInterruptEvents.h>
#define STATE_IDLE 0
#define STATE_RX 1
#define STATE_TX_WAIT 3
#define STATE_TX_DONE 4
#define RADIOLIB_ERR_NONE 0
static uint32_t elapsed_us = 100000;
uint32_t profileTimestamp(bool) { return elapsed_us; }
uint32_t profileElapsedUs(uint32_t since,uint32_t now,bool) { return now-since; }
uint32_t millis() { return elapsed_us/1000; }
static RadioInterruptEvents radio_interrupt;
struct Chip {
  int standbyResult=0;
  bool injectInterrupt=false;
  mesh::RadioProfileParams physical;
  unsigned reads=0,lengthQueries=0;
  int getPacketLength() { ++lengthQueries;return 4; }
  int16_t readData(uint8_t*,int) { ++reads;return 0; }
  float getRSSI() { return -102; }
  float getSNR() { return 4.5; }
  int16_t standby() {
    if (injectInterrupt) { injectInterrupt=false;radio_interrupt.record(); }
    return standbyResult;
  }
};
struct RadioLibWrapper {
  mesh::RadioProfiles _profiles;
  mesh::RadioProfileParams _physical_params;
  Chip chip; Chip* _radio=&chip;
  uint8_t _state=STATE_RX,_active_profile=0,_last_rx_profile=0;
  uint32_t n_recv=0,n_recv_errors=0,_last_rx_generation=0;
  float _last_rssi=0,_last_snr=0;
  uint32_t _profile_generation=1,_profile_visit_stamp=0;
  uint32_t _profile_scan_generation[2]={};
  uint16_t _physical_preamble=32;
  bool _params_valid=true,_profile_refresh_required=false,_physical_dual=false;
  int16_t _profile_noise_floor[2]={},_noise_floor=0;
  uint16_t _profile_floor_samples[2]={},_num_floor_samples=0;
  int32_t _profile_floor_sum[2]={},_floor_sample_sum=0;
  bool packet=false,busy=false,warm=false;
  unsigned applies=0,failApplies=0,failRx=0,ends=0;
  bool lastEnd=false;
  RadioLibWrapper() {
    radio_interrupt.claim();
    _physical_params.freq=909.5f;_physical_params.bw=62.5f;
    _physical_params.sf=7;_physical_params.cr=5;
    _profiles.primary=_physical_params;chip.physical=_physical_params;
  }
  uint16_t profilePreamble(uint8_t p) const { return _profiles.preamble(p,32); }
  bool validateProfile(const mesh::RadioProfileParams& p) const { return mesh::RadioProfiles::valid(p); }
  bool isChipBusy() { return busy; }
  bool isReceivingPacket() { return packet; }
  void beginProfileRetune(bool) {}
  void endProfileRetune(bool success) { ++ends;lastEnd=success; }
  void setProfileStandbyWarm(bool enabled) { warm=enabled; }
  void saveNoiseFloor(uint8_t) {}
  void loadNoiseFloor(uint8_t) {}
  bool startRecv() {
    elapsed_us+=50;
    if(failRx) { --failRx;_state=STATE_IDLE;return false; }
    _state=STATE_RX;return true;
  }
  bool applyParams(float f,float bw,uint8_t sf,uint8_t cr) {
    ++applies;elapsed_us+=100;
    chip.physical.freq=f;chip.physical.bw=bw;
    chip.physical.sf=sf;chip.physical.cr=cr;
    if(failApplies) { --failApplies;return false; }
    return true;
  }
  @PRODUCTION@
};
void second(RadioLibWrapper& r) {
  mesh::RadioProfileConfig p;
  p.mode=mesh::RadioProfileMode::RxTx;p.params=r._profiles.primary;
  p.params.freq=910.5f;r._profiles.setSecondary(p,false);
}
using Result=mesh::RadioParamApplyResult;
int main() {
  for(int reason=0;reason<5;++reason) {
    RadioLibWrapper r;second(r);
    if(reason==0)radio_interrupt.record();
    if(reason==1)r.packet=true;
    if(reason==2)r.busy=true;
    if(reason==3)r._state=STATE_TX_WAIT;
    if(reason==4)r._state=STATE_TX_DONE;
    assert(r.tuneProfile(1)==Result::BUSY);
    assert(r.applies==0 && r._active_profile==0 && r._profile_generation==1);
    radio_interrupt.claim();
  }
  {
    RadioLibWrapper r;second(r);r.chip.injectInterrupt=true;
    assert(r.tuneProfile(1)==Result::BUSY);
    assert(radio_interrupt.pending() && r.applies==0 && r._active_profile==0);
    // Old FIFO ownership survives a completion delivered by standby.
    assert(r._state==STATE_RX);radio_interrupt.claim();
  }
  for(int failure=0;failure<3;++failure) {
    RadioLibWrapper r;second(r);const auto previous=r._physical_params;
    if(failure==0)r.failApplies=1;
    if(failure==1)r.failRx=1;
    if(failure==2)r.failApplies=2;
    assert(r.tuneProfile(1)==Result::FAILED);
    assert(r._active_profile==0 && r._profile_generation==1);
    assert(r._physical_params==previous && r.chip.physical==previous);
    assert(r._physical_preamble==32 && r.applies==2 && !r.lastEnd);
    assert(r._profile_refresh_required==(failure==2));
    if(failure!=2)assert(r._state==STATE_RX);
    else {
      r.packet=true;radio_interrupt.record();uint8_t packet[8]={};
      const auto before=r.applies;
      assert(r.recvRaw(packet,sizeof(packet))==0);
      assert(r.chip.reads==0 && r.chip.lengthQueries==0 && r.applies==before);
      r.serviceProfiles(); // Repair ignores stale flags from the unknown tuple.
      assert(!r._profile_refresh_required && !radio_interrupt.pending() && r._state==STATE_RX);
      r.packet=false;radio_interrupt.record();
      assert(r.recvRaw(packet,sizeof(packet))==4 && r.chip.reads==1);
      assert(r._last_rx_profile==r._active_profile && r._last_rx_generation==r._profile_generation);
      assert(r._last_rssi==-102 && r._last_snr==4.5);
    }
  }
  {
    RadioLibWrapper r;second(r);r.chip.standbyResult=-7;
    assert(r.tuneProfile(1)==Result::FAILED);
    assert(r.applies==0 && r._profile_refresh_required && r._state==STATE_IDLE);
    r.chip.standbyResult=0;
    assert(r.tuneProfile(0)==Result::APPLIED && !r._profile_refresh_required);
  }
  {
    RadioLibWrapper r;second(r);assert(r.applies==0); // config does not retune
    assert(r.tuneProfile(1)==Result::APPLIED && r._active_profile==1);
    const auto secondary=r._physical_params;
    auto requested=r._profiles.primary;requested.freq=915.5f;requested.preamble=64;
    const auto original=r._profiles.primary;const auto generation=r._profiles.generation[0];
    r.failRx=1;
    assert(r.trySetPrimaryParams(requested,true,nullptr)==Result::FAILED);
    assert(r._profiles.primary==original && !r._profiles.primary_temporary);
    assert(r._profiles.generation[0]==generation && r._active_profile==1);
    assert(r._physical_params==secondary && r._state==STATE_RX);
    assert(r.trySetPrimaryParams(requested,true,nullptr)==Result::APPLIED);
    assert(r._profiles.primary==requested && r._profiles.primary_temporary);
    assert(r._profiles.generation[0]==generation+1 && r._active_profile==0);
    assert(r._profile_generation==r._profiles.generation[0]);
    requested.sf=2;const auto applies=r.applies;
    assert(r.trySetPrimaryParams(requested,true,nullptr)==Result::FAILED && r.applies==applies);
  }
  {
    RadioLibWrapper r;second(r);r.serviceProfiles();assert(r.warm);
    for(unsigned i=0;i<12;++i) {
      const auto before=r._active_profile;
      elapsed_us+=r._profiles.listenUs(before,r.profilePreamble(r._profiles.slowerProfile()))+1;
      r.serviceProfiles();assert(r._active_profile==(before^1));
    }
    assert(r._profiles.switchTestReady() && r._profiles.switchBudgetUs()>=150);
    auto off=r._profiles.secondary;off.mode=mesh::RadioProfileMode::Off;
    r._profiles.setSecondary(off,false);radio_interrupt.record();
    const auto before=r._active_profile; r.serviceProfiles();assert(r._active_profile==before);
    radio_interrupt.claim();r.serviceProfiles();assert(r._active_profile==0 && !r.warm);
  }
}
'''
        self.compile_run(harness.replace("@PRODUCTION@", production).replace(
            "#include <RadioProfiles.h>", '#include "'+str(ROOT / "src/RadioProfiles.h").replace("\\", "/")+'"').replace(
            "#include <helpers/radiolib/RadioInterruptEvents.h>", '#include "'+str(ROOT / "src/helpers/radiolib/RadioInterruptEvents.h").replace("\\", "/")+'"'))
