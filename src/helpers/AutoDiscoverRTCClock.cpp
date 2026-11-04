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

// RV3028 registers used to store its configuration in EEPROM (RV-3028-C7 Application Manual, 4.6)
#define RV3028_STATUS      0x0E  // bit 7: EEbusy, bit 5: BSF
#define RV3028_CONTROL1    0x0F  // bit 3: EERD, disables the automatic refresh from EEPROM
#define RV3028_EE_ADDR     0x25
#define RV3028_EE_DATA     0x26
#define RV3028_EE_COMMAND  0x27
#define RV3028_EE_REFRESH  0x12  // copy all configuration EEPROM to RAM
#define RV3028_EE_WRITE    0x21  // write EE_DATA to one EEPROM byte
#define RV3028_EE_READ     0x22  // read one EEPROM byte into EE_DATA
#define RV3028_EE_CLKOUT   0x35
#define RV3028_EE_BACKUP   0x37
#define RV3028_BACKUP_BSM  0x0C  // 37h switchover mode, 00 = disabled

// Configuration MeshCore sets: { register, mask, value }. Bits outside the mask keep the chip's
// own values, notably 37h bit 7, the LSB of the factory frequency calibration.
static const uint8_t rv3028_config[][3] = {
  { RV3028_EE_CLKOUT, 0x80, 0x00 },  // CLKOE = 0: CLKOUT pin off
  // TCE = 1, FEDE = 1 (manual: "should always be set to 1"), BSM = 01 (DSM), TCR = 3 kOhm
  { RV3028_EE_BACKUP, 0x3F, 0x34 },
};
#define RV3028_CONFIG_COUNT (sizeof(rv3028_config) / sizeof(rv3028_config[0]))

// Melopero's readFromRegister() returns 0xFF when the I2C transfer fails, and its writes report
// nothing. A failed read must never be written back, least of all to the EEPROM, so these two
// helpers check every transfer.
static bool rv3028Read(uint8_t reg, uint8_t& val) {
  TwoWire* wire = rtc_rv3028.i2c;
  wire->beginTransmission(RV3028_ADDRESS);
  wire->write(reg);
  if (wire->endTransmission() != 0 || wire->requestFrom((uint8_t)RV3028_ADDRESS, (uint8_t)1) != 1) {
    return false;
  }
  val = wire->read();
  return true;
}

static bool rv3028Write(uint8_t reg, uint8_t val) {
  TwoWire* wire = rtc_rv3028.i2c;
  wire->beginTransmission(RV3028_ADDRESS);
  wire->write(reg);
  wire->write(val);
  return wire->endTransmission() == 0;
}

// Waits for EEbusy to clear: ~66 ms after the RTC powers on, ~16 ms for a byte write. A failed
// read ends the wait at once, rather than polling a dead bus through each transfer's timeout.
static bool rv3028EepromIdle() {
  for (int i = 0; i < 100; i++) {
    uint8_t status;
    if (!rv3028Read(RV3028_STATUS, status)) return false;
    if ((status & 0x80) == 0) return true;
    delay(1);
  }
  return false;
}

// Manual 4.6.7: wait 10 ms after an EEPROM write, 1 ms after a read or a Refresh, before
// checking EEbusy. delay() can return early on some cores: the extra 1 ms, with the bus time of
// the status read that follows, covers it.
static bool rv3028EepromCommand(uint8_t cmd, uint32_t wait_ms) {
  if (!rv3028Write(RV3028_EE_COMMAND, 0x00) || !rv3028Write(RV3028_EE_COMMAND, cmd)) return false;
  delay(wait_ms + 1);
  return rv3028EepromIdle();
}

// One EEPROM byte, read (manual 4.6.6) or written (4.6.5)
static bool rv3028EepromRead(uint8_t reg, uint8_t& val) {
  return rv3028Write(RV3028_EE_ADDR, reg) && rv3028EepromCommand(RV3028_EE_READ, 1)
         && rv3028Read(RV3028_EE_DATA, val);
}

static bool rv3028EepromWrite(uint8_t reg, uint8_t val) {
  return rv3028Write(RV3028_EE_ADDR, reg) && rv3028Write(RV3028_EE_DATA, val)
         && rv3028EepromCommand(RV3028_EE_WRITE, 10);
}

// Stores rv3028_config in the RV3028's EEPROM. Setting only the RAM mirror does not last: while
// EERD = 0 the chip reloads it from EEPROM each day at midnight (manual 4.6.2, 4.6.9), which
// turns the switchover and trickle charger back off. The EEPROM is accessed with the automatic
// refresh held off (EERD = 1, 4.6.7) and the switchover disabled in RAM (3.15.6: BSM must be
// 00 or 10 for any EEPROM read or write). Each byte is compared against the EEPROM itself and
// written only if it differs, so the factory calibration in 36h is never rewritten. A closing
// Refresh reloads RAM from the EEPROM, which restores the switchover. Returns false if the
// configuration was not confirmed stored, or if EERD could not be cleared afterwards.
static bool rv3028StoreConfig() {
  uint8_t ctrl1, backup;
  if (!rv3028Read(RV3028_CONTROL1, ctrl1)) return false;

  // EERD = 1, wait for EEbusy = 0, then disable the switchover while the EEPROM is accessed
  bool ok = rv3028Write(RV3028_CONTROL1, ctrl1 | 0x08) && rv3028EepromIdle()
            && rv3028Read(RV3028_EE_BACKUP, backup);
  bool held = ok;
  ok = ok && rv3028Write(RV3028_EE_BACKUP, backup & ~RV3028_BACKUP_BSM);

  for (size_t i = 0; ok && i < RV3028_CONFIG_COUNT; i++) {
    uint8_t reg = rv3028_config[i][0], mask = rv3028_config[i][1];
    uint8_t val = rv3028_config[i][2] & mask;
    uint8_t old;
    ok = rv3028EepromRead(reg, old);
    if (ok && (old & mask) != val) ok = rv3028EepromWrite(reg, (old & ~mask) | val);
  }

  // Refresh reloads RAM from the EEPROM, which also restores the switchover. The read-back runs
  // even when nothing was written, so every boot confirms that the switchover came back.
  bool refreshed = ok && rv3028EepromCommand(RV3028_EE_REFRESH, 1);
  ok = refreshed;
  for (size_t i = 0; ok && i < RV3028_CONFIG_COUNT; i++) {
    uint8_t mask = rv3028_config[i][1], now;
    ok = rv3028Read(rv3028_config[i][0], now) && (now & mask) == (rv3028_config[i][2] & mask);
  }

  // If the Refresh did not run or finish, put the switchover back once EEbusy reads 0. A command
  // whose write reported failure may still have been latched, so the check waits as long as
  // after a write (4.6.7). While an EEPROM operation may still be running the switchover stays
  // off, as 3.15.6 requires, until a later attempt succeeds; on a part whose EEPROM still holds
  // the factory BSM = 00, a refresh alone does not bring it back.
  if (held && !refreshed) {
    delay(11);
    if (rv3028EepromIdle()) rv3028Write(RV3028_EE_BACKUP, backup);
  }

  // EERD = 0 once it may have been set. Read Control1 again first, since the chip clears TE itself
  // when a single-shot countdown ends, so the earlier copy may be stale. If that read fails,
  // Control1 is left alone and the store reports failure, for a retry to clear EERD if one is
  // left.
  return rv3028Read(RV3028_CONTROL1, ctrl1) && rv3028Write(RV3028_CONTROL1, ctrl1 & ~0x08) && ok;
}

// Sets rv3028_config in the RAM mirror only, which holds until the next refresh. Returns false
// if any register could not be read or written.
static bool rv3028SetRam() {
  bool ok = true;
  for (size_t i = 0; i < RV3028_CONFIG_COUNT; i++) {
    uint8_t reg = rv3028_config[i][0], mask = rv3028_config[i][1];
    uint8_t val = rv3028_config[i][2] & mask;
    uint8_t old;
    ok = rv3028Read(reg, old) && rv3028Write(reg, (old & ~mask) | val) && ok;
  }
  return ok;
}

// Reads the time registers 00h-06h in one transfer: 1 if a register shows a bit that an RV3028
// always reads as 0 (manual 3.2), as an EEPROM at 0x52 would, 0 if none does, -1 if the read
// failed
static int rv3028Check() {
  static const uint8_t zero[7] = { 0x80, 0x80, 0xC0, 0xF8, 0xC0, 0xE0, 0x00 };  // 00h-06h
  TwoWire* wire = rtc_rv3028.i2c;
  wire->beginTransmission(RV3028_ADDRESS);
  wire->write((uint8_t)0x00);
  if (wire->endTransmission() != 0 || wire->requestFrom((uint8_t)RV3028_ADDRESS, (uint8_t)7) != 7) {
    return -1;
  }
  int ruled_out = 0;
  for (uint8_t i = 0; i < 7; i++) {
    if (wire->read() & zero[i]) ruled_out = 1;
  }
  return ruled_out;
}

// 1 if a read of 00h-06h reads like an RV3028, so its config may be written, 0 if two reads
// both show a bit an RV3028 always reads as 0, -1 if that is not settled. A second read is taken
// when the first rules the device out, so one corrupted read cannot hide a real RV3028. Two
// reads that rule it out count only if BSF reads 0: a switchover releases the bus (manual 4.2),
// which reads as 1s. Only those bits are checked, so a device at 0x52 that reads them as 0
// still passes.
static int rv3028Identify() {
  int r = rv3028Check();
  if (r == 1) r = rv3028Check();
  if (r == 0) return 1;
  uint8_t status;
  if (r == 1 && rv3028Read(RV3028_STATUS, status) && (status & 0x20) == 0) return 0;
  return -1;
}

// True if the device stays on VDD across a read of 00h-06h: BSF is cleared, which works only on
// VDD (manual 3.7), the time registers are read, and BSF must still read 0. A switchover in
// between means VDD is unstable, and the EEPROM needs VDD (4.6.8), so the store waits for a
// retry. BSF is cleared first, as a power cut before this boot leaves it set.
static bool rv3028OnVdd() {
  uint8_t status;
  return rv3028Read(RV3028_STATUS, status) && rv3028Write(RV3028_STATUS, status & ~0x20)
         && rv3028Check() == 0 && rv3028Read(RV3028_STATUS, status) && (status & 0x20) == 0;
}

// A failed configuration, or one skipped because a read failed, is retried from
// getCurrentTime(), a few times per boot only: a password-locked chip never succeeds, and every
// attempt writes to it again.
#define RV3028_RETRY_MS  (10UL * 60 * 1000)
#define RV3028_RETRIES   3

static bool rv3028_pending = false;
static uint8_t rv3028_tries = 0;
static unsigned long rv3028_tried;

// Stores the configuration once the device reads like an RV3028, falling back to the RAM mirror
// if that fails. Without the EEPROM store the RAM config lasts only until the next refresh, which
// on a part still holding the factory EEPROM turns the switchover back off. If the RAM fallback
// fails too, or EEbusy never reads 0, the RAM may still hold BSM = 00 from the store, with the
// switchover off until a later attempt succeeds. Either way it is retried while attempts
// remain. An identification that is not settled, or a switchover or failed transfer around the
// boot read, writes no config and is retried the same way.
static void rv3028Configure() {
  rv3028_tried = millis();
  rv3028_tries++;
  int id = rv3028Identify();
  if (id != 1) {
    rv3028_pending = id < 0;
    MESH_DEBUG_PRINTLN("RV3028: %s, config skipped (attempt %d)",
                       id < 0 ? "identification not settled" : "device at 0x52 is not an RV3028",
                       rv3028_tries);
    return;
  }
  if (!rv3028OnVdd()) {
    rv3028_pending = true;
    MESH_DEBUG_PRINTLN("RV3028: switchover or failed transfer around the read, config deferred (attempt %d)",
                       rv3028_tries);
    return;
  }
  rv3028_pending = !rv3028StoreConfig();
  if (rv3028_pending) {
    // The switchover must stay off while an EEPROM operation may still be running (3.15.6)
    bool ram = rv3028EepromIdle() && rv3028SetRam();
    MESH_DEBUG_PRINTLN("RV3028: config not stored in EEPROM, %s in RAM (attempt %d)",
                       ram ? "set" : "NOT set", rv3028_tries);
  }
}

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
    // Store the backup switchover config: Direct Switching Mode (DSM), where the switchover to
    // VBACKUP occurs when VDD < VBACKUP, with the trickle charger on
    rv3028Configure();
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
}

uint32_t AutoDiscoverRTCClock::getCurrentTime() {
  if (ds3231_success) {
    return rtc_3231.now().unixtime();
  }

  if (rv3028_success) {
    if (rv3028_pending && rv3028_tries <= RV3028_RETRIES
        && millis() - rv3028_tried >= RV3028_RETRY_MS) {
      rv3028Configure();
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
