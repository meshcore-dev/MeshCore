/**
 * @file RAK12035_SoilMoisture.h
 * @author Bernd Giesecke (bernd.giesecke@rakwireless.com)
 * @brief Header file for Class RAK12035
 * @version 0.1
 * @date 2021-11-20
 * 
 * Updates for MeshCore integration
 * Ken Privitt
 * 2/26/2026
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#ifndef RAK12035_SOILMOISTURE_H
#define RAK12035_SOILMOISTURE_H
#endif

#ifndef ENABLE_RAK12025_CALIBRATION
#define ENABLE_RAK12025_CALIBRATION = 0  // Used to generate Calibration Version of Firmware

#include <Arduino.h>
#include <Wire.h>

#define RAK12035_I2C_ADDR_DEFAULT   0x20
#define RAK12035_0_ADDR             0x20
#define RAK12035_1_ADDR             0x21
#define RAK12035_2_ADDR             0x22

// Command codes used by the RAK12035 firmware
#define SOILMOISTURESENSOR_GET_CAPACITANCE   0x01   // (r)   2 bytes
#define SOILMOISTURESENSOR_GET_I2C_ADDR      0x02   // (r)   1 bytes
#define SOILMOISTURESENSOR_SET_I2C_ADDR      0x03   // (w)   1 bytes
#define SOILMOISTURESENSOR_GET_VERSION       0x04   // (r)   1 bytes
#define SOILMOISTURESENSOR_GET_TEMPERATURE   0x05   // (r)   2 bytes
#define SOILMOISTURESENSOR_SET_SLEEP         0x06   // (w)   1 bytes
#define SOILMOISTURESENSOR_SET_WET_CAL       0x07   // (w)   2 bytes
#define SOILMOISTURESENSOR_SET_DRY_CAL       0x08   // (w)   2 bytes
#define SOILMOISTURESENSOR_GET_MOISTURE      0x09   // (r)   1 bytes
#define SOILMOISTURESENSOR_GET_WET_CAL       0x0A   // (r)   2 bytes
#define SOILMOISTURESENSOR_GET_DRY_CAL       0x0B   // (r)   2 bytes

class RAK12035_SoilMoisture
{
public:
    RAK12035_SoilMoisture(uint8_t addr = RAK12035_I2C_ADDR_DEFAULT);

    void setup(TwoWire& i2c);
    bool begin(uint8_t addr);
    bool getEvent(uint8_t *humidity, uint16_t *temperature);
   
    uint16_t get_sensor_capacitance();                      //Command 01 - (r) 2 byte
    uint8_t get_I2C_address();                              //Command 02 - (r) 1 byte
    bool set_sensor_addr(uint8_t addr);                     //Command 03 - (w) 1 byte
    uint8_t get_sensor_version();                           //Command 04 - (r) 1 byte
    float get_sensor_temperature();                         //Command 05 - (r) 2 bytes
    bool sensor_sleep();                                    //Command 06 - (w) 1 byte
    bool set_humidity_full(uint16_t hundred_val);           //Command 07 - (w) 2 bytes
    bool set_humidity_zero(uint16_t zero_val);              //Command 08 - (w) 2 bytes
    uint8_t get_sensor_moisture();                          //Command 09 - (r) 1 byte
    uint8_t get_sensor_humid();                             //Command 09 - (r) 1 byte
    uint16_t get_humidity_full();                           //Command 0A - (r) 2 bytes
    uint16_t get_humidity_zero();                           //Command 0B - (r) 2 bytes

    bool read_rak12035(uint8_t cmd, uint8_t *data, uint8_t length);
    bool write_rak12035(uint8_t cmd, uint8_t *data, uint8_t length);

    bool query_sensor(); 
    bool sensor_on();
    bool reset();

    uint16_t _dry_cal;
    uint16_t _wet_cal;

private:
    bool read_reg(uint8_t reg, uint8_t *data, uint8_t len);
    bool write_reg(uint8_t reg, uint8_t *data, uint8_t len);

    TwoWire *_i2c = &Wire;
    uint8_t _addr;

    uint16_t default_dry_cal = 2000;
    uint16_t default_wet_cal = 50;
    uint8_t _capacitance = 0;
    uint16_t _temperature = 0;
    uint8_t _moisture = 0;
};
#endif
