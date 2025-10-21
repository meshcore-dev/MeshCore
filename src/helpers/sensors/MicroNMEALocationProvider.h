#pragma once

#include "LocationProvider.h"
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

class MicroNMEALocationProvider : public LocationProvider {
    char _nmeaBuffer[100];
    MicroNMEA nmea;
    mesh::RTCClock* _clock;
    Stream* _gps_serial;
    RefCountedDigitalPin* _peripher_power;
    int _pin_reset;
    int _pin_en;
    long next_check = 0;
    long time_valid = 0;
    unsigned long next_auto_sync = 0;
    bool _autosync_enabled = true;
    bool _gps_turned_on_for_sync = false;
    unsigned long gps_sync_timeout = 0;

public :
    MicroNMEALocationProvider(Stream& ser, mesh::RTCClock* clock = NULL, int pin_reset = GPS_RESET, int pin_en = GPS_EN,RefCountedDigitalPin* peripher_power=NULL) :
    _gps_serial(&ser), nmea(_nmeaBuffer, sizeof(_nmeaBuffer)), _pin_reset(pin_reset), _pin_en(pin_en), _clock(clock), _peripher_power(peripher_power) {
        if (_pin_reset != -1) {
            pinMode(_pin_reset, OUTPUT);
            digitalWrite(_pin_reset, GPS_RESET_FORCE);
        }
        if (_pin_en != -1) {
            pinMode(_pin_en, OUTPUT);
            digitalWrite(_pin_en, LOW);
        }
        next_auto_sync = millis() + (48UL * 60 * 60 * 1000);
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
        LocationProvider::stop();
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

    void syncTime() override { nmea.clear(); LocationProvider::syncTime(); }
    long getLatitude() override { return nmea.getLatitude(); }
    long getLongitude() override { return nmea.getLongitude(); }
    long getAltitude() override { 
        long alt = 0;
        nmea.getAltitude(alt);
        return alt;
    }
    long satellitesCount() override { return nmea.getNumSatellites(); }
    bool isValid() override { return nmea.isValid(); }
    
    void setAutosyncEnabled(bool enabled) { _autosync_enabled = enabled; }
    bool isAutosyncEnabled() { return _autosync_enabled; }

    long getTimestamp() override { 
        DateTime dt(nmea.getYear(), nmea.getMonth(),nmea.getDay(),nmea.getHour(),nmea.getMinute(),nmea.getSecond());
        return dt.unixtime();
    } 

    void sendSentence(const char *sentence) override {
        nmea.sendSentence(*_gps_serial, sentence);
    }

    void loop() override {

        while (_gps_serial->available()) {
            char c = _gps_serial->read();
            #ifdef GPS_NMEA_DEBUG
            Serial.print(c);
            #endif
            nmea.process(c);
        }

        if (!isValid()) time_valid = 0;

        if (millis() > next_check) {
            next_check = millis() + 1000;
            
            if (next_auto_sync && (long)(millis() - next_auto_sync) > 0) {
                if (_autosync_enabled) {
                    if (isEnabled()) {
                        _time_sync_needed = true;
                        next_auto_sync = millis() + (48UL * 60 * 60 * 1000);
                        MESH_DEBUG_PRINTLN("Auto GPS Time Sync triggered");
                    } else {
                        if (_pin_en != -1) {
                            digitalWrite(_pin_en, PIN_GPS_EN_ACTIVE);
                            _gps_turned_on_for_sync = true;
                            gps_sync_timeout = millis() + (30 * 1000);
                            MESH_DEBUG_PRINTLN("Auto GPS sync - turning GPS on briefly (30s timeout)");
                            next_auto_sync = millis() + (30 * 1000);
                        } else {
                            _time_sync_needed = true;
                            next_auto_sync = millis() + (48UL * 60 * 60 * 1000);
                            MESH_DEBUG_PRINTLN("Auto GPS Time Sync triggered (no enable pin - assuming GPS always on)");
                        }
                    }
                } else {
                    next_auto_sync = millis() + (1000);
                    MESH_DEBUG_PRINTLN("Auto GPS sync skipped - autosync disabled");
                }
            }
            
            if (_time_sync_needed && time_valid > 2) {
                if (_clock != NULL) {
                    _clock->setCurrentTime(getTimestamp());
                    _time_sync_needed = false;
                    MESH_DEBUG_PRINTLN("GPS time sync completed");
                    
                    if (_gps_turned_on_for_sync && _pin_en != -1) {
                        digitalWrite(_pin_en, !PIN_GPS_EN_ACTIVE);
                        _gps_turned_on_for_sync = false;
                        MESH_DEBUG_PRINTLN("GPS turned off immediately after sync to save power");
                    }
                }
            }
            
            if (gps_sync_timeout && (long)(millis() - gps_sync_timeout) > 0) {
                if (_pin_en != -1) {
                    digitalWrite(_pin_en, !PIN_GPS_EN_ACTIVE);
                    MESH_DEBUG_PRINTLN("GPS sync timeout (30s) - turning GPS off");
                }
                gps_sync_timeout = 0;
                _gps_turned_on_for_sync = false;
                next_auto_sync = millis() + (60 * 60 * 1000);
            }
            
            if (isValid()) {
                time_valid ++;
            }
        }
    }
};
