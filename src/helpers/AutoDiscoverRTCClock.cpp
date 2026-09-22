#include "AutoDiscoverRTCClock.h"
#include "RTClib.h"
#include <Melopero_RV3028.h>
#include "RTC_RX8130CE.h"

static RTC_DS3231 rtc_3231;
static bool ds3231_success = false;

static Melopero_RV3028 rtc_rv3028;
static bool rv3028_success = false;

static RTC_PCF8563 rtc_8563;
static bool rtc_8563_success = false;

static RTC_RX8130CE rtc_8130;
static bool rtc_8130_success = false;

#define DS3231_ADDRESS   0x68
#define RV3028_ADDRESS   0x52
#define PCF8563_ADDRESS  0x51
#define RX8130CE_ADDRESS 0x32

// RV-3028-C7 clock registers, contiguous from 0x00
#define RV3028_REG_SECONDS  0x00
#define RV3028_NUM_CLOCK_REGS  7

static TwoWire* rv3028_wire = NULL;

static inline uint8_t bcd_to_dec(uint8_t bcd) {
  return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F));
}

// A corrupt register can hold a non-BCD nibble that still decodes into a
// plausible range (0x0A decodes to 10), so check the nibbles themselves.
static inline bool is_bcd(uint8_t b) {
  return (b & 0x0F) <= 9 && (b >> 4) <= 9;
}

// Read the RV-3028 clock registers in one burst.
//
// The Melopero getters read one register per transaction, so reading the time
// field by field lets the counters roll over mid-read. Reading most-significant
// first, an hour boundary crossed between the hour and minute reads yields a
// timestamp a full hour in the past (e.g. 15:59:59 -> 16:00:00 reads back as
// 15:00:00); minute and day boundaries misread in the same way.
//
// The fix is the single seven byte read below: those bytes come from one
// uninterrupted transaction, so the counters cannot advance part way through.
// Ending the pointer write without a stop additionally avoids releasing the
// bus, but is not what makes the read coherent, and not every core honours it
// (the STM32 core ignores the flag unless I2C_OTHER_FRAME or USE_HALV2_DRIVER
// is defined). Where it is ignored this degrades to stop-then-start, which is
// what readFromRegister() has always done against this part.
//
// Returns false if the transfer fails or the fields are not sane, leaving the
// caller to fall back to the field-by-field path.
static bool rv3028_read_clock(uint32_t& unix_time) {
  if (rv3028_wire == NULL) return false;
  TwoWire& wire = *rv3028_wire;

  wire.beginTransmission(RV3028_ADDRESS);
  wire.write((uint8_t)RV3028_REG_SECONDS);
  if (wire.endTransmission(false) != 0) return false;  // no stop where the core honours it

  if (wire.requestFrom((uint8_t)RV3028_ADDRESS, (uint8_t)RV3028_NUM_CLOCK_REGS)
        != RV3028_NUM_CLOCK_REGS) {
    return false;
  }

  uint8_t regs[RV3028_NUM_CLOCK_REGS];
  for (uint8_t i = 0; i < RV3028_NUM_CLOCK_REGS; i++) {
    regs[i] = wire.read();
  }

  const uint8_t secs  = regs[0] & 0x7F;
  const uint8_t mins  = regs[1] & 0x7F;
  const uint8_t hours = regs[2] & 0x3F;   // begin() selects 24 hour mode
  // regs[3] is weekday, which DateTime derives itself
  const uint8_t date  = regs[4] & 0x3F;
  const uint8_t month = regs[5] & 0x1F;
  const uint8_t year  = regs[6];

  if (!is_bcd(secs) || !is_bcd(mins) || !is_bcd(hours)
      || !is_bcd(date) || !is_bcd(month) || !is_bcd(year)) {
    return false;
  }

  DateTime dt(2000 + bcd_to_dec(year), bcd_to_dec(month), bcd_to_dec(date),
              bcd_to_dec(hours), bcd_to_dec(mins), bcd_to_dec(secs));

  // isValid() round-trips through unixtime(), so it rejects out of range
  // fields and impossible dates such as 31 February in one check.
  if (!dt.isValid()) return false;

  unix_time = dt.unixtime();
  return true;
}

bool AutoDiscoverRTCClock::i2c_probe(TwoWire& wire, uint8_t addr) {
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

void AutoDiscoverRTCClock::begin(TwoWire& wire) {
  #if !defined(DISABLE_DS3231_PROBE)
  if (i2c_probe(wire, DS3231_ADDRESS)) {
    MESH_DEBUG_PRINTLN("DS3231: Found");
    ds3231_success = rtc_3231.begin(&wire);
  }
  #endif

  if (i2c_probe(wire, RV3028_ADDRESS)) {
    MESH_DEBUG_PRINTLN("RV3028: Found");
    rv3028_wire = &wire;
    rtc_rv3028.initI2C(wire);
    rtc_rv3028.writeToRegister(0x35, 0x00);
    rtc_rv3028.writeToRegister(0x37, 0xB4); // Direct Switching Mode (DSM): when VDD < VBACKUP, switchover occurs from VDD to VBACKUP
    rtc_rv3028.set24HourMode(); // Set the device to use the 24hour format (default) instead of the 12 hour format
    rv3028_success = true;
  }

  if (i2c_probe(wire, PCF8563_ADDRESS)) {
    MESH_DEBUG_PRINTLN("PCF8563: Found");
    rtc_8563_success = rtc_8563.begin(&wire);
  }

  if (i2c_probe(wire, RX8130CE_ADDRESS)) {
    MESH_DEBUG_PRINTLN("RX8130CE: Found");
    rtc_8130.begin(&wire);
    rtc_8130_success = true;
    MESH_DEBUG_PRINTLN("RX8130CE: Initialized");
  }

  MESH_DEBUG_PRINTLN("RTC: using %s",
    ds3231_success   ? "DS3231" :
    rv3028_success   ? "RV3028" :
    rtc_8563_success ? "PCF8563" :
    rtc_8130_success ? "RX8130CE" : "none (falling back to volatile clock)");
}

uint32_t AutoDiscoverRTCClock::getCurrentTime() {
  if (ds3231_success) {
    return rtc_3231.now().unixtime();
  }

  if (rv3028_success) {
    uint32_t unix_time;
    if (rv3028_read_clock(unix_time)) return unix_time;

    // Reaching here means the transfer errored, came up short, or the decoded
    // fields failed validation, not that the core ignored the no-stop flag:
    // where it is ignored, endTransmission() still reports success and the
    // read proceeds. Those causes tend to persist, and getCurrentTime() runs
    // on every received packet, so report the fallback once.
    static bool burst_failure_logged = false;
    if (!burst_failure_logged) {
      burst_failure_logged = true;
      MESH_DEBUG_PRINTLN("RV3028: burst read failed, reading fields individually");
    }
    return DateTime(
        rtc_rv3028.getYear(),
        rtc_rv3028.getMonth(),
        rtc_rv3028.getDate(),
        rtc_rv3028.getHour(),
        rtc_rv3028.getMinute(),
        rtc_rv3028.getSecond()
    ).unixtime();
  }

  if (rtc_8563_success) {
    return rtc_8563.now().unixtime();
  }

  if (rtc_8130_success) {
    MESH_DEBUG_PRINTLN("RX8130CE: Reading time");
    return rtc_8130.now().unixtime();
  }

  return _fallback->getCurrentTime();
}

void AutoDiscoverRTCClock::setCurrentTime(uint32_t time) { 
  if (ds3231_success) {
    rtc_3231.adjust(DateTime(time));
  } else if (rv3028_success) {
    auto dt = DateTime(time);
	  uint8_t weekday = (dt.day() + (uint16_t)((2.6 * dt.month()) - 0.2) - (2 * (dt.year() / 100)) + dt.year() + (uint16_t)(dt.year() / 4) + (uint16_t)(dt.year() / 400)) % 7;
    rtc_rv3028.setTime(dt.year(), dt.month(), weekday, dt.day(), dt.hour(), dt.minute(), dt.second());
  } else if (rtc_8563_success) {
    rtc_8563.adjust(DateTime(time));
  } else if (rtc_8130_success) {
    MESH_DEBUG_PRINTLN("RX8130CE: Setting time");
    rtc_8130.adjust(DateTime(time));
  } else {
    _fallback->setCurrentTime(time);
  }
}
