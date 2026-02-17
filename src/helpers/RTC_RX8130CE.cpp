#include "RTC_RX8130CE.h"
#include "RTClib.h"


bool RTC_RX8130CE::stop(bool stop) {
    write_register(0x1E, stop ? 0x040 : 0x00);
    return true;
}

bool RTC_RX8130CE::begin(TwoWire *wire) {
    if (i2c_dev) {
       delete i2c_dev;
    }

    i2c_dev = new Adafruit_I2CDevice(this->_addr, wire);
    if (!i2c_dev->begin()) {
       return false;
    }

    /* 
     * Digital offset register:
     *   [7]   DET: 0 ->  disabled
     *   [6:0] L7-L1: 0 -> no offset
     */
    write_register(0x30, 0x00);

    /* 
     * Extension Register register:
     *   [7:6]   FSEL: 0 ->  0
     *   [5]     USEL: 0 -> 0
     *   [4]     TE: 0 ->
     *   [3]     WADA: 0 -> 0
     *   [2-0]   TSEL: 0 -> 0
     */
    write_register(0x1C, 0x00);

    /* 
     * Flag Register register:
     *   [7]     VBLF: 0 ->  0
     *   [6]     0: 0 -> 
     *   [5]     UF: 0 -> 
     *   [4]     TF: 0 ->
     *   [3]     AF: 0 -> 0
     *   [2]     RSF: 0 -> 0
     *   [1]     VLF: 0 -> 0
     *   [0]     VBFF: 0 -> 0
     */
    write_register(0x1D, 0x00);

    /* 
     * Control Register0 register:
     *   [7]     TEST: 0 ->  0
     *   [6]     STOP: 0 -> 
     *   [5]     UIE: 0 -> 
     *   [4]     TIE: 0 ->
     *   [3]     AIE: 0 -> 0
     *   [2]     TSTP: 0 -> 0
     *   [1]     TBKON: 0 -> 0
     *   [0]     TBKE: 0 -> 0
     */
    write_register(0x1E, 0x00);

    /* 
     * Control Register1 register:
     *   [7-6]   SMPTSEL: 0 ->  0
     *   [5]     CHGEN: 0 -> 
     *   [4]     INIEN: 0 -> 
     *   [3]     0: 0 ->
     *   [2]     RSVSEL: 0 -> 0
     *   [1-0]   BFVSEL: 0 -> 0
     */
    write_register(0x1F, 0x00);

    this->stop(false); // clear STOP bit

    /* 
     * Function register:
     *   [7]   100TH: 0 ->  disabled
     *   [6:5] Periodic interrupt: 0 -> no periodic interrupt
     *   [4]   RTCM: 0 -> real-time clock mode
     *   [3]   STOPM: 0 -> RTC stop is controlled by STOP bit only
     *   [2:0] Clock output frequency: 000 (Default value)
     */
    write_register(0x28, 0x00); 
    
    // Battery switch register
    write_register(0x26, 0x00); // enable battery switch feature

    return true;
}

bool RTC_RX8130CE::setTime(struct tm *t) {
    uint8_t buf[8];
    buf[0] = 0x10;
    buf[1] = bin2bcd(t->tm_sec) & 0x7F;
    buf[2] = bin2bcd(t->tm_min) & 0x7F;
    buf[3] = bin2bcd(t->tm_hour) & 0x3F;
    buf[4] = bin2bcd(t->tm_wday) & 0x07;
    buf[5] = bin2bcd(t->tm_mday) & 0x3F;
    buf[6] = bin2bcd(t->tm_mon + 1) & 0x1F;
    buf[7] = bin2bcd((t->tm_year - 100));

    this->stop(true);
    i2c_dev->write(buf, sizeof(buf));
    this->stop(false);

    return true;
}

void RTC_RX8130CE::adjust(DateTime dt) {
   struct tm *atv;
   time_t utime;

   utime = (time_t)dt.unixtime();
   atv = gmtime(&utime);

   this->setTime(atv);
}

DateTime RTC_RX8130CE::now() {
   struct tm atv;
   this->getTime(&atv);

   return DateTime((uint32_t)mktime(&atv));
}

uint32_t RTC_RX8130CE::unixtime() {
   struct tm atv;
   this->getTime(&atv);

   return (uint32_t)mktime(&atv);
}

bool RTC_RX8130CE::getTime(struct tm *t) {
    uint8_t buff[7];

    buff[0] = 0x10;

    i2c_dev->write_then_read(buff, 1, buff, 7);

    t->tm_sec =  bcd2bin(buff[0] & 0x7F);
    t->tm_min =  bcd2bin(buff[1] & 0x7F);
    t->tm_hour = bcd2bin(buff[2] & 0x3F);
    t->tm_wday = bcd2bin(buff[3] & 0x07);
    t->tm_mday = bcd2bin(buff[4] & 0x3F);
    t->tm_mon =  bcd2bin(buff[5] & 0x1F) - 1;
    t->tm_year = bcd2bin(buff[6]) + 100;

    return true;
}

bool RTC_RX8130CE::writeRAM(uint8_t address, uint8_t value) {
    return this->writeRAM(address, &value, 1);
}

size_t RTC_RX8130CE::writeRAM(uint8_t address, uint8_t *value, size_t len) {
    uint8_t buf[len + 1];
    
    if (address > 3) {
        return 0;
    }

    if ((address + len) > 3) {
        len = 3 - address;
    }

    buf[0] = 0x20 + address;

    for (int i = 1; i <= len + 1; i++) {
       buf[i] = value[i - 1];
    }

    i2c_dev->write(buf, len + 1);

    return len;
}

bool RTC_RX8130CE::readRAM(uint8_t address, uint8_t *value, size_t len) {
    uint8_t real_address = 0x20 + address;

    if (address > 3) { // Oversize of 64-bytes RAM
        return false;
    }

    if ((address + len) > 3) { // Data size over RAM size
        len = 3 - address;
    }

    i2c_dev->write_then_read(&real_address, 1, value, len);
    return true;
}

uint8_t RTC_RX8130CE::readRAM(uint8_t address) {
    uint8_t value = 0xFF;
    this->readRAM(address, &value, 1);
    return value;
}
