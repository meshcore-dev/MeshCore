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

#if defined(INHERO_MR2)
static bool configureMr2RtcBackup(TwoWire& wire) {
  // The library's EEPROM helpers omit the command delays and I2C error checks.
  auto read = [&wire](uint8_t reg, uint8_t& value) {
    wire.beginTransmission(RV3028_ADDRESS);
    wire.write(reg);
    if (wire.endTransmission(false) != 0 ||
        wire.requestFrom((uint8_t)RV3028_ADDRESS, (uint8_t)1) != 1) return false;
    value = wire.read();
    return true;
  };
  auto write = [&wire](uint8_t reg, uint8_t value) {
    wire.beginTransmission(RV3028_ADDRESS);
    wire.write(reg);
    wire.write(value);
    return wire.endTransmission() == 0;
  };
  auto waitReady = [&read]() {
    const uint32_t start = millis();
    do {
      uint8_t status;
      if (!read(0x0E, status)) return false;
      if ((status & 0x80) == 0) return true; // EEbusy
      delay(1);
    } while ((uint32_t)(millis() - start) < 500);
    return false;
  };
  auto startBackupRead = [&]() {
    return write(0x25, 0x37) && write(0x27, 0x00) && write(0x27, 0x22);
  };

  uint8_t control1;
  if (!read(0x0F, control1)) return false;
  const bool ok = [&]() {
    if (!write(0x0F, control1 | 0x08) || !waitReady()) return false; // EERD
    if (!startBackupRead()) return false;
    delay(1); // RV-3028 manual 4.6.7: wait before checking EEbusy after a read.
    uint8_t stored;
    if (!waitReady() || !read(0x26, stored)) return false;
    // No backup battery: BSM=00, TCE=0, BSIE=0, FEDE=1. Keep EEOffset[0]/TCR.
    const uint8_t desired = (stored & 0x83) | 0x10;
    if (stored != desired) {
      // Write only EEPROM byte 0x37, never issue an Update All command.
      if (!write(0x25, 0x37) || !write(0x26, desired) ||
          !write(0x27, 0x00) || !write(0x27, 0x21)) return false;
      delay(10); // RV-3028 manual 4.6.7: wait before checking EEbusy after a write.
      if (!waitReady() || !startBackupRead()) return false;
      delay(1);
      uint8_t verified;
      if (!waitReady() || !read(0x26, verified) || verified != desired) return false;
    }
    // A single-byte EEPROM write does not update the active RAM mirror.
    uint8_t active;
    return write(0x37, desired) && read(0x37, active) && active == desired;
  }();

  // Re-enable automatic refresh on both success and failure, without recovery.
  const bool released = write(0x0F, control1 & ~0x08);
  uint8_t finalControl1;
  return ok && released && read(0x0F, finalControl1) && (finalControl1 & 0x08) == 0;
}
#endif

bool AutoDiscoverRTCClock::i2c_probe(TwoWire& wire, uint8_t addr) {
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

void AutoDiscoverRTCClock::begin(TwoWire& wire) {
  #if !defined(DISABLE_DS3231_PROBE)
  if (i2c_probe(wire, DS3231_ADDRESS)) {
    ds3231_success = rtc_3231.begin(&wire);
  }
  #endif

  if (i2c_probe(wire, RV3028_ADDRESS)) {
    rtc_rv3028.initI2C(wire);
    rtc_rv3028.writeToRegister(0x35, 0x00);
#if defined(INHERO_MR2)
    // MR2 has no backup battery: VDD and VBACKUP share the 3.3 V supply.
    rv3028_success = configureMr2RtcBackup(wire);
#else
    rtc_rv3028.writeToRegister(0x37, 0xB4); // Direct Switching Mode (DSM): when VDD < VBACKUP, switchover occurs from VDD to VBACKUP
    rv3028_success = true;
#endif
    rtc_rv3028.set24HourMode(); // Set the device to use the 24hour format (default) instead of the 12 hour format
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
