"""Exercise concrete-chip profile limits and LR2021 band/detector handling."""
from pathlib import Path
from radio_profile_test_support import NativeTest, method
ROOT = Path(__file__).resolve().parents[1]

class ChipProfileTests(NativeTest):
    def test_concrete_chip_bandwidth_and_frequency_limits(self):
        source = (ROOT / "src/helpers/radiolib/RadioLibWrappers.cpp").read_text()
        validation = method(source, "bool RadioLibWrapper::validateProfile(").replace("RadioLibWrapper::", "")
        cases = {
            1262: 'p.bw=41.7f;assert(r.validateProfile(p));p.bw=41.67f;assert(!r.validateProfile(p));p.bw=83.34f;assert(!r.validateProfile(p));p.bw=125;p.freq=137;assert(!r.validateProfile(p));',
            1268: 'assert(r.validateProfile(p));p.freq=915;assert(!r.validateProfile(p));',
            1276: 'p.freq=137;assert(r.validateProfile(p));p.sf=6;assert(!r.validateProfile(p));',
            1272: 'p.freq=868;assert(r.validateProfile(p));p.freq=433;assert(!r.validateProfile(p));',
            68: 'p.sf=9;assert(r.validateProfile(p));p.sf=10;assert(!r.validateProfile(p));p.bw=250;assert(r.validateProfile(p));',
            1110: 'assert(r.validateProfile(p));p.bw=83.34f;assert(!r.validateProfile(p));p.bw=203.125f;assert(!r.validateProfile(p));',
            1121: 'p.freq=2450;p.bw=203.125f;assert(r.validateProfile(p));p.freq=2300;assert(!r.validateProfile(p));p.freq=433;assert(!r.validateProfile(p));',
            2021: 'for(float bw:{41.67f,41.7f,83.34f,101.56f,203.125f,812.5f,1000.0f}){p.bw=bw;assert(r.validateProfile(p));}p.freq=2300;assert(!r.validateProfile(p));p.freq=2450;assert(r.validateProfile(p));',
        }
        harness = r'''
#include <cassert>
#include <initializer_list>
#include "@MODEL@"
#define MESH_RADIO_PROFILE_CHIP @CHIP@
struct RadioLibWrapper { @VALIDATE@ };
int main() {
  RadioLibWrapper r;mesh::RadioProfileParams p;
  p.freq=433;p.bw=125;p.sf=7;p.cr=5;
  @CHECKS@
}
'''
        for chip, checks in cases.items():
            with self.subTest(chip=chip):
                self.compile_run(harness.replace("@MODEL@", str(ROOT / "src/RadioProfiles.h").replace("\\", "/"))
                    .replace("@CHIP@", str(chip)).replace("@VALIDATE@", validation).replace("@CHECKS@", checks))

    def test_band_crossing_reselects_pa_with_native_power_limits(self):
        source = (ROOT / "src/helpers/radiolib/CustomLR2021.h").read_text()
        methods = "\n".join(method(source, s).replace(" override", "")
            for s in ("int16_t setOutputPower(int8_t power)", "int16_t setFrequency(float frequency)"))
        harness = r'''
#include <cassert>
#include <cstdint>
#define RADIOLIB_ERR_NONE 0
struct LR2021 {
  float frequency=433;int8_t power=0;unsigned powerWrites=0;
  int16_t frequencyResult=0,powerResult=0;
  int16_t checkOutputPower(int8_t p,int8_t* clipped) {
    const int8_t low=frequency>1500?-19:-9,high=frequency>1500?12:22;
    *clipped=p<low?low:p>high?high:p;return 0;
  }
  int16_t setOutputPower(int8_t p) { ++powerWrites;power=p;return powerResult; }
  int16_t setFrequency(float f) { if(!frequencyResult)frequency=f;return frequencyResult; }
};
struct Radio : LR2021 {
  int8_t _txPower=0;bool _txPowerSet=false;
  @METHODS@
};
int main() {
  Radio r;assert(r.setOutputPower(22)==0 && r.power==22);
  assert(r.setFrequency(2450)==0 && r.power==12 && r._txPower==22);
  assert(r.setFrequency(433)==0 && r.power==22);
  const auto writes=r.powerWrites;r.frequencyResult=-7;
  assert(r.setFrequency(2450)==-7 && r.powerWrites==writes && r.frequency==433);
  r.frequencyResult=0;r.powerResult=-9;
  assert(r.setFrequency(2450)==-9 && r._txPower==22);
}
'''
        self.compile_run(harness.replace("@METHODS@", methods))

    def test_dual_profiles_suspend_detectors_and_restore_ldro(self):
        source = (ROOT / "src/helpers/radiolib/CustomLR2021Wrapper.h").read_text()
        methods = "\n".join(method(source, s) for s in
            ("int16_t applySideDetectorConfig(float bw)", "float calcTsym("))
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include "@MODEL@"
struct LR2021LoRaSideDetector_t { uint8_t sf=0;bool ldro=false,invertIQ=false;uint8_t syncWord=0; };
struct CustomLR2021 {
  size_t count=0;LR2021LoRaSideDetector_t observed[3];int16_t result=0;
  int16_t setSideDetector(const LR2021LoRaSideDetector_t* p,size_t n) {
    count=n;for(size_t i=0;i<n;++i)observed[i]=p[i];return result;
  }
};
struct Wrapper {
  mesh::RadioProfiles _profiles;CustomLR2021 radio;CustomLR2021* _radio=&radio;
  LR2021LoRaSideDetector_t _sideDet[3];size_t _numSideDet=2;
  @METHODS@
};
int main() {
  Wrapper r;r._sideDet[0].sf=10;r._sideDet[1].sf=11;
  assert(r.applySideDetectorConfig(125)==0 && r.radio.count==2);
  assert(!r.radio.observed[0].ldro && r.radio.observed[1].ldro);
  r._profiles.secondary.mode=mesh::RadioProfileMode::Rx;
  assert(r.applySideDetectorConfig(125)==0 && r.radio.count==0);
  assert(r._numSideDet==2 && r._sideDet[1].sf==11);
  r._profiles.secondary.mode=mesh::RadioProfileMode::Off;
  assert(r.applySideDetectorConfig(500)==0 && r.radio.count==2);
  assert(!r.radio.observed[0].ldro && !r.radio.observed[1].ldro);
  r.radio.result=-7;assert(r.applySideDetectorConfig(125)==-7);
}
'''
        self.compile_run(harness.replace("@MODEL@", str(ROOT / "src/RadioProfiles.h").replace("\\", "/"))
            .replace("@METHODS@", methods))

    def test_profile_airtime_keeps_lr_floor_and_high_band_ldro(self):
        source = (ROOT / "src/helpers/radiolib/RadioLibWrappers.cpp").read_text()
        airtime = method(source, "uint32_t RadioLibWrapper::getProfileAirtime(").replace("RadioLibWrapper::", "")
        harness = r'''
#include <cassert>
#include "@MODEL@"
#define MESH_RADIO_PROFILE_CHIP @CHIP@
#define MAX_TRANS_UNIT 255
struct Wrapper {
  mesh::RadioProfiles _profiles;
  bool validateProfile(const mesh::RadioProfileParams& p) const { return mesh::RadioProfiles::valid(p); }
  uint16_t profilePreamble(uint8_t) const { return 32; }
  uint32_t minimumAirtimeMillis() const { return 200; }
  @AIRTIME@
};
int main() {
  Wrapper r;auto& p=r._profiles.primary;p.freq=433;p.bw=500;p.sf=7;p.cr=5;
  assert(r.getProfileAirtime(0,1,0)==200);
  p.freq=2450;p.bw=406.25f;p.sf=11;p.cr=8;
  assert(r.getProfileAirtime(0,200,0)==@EXPECTED@);
}
'''
        for chip, expected in ((1121, 2038), (1110, 1716)):
            with self.subTest(chip=chip):
                self.compile_run(harness.replace("@MODEL@", str(ROOT / "src/RadioProfiles.h").replace("\\", "/"))
                    .replace("@AIRTIME@", airtime).replace("@CHIP@", str(chip)).replace("@EXPECTED@", str(expected)))
