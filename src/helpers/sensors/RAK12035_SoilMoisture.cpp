/*----------------------------------------------------------------------*
 * RAK12035_SoilMoistureSensor.cpp - Arduino library for the Sensor     *
 * version of I2C Soil Moisture Sensor version from Chrirp              *
 * (https://github.com/Miceuz/i2c-moisture-sensor).                     *
 *                                                                      *
 * Ingo Fischer 11Nov2015                                               *
 * https://github.com/Apollon77/I2CSoilMoistureSensor                   *
 *                                                                      *    
 * Ken Privitt 8Feb2026                                                 *
 * Adapted for MeshCore Firmware Stack                                  *
 *                                                                      *                        
 * MIT license                                                          *
 *                                                                      *
 * This file contains a collection of routines to access the            *
 * RAK12035 Soil Moisture Sensor via I2C. The sensor provides           *
 * Soil Temperature and capacitance-based Soil Moisture Readings.       *
 *                                                                      *
 *----------------------------------------------------------------------*/

#include "RAK12035_SoilMoisture.h"
#include "MeshCore.h"
#include <Wire.h>

/*----------------------------------------------------------------------*
 * Constructor.                                                         *
 *----------------------------------------------------------------------*/
 // RAK12035_SoilMoisture(uint8_t addr)
 //
 // Accepts the I2C Address to look for the RAK12035
 // Initializes the I2C to null (will be setup later in Wire.Begin()
 //
 // No hardware is touched in the constructor.
 // I2C communication is deferred until begin() is called. 
 //------------------------------------------------------------------------------

RAK12035_SoilMoisture::RAK12035_SoilMoisture(uint8_t addr)
{
    _addr = addr;   // Save the sensor's I2C address
    _i2c = nullptr; // Bus not assigned yet; must be set in begin()
}

//------------------------------------------------------------------------------ 
// setup() 
//------------------------------------------------------------------------------ 
// setup(TwoWire &i2c) 
// 
// Assigns the I2C bus that this driver instance will use. This allows the 
// application to choose between Wire, Wire1, or any other TwoWire instance 
// supported by the platform. 
// 
// No I2C communication occurs here; setup() simply stores the pointer so that 
// begin() and all register‑level operations know which bus to use. 
//------------------------------------------------------------------------------
void RAK12035_SoilMoisture::setup(TwoWire &i2c)

{
    _i2c = &i2c;                  // assigns the bus pointer
    _i2c->begin();                // Initialize the bus to Wire or Wire1
}

//------------------------------------------------------------------------------ 
// RAK12035 Soil Moisture begin() 
//------------------------------------------------------------------------------ 
// 
// Performs initialization of the RAK12035 soil‑moisture sensor. This 
// routine assumes that the application has already selected the I2C bus via 
// setup() and that the bus has been initialized externally (Wire.begin()).
// It uses the passed in I2C Address (default 0x20)
//
// ***  This code does not supprt three sensors ***
//      The RAK12023 has three connectors, but each of the sensors attached must
//      all have a different I2C addresses.
//      This code has a function to set the I2C adress of a sensor
//      and currently only supports one address 0x20 (the default).
//      To support three sensors, EnvironmentSensorManager would need to be modified
//      to support multiple instances of the RAK12035_SoilMoisture class,
//      each with a different address. (0x20, 0x21, 0x22) 
//      The begin() function would need to be modified to loop through the three addresses
//
// DEBUG STATEMENTS: Can be enabled by uncommenting or adding:
//      File:    varients/rak4631 platformio.ini
//      Section example: [env:RAK_4631_companion_radio_ble] 
//      Enable Debug statements: -D MESH_DEBUG=1
// 
//------------------------------------------------------------------------------
bool RAK12035_SoilMoisture::begin(uint8_t addr)
{
//    MESH_DEBUG_PRINTLN("begin() - Start of RAK12035 initialization");
//    MESH_DEBUG_PRINTLN("begin() - RAK12035 passed in Address %02X", addr);

// 1. Ensure setup() was called
    if (_i2c == nullptr) {
        MESH_DEBUG_PRINTLN("RAK12035 ERROR: I2C bus not set!");
        return false;
}

    uint16_t _dry_cal = 200;
    uint16_t _wet_cal = 600;
    uint8_t _version = 0;
    uint8_t _addr;                   // The I2C address to be used (passed in parameter)

/*------------------------------------------------------------------------------------------
 * Set Calibration values -  This is done with custom a firmware version
 *
 *      USE the Build Flag: -D ENABLE_RAK12035_CALIBRATION = 1
 * OR
 *      Change the value to 1 in the RAK12035_SoilMoisture.h file
 * 
 * Calibration Procedure:
 * 1) Flash the the Calibration version of the firmware.
 * 2) Leave the sensor dry, power up the device.
 * 3) After detecting the RAK12035 this firmware will display calibration data on Channel 3
 * 
 *      Frequency = Current Capacitance Value
 *      Temperature = Current Wet calibration value
 *      Power = Current Dry calibration value
 *  
 *  4) Click refresh several times.  This will take a capacitance reading and if it is
 *     greater than the current Dry value it will store it in the sensor
 *     The value will bounce a little as you click refresh, but it eventually settles down (a few clicks)
 *     the stored value will stabalize at it's Maximum value.
 * 
 * 5) Put the sensor in water.  
 * 
 * 6) Click refresh several times.  This will take a capacitance reading and if it is
 *     less than the current Wet value it will store it in the sensor
 *     The value will bounce a little as you click refresh, but it eventually settles down (a few clicks)
 *     the stored value will stabalize at it's Minimum value.
 * 
 * 7) The Sensor is now calibrated, turn off the device.
 * 
 * 8) Reflash the device with the non-Calibration Firmware, Data will be shown on Channel 2
 *                             
 *------------------------------------------------------------------------------------------
*/ 

#if ENABLE_RAK12035_CALIBRATION 
    uint16_t _wet = 2000;         // A high value the should be out of the normal Wet range
     set_humidity_full(_wet);

   uint16_t _dry = 50;            // A low value the should be out of the normal Dry range
   set_humidity_zero(_dry);
#endif

 /*--------------------------------------------------------------------------------
  * 
  *   Check if a sensor is present and return true if found, false if not present
  *
  *--------------------------------------------------------------------------------
  */
       if (query_sensor()) {
             MESH_DEBUG_PRINTLN("begin() - Sensor responded with valid version");
             return true;
    }
    else {
        MESH_DEBUG_PRINTLN("begin() - Sensor version FAIL");
        return false;
    }
}

/*---------------------------------------------------------------------------------
 *
 * Below are all the routines to execute the various I2C commands supported
 * by the RAK12035 sensor
 * 
 *--------------------------------------------------------------------------------*/

uint16_t RAK12035_SoilMoisture::get_sensor_capacitance()            //Command 01 - (r) 2 byte
{
    uint8_t buf[2] = {0};
    if (!read_rak12035(SOILMOISTURESENSOR_GET_CAPACITANCE, buf, 2)) {
        MESH_DEBUG_PRINTLN("Function 1: get_capacitance() FAIL: Bad data returned = %02X %02X", buf[0], buf[1]);
        return (buf[0] << 8) | buf[1];   // return raw for debugging
    }
    uint16_t cap = (buf[0] << 8) | buf[1];
    MESH_DEBUG_PRINTLN("Function 1: get_capacitance() SUCCESS: Capacitance = %d", cap);
    return cap;
}


uint8_t RAK12035_SoilMoisture::get_I2C_address()                     //Command 02 - (r) 1 byte
{
    uint8_t addr = 0;
    if (!read_rak12035(SOILMOISTURESENSOR_GET_I2C_ADDR, &addr, 1)) {
        MESH_DEBUG_PRINTLN("Function 2: get_I2C_address() FAIL: Bad data returned = %02X", addr);
        return addr;   // return raw for debugging
    }
    MESH_DEBUG_PRINTLN("Function 2: get_I2C_address() SUCCESS: I2C Address = %02X", addr);
    return addr;
}


bool RAK12035_SoilMoisture::set_sensor_addr(uint8_t addr)           //Command 03 - (w) 1 byte
{
    if (!write_rak12035(SOILMOISTURESENSOR_SET_I2C_ADDR, &addr, 1)) {
        MESH_DEBUG_PRINTLN("Function 3: set_I2C_address() FAIL: Could not set new address %02X", addr);
        return false;
    }
    MESH_DEBUG_PRINTLN("Function 3: set_I2C_address() SUCCESS: New address = %02X", addr);
    return true;
}


uint8_t RAK12035_SoilMoisture::get_sensor_version()                 // Command 04 - 1 byte
{
    uint8_t v = 0;

    read_rak12035(SOILMOISTURESENSOR_GET_VERSION, &v, 1);
    if (!read_rak12035(SOILMOISTURESENSOR_GET_VERSION, &v, 1)) {
    MESH_DEBUG_PRINTLN("Function 4: get_sensor_version() FAIL: Bad data returned = %02X", v);
        return v;
    }
    MESH_DEBUG_PRINTLN("Function 4: get_sensor_version() SUCCESS: Version = %02X", v);
    return v;
}


float RAK12035_SoilMoisture::get_sensor_temperature()               //Command 05 - (r) 2 bytes
{
    uint8_t buf[2] = {0};
    if (!read_rak12035(SOILMOISTURESENSOR_GET_TEMPERATURE, buf, 2)) {
        MESH_DEBUG_PRINTLN("Function 5: get_temperature() FAIL: Bad data returned = %02X %02X", buf[0], buf[1]);
        return (buf[0] << 8) | buf[1];   // raw data returned for debugging 0XFFFF is error
    }
    // Sensor returns a 16-bit signed integer (°C * 10)
    int16_t raw = (buf[0] << 8) | buf[1];
    float tempC = raw / 10.0f;
    MESH_DEBUG_PRINTLN("Function 5: get_temperature() SUCCESS: Raw=%04X  Temp=%.1f C", raw, tempC);
    return tempC;
}


bool RAK12035_SoilMoisture::sensor_sleep()                          //Command 06 - (w) 1 byte
{
    uint8_t tmp = 0;
    if (!write_rak12035(SOILMOISTURESENSOR_SET_SLEEP, &tmp, 1)) {
        MESH_DEBUG_PRINTLN("Function 6: sensor_sleep() FAIL: Could not send sleep command");
        return false;
    }
    MESH_DEBUG_PRINTLN("Function 6: sensor_sleep() SUCCESS: Sensor acknowledged sleep command");

// Optional: turn off sensor power AFTER successful sleep command

// This has been commented out due to a pin name conflict with the Heltec v3
// This will need to be resolved if this funstion is to be utilized in the future
/*
    digitalWrite(WB_IO2, LOW);
*/
    return true;
}


bool RAK12035_SoilMoisture::set_humidity_full(uint16_t full)           //Command 07 - (w) 2 bytes
{
    uint8_t buf[2];
    buf[0] = (full >> 8) & 0xFF;   // High byte
    buf[1] = full & 0xFF;          // Low byte

    if (!write_rak12035(SOILMOISTURESENSOR_SET_WET_CAL, buf, 2)) {
        MESH_DEBUG_PRINTLN("Function 7: set_humidity_full() FAIL: Could not set wet calibration value"
        );
        return false;
    }
    MESH_DEBUG_PRINTLN("Function 7: set_humidity_full() SUCCESS: New Full = %04X", full);
    return true;
}


bool RAK12035_SoilMoisture::set_humidity_zero(uint16_t zero)          //Command 08 - (w) 2 bytes
{
    uint8_t buf[2];
    buf[0] = (zero >> 8) & 0xFF;   // High byte
    buf[1] = zero & 0xFF;          // Low byte

    if (!write_rak12035(SOILMOISTURESENSOR_SET_DRY_CAL, buf, 2)) {
        MESH_DEBUG_PRINTLN("Function 8: set_humidity_zero() FAIL: Could not set dry calibration value");
        return false;
    }
    MESH_DEBUG_PRINTLN("Function 8: set_humidity_zero() SUCCESS: New Zero = %04X", zero);
    return true;
}


uint8_t RAK12035_SoilMoisture::get_sensor_moisture()                    //Command 09 - (r) 1 byte
{
// Load calibration values from sensor 
    _wet_cal = get_humidity_full();
    _dry_cal = get_humidity_zero();

    MESH_DEBUG_PRINTLN("Function 9: get_moisture() - Read from sensor or calculate from capacitance");

    // Read sensor version
    uint8_t v = get_sensor_version();

    // If version > 2, read moisture directly from the sensor
    if (v > 2) {
        MESH_DEBUG_PRINTLN("Version > 02 - Reading moisture directly from sensor");
        uint8_t moisture = get_sensor_humid();
        MESH_DEBUG_PRINTLN("get_moisture() Direct Read = %d%%", moisture);
        return moisture;
    }
    // Otherwise calculate moisture from capacitance
    MESH_DEBUG_PRINTLN("Calculating moisture from capacitance");

    uint16_t cap = get_sensor_capacitance();

    // Clamp capacitance between calibration points
    if (_dry_cal < _wet_cal) {
        if (cap <= _dry_cal) cap = _dry_cal;
        if (cap >= _wet_cal) cap = _wet_cal;

        float pct = (_wet_cal - cap) * 100.0f / (_wet_cal - _dry_cal);
        if (pct > 100.0f) pct = 100.0f;

        MESH_DEBUG_PRINTLN("get_moisture Case 1() Calculated = %d%%", (uint8_t)pct);
        return (uint8_t)pct;
    } else {
        if (cap >= _dry_cal) cap = _dry_cal;
        if (cap <= _wet_cal) cap = _wet_cal;

        float pct = (_dry_cal - cap) * 100.0f / (_dry_cal - _wet_cal);
        if (pct > 100.0f) pct = 100.0f;

        MESH_DEBUG_PRINTLN("get_moisture Case 2() Calculated = %d%%", (uint8_t)pct);
        return (uint8_t)pct;
    }
}


uint8_t RAK12035_SoilMoisture::get_sensor_humid()                   //Command 09 - (r) 1 byte
{
    uint8_t moisture = 0;

    if (!read_rak12035(SOILMOISTURESENSOR_GET_MOISTURE, &moisture, 1)) {
        MESH_DEBUG_PRINTLN("Function 9: get_sensor_humid() FAIL: Bad data returned = %02X", moisture);
        return moisture; // raw fallback
    }
    MESH_DEBUG_PRINTLN("Function 9: get_sensor_humid() SUCCESS: Moisture = %d%%",moisture);
    return moisture;
}


uint16_t RAK12035_SoilMoisture::get_humidity_full()                 //Command 0A - (r) 2 bytes
{
    uint8_t buf[2] = {0};

    if (!read_rak12035(SOILMOISTURESENSOR_GET_WET_CAL, buf, 2)) {
        MESH_DEBUG_PRINTLN("Function A: get_humidity_full() FAIL: Bad data returned = %02X%02X", buf[0], buf[1]);
        return 0xFFFF;   // error indicator
    }

    uint16_t full = (buf[0] << 8) | buf[1];

    MESH_DEBUG_PRINTLN("Function A: get_humidity_full() SUCCESS: Full = %04X = %d", full, full);
    return full;
}


uint16_t RAK12035_SoilMoisture::get_humidity_zero()                 //Command 0B - 2 bytes
{
    uint8_t buf[2] = {0};

    if (!read_rak12035(SOILMOISTURESENSOR_GET_DRY_CAL, buf, 2)) {
        MESH_DEBUG_PRINTLN("Function B: get_humidity_zero() FAIL: Bad data returned = %02X%02X", buf[0], buf[1]);
        return 0xFFFF;   // error indicator
    }

    uint16_t zero = (buf[0] << 8) | buf[1];

    MESH_DEBUG_PRINTLN("Function B: get_humidity_zero() SUCCESS: Zero = %04X = %d", zero, zero);
    return zero;
}


/*------------------------------------------------------------------------------------------*
 * getEvent() - High-level function to read both moisture and temperature in one call.      *
 *------------------------------------------------------------------------------------------*
 * This function reads the moisture percentage and temperature from the sensor and returns  *
 * them via output parameters. This may be used for the telemerty delivery in the MeshCore  *
 * firmware, with a single function to get all sensor data.                                 *
 *                                                                                          *
 * The function returns true if both readings were successfully obtained, or false if any   *
 * error occurred during I2C communication.                                                 *
 *                                                                                          *
 * This function is currently not used                                                      *
 *------------------------------------------------------------------------------------------*/
bool RAK12035_SoilMoisture::getEvent(uint8_t *humidity, uint16_t *temp)
{
    // Read moisture (0-100%)
    uint8_t moist = get_sensor_moisture();
    if (moist == 0xFF)      //error indicator
        return false;
    MESH_DEBUG_PRINTLN("getEvent() - Humidity = %d", moist);  
    *humidity = moist;

    //Read temperature (degrees C)
    uint16_t t = get_sensor_temperature();
    if (t == 0XFFFF) // error indicator
    return false;
    
    *temp = t; 
    MESH_DEBUG_PRINTLN("getEvent() - Temperature = %d", t);   
    return true;
}

/*------------------------------------------------------------------------------------------*
 * Sensor Power Management and Reset Routines
 * 
 * These routines manage the power and reset state of the sensor. The sensor_on() routine is 
 * designed to power on the sensor and wait for it to become responsive, while the reset() 
 * routine toggles the reset pin and waits for the sensor to respond with a valid version.
 * 
 * They are for a future sensor power management function.
 *------------------------------------------------------------------------------------------*/

bool RAK12035_SoilMoisture::sensor_on()
{
    uint8_t data;
 // This has been commented out due to a pin name conflict with the Heltec v3
 // This will need to be resolved if this funstion is to be utilized in the future

/*
    pinMode(WB_IO2, OUTPUT);
	digitalWrite(WB_IO2, HIGH);     //Turn on Sensor Power

    pinMode(WB_IO4, OUTPUT);        //Set IO4 Pin to Output (connected to *reset on sensor)
	digitalWrite(WB_IO4, LOW);      //*reset - Reset the Sensor
	delay(1);                       //Wait for the minimum *reset, 1mS is longer than required minimum
    digitalWrite(WB_IO4, HIGH);     //Deassert Reset

    delay(10);                     // Wait for the sensor code to complete initialization
*/
	uint8_t v = 0;
    uint32_t timeout = millis();
	while ((!query_sensor()))                    //Wait for sensor to respond to I2C commands, 
	{                                            //indicating it is ready
		if ((millis() - timeout) > 50){          //0.5 second timeout for sensor to respond
            MESH_DEBUG_PRINTLN("reset() - Timeout, no response from I2C commands");
            return false;
        }
        else {
			delay(10);                           //delay 10mS
        }
    }
}

bool RAK12035_SoilMoisture::reset()
{
// This function is for a future Sensor Power Management function.
// When power is reapplied this will reset the sensor and wait for it to respond
// with a valid version.
//
// The Atmel 8495 Microcoltroller: Reset input. A low level on this pin for longer than
// the minimum pulse length will generate a reset, even if the clock is not
// running and provided the reset pin has not been disabled. The minimum pulse length is 
// given in Table 25-5 on page 240. 2000ns = .002mS
// Shorter pulses are not guaranteed to generate a reset.
//
//  Power is never removed so the Sensor reset was removed and is not needed, 
//  But might be needed if power is ever switched off.  Here is tested code.

// This has been commented out due to a pin name conflict with the Heltec v3
// This will need to be resolved if this funstion is to be utilized in the future

/*
    pinMode(WB_IO4, OUTPUT);        //Set IO4 Pin to Output (connected to *reset on sensor)
    MESH_DEBUG_PRINTLN("Assert *reset (Low) for 1 mS");
    digitalWrite(WB_IO4, LOW);      //Reset the Sensor
    delay(1);                       //Wait for the minimum *reset, 1mS is longer than required minimum
    MESH_DEBUG_PRINTLN("reset() - De-assert *reset (High)");
    digitalWrite(WB_IO4, HIGH);     // Deassert Reset
*/

    MESH_DEBUG_PRINTLN("reset() - Begin poling in 100mS intervals for a non-zero version");
    uint32_t start_time = millis();
    MESH_DEBUG_PRINTLN("reset() - Timeout, Start Time: %d milliseconds", start_time);

    const uint32_t timeout_ms = 500;    // Wait for 0.5 seconds
    uint32_t start = millis();
    
    while (true) {
        if (query_sensor()) {
            MESH_DEBUG_PRINTLN("reset() - First Pass, Sensor responded with valid version");
            uint32_t stop_time = millis();
            MESH_DEBUG_PRINTLN("reset() - Timeout, Stop Time: %d mS", stop_time);    
            MESH_DEBUG_PRINTLN("reset() - Timeout, Duration: %d mS", (stop_time - start_time));

             return true;
        }
            if (millis() - start > timeout_ms) {
                MESH_DEBUG_PRINTLN("reset() - Timeout waiting for valid sensor version");
                uint32_t stop_time = millis();
                MESH_DEBUG_PRINTLN("reset() - Timeout, Stop Time: %d mS", stop_time);    
                MESH_DEBUG_PRINTLN("reset() - Timeout, Duration: %d mS", (stop_time - start_time));
                return false;
         }
            delay(100);
    }
}

 bool RAK12035_SoilMoisture::query_sensor()
{
    uint8_t v = 0;
    v = get_sensor_version();

   // Treat 0x00 and 0xFF as invalid / bootloader / garbage
    if (v == 0x00 || v == 0xFF) {
        MESH_DEBUG_PRINTLN("query_sensor() FAIL: Version value invalid: %02X", v);
        return false;
    }
        MESH_DEBUG_PRINTLN("query_sensor() SUCCESS: Sensor Present, Version = %02X", v);
        return true;
}


/*------------------------------------------------------------------------------------------*
 * Below are the low-level I2C read and write functions. These handle the actual
 * communication with the sensor registers. The higher-level functions call these
 * to perform specific tasks.
 *------------------------------------------------------------------------------------------*/

bool RAK12035_SoilMoisture::read_rak12035(uint8_t cmd, uint8_t *data, uint8_t length)
{
    _i2c->beginTransmission(_addr);
    _i2c->write(cmd);                     // <-- COMMAND, not register index
    if (_i2c->endTransmission() != 0)
        return false;

    delay(20);

    int received = _i2c->requestFrom(_addr, length);
    if (received != length)
        return false;

    for (int i = 0; i < length; i++)
        data[i] = _i2c->read();

    return true;
}

bool RAK12035_SoilMoisture::write_rak12035(uint8_t cmd, uint8_t *data, uint8_t length)
{
    _i2c->beginTransmission(_addr);
    _i2c->write(cmd);          // <-- COMMAND, not register index

    for (uint8_t i = 0; i < length; i++)
        _i2c->write(data[i]);

    if (_i2c->endTransmission() != 0)
        return false;

    delay(20);
    return true;
}
