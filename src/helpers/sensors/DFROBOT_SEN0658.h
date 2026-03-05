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
    float windSpeed;
    float airPressure;
    float noiseDb;
    float windDirection;
    float windAngle;
    float luminosity;
    float pm2_5;
    float pm10;
};

class DFROBOT_SEN0658
{
    public:
        bool begin();
        bool readSample(DFROBOT_SEN0658_Sample &sample);
    private:
        static uint16_t CRC16_2(const uint8_t *buf, int len);
        static void sendCommand(const uint8_t command[6]);
        static bool readBytes(uint8_t *buffer, int len);
        static void flushSerial();
        bool readWind(DFROBOT_SEN0658_Sample &sample);
        bool readTemperature(DFROBOT_SEN0658_Sample &sample);
        bool readAir(DFROBOT_SEN0658_Sample &sample);
        bool readLight(DFROBOT_SEN0658_Sample &sample);
};