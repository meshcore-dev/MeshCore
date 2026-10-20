#include <RadioProfiles.h>
#include <cassert>
#include <cmath>
#include <limits>
#include <cstring>
#include <utility>

using Profiles=mesh::RadioProfiles;
mesh::RadioProfileParams params(unsigned sf,float bw) {
  mesh::RadioProfileParams p; p.freq=909.5;p.sf=sf;p.bw=bw;p.cr=5;return p;
}
static bool originalSafePreamble(const mesh::RadioProfileParams& p, uint16_t symbols) {
  const double symbol = Profiles::symbolUs(p);
  if (symbol <= 0) return false;
  const int de = symbol >= 16000 ? 1 : 0;
  const double payload = 8 + std::ceil((8.0 * 255 - 4 * p.sf
      + (p.sf <= 6 ? 20 : 28) + 16) / (4 * (p.sf - 2 * de))) * 8;
  return (symbols + (p.sf <= 6 ? 6.25 : 4.25) + payload) * symbol * 4 <= UINT32_MAX;
}
static void checkSafePreamble(unsigned sf, float bw, uint16_t symbols) {
  const auto p = params(sf, bw);
  assert(Profiles::safePreamble(p, symbols) == originalSafePreamble(p, symbols));
}
int main() {
  // Sharing the read-only validation table must preserve every supported
  // value and the strict float tolerance on both sides of its boundary.
  for (float bw : {7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f,
                   62.5f, 125.0f, 250.0f, 500.0f, 1000.0f, 812.5f, 1625.0f}) {
    auto p = params(7, bw);
    assert(Profiles::valid(p));
    const float lower = bw - 0.01f, upper = bw + 0.01f;
    for (float inside : {std::nextafter(lower, bw), std::nextafter(upper, bw)}) {
      p.bw = inside;
      assert(std::fabs(inside - bw) < 0.01f && Profiles::valid(p));
    }
    for (float outside : {std::nextafter(lower, -std::numeric_limits<float>::infinity()),
                          std::nextafter(upper, std::numeric_limits<float>::infinity())}) {
      p.bw = outside;
      assert(std::fabs(outside - bw) >= 0.01f && !Profiles::valid(p));
    }
  }
  for (float unsupported : {0.0f, 1.0f, 8.0f, 12.0f, 100.0f, 1500.0f, 2000.0f})
    assert(!Profiles::valid(params(7, unsupported)));
  for (float nonfinite : {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()}) {
    assert(!Profiles::valid(params(7, nonfinite)));
    auto p = params(7, 125);
    p.freq = nonfinite;
    assert(!Profiles::valid(p));
  }
  // Compare the production integer ceiling with the original double formula,
  // including invalid SFs where zero/negative denominators were observable.
  for (unsigned sf = 0; sf < 256; ++sf) {
    const float transition = float(double(1u << (sf & 31)) * 1000 / 16000);
    for (float bw : {0.0f, -0.0f, -1.0f, 7.8f, 62.5f, 125.0f, 500.0f,
                     1000.0f, transition, std::nextafter(transition, 0.0f),
                     std::nextafter(transition, std::numeric_limits<float>::infinity()),
                     std::numeric_limits<float>::min(), std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::denorm_min(),
                     std::numeric_limits<float>::quiet_NaN(),
                     std::numeric_limits<float>::infinity(),
                     -std::numeric_limits<float>::infinity()}) {
      for (uint16_t symbols : {0, 1, 8, 16, 32, 128, 1024, 65528, 65535})
        checkSafePreamble(sf, bw, symbols);
    }
  }
  for (unsigned sf = 0; sf <= 12; ++sf)
    for (float bw : {0.001f, 7.8f, 125.0f})
      for (unsigned symbols = 0; symbols <= UINT16_MAX; ++symbols)
        checkSafePreamble(sf, bw, uint16_t(symbols));
  uint32_t random = 0x89abcdef;
  for (unsigned i = 0; i < 16384; ++i) {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    float bw;
    static_assert(sizeof(bw) == sizeof(random), "Radio bandwidth is a 32-bit float");
    std::memcpy(&bw, &random, sizeof(bw));
    checkSafePreamble((i & 1) ? i % 13 : random & 255, bw, uint16_t(random >> 16));
  }
  assert(Profiles::MinListenSymbols==4.6);
  assert(Profiles::SlowListenSymbols==4.6);
  assert(Profiles::MinFastListenSymbols==4.6);
  assert(Profiles::LoopBudgetUs==0);
  auto a=params(10,125),b=params(10,125);
  auto t=Profiles::calculateChirpTiming(a,b,1,0,1000,4000);
  assert(t.valid && t.slow==0 && t.symbol_us[0]==8192);
  assert(t.listen_us[0]==37684 && t.listen_us[1]==37684);
  assert(t.preamble[0]==32 && t.preamble[1]==32);
  // The function accepts board-specific budgets; a V4 measurement must not
  // become a universal allowance for the GPIO-expander Indicator.
  a=params(6,125);b=a;
  auto fast=Profiles::calculateChirpTiming(a,b,0,0,1000,4000);
  auto indicator=Profiles::calculateChirpTiming(a,b,0,0,9000,4000);
  assert(indicator.preamble[0]>fast.preamble[0]);
  assert(indicator.preamble[1]>fast.preamble[1]);
  assert(fast.listen_us[0]>=4.6*512 && fast.listen_us[1]>=4.6*512);
  b.bw=0;assert(!Profiles::calculateChirpTiming(a,b).valid);
  b.bw=NAN;assert(!Profiles::calculateChirpTiming(a,b).valid);
  b=params(13,125);assert(!Profiles::calculateChirpTiming(a,b).valid);
  b=params(10,1e-30f);assert(Profiles::minimumListenUs(b)==0);
  a=params(7,62.5);b=params(8,500);
  t=Profiles::calculateChirpTiming(a,b);
  assert(t.preamble[1]==40); // Derived from dwell and acquisition, not a pair-specific floor.
  auto swapped=Profiles::calculateChirpTiming(b,a);
  assert(swapped.slow==1 && swapped.preamble[0]==40);
  assert(swapped.preamble[1]==t.preamble[0]);
  for (float bw0: {7.8f,62.5f,125.0f,250.0f,500.0f})
    for (float bw1: {7.8f,62.5f,125.0f,250.0f,500.0f})
      for (unsigned sf0=5;sf0<=12;++sf0) for(unsigned sf1=5;sf1<=12;++sf1) {
        Profiles p;p.primary=params(sf0,bw0);p.secondary.params=params(sf1,bw1);
        p.secondary.mode=mesh::RadioProfileMode::Rx;
        for (unsigned n=0;n<Profiles::SwitchTestSamplesPerDirection;++n) {
          p.sampleSwitch(0,1,545);p.sampleSwitch(1,0,545);
        }
        assert(p.switchBudgetUs()==600);
        auto minimum=Profiles::calculateChirpTiming(p.primary,p.secondary.params,0,0,p.switchBudgetUs());
        assert(minimum.valid);
        for(unsigned i=0;i<2;++i) {
          const double symbols=4.6;
          assert(minimum.listen_us[i]==std::ceil(symbols*minimum.symbol_us[i]));
          assert(minimum.preamble[i]>=32 && std::fmod(minimum.preamble[i],8)==0);
          assert(p.automaticPreamble(i)==minimum.preamble[i]);
        }
        if (!p.automaticPreambleFits()) continue;
        if (p.automaticPreamble(0)>Profiles::MaxAutomaticPreamble
            || p.automaticPreamble(1)>Profiles::MaxAutomaticPreamble) {
          assert(p.preamble(0,32)<=512 && p.preamble(1,32)<=512);
          continue;
        }
        auto actual=p.chirpTiming();
        assert(actual.valid);
        assert(p.listenUs(actual.slow)==Profiles::minimumListenUs(p.params(actual.slow),true));
        assert(p.listenUs(actual.slow^1)>=Profiles::minimumListenUs(p.params(actual.slow^1)));
        const double available=std::floor(p.preamble(actual.slow,32)*actual.symbol_us[actual.slow]/2
            -p.listenUs(actual.slow)-2*p.switchBudgetUs()-Profiles::LoopBudgetUs);
        const uint32_t fast_floor=Profiles::minimumListenUs(p.params(actual.slow^1));
        assert(p.listenUs(actual.slow^1)==(available>fast_floor ? (uint32_t)available : fast_floor));
        assert(2*actual.cycle_us<=p.preamble(actual.slow,32)*actual.symbol_us[actual.slow]+1e-6);
        assert(actual.preamble[0]<=p.preamble(0,32));
        assert(actual.preamble[1]<=p.preamble(1,32));
      }
  Profiles p;p.primary=params(7,62.5);p.secondary.params=params(7,500);
  p.secondary.mode=mesh::RadioProfileMode::Rx;
  assert(Profiles::MaxAutomaticPreamble==512);
  assert(p.preamble(0,32)==512 && p.preamble(1,32)==512 && !p.switchTestReady());
  for (unsigned n=0;n<Profiles::SwitchTestSamplesPerDirection;++n) {
    p.sampleSwitch(0,1,545);p.sampleSwitch(1,0,545);
  }
  assert(p.listenUs(0)==9421 && p.listenUs(1)==22147);
  assert(p.chirpTiming().switch_us==600 && p.chirpTiming().loop_us==0 && p.chirpTiming().preamble[1]==64);
  auto before=p.chirpTiming();const auto wire0=p.preamble(0,32),wire1=p.preamble(1,32);
  p.sampleSwitch(0,1,8428);auto overrun=p.chirpTiming();
  assert(overrun.switch_us==9271 && overrun.preamble[1]>before.preamble[1]);
  assert(p.preamble(0,32)>=wire0 && p.preamble(1,32)>=wire1);
  p.resetSwitchTest();
  for (unsigned n=0;n<Profiles::SwitchTestSamplesPerDirection;++n) {
    p.sampleSwitch(0,1,545);p.sampleSwitch(1,0,545);
  }
  p.primary.preamble=64;
  assert(p.chirpTiming().preamble[0]==64 && p.listenUs(1)>21847);
  p.secondary.params.preamble=32;
  assert(p.preamble(1,32)==32 && p.chirpTiming().preamble[1]>32);
  p.secondary.mode=mesh::RadioProfileMode::Off;
  assert(!p.chirpTiming().valid);
  assert(p.listenUs(0)==9421); // Same minimum helper; single-profile RX does not scan.
  // Exact tested SF8/500 pair, explicit 32 preserved; order is not hard-coded.
  p.primary=params(7,62.5);p.primary.preamble=32;
  p.secondary.params=params(8,500);p.secondary.params.preamble=32;
  p.secondary.mode=mesh::RadioProfileMode::Rx;
  assert(p.automaticPreambleFits() && p.listenUs(0)==9421 && p.listenUs(1)==22147);
  assert(p.preamble(0,32)==32 && p.preamble(1,32)==32);
  assert(p.chirpTiming().preamble[1]==40);
  std::swap(p.primary,p.secondary.params);
  assert(p.slowerProfile()==1 && p.listenUs(1)==9421 && p.listenUs(0)==22147);
  p.secondary.params.preamble=8;
  assert(!p.automaticPreambleFits());
  // With no fixed loop reserve, 27 symbols cannot cover both 4.6-symbol visits.
  p.primary=params(7,500);p.secondary.params=params(7,500);
  p.primary.preamble=27;
  assert(p.listenUs(0)==1178 && p.listenUs(1)>=1178);
  assert(!p.automaticPreambleFits());
  p.primary.preamble=32;
  assert(p.automaticPreambleFits() && p.listenUs(1)==1718);
  // Long requested visits remain visible, not replaced by policy minima.
  t=Profiles::calculateChirpTiming(params(7,62.5),params(8,500),20000,30000);
  assert(t.listen_us[0]==20000 && t.listen_us[1]==30000 && t.cycle_us==50000);
  Profiles measured;
  measured.primary=params(7,500);
  measured.secondary.params=params(7,500);
  measured.secondary.mode=mesh::RadioProfileMode::Rx;
  assert(!measured.switchTestReady() && measured.switchBudgetUs()==0);
  assert(measured.preamble(0,32)==512 && measured.preamble(1,32)==512);
  measured.sampleSwitch(0,0,10000); measured.sampleSwitch(0,1,0);
  assert(measured.switch_test_samples[0]==0);
  for (unsigned n=0;n<Profiles::SwitchTestSamplesPerDirection;++n) {
    measured.sampleSwitch(0,1,531); measured.sampleSwitch(1,0,480);
  }
  assert(measured.switchTestReady() && measured.switchBudgetUs()==585); // ceil(531 * 1.1)
  assert(measured.preamble(0,32)>=32 && measured.preamble(0,32)%8==0);
  assert(measured.preamble(1,32)>=32 && measured.preamble(1,32)%8==0);
  measured.sampleSwitch(0,1,8428);
  assert(measured.switchBudgetUs()==9271);
  assert(measured.automaticPreamble(0)>128);
  assert(measured.preamble(0,32)==168 && measured.chirpTiming().preamble[0]==168);
  // Ratios requiring 129..512 symbols must no longer be clipped at 128.
  Profiles usa_fast;
  usa_fast.primary=params(7,62.5);usa_fast.secondary.params=params(5,500);
  usa_fast.secondary.mode=mesh::RadioProfileMode::RxTx;
  for (unsigned n=0;n<Profiles::SwitchTestSamplesPerDirection;++n) {
    usa_fast.sampleSwitch(0,1,2400);usa_fast.sampleSwitch(1,0,2400);
  }
  assert(usa_fast.automaticPreamble(1)>128 && usa_fast.automaticPreamble(1)<=512);
  assert(usa_fast.preamble(1,32)==usa_fast.automaticPreamble(1));
  assert(usa_fast.preamble(1,32)%8==0);
  // Keep the new cap and uncapped diagnostics independently tested.
  measured.sampleSwitch(0,1,40000);
  assert(measured.automaticPreamble(0)>512 && measured.preamble(0,32)==512);
  assert(measured.chirpTiming().preamble[0]>512);
  measured.secondary.params.preamble=88; // explicit overrides remain unchanged
  assert(measured.preamble(1,32)==88);
  auto replacement=measured.secondary;replacement.params.freq=910.5;
  measured.setSecondary(replacement,true);
  assert(!measured.switchTestReady() && measured.switchBudgetUs()==0);
  assert(measured.preamble(0,32)==512 && measured.preamble(1,32)==88);

  // A T1000-E-sized 977 us hop calibrates to 1075 us, yet SF8/500 no longer
  // inherits the historical 88-symbol floor or a fixed 300 us loop pad.
  Profiles t1000;
  t1000.primary=params(7,62.5);
  t1000.secondary.params=params(8,500);
  t1000.secondary.mode=mesh::RadioProfileMode::RxTx;
  for (unsigned n=0;n<Profiles::SwitchTestSamplesPerDirection;++n) {
    t1000.sampleSwitch(0,1,977);t1000.sampleSwitch(1,0,977);
  }
  assert(t1000.switchBudgetUs()==1075);
  assert(t1000.preamble(0,32)==32 && t1000.preamble(1,32)==40);
  assert(t1000.chirpTiming().loop_us==0 && t1000.chirpTiming().preamble[1]==40);
}
