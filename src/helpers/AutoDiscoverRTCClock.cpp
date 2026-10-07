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

bool AutoDiscoverRTCClock::i2c_probe(TwoWire& wire, uint8_t addr) {
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

// An ACK only says that something answers at an RTC's address: an IMU at 0x68 or an EEPROM in
// 0x50-0x57 answers too, and would then be read as the clock and written on every time sync.
// So an RTC is adopted only if its time registers read like that chip's, per its datasheet:
//  - bits documented as always 0 must read 0 (the PCF8563 documents none), and the seven
//    registers must not all read 0xFF, as an erased EEPROM does;
//  - seconds, minutes, date and month must be valid BCD in range, unless the chip's power-loss
//    flag is set: each of these chips sets it at power-up, when its time may be undefined
//    (not checked for the RX8130CE, see its entry below).
// The year is not checked, as MeshCore can write an out-of-range one from a bad epoch. This
// cannot catch every device: one whose bytes happen to fit is still adopted, as before.
struct RtcId {
  uint8_t time_reg;   // seconds register; the seven time registers follow it
  uint8_t zero[7];    // bits that always read 0
  uint8_t date_idx;   // index of the date register within the seven
  uint8_t flag_reg;   // power-loss flag register and bit; flag_bit 0 skips the field check
  uint8_t flag_bit;
};

#if !defined(DISABLE_DS3231_PROBE)
// DS3231 (Maxim 19-5170 Rev 10): Figure 1, p. 11; OSF in Status (0Fh) bit 7, p. 14. 00h bit 7
// is left out: a DS1307 at 0x68, which this code also drives, powers up with its clock-halt bit
// there set (Maxim DS1307 Rev 3/15, Table 2 and text, p. 8). Its other bits match.
static const RtcId DS3231_ID =
  { 0x00, { 0x00, 0x80, 0x80, 0xF8, 0xC0, 0x60, 0x00 }, 4, 0x0F, 0x80 };
#endif
// RV3028 (RV-3028-C7 App Manual Rev 1.4): 3.2, p. 12; PORF in Status (0Eh) bit 0, 3.7, p. 22
static const RtcId RV3028_ID =
  { 0x00, { 0x80, 0x80, 0xC0, 0xF8, 0xC0, 0xE0, 0x00 }, 4, 0x0E, 0x01 };
// PCF8563 (NXP Rev 11.1): unused bits are "not relevant", not 0, Table 4, p. 10; VL in
// VL_seconds (02h) bit 7, Table 8, p. 13, and set at power-up, Table 27, p. 24
static const RtcId PCF8563_ID = { 0x02, { 0 }, 3, 0x02, 0x80 };
// RX8130CE (Epson ETM50E-10): read value always 0, 13.2.1, p. 22. No field check: its power-loss
// flag VLF (1Dh bit 1) cannot vouch for the fields, as RTC_RX8130CE::begin() clears it on every
// boot without setting the time.
static const RtcId RX8130CE_ID =
  { 0x10, { 0x80, 0x80, 0xC0, 0x80, 0xC0, 0xE0, 0x00 }, 4, 0x1D, 0x00 };

// Reads n registers from reg, with a repeated start as every one of these datasheets documents
static bool rtcRead(TwoWire& wire, uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n) {
  wire.beginTransmission(addr);
  wire.write(reg);
  if (wire.endTransmission(false) != 0) return false;
  if (wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = wire.read();
  return true;
}

static bool bcdInRange(uint8_t v, uint8_t lo, uint8_t hi) {
  if ((v & 0x0F) > 9 || (v >> 4) > 9) return false;
  uint8_t d = (v >> 4) * 10 + (v & 0x0F);
  return d >= lo && d <= hi;
}

// 1 if this read rules the device out, 0 if not, -1 if a read failed
static int rtcCheck(TwoWire& wire, uint8_t addr, const RtcId& id) {
  uint8_t t[7];
  if (!rtcRead(wire, addr, id.time_reg, t, 7)) return -1;
  bool all_ff = true;
  for (int i = 0; i < 7; i++) {
    if (t[i] & id.zero[i]) return 1;
    all_ff = all_ff && t[i] == 0xFF;
  }
  if (all_ff) return 1;  // an erased EEPROM
  if (id.flag_bit == 0) return 0;
  if (bcdInRange(t[0] & 0x7F, 0, 59) && bcdInRange(t[1] & 0x7F, 0, 59)
      && bcdInRange(t[id.date_idx] & 0x3F, 1, 31) && bcdInRange(t[5] & 0x1F, 1, 12)) {
    return 0;
  }
  uint8_t flag;
  if (!rtcRead(wire, addr, id.flag_reg, &flag, 1)) return -1;
  return (flag & id.flag_bit) ? 0 : 1;  // power was lost, so the time is undefined
}

// True if two reads both rule the device out. A failed read proves nothing, so the device is
// adopted as before, and one corrupted read cannot hide a real RTC.
static bool rtcRuledOut(TwoWire& wire, uint8_t addr, const RtcId& id) {
  if (rtcCheck(wire, addr, id) != 1 || rtcCheck(wire, addr, id) != 1) return false;
  MESH_DEBUG_PRINTLN("RTC: device at 0x%02X does not read like the RTC expected there", addr);
  return true;
}

void AutoDiscoverRTCClock::begin(TwoWire& wire) {
  #if !defined(DISABLE_DS3231_PROBE)
  if (i2c_probe(wire, DS3231_ADDRESS)
      && !rtcRuledOut(wire, DS3231_ADDRESS, DS3231_ID)) {
    ds3231_success = rtc_3231.begin(&wire);
  }
  #endif

  if (i2c_probe(wire, RV3028_ADDRESS)
      && !rtcRuledOut(wire, RV3028_ADDRESS, RV3028_ID)) {
    rtc_rv3028.initI2C(wire);
    rtc_rv3028.writeToRegister(0x35, 0x00);
    rtc_rv3028.writeToRegister(0x37, 0xB4); // Direct Switching Mode (DSM): when VDD < VBACKUP, switchover occurs from VDD to VBACKUP
    rtc_rv3028.set24HourMode(); // Set the device to use the 24hour format (default) instead of the 12 hour format
    rv3028_success = true;
  }

  if (i2c_probe(wire, PCF8563_ADDRESS)
      && !rtcRuledOut(wire, PCF8563_ADDRESS, PCF8563_ID)) {
    MESH_DEBUG_PRINTLN("PCF8563: Found");
    rtc_8563_success = rtc_8563.begin(&wire);
  }

  if (i2c_probe(wire, RX8130CE_ADDRESS)
      && !rtcRuledOut(wire, RX8130CE_ADDRESS, RX8130CE_ID)) {
    MESH_DEBUG_PRINTLN("RX8130CE: Found");
    rtc_8130.begin(&wire);
    rtc_8130_success = true;
    MESH_DEBUG_PRINTLN("RX8130CE: Initialized");
  }
}

uint32_t AutoDiscoverRTCClock::getCurrentTime() {
  if (ds3231_success) {
    return rtc_3231.now().unixtime();
  }

  if (rv3028_success) {
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
