#pragma once

#include "NmeaGpsDriver.h"

class AirohaGpsDriver : public NmeaGpsDriver {
    bool waitFor(const char* prefix, uint32_t timeout_ms) { // TODO: Refactor so it doesn't block the loop !
        char sentence[100];
        size_t length = 0;
        const size_t prefixLength = strlen(prefix);
        const uint32_t deadline = millis() + timeout_ms;

        while ((int32_t)(millis() - deadline) < 0) {
            if (available() <= 0) {
                yield();
                continue;
            }

            const char c = (char)read();
#ifdef GPS_NMEA_DEBUG
            Serial.print(c);
#endif
            if (c == '$') length = 0;
            if (length < sizeof(sentence) - 1) {
                sentence[length++] = c;
                sentence[length] = '\0';
                if (length >= prefixLength && strncmp(sentence, prefix, prefixLength) == 0) {
                    return true;
                }
            }
        }
        return false;
    }

    void enterBackupSleep() {
        for (uint8_t attempt = 0; attempt < 3; attempt++) {
            drain();
            sendNmeaSentence("$PAIR650,0");
            if (waitFor("$PAIR001,650,0", 50)) {
#ifdef GPS_NMEA_DEBUG
                Serial.printf("Airoha RTC backup sleep accepted after %u attempts\r\n", attempt + 1);
#endif
                waitFor("$PAIR650,0", 50);
                return;
            }
        }
    }

public:
    AirohaGpsDriver(UART_TYPE& serial)
        : NmeaGpsDriver(serial, -1, -1, -1, -1, nullptr, HIGH, LOW, GPS_BAUD_RATE) {} 

    void initialize() {
        NmeaGpsDriver::begin();
        reset();
    }

    void begin() override {
        NmeaGpsDriver::begin();
        digitalWrite(GPS_EN, HIGH);
        delay(10);
        digitalWrite(GPS_RTC_INT, HIGH);
        delay(5);
        digitalWrite(GPS_RTC_INT, LOW);
    }

    void reset() override {
        digitalWrite(GPS_RESET, HIGH);
        delay(10);
        digitalWrite(GPS_RESET, LOW);
    }

    void stop() override {
        digitalWrite(GPS_VRTC_EN, HIGH);
        digitalWrite(GPS_RTC_INT, LOW);
        enterBackupSleep();
        digitalWrite(GPS_EN, LOW);
        NmeaGpsDriver::stop();
    }

    bool isActive() const override {
        return digitalRead(GPS_EN) == HIGH;
    }
};
