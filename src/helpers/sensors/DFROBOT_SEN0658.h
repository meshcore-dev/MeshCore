#pragma once

#include <Arduino.h>
#include <target.h>
#include <helpers/IdentityStore.h>

#if defined(ENV_INCLUDE_GPS) && \
    (PIN_GPS_TX == WITH_SEN0658_TX || PIN_GPS_TX == WITH_SEN0658_RX || \
     PIN_GPS_RX == WITH_SEN0658_TX || PIN_GPS_RX == WITH_SEN0658_RX)
    #error "GPS pins conflict with SEN0658 pins. Please change the pin definitions to avoid conflicts"
#endif


#ifndef SEN0658_POLL_PERIOD_SECONDS
#define SEN0658_POLL_PERIOD_SECONDS (10 * 60)
#endif

#ifndef SEN0658_CACHE_MAX_AGE_SECONDS
#define SEN0658_CACHE_MAX_AGE_SECONDS (SEN0658_POLL_PERIOD_SECONDS * 5 / 2)
#endif

#ifndef SEN0658_WARMUP_SECONDS
#define SEN0658_WARMUP_SECONDS 30
#endif

#ifndef SEN0658_IDLE_TIMEOUT_SECONDS
#define SEN0658_IDLE_TIMEOUT_SECONDS 30
#endif

struct DFROBOT_SEN0658_Sample {
    float temperature;
    float humidity;
    float airPressure;
    float pm2_5;
    float pm10;
    float windSpeed;
    float windAngle;
    float noiseDb;
    float luminosity;
    uint32_t timestamp;
};

class DFROBOT_SEN0658
{
    public:
        bool begin();
        bool readSample(DFROBOT_SEN0658_Sample &sample);
        bool readWindDirectionOffset(uint16_t &offset);
        bool writeWindDirectionOffset(uint16_t offset);
        bool zeroWindSpeed();
        bool zeroRainfall();
        void loop();
        bool hasPendingWork();
        void loadPrefs(FILESYSTEM* fs);
        void savePrefs(FILESYSTEM* fs);
        int getNumSettings() const;
        const char* getSettingName(int i) const;
        int getSettingValue(int i, char* buf, int bufLen);
        bool setSettingValue(const char* name, const char* value);

    private:
        struct Config {
            uint16_t version = 1;
            uint16_t pollPeriodSeconds = SEN0658_POLL_PERIOD_SECONDS;
            uint16_t cacheMaxAgeSeconds = SEN0658_CACHE_MAX_AGE_SECONDS;
            uint16_t warmupSeconds = SEN0658_WARMUP_SECONDS;
            uint16_t idleTimeoutSeconds = SEN0658_IDLE_TIMEOUT_SECONDS;
        } __attribute__((packed)) config;

        DFROBOT_SEN0658_Sample _cachedSample = {0};
        uint32_t _lastCacheTime = 0;
        uint32_t _lastPollTime = 0;
        uint32_t _powerOnTime = 0;
        uint32_t _lastActivityTime = 0;
        uint32_t _lastWarmupLog = 0;
        void powerOn();
        static uint16_t CRC16_2(const uint8_t *buf, int len);
        void sendCommand(uint8_t function, uint16_t registerStart, uint16_t registerLengthOrValue);
        void sendWriteCommand(uint16_t registerIndex, uint16_t value);
        bool readBytes(uint8_t *buffer, int len);
        void flushSerial();
        bool updateSample(DFROBOT_SEN0658_Sample &sample);
        template<typename PacketType> bool readRegisters(uint16_t registerStart, PacketType& packet);
        bool writeRegister(uint16_t registerIndex, uint16_t value);
        bool readWind(DFROBOT_SEN0658_Sample &sample);
        bool readTemperature(DFROBOT_SEN0658_Sample &sample);
        bool readAir(DFROBOT_SEN0658_Sample &sample);
        bool readLight(DFROBOT_SEN0658_Sample &sample);
        // helpers
        bool isPollDue() { return config.pollPeriodSeconds > 0 && millis() - _lastPollTime >= config.pollPeriodSeconds * 1000; }
        bool isPoweredOn() { return _powerOnTime != 0; }
        bool isSensorReady() { return isPoweredOn() && millis() - _powerOnTime >= config.warmupSeconds * 1000; }
        bool hasCachedSample() { return _lastCacheTime != 0; }
        bool isCachedSampleValid() { return hasCachedSample() && config.cacheMaxAgeSeconds > 0 && millis() - _lastCacheTime < config.cacheMaxAgeSeconds * 1000; }
        bool isIdle() { return isSensorReady() &&config.idleTimeoutSeconds > 0 && millis() - _lastActivityTime >= config.idleTimeoutSeconds * 1000; }
};
