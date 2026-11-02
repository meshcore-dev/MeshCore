#pragma once

#include "GpsDriver.h"

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>

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
    #define GPS_BAUD_RATE 9600
#endif

#ifndef PIN_GPS_SWITCH
    #define PIN_GPS_SWITCH (-1)
#endif

#ifndef GPS_SWITCH_ACTIVE
    #define GPS_SWITCH_ACTIVE HIGH
#endif

class NmeaGpsDriver : public GpsDriver {
    Stream* _stream = nullptr;
    HardwareSerial* _hardwareSerial = nullptr;
    RefCountedDigitalPin* _peripheralPower = nullptr;
    int _txPin = -1;
    int _rxPin = -1;
    int _resetPin = -1;
    int _enablePin = -1;
    int _enableActive = HIGH;
    int _resetActive = LOW;
    uint32_t _baudRate = 9600;
    int _hardwareEnablePin = -1;
    int _hardwareEnableActive = HIGH;
    bool _started = false;

public:
    NmeaGpsDriver() = default;

    NmeaGpsDriver(Stream& stream, int txPin = PIN_GPS_TX, int rxPin = PIN_GPS_RX,
                  int resetPin = GPS_RESET, int enablePin = GPS_EN,
                  RefCountedDigitalPin* peripheralPower = nullptr,
                  int enableActive = GPS_EN_ACTIVE, int resetActive = GPS_RESET_ACTIVE,
                  uint32_t baudRate = GPS_BAUD_RATE,
                  int hardwareEnablePin = PIN_GPS_SWITCH,
                  int hardwareEnableActive = GPS_SWITCH_ACTIVE)
        : _stream(&stream), _peripheralPower(peripheralPower), _txPin(txPin), _rxPin(rxPin),
          _resetPin(resetPin), _enablePin(enablePin), _enableActive(enableActive),
          _resetActive(resetActive), _baudRate(baudRate),
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

    NmeaGpsDriver(HardwareSerial& serial, int txPin = PIN_GPS_TX, int rxPin = PIN_GPS_RX,
                  int resetPin = GPS_RESET, int enablePin = GPS_EN,
                  RefCountedDigitalPin* peripheralPower = nullptr,
                  int enableActive = GPS_EN_ACTIVE, int resetActive = GPS_RESET_ACTIVE,
                  uint32_t baudRate = GPS_BAUD_RATE,
                  int hardwareEnablePin = PIN_GPS_SWITCH,
                  int hardwareEnableActive = GPS_SWITCH_ACTIVE)
        : NmeaGpsDriver(static_cast<Stream&>(serial), txPin, rxPin, resetPin, enablePin,
                        peripheralPower, enableActive, resetActive, baudRate,
                        hardwareEnablePin, hardwareEnableActive) {
        _hardwareSerial = &serial;
    }

    void begin() override {
        if (_stream == nullptr || _started) return;

        if (_hardwareSerial != nullptr) {
#ifdef ESP32_PLATFORM
            _hardwareSerial->setPins(_txPin, _rxPin);
#endif
            _hardwareSerial->begin(_baudRate);
        }

        if (_peripheralPower != nullptr) _peripheralPower->claim();

        if (_enablePin != -1) digitalWrite(_enablePin, _enableActive);
        if (_resetPin != -1) digitalWrite(_resetPin, !_resetActive);
        _started = true;
    }

    void reset() override {
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
        if (!_started) return;

        if (_enablePin != -1) digitalWrite(_enablePin, !_enableActive);
        if (_resetPin != -1) digitalWrite(_resetPin, _resetActive);

        if (_peripheralPower != nullptr) _peripheralPower->release();
        _started = false;
    }

    bool isEnabled() const override {
        if (_enablePin == -1) return true;
        return digitalRead(_enablePin) == _enableActive;
    }

    bool hasHardwareEnableState() const override {
        return _hardwareEnablePin != -1;
    }

    bool isHardwareEnabled() const override {
        return _hardwareEnablePin == -1 ||
            digitalRead(_hardwareEnablePin) == _hardwareEnableActive;
    }

    bool detect(uint32_t timeout_ms) override {
        if (available() > 0) return true;

        const uint32_t deadline = millis() + timeout_ms;
        while ((int32_t)(millis() - deadline) < 0) {
            if (available() > 0) return true;
            delay(10);
        }
        return false;
    }

    int available() {
        return _stream == nullptr ? 0 : _stream->available();
    }

    int read() {
        return _stream == nullptr ? -1 : _stream->read();
    }

    void drain() {
        while (available() > 0) read();
    }

    void sendNmeaSentence(const char* sentence) {
        if (_stream == nullptr || sentence == nullptr || sentence[0] != '$') return;

        uint8_t checksum = 0;
        const char* p = sentence + 1;
        while (*p != '\0' && *p != '*') checksum ^= (uint8_t)*p++;

        _stream->print(sentence);
        if (*p == '\0') {
            char checksumSuffix[6];
            snprintf(checksumSuffix, sizeof(checksumSuffix), "*%02X\r\n", checksum);
            _stream->print(checksumSuffix);
        } else {
            _stream->print("\r\n");
        }
    }
};
