"""Exercise production profile-switch ownership, caching and failure paths."""
from pathlib import Path
from radio_profile_test_support import NativeTest, method
ROOT = Path(__file__).resolve().parents[1]

class ModemCacheTests(NativeTest):
    def test_confirmed_modem_requires_owned_warm_rx_and_survives_no_failure(self):
        source = (ROOT / "src/helpers/radiolib/CustomSX1262.h").read_text()
        state = (ROOT / "src/helpers/radiolib/SX1262ProfileSwitchState.h").read_text().replace("#pragma once", "")
        signatures = ("void invalidateProfileCache(", "void setProfileSwitchOptimization(",
                      "int16_t begin(const ConfigLoRa_t&", "int16_t beginFSK(const ConfigFSK_t&",
                      "int16_t beginLRFHSS(const ConfigLRFHSS_t&", "int16_t setModem(",
                      "int16_t setLoRaModulationParams(")
        production = "\n".join(method(source, signature).replace(" override", "") for signature in signatures)
        fixture = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstddef>
@DEFINES@
@STATE@
using std::isfinite;
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_ERR_INVALID_SPREADING_FACTOR -1
#define RADIOLIB_ERR_INVALID_CODING_RATE -2
#define RADIOLIB_ERR_INVALID_BANDWIDTH -3
#define RADIOLIB_ERR_WRONG_MODEM -4
#define RADIOLIB_SX126X_PACKET_TYPE_LORA 1
#define RADIOLIB_SX126X_LORA_BW_7_8 0
#define RADIOLIB_SX126X_LORA_BW_10_4 8
#define RADIOLIB_SX126X_LORA_BW_15_6 1
#define RADIOLIB_SX126X_LORA_BW_20_8 9
#define RADIOLIB_SX126X_LORA_BW_31_25 2
#define RADIOLIB_SX126X_LORA_BW_41_7 10
#define RADIOLIB_SX126X_LORA_BW_62_5 3
#define RADIOLIB_SX126X_LORA_BW_125_0 4
#define RADIOLIB_SX126X_LORA_BW_250_0 5
#define RADIOLIB_SX126X_LORA_BW_500_0 6
struct ConfigLoRa_t {};
struct ConfigFSK_t {};
struct ConfigLRFHSS_t {};
using ModemType_t = int;
struct SX1262 {
  SX1262ProfileSwitchState* cache = nullptr;
  int result = 0, initializations = 0;
  int init() {
    // The capability must be revoked BEFORE any possibly failing base call.
    assert(cache && !cache->rxValid && !cache->active);
    assert(!cache->modulationValid && !cache->packetValid);
    ++initializations;
    return result;
  }
  int16_t begin(const ConfigLoRa_t&) { return init(); }
  virtual int16_t beginFSK(const ConfigFSK_t&) { return init(); }
  virtual int16_t beginLRFHSS(const ConfigLRFHSS_t&) { return init(); }
  int16_t beginFSK(float) { return beginFSK(ConfigFSK_t{}); }
  int16_t beginLRFHSS(float) { return beginLRFHSS(ConfigLRFHSS_t{}); }
  int16_t setModem(ModemType_t) { return init(); }
};
struct Radio : SX1262 {
  SX1262ProfileSwitchState _profileSwitch{true};
  bool _coldStandby = false, _tcxoWakePending = true;
  uint8_t spreadingFactor = 9, bandwidth = 4, codingRate = 3, ldrOptimize = 0;
  float bandwidthKhz = 125;
  bool ldroAuto = true;
  unsigned queries = 0, writes = 0;
  uint8_t packetType = 1;
  int16_t writeResult = 0;
  using SX1262::beginFSK;
  using SX1262::beginLRFHSS;
  Radio() { cache = &_profileSwitch; }
  uint8_t getPacketType() { ++queries; return packetType; }
  int16_t setModulationParams(uint8_t sf, uint8_t bw, uint8_t cr, uint8_t ldro) {
    assert(spreadingFactor == sf && bandwidth == bw && codingRate == cr);
    ldrOptimize = ldroAuto ? ((1U << sf) / bandwidthKhz >= 16.0f) : ldro;
    ++writes;
    return writeResult;
  }
  @PRODUCTION@
};
bool skipsModem(const Radio& r) {
  (void)r;
  return MC_SX1262_FAST_MODEM_CACHE != 0;
}
void selectCache(Radio& r, bool enabled) {
  (void)r; (void)enabled;
}
void prime(Radio& r) {
  // Models the full RX path's successful live LoRa confirmation. The ordinary
  // lifecycle tests separately execute startReceive's real confirmation path.
  r._profileSwitch.end(true);
  r._profileSwitch.rxResult(0, true);
  r._profileSwitch.begin(true, true);
  r._profileSwitch.standbyResult(0);
}
int main() {
  for (int requested = 0; requested < 2; ++requested) {
    Radio r; selectCache(r, requested);
    // No existing RX context: even an opted-in image must perform the query.
    assert(r.setLoRaModulationParams(125, 9, 7) == 0 && r.queries == 1);
    prime(r);
    assert(r.setLoRaModulationParams(125, 9, 7) == 0 && r.writes == 1);
    assert(r.queries == (skipsModem(r) ? 1U : 2U));
    // A changed modulation tuple still writes the complete tuple. Only the
    // confirmed packet-type read may disappear.
    assert(r.setLoRaModulationParams(500, 8, 5) == 0 && r.writes == 2);
    assert(r.queries == (skipsModem(r) ? 1U : 3U));
    // Every ownership gate independently restores the live query.
    for (int gate = 0; gate < 9; ++gate) {
      Radio x; selectCache(x, requested); prime(x);
      switch (gate) {
        case 0: x._profileSwitch.end(true); break;
        case 1: x._profileSwitch.begin(true, true); break; // standby missing
        case 2: x._profileSwitch.begin(false, true); break; // discontinuous RX
        case 3: x._profileSwitch.begin(true, false); break; // cold oscillator
        case 4: x.setProfileSwitchOptimization(false); break;
        case 5: x._profileSwitch.rxResult(-707, true); break;
        case 6: x._profileSwitch.standbyResult(-707); break;
        case 7: x._profileSwitch.end(false); break;
        case 8: x.invalidateProfileCache(); break;
      }
      assert(x.setLoRaModulationParams(125, 9, 7) == 0 && x.queries == 1);
    }
    // Failed modulation must revoke query reuse before the restoration path.
    Radio failed; selectCache(failed, requested); prime(failed);
    failed.writeResult = -707;
    assert(failed.setLoRaModulationParams(500, 8, 5) == -707);
    assert(!failed._profileSwitch.rxValid && failed._profileSwitch.failed());
    const auto before = failed.queries;
    failed.writeResult = 0;
    assert(failed.setLoRaModulationParams(125, 9, 7) == 0);
    assert(failed.queries == before + 1);

    // A live wrong-modem/error-byte observation revokes the WHOLE context.
    // Reopening it without a fresh full RX confirmation cannot restore reuse.
    Radio wrong; selectCache(wrong, requested); prime(wrong);
    wrong._profileSwitch.end(true); wrong.packetType = 255;
    assert(wrong.setLoRaModulationParams(125, 9, 7) == -4 && wrong.queries == 1);
    assert(!wrong._profileSwitch.rxValid && !wrong._profileSwitch.modulationValid);
    wrong._profileSwitch.begin(true, true); wrong._profileSwitch.standbyResult(0);
    wrong.packetType = 1;
    assert(wrong.setLoRaModulationParams(125, 9, 7) == 0 && wrong.queries == 2);
  }
  // Public modem initialization calls invalidate before success AND failure,
  // including inherited float overloads dispatching to the config overrides.
  for (int api = 0; api < 6; ++api) for (int failure = 0; failure < 2; ++failure) {
    Radio r; selectCache(r, true); prime(r);
    r._profileSwitch.modulationResult(0, 9, 4, 3, 0);
    r._profileSwitch.packetResult(0, 32, 1, 255, 0, 0);
    r.result = failure ? -707 : 0;
    int rc = 0;
    switch (api) {
      case 0: rc = r.begin(ConfigLoRa_t{}); break;
      case 1: rc = r.beginFSK(ConfigFSK_t{}); break;
      case 2: rc = r.beginLRFHSS(ConfigLRFHSS_t{}); break;
      case 3: rc = r.setModem(0); break;
      case 4: rc = r.beginFSK(910.0f); break;
      case 5: rc = r.beginLRFHSS(910.0f); break;
    }
    assert(rc == r.result && r.initializations == 1);
    assert(r._coldStandby && !r._tcxoWakePending);
    assert(r.setLoRaModulationParams(125, 9, 7) == 0 && r.queries == 1);
  }
}
'''
        for policy in ("#define MC_SX1262_FAST_MODEM_CACHE 0",
                       "#define MC_SX1262_FAST_MODEM_CACHE 1"):
            with self.subTest(policy=policy):
                self.compile_run(fixture.replace("@DEFINES@", policy)
                                 .replace("@STATE@", state).replace("@PRODUCTION@", production))
