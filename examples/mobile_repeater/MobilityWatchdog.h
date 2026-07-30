#pragma once
#include <Arduino.h>
#include <math.h>
#include "target.h"

#ifndef STATIONARY_SECS
#define STATIONARY_SECS    600
#endif
#ifndef MOVE_THRESHOLD_M
#define MOVE_THRESHOLD_M   8
#endif
#ifndef WATCHDOG_POLL_SECS
#define WATCHDOG_POLL_SECS 20
#endif
// Don't poll at all until this many seconds after boot.
// Gives sensors.begin() / NMEA parser time to fully initialise.
#ifndef WATCHDOG_BOOT_DELAY_SECS
#define WATCHDOG_BOOT_DELAY_SECS 30
#endif

class MyMesh;

class MobilityWatchdog {
public:
    enum State { DRIVING, PARKED };

    explicit MobilityWatchdog(MyMesh& mesh)
        : _mesh(mesh), _state(DRIVING),
          _lastPollS(0), _stationaryStartS(0),
          _anchorLat(0.0), _anchorLon(0.0),
          _gpsEverValid(false) {}

    void begin() {
        char reply[64];
        _mesh.handleCommand(0, "set repeat off", reply);
        Serial.println("[watchdog] driving mode — repeat off");
    }

    void loop() {
        uint32_t now_s = millis() / 1000UL;

        // Wait for boot delay before doing anything
        if (now_s < (uint32_t)WATCHDOG_BOOT_DELAY_SECS) return;

        if ((now_s - _lastPollS) < (uint32_t)WATCHDOG_POLL_SECS) return;
        _lastPollS = now_s;

        // Safely get the location provider
        LocationProvider* gps = sensors.getLocationProvider();
        if (!gps) return;

        // isValid() and coordinate reads are virtual calls — guard with
        // a local copy of the pointer to avoid any race with sensors.loop()
        bool valid = false;
        long rawLat = 0, rawLon = 0;

        // Wrap in a simple validity check before reading coords
        valid = gps->isValid();
        if (!valid) {
            if (!_gpsEverValid) {
                Serial.println("[watchdog] no GPS fix yet");
            }
            _stationaryStartS = 0;
            return;
        }

        rawLat = gps->getLatitude();
        rawLon = gps->getLongitude();

        // Sanity check — discard obviously bad reads
        if (rawLat == 0 && rawLon == 0) return;

        _gpsEverValid = true;

        double lat = rawLat / 1000000.0;
        double lon = rawLon / 1000000.0;

        if (_stationaryStartS == 0) {
            _anchorLat = lat;
            _anchorLon = lon;
            _stationaryStartS = now_s;
            Serial.printf("[watchdog] first fix %.6f, %.6f\n", lat, lon);
            return;
        }

        float distM = _haversineM(_anchorLat, _anchorLon, lat, lon);

        if (distM > (float)MOVE_THRESHOLD_M) {
            if (_state == PARKED) {
                Serial.printf("[watchdog] moving (%.1fm) — repeat off\n", distM);
                char reply[64];
                _mesh.handleCommand(0, "advert",         reply);
                _mesh.handleCommand(0, "set repeat off", reply);
                _state = DRIVING;
            }
            _anchorLat = lat;
            _anchorLon = lon;
            _stationaryStartS = now_s;
            return;
        }

        uint32_t stationaryS = now_s - _stationaryStartS;
        if (_state == DRIVING && stationaryS >= (uint32_t)STATIONARY_SECS) {
            Serial.println("[watchdog] parked — repeat on");
            char reply[64];
            _mesh.handleCommand(0, "set repeat on", reply);
            _mesh.handleCommand(0, "advert",        reply);
            _state = PARKED;
        } else if (_state == DRIVING) {
            Serial.printf("[watchdog] stationary %lus/%us (%.1fm)\n",
                          (unsigned long)stationaryS,
                          (unsigned)STATIONARY_SECS,
                          distM);
        }
    }

    State getState() const { return _state; }

private:
    MyMesh&  _mesh;
    State    _state;
    uint32_t _lastPollS;
    uint32_t _stationaryStartS;
    double   _anchorLat, _anchorLon;
    bool     _gpsEverValid;

    static float _haversineM(double lat1, double lon1,
                              double lat2, double lon2) {
        const double R = 6371000.0;
        double dLat = (lat2 - lat1) * M_PI / 180.0;
        double dLon = (lon2 - lon1) * M_PI / 180.0;
        double a = sin(dLat/2)*sin(dLat/2)
                 + cos(lat1*M_PI/180.0)*cos(lat2*M_PI/180.0)
                 * sin(dLon/2)*sin(dLon/2);
        return (float)(R * 2.0 * atan2(sqrt(a), sqrt(1.0 - a)));
    }
};