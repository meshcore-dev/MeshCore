"""Execute the production profile clock across RTC and software wraps."""
from pathlib import Path
from radio_profile_test_support import NativeTest
ROOT = Path(__file__).resolve().parents[1]

class ProfileClockTests(NativeTest):
    def test_extended_rtc_reaches_long_visit_and_wraps_32bit_ticks(self):
        source = (ROOT / "src/helpers/radiolib/RadioLibWrappers.cpp").read_text()
        clock = source[source.index("// Extend the 24-bit RTC counter"):
                       source.index("void RadioLibWrapper::begin()")]
        harness = r'''
#include <cassert>
#include <cstdint>
#define NRF52_PLATFORM 1
struct RTC {
  uint32_t TASKS_STOP=0,PRESCALER=0,TASKS_START=0,COUNTER=0;
} rtc;
#define NRF_RTC2 (&rtc)
static uint32_t fake_us=123456;
uint32_t micros() { return fake_us; }
@CLOCK@
void advance(uint32_t ticks) { rtc.COUNTER=(rtc.COUNTER+ticks)&0x00ffffffUL; }
int main() {
  assert(profileTimestamp(false)==fake_us);
  rtc.COUNTER=0x00fedcba; // Nonzero retained counter near hardware wrap.
  const uint32_t visit=profileTimestamp(true);
  assert(visit==0 && rtc.TASKS_START==1 && rtc.PRESCALER==0);
  for(unsigned second=1;second<=532;++second) {
    advance(32768);
    const auto stamp=profileTimestamp(true);
    assert(stamp==second*32768UL);
    assert(profileElapsedUs(visit,stamp,true)==second*1000000UL);
    if(second==512)assert(stamp==0x01000000UL); // Not truncated to zero.
    if(second==531)assert(profileElapsedUs(visit,stamp,true)<532000000UL);
    if(second==532)assert(profileElapsedUs(visit,stamp,true)>=532000000UL);
  }
  // Disabling returns to the ordinary microsecond clock. A fresh enable
  // resets the software origin without clearing the retained hardware count.
  fake_us=987654;assert(profileTimestamp(false)==fake_us && rtc.TASKS_STOP==1);
  const auto retained=rtc.COUNTER;
  assert(profileTimestamp(true)==0 && rtc.COUNTER==retained);
  advance(100);assert(profileElapsedUs(0,profileTimestamp(true),true)==3052);
  profileTimestamp(false);profileTimestamp(true);
  // Sample more often than each 24-bit wrap while advancing to the end of
  // the 32-bit extended clock (131072 seconds at32768Hz).
  for(unsigned chunk=0;chunk<511;++chunk) { advance(256UL*32768);profileTimestamp(true); }
  advance(255UL*32768);const auto before=profileTimestamp(true);
  assert(before==0xffff8000UL);
  advance(32768);const auto wrapped=profileTimestamp(true);assert(wrapped==0);
  assert(profileElapsedUs(before,wrapped,true)==1000000UL);
  advance(32768);assert(profileElapsedUs(before,profileTimestamp(true),true)==2000000UL);
  assert(profileElapsedUs(0,0xffffffffUL,true)==UINT32_MAX); // Saturate long overdue visits.
  // The ordinary microsecond clock retains its existing32-bit wrap behavior.
  assert(profileElapsedUs(0xfffffff0UL,0x10UL,false)==32);
}
'''
        self.compile_run(harness.replace("@CLOCK@", clock))

    def test_non_nrf_clock_retains_microsecond_wrap_behavior(self):
        source = (ROOT / "src/helpers/radiolib/RadioLibWrappers.cpp").read_text()
        clock = source[source.index("// Extend the 24-bit RTC counter"):
                       source.index("void RadioLibWrapper::begin()")]
        harness = r'''
#include <cassert>
#include <cstdint>
static uint32_t fake_us=0;
uint32_t micros() { return fake_us; }
@CLOCK@
int main() {
  fake_us=0xfffffff0UL;const auto before=profileTimestamp(true);
  fake_us=0x10;assert(profileElapsedUs(before,profileTimestamp(true),true)==32);
  assert(profileTimestamp(false)==fake_us);
}
'''
        self.compile_run(harness.replace("@CLOCK@", clock))
