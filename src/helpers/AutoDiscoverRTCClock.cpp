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
#define RV3028_REG_YEAR  0x06
#define RV3028_YEAR_UNSET  0xA0  // not BCD, so no time write ever leaves it
#define RV3028_REG_STATUS  0x0E
#define RV3028_STATUS_BSF  0x20  // backup switchover flag
#define RV3028_REG_CONTROL2  0x10
#define RV3028_CONTROL2_12_24  0x02  // set: Hours counts 1-12 with an AM/PM bit
#define RV3028_CONTROL2_RESET  0x01  // always reads 0

static TwoWire* rv3028_wire = NULL;
static bool rv3028_good = false;   // rv3028_good_time holds an accepted or set time
static uint32_t rv3028_good_time;
static unsigned long rv3028_good_millis;  // millis() at rv3028_good_time
static bool rv3028_hold = false;   // a time write is not confirmed, so the RTC is not read
static unsigned long rv3028_hold_millis;  // millis() at the last write attempt
static uint8_t rv3028_hold_tries;   // write attempts since the time was set

// An unconfirmed write is retried this often, up to this many attempts in all,
// so a bus that is down cannot stall every caller of getCurrentTime()
#define RV3028_RETRY_MILLIS  5000
#define RV3028_MAX_TRIES  13

static inline uint8_t bcd_to_dec(uint8_t bcd) {
  return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F));
}

static inline uint8_t dec_to_bcd(uint8_t dec) {
  return (uint8_t)(((dec / 10) << 4) | (dec % 10));
}

// A corrupt register can hold a non-BCD nibble that still decodes into a
// plausible range (0x0A decodes to 10), so check the nibbles themselves.
static inline bool is_bcd(uint8_t b) {
  return (b & 0x0F) <= 9 && (b >> 4) <= 9;
}

// Reads Status (0Eh) and Control 2 (10h) in one access, from 0Eh to 10h.
// Control 2's RESET bit always reads 0 (manual p. 24) and Control 2 is the
// last byte, so a read cut by an interface reset, which returns 1s, shows it
// set. Such a read fails, so it never drives a write back.
static bool rv3028_read_status(uint8_t& status, uint8_t& control2) {
  TwoWire& wire = *rv3028_wire;
  wire.beginTransmission(RV3028_ADDRESS);
  wire.write((uint8_t)RV3028_REG_STATUS);
  if (wire.endTransmission(false) != 0 || wire.requestFrom((uint8_t)RV3028_ADDRESS, (uint8_t)3) != 3) {
    return false;
  }
  status = wire.read();
  wire.read();  // Control 1
  control2 = wire.read();
  return (control2 & RV3028_CONTROL2_RESET) == 0;
}

static bool rv3028_write_regs(uint8_t reg, const uint8_t* data, uint8_t n) {
  TwoWire& wire = *rv3028_wire;
  wire.beginTransmission(RV3028_ADDRESS);
  wire.write(reg);
  wire.write(data, n);
  return wire.endTransmission() == 0;
}

static inline bool rv3028_unsettled(uint8_t status, uint8_t control2) {
  return (status & RV3028_STATUS_BSF) || (control2 & RV3028_CONTROL2_12_24);
}

// Clears BSF if it is set, which works only on VDD (manual 3.7, p. 22), and
// selects 24 hour mode if 12 hour mode is set: in 12 hour mode the Hours
// register holds an AM/PM bit, so PM 1 (21h) would decode as 21:00. Clearing
// 12_24 converts the Hours register itself (02h, p. 15). A Status flag is
// kept until a 0 is written to it (p. 23), and writing 1 leaves it as it is
// (tested on two RV-3028-C7), so BSF is cleared by writing 0 to it alone: no
// other flag is cleared, even one set since Status was read. Control 2's other
// bits are written back as read.
static bool rv3028_settle(uint8_t status, uint8_t control2) {
  if (status & RV3028_STATUS_BSF) {
    const uint8_t cleared = (uint8_t)~RV3028_STATUS_BSF;
    if (!rv3028_write_regs(RV3028_REG_STATUS, &cleared, 1)) return false;
  }
  if (control2 & RV3028_CONTROL2_12_24) {
    const uint8_t cleared = control2 & ~RV3028_CONTROL2_12_24;
    if (!rv3028_write_regs(RV3028_REG_CONTROL2, &cleared, 1)) return false;
  }
  return true;
}

// The time runs on from the last accepted or set time. The reference moves
// forward with each call, so millis() wrapping after 49.7 days cannot step it
// back.
static uint32_t rv3028_run_on() {
  const unsigned long secs = (millis() - rv3028_good_millis) / 1000;
  rv3028_good_time += secs;
  rv3028_good_millis += secs * 1000;
  return rv3028_good_time;
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
// A switchover to VBACKUP during the read garbles it: the chip disables and
// resets its I2C interface (manual 4.2, p. 45; 5.10, p. 95), so the rest of
// the read comes back as 1s, and a byte can still decode to a valid but larger
// value (year 0x26 read as 0x27). So the read is used only if the backup
// switchover flag BSF reads 0 after it. A set BSF is cleared, which is possible
// only on VDD (3.7, p. 22), and the read is rejected. BSF may also be left set
// by an earlier power cut, so the caller reads once more if it was cleared.
// 12 hour mode is checked in the same access and handled the same way, before
// the fields are validated, since most PM hours do not decode as valid hours.
//
// Returns 0 with unix_time set, 1 if the read was rejected for a set BSF or
// 12 hour mode that is now cleared, 2 if either could not be cleared, -2 if
// the clock is not set (year A0h, or counted on from it), or -1 if the
// transfer failed or the fields are not sane.
static int rv3028_read_clock(uint32_t& unix_time) {
  if (rv3028_wire == NULL) return -1;
  TwoWire& wire = *rv3028_wire;

  wire.beginTransmission(RV3028_ADDRESS);
  wire.write((uint8_t)RV3028_REG_SECONDS);
  if (wire.endTransmission(false) != 0) return -1;  // no stop where the core honours it

  if (wire.requestFrom((uint8_t)RV3028_ADDRESS, (uint8_t)RV3028_NUM_CLOCK_REGS)
        != RV3028_NUM_CLOCK_REGS) {
    return -1;
  }

  uint8_t regs[RV3028_NUM_CLOCK_REGS];
  for (uint8_t i = 0; i < RV3028_NUM_CLOCK_REGS; i++) {
    regs[i] = wire.read();
  }

  uint8_t status, control2;
  if (!rv3028_read_status(status, control2)) return -1;
  if (rv3028_unsettled(status, control2)) return rv3028_settle(status, control2) ? 1 : 2;

  const uint8_t secs  = regs[0] & 0x7F;
  const uint8_t mins  = regs[1] & 0x7F;
  const uint8_t hours = regs[2] & 0x3F;   // 24 hour mode, checked above
  // regs[3] is weekday, which DateTime derives itself
  const uint8_t date  = regs[4] & 0x3F;
  const uint8_t month = regs[5] & 0x1F;
  const uint8_t year  = regs[6];

  // A0h is the mark a time write sets before its burst, so a write that was
  // cut reads as a clock not yet set. Each new year counts the mark on (A1h,
  // ..., A9h, B0h), never to a BCD year and never to FFh, which is what a read
  // garbled by an interface reset returns. Year 00 is 2000, a valid year.
  if (year >= RV3028_YEAR_UNSET && year != 0xFF) return -2;
  if (!is_bcd(secs) || !is_bcd(mins) || !is_bcd(hours)
      || !is_bcd(date) || !is_bcd(month) || !is_bcd(year)) {
    return -1;
  }
  // DateTime indexes its month table without checking, so the month is
  // range-checked before isValid() decodes it
  if (bcd_to_dec(month) < 1 || bcd_to_dec(month) > 12) return -1;

  DateTime dt(2000 + bcd_to_dec(year), bcd_to_dec(month), bcd_to_dec(date),
              bcd_to_dec(hours), bcd_to_dec(mins), bcd_to_dec(secs));

  // isValid() round-trips through unixtime(), so it rejects out of range
  // fields and impossible dates such as 31 February in one check.
  if (!dt.isValid()) return -1;

  unix_time = dt.unixtime();
  return 0;
}

// Writes the clock registers, bracketed like a read: BSF is cleared and 24 hour
// mode selected first, and both must still hold after the write. A switchover
// during a write resets the interface (manual 4.2, p. 45), as the bus timeout
// does (4.5.1, p. 53). Tested on a bus timeout, each byte acked before the
// reset is already stored and the rest keep their old values: a mixed time
// that reads back as valid. So the year is first marked with A0h, which is not
// BCD and which the chip keeps while the clock counts, and the time then goes
// in one access from Seconds to Year, as 4.5 (p. 52) requires. Year is the
// last byte of that burst, so a write cut anywhere in it leaves the mark,
// which reads as a clock not yet set.
// The chip counts years 00-99 only, and 2100 would encode as A0h, the mark, so
// a time outside 2000-2099 is never written: it is not confirmed, and the
// clock runs on from it without the RTC.
// Returns true only if the write is confirmed, trying twice.
static bool rv3028_write_time(uint32_t time) {
  if (time < 946684800UL || time >= 4102444800UL) return false;  // 2000-01-01 to 2100-01-01
  DateTime dt(time);
  const uint8_t regs[RV3028_NUM_CLOCK_REGS] = {
    dec_to_bcd(dt.second()), dec_to_bcd(dt.minute()), dec_to_bcd(dt.hour()), dt.dayOfTheWeek(),
    dec_to_bcd(dt.day()), dec_to_bcd(dt.month()), dec_to_bcd(dt.year() - 2000)
  };
  for (int attempt = 0; attempt < 2; attempt++) {
    uint8_t status, control2;
    const uint8_t unset_year = RV3028_YEAR_UNSET;
    if (rv3028_read_status(status, control2) && rv3028_settle(status, control2)
        && rv3028_write_regs(RV3028_REG_YEAR, &unset_year, 1)
        && rv3028_write_regs(RV3028_REG_SECONDS, regs, RV3028_NUM_CLOCK_REGS)
        && rv3028_read_status(status, control2) && !rv3028_unsettled(status, control2)) {
      return true;
    }
  }
  return false;
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
    // 24 hour mode is selected, with checks, by each time read and write
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
    int r = -1;
    if (!rv3028_hold) {
      uint32_t unix_time;
      r = rv3028_read_clock(unix_time);
      if (r == 1) r = rv3028_read_clock(unix_time);  // BSF or 12 hour mode was cleared
      if (r == 0) {
        rv3028_good = true;
        rv3028_good_time = unix_time;
        rv3028_good_millis = millis();
        return unix_time;
      }
    }

    // A rejected read is never used: the time runs on from the last accepted
    // or set one, or, before there is one, comes from the fallback clock.
    // Reaching here means a time write is not yet confirmed, the clock is not
    // set, or the read's transfer errored, came up short, its decoded fields
    // failed validation, or BSF or 12 hour mode was set, not that the core
    // ignored the no-stop flag: where it is ignored, endTransmission() still
    // reports success and the read proceeds. Those failures tend to persist,
    // and getCurrentTime() runs on every received packet, so the first one is
    // reported once; a clock not yet set is expected and not reported.
    static bool read_failure_logged = false;
    if (!rv3028_hold && r != -2 && !read_failure_logged) {
      read_failure_logged = true;
      MESH_DEBUG_PRINTLN("RV3028: time read rejected (%d)", r);
    }
    if (!rv3028_good) return _fallback->getCurrentTime();
    const uint32_t now = rv3028_run_on();
    // An unconfirmed write may have left a mixed time in the chip, so the
    // write is repeated until one is confirmed, every RV3028_RETRY_MILLIS and
    // at most RV3028_MAX_TRIES times. If none is, this boot runs on without
    // the RTC.
    if (rv3028_hold && rv3028_hold_tries < RV3028_MAX_TRIES
        && millis() - rv3028_hold_millis >= RV3028_RETRY_MILLIS) {
      rv3028_hold_millis = millis();
      rv3028_hold_tries++;
      if (rv3028_write_time(now)) rv3028_hold = false;
      else if (rv3028_hold_tries == RV3028_MAX_TRIES) MESH_DEBUG_PRINTLN("RV3028: time write not confirmed, giving up");
    }
    return now;
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
    // Until a write is confirmed the RTC is not read: this boot runs on from
    // the time being set, and getCurrentTime() repeats the write
    rv3028_hold = !rv3028_write_time(time);
    rv3028_hold_millis = millis();
    rv3028_hold_tries = 1;
    if (rv3028_hold) MESH_DEBUG_PRINTLN("RV3028: time write not confirmed, will retry");
    rv3028_good = true;
    rv3028_good_time = time;
    rv3028_good_millis = millis();
  } else if (rtc_8563_success) {
    rtc_8563.adjust(DateTime(time));
  } else if (rtc_8130_success) {
    MESH_DEBUG_PRINTLN("RX8130CE: Setting time");
    rtc_8130.adjust(DateTime(time));
  } else {
    _fallback->setCurrentTime(time);
  }
}
