#pragma once

#include "LocationProvider.h"
#include <MicroNMEA.h>
#include <RTClib.h>

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

#ifndef GPS_STANDBY
    #ifdef PIN_GPS_STANDBY
        #define GPS_STANDBY PIN_GPS_STANDBY
    #else
        #define GPS_STANDBY (-1)
    #endif
#endif

#ifndef GPS_STANDBY_ACTIVE
    #define GPS_STANDBY_ACTIVE LOW
#endif

#ifdef GPS_BAUD_RATE
  #define GPS_BAUD GPS_BAUD_RATE
#else
  #define GPS_BAUD 9600
#endif
#ifdef PIN_GPS_RX
  #define GPS_RX PIN_GPS_RX
#else
  #define GPS_RX (-1)
#endif
#ifdef PIN_GPS_TX
  #define GPS_TX PIN_GPS_TX
#else
  #define GPS_TX (-1)
#endif

class MicroNMEALocationProvider : public LocationProvider {
    char _nmeaBuffer[100];
    MicroNMEA nmea;
    int _pin_reset;
    int _pin_en;
    int _pin_standby;
    int _baud_rate;
    int _pin_rx;
    int _pin_tx;
    bool _enabled = false;

protected:
    HardwareSerial* _gps_serial;

public :
    MicroNMEALocationProvider(HardwareSerial& ser, mesh::RTCClock* clock = NULL, int pin_reset = GPS_RESET, int pin_en = GPS_EN,
        RefCountedDigitalPin* peripher_power = NULL, int pin_standby = GPS_STANDBY,
        int baud_rate = GPS_BAUD, int pin_rx = GPS_RX, int pin_tx = GPS_TX) :
    LocationProvider(clock, peripher_power),
    _gps_serial(&ser), nmea(_nmeaBuffer, sizeof(_nmeaBuffer)), _pin_reset(pin_reset), _pin_en(pin_en),
    _baud_rate(baud_rate), _pin_rx(pin_rx), _pin_tx(pin_tx), _pin_standby(pin_standby) {
        if (_pin_reset != -1) {
            pinMode(_pin_reset, OUTPUT);
            digitalWrite(_pin_reset, GPS_RESET_FORCE);  // hold in reset
        }
        if (_pin_en != -1) {
            pinMode(_pin_en, OUTPUT);
            digitalWrite(_pin_en, LOW);
        }
        if (_pin_standby != -1) {
            pinMode(_pin_standby, OUTPUT);
            digitalWrite(_pin_standby, !GPS_STANDBY_ACTIVE);  // wake state
        }
    }

    void enable() {
        if (_enabled) return;
        powerOn();
        if (_pin_reset != -1) {
            digitalWrite(_pin_reset, !GPS_RESET_FORCE);  // release from reset
        }
        if (_pin_en != -1) {
            MESH_DEBUG_PRINTLN("GPS: enable");
            digitalWrite(_pin_en, PIN_GPS_EN_ACTIVE);
        }
        _enabled = true;
    }

    void disable() {
        if (!_enabled) return;
        if (_pin_en != -1) {
            MESH_DEBUG_PRINTLN("GPS: disable");
            digitalWrite(_pin_en, !PIN_GPS_EN_ACTIVE);
        }
        if (_pin_reset != -1) {
            digitalWrite(_pin_reset, GPS_RESET_FORCE);  // hold in reset
        }
        _enabled = false;
        powerOff();
    }

    void reset() override {
        if (_pin_reset != -1) {
            MESH_DEBUG_PRINTLN("GPS: reset");
            digitalWrite(_pin_reset, GPS_RESET_FORCE);
            delay(10);
            digitalWrite(_pin_reset, !GPS_RESET_FORCE);
        }
    }

    // Wake from hardware standby
    void wakeup() {
        if (_pin_standby != -1) {
            MESH_DEBUG_PRINTLN("GPS: wakeup from standby");
            digitalWrite(_pin_standby, !GPS_STANDBY_ACTIVE);
        }
    }

    // Initialize serial port with platform-specific pin configuration
    void beginSerial() {
        // PIN_GPS_RX/TX are named from GPS module perspective:
        // _pin_tx = GPS TX output = MCU RX pin, _pin_rx = GPS RX input = MCU TX pin
        #if defined(NRF52_PLATFORM)
        if (_pin_rx >= 0 && _pin_tx >= 0)
            ((Uart*)_gps_serial)->setPins(_pin_tx, _pin_rx);
        #elif defined(ESP32_PLATFORM)
        if (_pin_rx >= 0 && _pin_tx >= 0)
            _gps_serial->setPins(_pin_tx, _pin_rx);
        #endif
        _gps_serial->begin(_baud_rate);
    }

    bool detect() override {
        beginSerial();

        #ifdef ENV_SKIP_GPS_DETECT
        MESH_DEBUG_PRINTLN("GPS detection skipped");
        return true;
        #endif

        // Power on and reset GPS to check if it's physically connected
        enable();
        reset();
        delay(1000);
        bool gps_detected = _gps_serial->available() > 0;

        if (gps_detected) {
            MESH_DEBUG_PRINTLN("Serial GPS detected");
        } else {
            MESH_DEBUG_PRINTLN("No Serial GPS detected");
        }
        disable();
        return gps_detected;
    }

    void begin() override {
        if (!_enabled) {
            MESH_DEBUG_PRINTLN("GPS: begin (cold start)");
            enable();
            wakeup();  // ensure standby pin is in wake state before boot
            reset();
            delay(1000);  // wait for boot banner to complete
            while (_gps_serial->available()) {  // drain boot messages
                char c = _gps_serial->read();
                #ifdef GPS_NMEA_DEBUG
                Serial.print(c);
                #endif
            }
            configure();
        } else {
            MESH_DEBUG_PRINTLN("GPS: begin (wake from standby)");
            wakeup();
        }
        _active = true;
    }

    // Override in subclasses to send chip-specific NMEA/PMTK/UBX config.
    // Called after every begin() (i.e. each power-on), since modules lose
    // volatile config when stop() cuts power.
    virtual void configure() { }

    // True if a dedicated standby pin is available for low-power sleep
    bool supportsStandby() {
        return _pin_standby != -1;
    }

    // Enter hardware standby (preserves ephemeris, keeps power rail)
    void sleep() {
        if (_pin_standby != -1) {
            MESH_DEBUG_PRINTLN("GPS: entering standby");
            digitalWrite(_pin_standby, GPS_STANDBY_ACTIVE);
        }
    }

    void stop() override {
        _active = false;
        if (supportsStandby()) {
            MESH_DEBUG_PRINTLN("GPS: stop (standby)");
            sleep();
        } else {
            MESH_DEBUG_PRINTLN("GPS: stop (power off)");
            disable();
        }
    }

    bool isEnabled() override {
        if (_pin_en != -1) {
            return digitalRead(_pin_en) == PIN_GPS_EN_ACTIVE;
        }
        return _enabled;
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

    long getTimestamp() override {
        DateTime dt(nmea.getYear(), nmea.getMonth(),nmea.getDay(),nmea.getHour(),nmea.getMinute(),nmea.getSecond());
        return dt.unixtime();
    }

    void sendSentence(const char *sentence) {
        nmea.sendSentence(*_gps_serial, sentence);
        delay(50);
    }

protected:
    void pollLocation() override {
        while (_gps_serial->available()) {
            char c = _gps_serial->read();
            #ifdef GPS_NMEA_DEBUG
            Serial.print(c);
            #endif
            nmea.process(c);
        }
    }
};
