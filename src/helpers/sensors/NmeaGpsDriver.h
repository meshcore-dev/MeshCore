#pragma once

#include "GpsDriver.h"

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <MeshCore.h>
#include <stdlib.h>
#include <string.h>

#ifndef UART_TYPE
#ifdef NRF52_PLATFORM
    #define UART_TYPE Uart
#else
    #define UART_TYPE HardwareSerial
#endif
#endif

#ifndef PIN_GPS_TX
    #define PIN_GPS_TX (-1)
#endif

#ifndef PIN_GPS_RX
    #define PIN_GPS_RX (-1)
#endif

#ifndef GPS_EN
    #ifdef PIN_GPS_EN
        #define GPS_EN PIN_GPS_EN
    #else
        #define GPS_EN (-1)
    #endif
#endif

#ifndef GPS_EN_ACTIVE
    #ifdef PIN_GPS_EN_ACTIVE
        #define GPS_EN_ACTIVE PIN_GPS_EN_ACTIVE
    #else
        #define GPS_EN_ACTIVE HIGH
    #endif
#endif

#ifndef GPS_RESET
    #ifdef PIN_GPS_RESET
        #define GPS_RESET PIN_GPS_RESET
    #else
        #define GPS_RESET (-1)
    #endif
#endif

#ifndef GPS_RESET_ACTIVE
    #ifdef PIN_GPS_RESET_ACTIVE
        #define GPS_RESET_ACTIVE PIN_GPS_RESET_ACTIVE
    #else
        #define GPS_RESET_ACTIVE LOW
    #endif
#endif

#ifndef GPS_BAUD_RATE
    #ifdef GPS_BAUDRATE
        #define GPS_BAUD_RATE GPS_BAUDRATE
    #else
        #define GPS_BAUD_RATE 9600
    #endif
#endif

// Optional rates detect() tries after GPS_BAUD_RATE, for GPS modules whose baud rate is
// not known in advance (e.g. -D GPS_BAUD_PROBE_LIST=9600,115200,38400). Used by every
// NmeaGpsDriver that does not pass its own list.
#ifdef GPS_BAUD_PROBE_LIST
    static const uint32_t NMEA_GPS_PROBE_BAUDS[] = { GPS_BAUD_PROBE_LIST };
    #define GPS_PROBE_BAUDS NMEA_GPS_PROBE_BAUDS
    #define GPS_PROBE_COUNT ((uint8_t)(sizeof(NMEA_GPS_PROBE_BAUDS) / sizeof(NMEA_GPS_PROBE_BAUDS[0])))
#else
    #define GPS_PROBE_BAUDS nullptr
    #define GPS_PROBE_COUNT 0
#endif

#ifndef PIN_GPS_SWITCH
    #define PIN_GPS_SWITCH (-1)
#endif

#ifndef GPS_SWITCH_ACTIVE
    #define GPS_SWITCH_ACTIVE HIGH
#endif

#ifndef GPS_SWITCH_IS_HARDWARE
    #define GPS_SWITCH_IS_HARDWARE 1
#endif

#ifndef GPS_SWITCH_TURNS_GPS_ON
    #define GPS_SWITCH_TURNS_GPS_ON 0
#endif

class NmeaGpsDriver : public GpsDriver {
    UART_TYPE* _hardwareSerial = nullptr;

    RefCountedDigitalPin* _peripheralPower = nullptr;
    int _txPin = -1;
    int _rxPin = -1;
    int _resetPin = -1;
    int _enablePin = -1;
    int _enableActive = HIGH;
    int _resetActive = LOW;
    uint32_t _baudRate = 9600;
    const uint32_t* _probeBauds = nullptr;  // optional rates to try in detect(), set by the board
    uint8_t _probeCount = 0;
    int _hardwareEnablePin = -1;
    int _hardwareEnableActive = HIGH;
    bool _started = false;
#if !GPS_SWITCH_IS_HARDWARE
    bool _requestedOn = false;
    bool _switchStateKnown = false;
    bool _lastSwitchEnabled = false;
    unsigned long _nextSwitchPoll = 0;
#endif

    void startHardware() {
        if (_hardwareSerial == nullptr || _started) return;

        if (_hardwareSerial != nullptr) {
            if (_txPin != -1 && _rxPin != -1) {
                _hardwareSerial->setPins(_txPin, _rxPin);
            }
            _hardwareSerial->begin(_baudRate);
        }

        if (_peripheralPower != nullptr) _peripheralPower->claim();

        if (_enablePin != -1) digitalWrite(_enablePin, _enableActive);
        if (_resetPin != -1) digitalWrite(_resetPin, !_resetActive);
        _started = true;
    }

    void reopenSerial(uint32_t baud) {
        _hardwareSerial->end();
        if (_txPin != -1 && _rxPin != -1) {
            _hardwareSerial->setPins(_txPin, _rxPin);
        }
        _hardwareSerial->begin(baud);
        drain();
    }

    // True if a complete NMEA sentence with a valid checksum arrives within timeout_ms.
    bool seesValidNmea(uint32_t timeout_ms) {
        char line[100];
        int len = -1;  // -1 = waiting for '$'
        const uint32_t start = millis();
        while ((uint32_t)(millis() - start) < timeout_ms) {
            if (available() <= 0) { delay(2); continue; }
            const char c = (char)read();
            if (c == '$') { len = 0; continue; }
            if (len < 0) continue;
            if (c == '\r' || c == '\n') {
                line[len] = '\0';
                const char* star = strchr(line, '*');
                if (star != nullptr && strlen(star) >= 3) {
                    uint8_t sum = 0;
                    for (const char* p = line; p < star; p++) sum ^= (uint8_t)*p;
                    if (sum == (uint8_t)strtoul(star + 1, nullptr, 16)) return true;
                }
                len = -1;
            } else if (len < (int)sizeof(line) - 1) {
                line[len++] = c;
            } else {
                len = -1;  // longer than any NMEA sentence: wrong baud rate
            }
        }
        return false;
    }

    // Bytes arriving does not mean the baud rate is right: at the wrong rate the
    // parser only sees garbage and never gets a fix. Try the configured rate, then the
    // board's list, and keep the first that yields valid NMEA. The rate is kept in
    // _baudRate, so later power cycles reopen the UART with it.
    bool tryBaudRate(uint32_t baud) {
        reopenSerial(baud);
        if (!seesValidNmea(2000)) return false;
        _baudRate = baud;
        MESH_DEBUG_PRINTLN("GPS baud rate: %lu", (unsigned long)_baudRate);
        return true;
    }

    void detectBaudRate() {
        const uint32_t configured = _baudRate;
        if (tryBaudRate(configured)) return;
        for (uint8_t i = 0; i < _probeCount; i++) {
            if (_probeBauds[i] != configured && tryBaudRate(_probeBauds[i])) return;
        }
        reopenSerial(configured);
        MESH_DEBUG_PRINTLN("GPS baud rate not detected, using %lu", (unsigned long)configured);
    }

    void stopHardware() {
        if (!_started) return;

        if (_enablePin != -1) digitalWrite(_enablePin, !_enableActive);
        if (_resetPin != -1) digitalWrite(_resetPin, _resetActive);

        if (_peripheralPower != nullptr) _peripheralPower->release();
        _started = false;
    }

public:
    NmeaGpsDriver() = default;

    NmeaGpsDriver(UART_TYPE& serial, int txPin = PIN_GPS_TX, int rxPin = PIN_GPS_RX,
                  int resetPin = GPS_RESET, int enablePin = GPS_EN,
                  RefCountedDigitalPin* peripheralPower = nullptr,
                  int enableActive = GPS_EN_ACTIVE, int resetActive = GPS_RESET_ACTIVE,
                  uint32_t baudRate = GPS_BAUD_RATE,
                  int hardwareEnablePin = PIN_GPS_SWITCH,
                  int hardwareEnableActive = GPS_SWITCH_ACTIVE,
                  const uint32_t* probeBauds = GPS_PROBE_BAUDS, uint8_t probeCount = GPS_PROBE_COUNT)
        : _hardwareSerial(&serial), _peripheralPower(peripheralPower), _txPin(txPin), _rxPin(rxPin),
          _resetPin(resetPin), _enablePin(enablePin), _enableActive(enableActive),
          _resetActive(resetActive), _baudRate(baudRate),
          _probeBauds(probeBauds), _probeCount(probeBauds != nullptr ? probeCount : 0),
          _hardwareEnablePin(hardwareEnablePin), _hardwareEnableActive(hardwareEnableActive) {
        if (_resetPin != -1) {
            pinMode(_resetPin, OUTPUT);
            digitalWrite(_resetPin, _resetActive);
        }
        if (_enablePin != -1) {
            pinMode(_enablePin, OUTPUT);
            digitalWrite(_enablePin, !_enableActive);
        }
        if (_hardwareEnablePin != -1) {
            pinMode(_hardwareEnablePin, INPUT);
        }
    }

    // Board with a GPS whose baud rate is not known in advance: detect() also tries the
    // rates in probeBauds (after the configured one). The array must stay valid (static).
    NmeaGpsDriver(UART_TYPE& serial, const uint32_t* probeBauds, uint8_t probeCount)
        : NmeaGpsDriver(serial, PIN_GPS_TX, PIN_GPS_RX, GPS_RESET, GPS_EN, nullptr,
                        GPS_EN_ACTIVE, GPS_RESET_ACTIVE, GPS_BAUD_RATE, PIN_GPS_SWITCH,
                        GPS_SWITCH_ACTIVE, probeBauds, probeCount) {}

    void begin() override {
#if !GPS_SWITCH_IS_HARDWARE
        _requestedOn = true;
        if (!isHardwareEnabled()) return;
#endif
        startHardware();
    }

    void reset() override {
        if (!_started) return;
        if (_resetPin != -1) {
            digitalWrite(_resetPin, _resetActive);
            delay(10);
            digitalWrite(_resetPin, !_resetActive);
        }

#ifdef GPS_L76K
        delay(250);
        sendNmeaSentence("$PCAS04,7");
        delay(250);
#endif
    }

    void stop() override {
#if !GPS_SWITCH_IS_HARDWARE
        _requestedOn = false;
#endif
        stopHardware();
    }

    bool isActive() const override {
        return _started && isHardwareEnabled();
    }

    bool hasHardwareEnableState() const override {
        return _hardwareEnablePin != -1;
    }

    bool isHardwareEnabled() const override {
        return _hardwareEnablePin == -1 ||
            digitalRead(_hardwareEnablePin) == _hardwareEnableActive;
    }

    bool pollGpsSwitch() override {
#if !GPS_SWITCH_IS_HARDWARE
        if (_hardwareEnablePin == -1 ||
            (long)(millis() - _nextSwitchPoll) < 0) return false;

        _nextSwitchPoll = millis() + 1000;
        const bool switchEnabled = isHardwareEnabled();
        const bool enablesGps = GPS_SWITCH_TURNS_GPS_ON && _switchStateKnown &&
            !_lastSwitchEnabled && switchEnabled;
        _switchStateKnown = true;
        _lastSwitchEnabled = switchEnabled;

        if (enablesGps) _requestedOn = true;
        if (_requestedOn && switchEnabled) {
            startHardware();
        } else {
            stopHardware();
        }
        return enablesGps;
#else
        return false;
#endif
    }

    bool detect(uint32_t timeout_ms) override {
        bool found = available() > 0;

        const uint32_t deadline = millis() + timeout_ms;
        while (!found && (int32_t)(millis() - deadline) < 0) {
            found = available() > 0;
            if (!found) delay(10);
        }
        // only on a real detection (timeout > 0), not on the quick "still active?" polls
        if (found && timeout_ms > 0 && _probeCount > 0 && _started && _hardwareSerial != nullptr) {
            detectBaudRate();
        }
        return found;
    }

    int available() {
        return _hardwareSerial == nullptr ? 0 : _hardwareSerial->available();
    }

    int read() {
        return _hardwareSerial == nullptr ? -1 : _hardwareSerial->read();
    }

    void drain() {
        while (available() > 0) read();
    }

    void sendNmeaSentence(const char* sentence) {
        if (_hardwareSerial == nullptr || sentence == nullptr || sentence[0] != '$') return;

        uint8_t checksum = 0;
        const char* p = sentence + 1;
        while (*p != '\0' && *p != '*') checksum ^= (uint8_t)*p++;

        _hardwareSerial->print(sentence);
        if (*p == '\0') {
            char checksumSuffix[6];
            snprintf(checksumSuffix, sizeof(checksumSuffix), "*%02X\r\n", checksum);
            _hardwareSerial->print(checksumSuffix);
        } else {
            _hardwareSerial->print("\r\n");
        }
    }
};
