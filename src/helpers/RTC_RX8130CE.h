#ifndef __RTC_RX8130CE_H__
#define __RTC_RX8130CE_H__

#include <Arduino.h>
#include <Wire.h>
#include <time.h>
#include "RTClib.h"

class RTC_RX8130CE : RTC_I2C {
    private:
        const uint8_t _addr = 0x32;

        bool stop(bool stop);

    protected:

    public:
        bool begin(TwoWire *wire);
        bool setTime(struct tm *t);
        bool getTime(struct tm *t);
        void adjust(DateTime t);
        
        DateTime now();
        uint32_t unixtime();

        bool writeRAM(uint8_t address, uint8_t value);
        size_t writeRAM(uint8_t address, uint8_t *value, size_t len);
        bool readRAM(uint8_t address, uint8_t *value, size_t len);
        uint8_t readRAM(uint8_t address);

};

#endif
