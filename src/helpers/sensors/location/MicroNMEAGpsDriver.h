#pragma once

#include "GpsDriver.h"
#include <MicroNMEA.h>
#include <RTClib.h>
#include <helpers/RefCountedDigitalPin.h>

#ifndef GPS_EN
    #ifdef PIN_GPS_EN
        #define GPS_EN PIN_GPS_EN
    #else
        #define GPS_EN (-1)
    #endif
#endif

#ifndef PIN_GPS_EN_ACTIVE
    #define PIN_GPS_EN_ACTIVE HIGH
#endif

#ifndef GPS_RESET
    #ifdef PIN_GPS_RESET
        #define GPS_RESET PIN_GPS_RESET
    #else
        #define GPS_RESET (-1)
    #endif
#endif

#ifndef GPS_RESET_FORCE
    #ifdef PIN_GPS_RESET_ACTIVE
        #define GPS_RESET_FORCE PIN_GPS_RESET_ACTIVE
    #else
        #define GPS_RESET_FORCE LOW
    #endif
#endif

class MicroNMEAGpsDriver : public GpsDriver {
    char _nmeaBuffer[100];
    MicroNMEA nmea;
    Stream* _gps_serial;
    RefCountedDigitalPin* _peripher_power;
    int _pin_reset;
    int _pin_en;

public :
    MicroNMEAGpsDriver(Stream& ser, int pin_reset = GPS_RESET, int pin_en = GPS_EN,RefCountedDigitalPin* peripher_power=NULL) :
    _gps_serial(&ser), nmea(_nmeaBuffer, sizeof(_nmeaBuffer)), _pin_reset(pin_reset), _pin_en(pin_en), _peripher_power(peripher_power) {
    }

    bool init() override {
        Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX);
        #ifdef GPS_BAUD_RATE
        Serial1.begin(GPS_BAUD_RATE);
        #else
        Serial1.begin(9600);
        #endif

        if (_pin_reset != -1) {
            pinMode(_pin_reset, OUTPUT);
            digitalWrite(_pin_reset, GPS_RESET_FORCE);
        }
        if (_pin_en != -1) {
            pinMode(_pin_en, OUTPUT);
            digitalWrite(_pin_en, LOW);
        }

        begin();
        reset();

        #ifndef PIN_GPS_EN
        MESH_DEBUG_PRINTLN("No GPS wake/reset pin found for this board. Continuing on...");
        #endif

        delay(1000);

        bool detected = (Serial1.available() > 0);

        if (detected) {
            MESH_DEBUG_PRINTLN("GPS detected on Serial1");
        } else {
            MESH_DEBUG_PRINTLN("No GPS detected on Serial1");
            stop();
        }

        return detected;
    }

    void begin() override {
        if (_peripher_power) _peripher_power->claim();
        if (_pin_en != -1) {
            digitalWrite(_pin_en, PIN_GPS_EN_ACTIVE);
        }
        if (_pin_reset != -1) {
            digitalWrite(_pin_reset, !GPS_RESET_FORCE);
        }
    }

    void reset() override {
        if (_pin_reset != -1) {
            digitalWrite(_pin_reset, GPS_RESET_FORCE);
            delay(10);
            digitalWrite(_pin_reset, !GPS_RESET_FORCE);
        }
    }

    void stop() override {
        if (_pin_en != -1) {
            digitalWrite(_pin_en, !PIN_GPS_EN_ACTIVE);
        }
        if (_peripher_power) _peripher_power->release();  
    }

    bool isEnabled() override {
        // directly read the enable pin if present as gps can be
        // activated/deactivated outside of here ...
        if (_pin_en != -1) {
            return digitalRead(_pin_en) == PIN_GPS_EN_ACTIVE;
        } else {
            return true; // no enable so must be active
        }
    }

    long getLatitude() override { return nmea.getLatitude(); }
    long getLongitude() override { return nmea.getLongitude(); }
    long getAltitude() override {
        long alt = 0;
        nmea.getAltitude(alt);
        return alt;
    }
    long satellitesCount() override { return nmea.getNumSatellites(); }

    long getTimestamp() override {
        DateTime dt(nmea.getYear(), nmea.getMonth(),nmea.getDay(),nmea.getHour(),nmea.getMinute(),nmea.getSecond());
        return dt.unixtime();
    }

    void sendSentence(const char *sentence) override {
        nmea.sendSentence(*_gps_serial, sentence);
    }

    // Sync - drain serial buffer and parse NMEA sentences
    void sync() override {
        while (_gps_serial->available()) {
            char c = _gps_serial->read();
            #ifdef GPS_NMEA_DEBUG
            Serial.print(c);
            #endif
            nmea.process(c);
        }
    }
};
