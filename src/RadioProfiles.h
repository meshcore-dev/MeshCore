#pragma once

#include <stdint.h>
#include <stddef.h>
#include <math.h>

namespace mesh {

enum class RadioProfileMode : uint8_t { Off = 0, Rx = 1, RxTx = 2 };
enum class RadioCrossMode : uint8_t { Auto = 0, On = 1, Off = 2 };
enum class RadioParamApplyResult : uint8_t { APPLIED, BUSY, FAILED };

// These are modulation profiles of ONE transceiver, never additional radios.
struct RadioProfileParams {
  float freq = 0;
  float bw = 0;
  uint16_t preamble = 0;  // 0 = automatic, otherwise symbols
  uint8_t sf = 0;
  uint8_t cr = 0;

  bool operator==(const RadioProfileParams& other) const {
    return freq == other.freq && bw == other.bw && sf == other.sf
        && cr == other.cr && preamble == other.preamble;
  }
  bool operator!=(const RadioProfileParams& other) const { return !(*this == other); }
};

struct RadioProfileConfig {
  RadioProfileParams params;
  RadioProfileMode mode = RadioProfileMode::Off;
};

// Portable timing/routing model, also used by the native tests. Hardware access
// remains in RadioLibWrapper; timers and persisted settings remain in the CLI.
class RadioProfiles {
 public:
  // Minimum continuous-RX listen window, measured in LoRa symbols.
  static constexpr double MinListenSymbols = 4.6;
  static constexpr double SlowListenSymbols = MinListenSymbols;
  static constexpr double MinFastListenSymbols = MinListenSymbols;
  static constexpr uint8_t AcquisitionSymbols = 8;
  static constexpr uint16_t MaxPreamble = 65528;
  static constexpr uint16_t MaxAutomaticPreamble = 512;
  // Successful RX-to-RX hops self-test the board-specific switch allowance.
  // Packet receptions can extend a visit. No fixed loop allowance is added to
  // the self-tested, guarded switch time in the idle-scan timing model.
  static constexpr uint32_t LoopBudgetUs = 0;
  static constexpr uint8_t SwitchTestSamplesPerDirection = 4;

  struct ChirpTiming {
    bool valid = false;
    uint8_t slow = 0;
    double symbol_us[2] = {};
    uint32_t listen_us[2] = {};
    double preamble[2] = {}; // Rounded recommendations, not silently capped.
    double cycle_us = 0;
    uint32_t switch_us = 0, loop_us = 0;
  };

  RadioProfileParams primary;
  RadioProfileConfig secondary;
  bool primary_temporary = false;
  bool secondary_temporary = false;
  RadioCrossMode cross = RadioCrossMode::Off;
  uint32_t generation[2] = {1, 1};
  uint32_t switches = 0;
  uint32_t rx_packets[2] = {};
  uint32_t tx_packets[2] = {};
  uint32_t switch_failures = 0;
  uint32_t longest_switch_us = 0;
  uint32_t switch_test_max_us[2] = {};
  uint8_t switch_test_samples[2] = {};

  void resetSwitchTest() {
    switch_test_max_us[0] = switch_test_max_us[1] = 0;
    switch_test_samples[0] = switch_test_samples[1] = 0;
  }
  void sampleSwitch(uint8_t from, uint8_t to, uint32_t elapsed_us) {
    if (!enabled() || from > 1 || to > 1 || from == to || !elapsed_us) return;
    if (switch_test_samples[from] < SwitchTestSamplesPerDirection) ++switch_test_samples[from];
    if (elapsed_us > switch_test_max_us[from]) switch_test_max_us[from] = elapsed_us;
  }
  bool switchTestReady() const {
    return switch_test_samples[0] >= SwitchTestSamplesPerDirection
        && switch_test_samples[1] >= SwitchTestSamplesPerDirection;
  }
  uint32_t switchBudgetUs() const {
    const uint32_t observed = switch_test_max_us[0] > switch_test_max_us[1]
        ? switch_test_max_us[0] : switch_test_max_us[1];
    if (!observed) return 0;
    const uint64_t guarded = (uint64_t(observed) * 11 + 9) / 10;
    return guarded > UINT32_MAX ? UINT32_MAX : uint32_t(guarded);
  }

  bool enabled() const { return secondary.mode != RadioProfileMode::Off; }
  bool canTransmit(uint8_t profile) const {
    return profile == 0 || (profile == 1 && secondary.mode == RadioProfileMode::RxTx);
  }
  bool canCross() const {
    return cross == RadioCrossMode::On || (cross == RadioCrossMode::Auto
        && primary_temporary == secondary_temporary);
  }
  uint8_t transmitMask(uint8_t origin) const {
    if (origin > 1) origin = 0;
    uint8_t result = canTransmit(origin) ? (1U << origin) : 0;
    if (enabled() && canCross() && canTransmit(origin ^ 1)) result |= 1U << (origin ^ 1);
    return result;
  }
  const RadioProfileParams& params(uint8_t profile) const {
    return profile == 1 ? secondary.params : primary;
  }
  static bool valid(const RadioProfileParams& p) {
    if (!isfinite(p.freq) || !isfinite(p.bw) || p.freq < 137 || p.freq > 2500
        || p.sf < 5 || p.sf > 12 || p.cr < 5 || p.cr > 8
        || (p.preamble != 0 && (p.preamble < 8 || p.preamble > MaxPreamble))) return false;
    static constexpr float bandwidths[] = {7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f,
                               41.67f, 62.5f, 83.34f, 101.56f, 125, 250, 500, 1000, 203.125f, 406.25f, 812.5f, 1625};
    for (float bw : bandwidths) if (fabsf(p.bw - bw) < 0.01f) return true;
    return false;
  }
  static double symbolUs(const RadioProfileParams& p) {
    return p.bw > 0 && p.sf <= 12 ? (double)(1U << p.sf) * 1000.0 / p.bw : 0;
  }
#if defined(STM32_PLATFORM) && defined(__GNUC__) && !defined(__clang__)
  // Share the complete validation on flash-constrained STM32 targets.
  __attribute__((noinline, noclone))
#endif
  static bool safePreamble(const RadioProfileParams& p, uint16_t symbols) {
    const double symbol = symbolUs(p);
    if (symbol <= 0) return false;
    const int de = symbol >= 16000 ? 1 : 0;
    const int denominator = 4 * (p.sf - 2 * de);
    // The payload expression is integral apart from this small rational
    // ceiling. Preserve even invalid-SF behavior: a zero denominator was
    // infinite (and failed the limit), while signed division rounds a
    // negative quotient toward zero, exactly its mathematical ceiling.
    if (!denominator) return false;
    const int numerator = 8 * 255 - 4 * p.sf + (p.sf <= 6 ? 20 : 28) + 16;
    const double payload = 8 + (denominator > 0
        ? (numerator + denominator - 1) / denominator : numerator / denominator) * 8;
    // RadioLib's microsecond airtime calculation multiplies by four before
    // dividing. Reject settings that overflow that intermediate for a full
    // packet, including a retry using coding rate 4/8.
    return (symbols + (p.sf <= 6 ? 6.25 : 4.25) + payload) * symbol * 4 <= UINT32_MAX;
  }
  uint8_t slowerProfile() const {
    return symbolUs(primary) >= symbolUs(secondary.params) ? 0 : 1;
  }
  static double roundPreamble(double symbols) {
    return ceil((symbols < 32 ? 32 : symbols) / 8.0) * 8.0;
  }
  static uint32_t minimumListenUs(const RadioProfileParams& p, bool slow = false) {
    const double symbol = symbolUs(p);
    const double symbols = slow ? SlowListenSymbols : MinFastListenSymbols;
    return isfinite(symbol) && symbol > 0 && symbol <= UINT32_MAX / symbols
        ? (uint32_t)ceil(symbols * symbol) : 0;
  }
  static double preambleForVisits(const RadioProfileParams& p, bool slow,
      double own_us, double other_us, double overhead_us) {
    const double symbol = symbolUs(p);
    if (symbol <= 0) return 32;
    return slow ? roundPreamble(2.0 * (own_us + other_us + overhead_us) / symbol)
        : roundPreamble((other_us + overhead_us) / symbol + 2 * AcquisitionSymbols);
  }
  // Reserve two return opportunities on the slow profile and enough preamble
  // for blind time plus acquisition on the faster profile. Packet activity or
  // a delayed main loop can extend a visit, so idle timing is not a loss guarantee.
  static ChirpTiming calculateChirpTiming(const RadioProfileParams& a, const RadioProfileParams& b,
      uint32_t visit_a_us = 0, uint32_t visit_b_us = 0,
      uint32_t switch_us = 0, uint32_t loop_us = LoopBudgetUs) {
    ChirpTiming t;
    if (!valid(a) || !valid(b)) return t;
    t.symbol_us[0] = symbolUs(a); t.symbol_us[1] = symbolUs(b);
    t.slow = t.symbol_us[0] >= t.symbol_us[1] ? 0 : 1;
    const uint32_t minimum_a = minimumListenUs(a, t.slow == 0);
    const uint32_t minimum_b = minimumListenUs(b, t.slow == 1);
    t.listen_us[0] = visit_a_us > minimum_a ? visit_a_us : minimum_a;
    t.listen_us[1] = visit_b_us > minimum_b ? visit_b_us : minimum_b;
    t.switch_us = switch_us; t.loop_us = loop_us;
    const double overhead = 2.0 * switch_us + loop_us;
    t.cycle_us = double(t.listen_us[0]) + t.listen_us[1] + overhead;
    t.preamble[0] = preambleForVisits(a, t.slow == 0, t.listen_us[0], t.listen_us[1], overhead);
    t.preamble[1] = preambleForVisits(b, t.slow == 1, t.listen_us[1], t.listen_us[0], overhead);
    t.valid = true;
    return t;
  }
  double automaticPreamble(uint8_t profile) const {
    if (profile > 1) return 32;
    // Keep the hot retune path to one profile's calculation; the full
    // validated diagnostic structure is only needed by CLI/reporting.
    const uint8_t slow = slowerProfile();
    return preambleForVisits(params(profile), profile == slow,
        minimumListenUs(params(profile), profile == slow), minimumListenUs(params(profile ^ 1), (profile ^ 1) == slow),
        2.0 * switchBudgetUs() + LoopBudgetUs);
  }
  uint32_t listenUs(uint8_t profile, uint16_t slow_preamble = 0) const {
    const uint8_t slow = slowerProfile();
    const double minimum = minimumListenUs(params(profile), enabled() && profile == slow);
    if (!enabled() || profile == slow) return (uint32_t)ceil(minimum);
    if (!slow_preamble) slow_preamble = preamble(slow, 32);
    const double available = double(slow_preamble) / 2.0 * symbolUs(params(slow))
        - minimumListenUs(params(slow), true) - 2.0 * switchBudgetUs() - LoopBudgetUs;
    // Rejected previews must not cause an out-of-range floating-to-int cast.
    return available > UINT32_MAX ? UINT32_MAX
        : available > minimum ? (uint32_t)floor(available) : (uint32_t)minimum;
  }
  ChirpTiming chirpTiming() const {
    if (!enabled()) return {};
    return calculateChirpTiming(primary, secondary.params, listenUs(0), listenUs(1), switchBudgetUs());
  }
  uint16_t preamble(uint8_t profile, uint16_t single_profile_default) const {
    const auto& p = params(profile);
    if (p.preamble) return p.preamble;
    if (!enabled()) return single_profile_default;
    // Do not expose an unmeasured short preamble while the startup test runs.
    if (!switchTestReady()) return MaxAutomaticPreamble;
    double symbols = automaticPreamble(profile);
    if (symbols < single_profile_default) symbols = single_profile_default;
    symbols = roundPreamble(symbols);
    if (symbols > MaxAutomaticPreamble) symbols = MaxAutomaticPreamble;
    return (uint16_t)symbols;
  }
  bool automaticPreambleFits() const {
    for (uint8_t i = 0; i < (enabled() ? 2 : 1); ++i) {
      if (!valid(params(i)) || !safePreamble(params(i), preamble(i, 32))) return false;
    }
    // Recommendations above the automatic cap remain visible, but the profile can
    // still run at the cap when its physical preamble is legal.
    if (enabled()) {
      const uint8_t slow = slowerProfile();
      const double available = double(preamble(slow, 32)) / 2.0 * symbolUs(params(slow))
          - minimumListenUs(params(slow), true) - 2.0 * switchBudgetUs() - LoopBudgetUs;
      if (available < minimumListenUs(params(slow ^ 1))
          && params(slow).preamble) return false;
    }
    return true;
  }
  void setPrimary(const RadioProfileParams& p, bool temporary) {
    if (primary != p || primary_temporary != temporary) { ++generation[0]; resetSwitchTest(); }
    primary = p;
    primary_temporary = temporary;
  }
  void setSecondary(const RadioProfileConfig& p, bool temporary) {
    if (secondary.params != p.params || secondary.mode != p.mode
        || secondary_temporary != temporary) { ++generation[1]; resetSwitchTest(); }
    secondary = p;
    secondary_temporary = temporary;
  }
};

}  // namespace mesh
