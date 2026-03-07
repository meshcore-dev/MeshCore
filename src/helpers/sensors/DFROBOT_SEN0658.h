#pragma once

#include <Arduino.h>
#include <target.h>

#if defined(ENV_INCLUDE_GPS) && \
    (PIN_GPS_TX == WITH_SEN0658_TX || PIN_GPS_TX == WITH_SEN0658_RX || \
     PIN_GPS_RX == WITH_SEN0658_TX || PIN_GPS_RX == WITH_SEN0658_RX)
    #error "GPS pins conflict with SEN0658 pins. Please change the pin definitions to avoid conflicts"
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
        #if defined(WITH_SEN0658_EN)
        uint32_t idleShutdownTime = 0;
        #endif
        static uint16_t CRC16_2(const uint8_t *buf, int len);
        void sendCommand(uint8_t function, uint16_t registerStart, uint16_t registerLengthOrValue);
        void sendWriteCommand(uint16_t registerIndex, uint16_t value);
        bool readBytes(uint8_t *buffer, int len);
        void flushSerial();
        template<typename PacketType> bool readRegisters(uint16_t registerStart, PacketType& packet);
        bool writeRegister(uint16_t registerIndex, uint16_t value);
        bool readWind(DFROBOT_SEN0658_Sample &sample);
        bool readTemperature(DFROBOT_SEN0658_Sample &sample);
        bool readAir(DFROBOT_SEN0658_Sample &sample);
        bool readLight(DFROBOT_SEN0658_Sample &sample);
};