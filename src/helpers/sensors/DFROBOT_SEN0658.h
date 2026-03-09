#pragma once

#include <Arduino.h>
#include <target.h>

#if defined(ENV_INCLUDE_GPS) && \
    (PIN_GPS_TX == WITH_SEN0658_TX || PIN_GPS_TX == WITH_SEN0658_RX || \
     PIN_GPS_RX == WITH_SEN0658_TX || PIN_GPS_RX == WITH_SEN0658_RX)
    #error "GPS pins conflict with SEN0658 pins. Please change the pin definitions to avoid conflicts"
#endif


#ifndef SEN0658_POLL_PERIOD_SECONDS
#define SEN0658_POLL_PERIOD_SECONDS (15 * 60)
#endif

#ifndef SEN0658_CACHE_MAX_AGE_SECONDS
#define SEN0658_CACHE_MAX_AGE_SECONDS (SEN0658_POLL_PERIOD_SECONDS * 5 / 2)
#endif

#ifndef SEN0658_WARMUP_SECONDS
#define SEN0658_WARMUP_SECONDS 30
#endif

#if defined(WITH_SEN0658_EN)
#ifndef SEN0658_IDLE_TIMEOUT_SECONDS
#define SEN0658_IDLE_TIMEOUT_SECONDS 30
#endif

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
    private:
        DFROBOT_SEN0658_Sample _cachedSample = {0};
        uint32_t _lastCacheTime = 0;
        uint32_t _lastPollTime = 0;
        uint32_t _powerOnTime = 0;
        uint32_t _lastActivityTime = 0;
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
};