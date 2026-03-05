#include <Arduino.h>
#include "target.h"
#include "DFROBOT_SEN0658.h"

const uint8_t WindCom[] =    { 0x01, 0x03, 0x01, 0xF4, 0x00, 0x04 }; //Wind speed and direction
const uint8_t TempCom[] =    { 0x01, 0x03, 0x01, 0xF8, 0x00, 0x03 }; //Temperature, humidity, noise
const uint8_t AirCom[] =     { 0x01, 0x03, 0x01, 0xFB, 0x00, 0x03 }; //PM2.5, PM10, atmospheric pressure
const uint8_t LightCom[] =   { 0x01, 0x03, 0x01, 0xFE, 0x00, 0x02 }; //Light
const uint8_t BaudCom[] =    { 0x01, 0x06, 0x07, 0xD1, 0x00, 0x06 }; //115200 baud

struct LightPacket {
  uint8_t address;
  uint8_t function;
  uint8_t validBytes;
  uint8_t light3;
  uint8_t light2;
  uint8_t light1;
  uint8_t light0;
  uint8_t crcHigh;
  uint8_t crcLow;
} __attribute__((packed));

struct WindPacket {
  uint8_t address;
  uint8_t function;
  uint8_t validBytes;
  uint8_t windSpeedHigh;
  uint8_t windSpeedLow;
  uint8_t Reserved0;
  uint8_t Reserved1;
  uint8_t windDirectionHigh;
  uint8_t windDirectionLow;
  uint8_t windAngleHigh;
  uint8_t windAngleLow;
  uint8_t crcHigh;
  uint8_t crcLow;
} __attribute__((packed));

struct TemperaturePacket {
  uint8_t address;
  uint8_t function;
  uint8_t validBytes;
  uint8_t humidityHigh;
  uint8_t humidityLow;
  uint8_t temperatureHigh;
  uint8_t temperatureLow;
  uint8_t noiseHigh;
  uint8_t noiseLow;
  uint8_t crcHigh;
  uint8_t crcLow;
} __attribute__((packed));

struct AirPacket {
  uint8_t address;
  uint8_t function;
  uint8_t validBytes;
  uint8_t pm2_5High;
  uint8_t pm2_5Low;
  uint8_t pm10High;
  uint8_t pm10Low;
  uint8_t airPressureHigh;
  uint8_t airPressureLow;
  uint8_t crcHigh;
  uint8_t crcLow;
} __attribute__((packed));

void DFROBOT_SEN0658::flushSerial() {
  while (WITH_SEN0658_SERIAL.available() > 0) {
    WITH_SEN0658_SERIAL.read();
  }
}

bool DFROBOT_SEN0658::readBytes(uint8_t *buffer, int len) {
  const int16_t timeout = 500;
  int remaining = len;
  long start = millis();
  while (remaining > 0) {
    if (WITH_SEN0658_SERIAL.available()) {
      *buffer = WITH_SEN0658_SERIAL.read();
      buffer++;
      remaining--;
    }
    if (millis() - start > timeout) {
      MESH_DEBUG_PRINTLN("Timed out reading bytes SEN0658. Read %i of %i bytes.", (int)(len - remaining), (int)len);
      return false;
    }
  }
  return true;
}

bool DFROBOT_SEN0658::begin() {
    WITH_SEN0658_SERIAL.setPins(WITH_SEN0658_RX, WITH_SEN0658_TX);
    WITH_SEN0658_SERIAL.begin(4800);
    sendCommand(BaudCom);
    flushSerial();

    WITH_SEN0658_SERIAL.end();
    WITH_SEN0658_SERIAL.begin(115200);

    DFROBOT_SEN0658_Sample sample = {0};
    if (!readSample(sample))
    {
        MESH_DEBUG_PRINTLN("Could not read sample from SEN0658.");
        return false;
    }
    
    return true;
}

bool DFROBOT_SEN0658::readWind(DFROBOT_SEN0658_Sample &sample) {
    flushSerial();
    sendCommand(WindCom);
    WindPacket packet;
    if (!readBytes((uint8_t *)&packet, sizeof(WindPacket))) {
        MESH_DEBUG_PRINTLN("Could not read wind packet from SEN0658.");
        return false;
    }
    if (packet.address != 0x01 || packet.function != 0x03 || packet.validBytes != sizeof(WindPacket) - 5) {
        MESH_DEBUG_PRINTLN(
            "Packet header mismatch reading wind packet from SEN0658. Address: %02X Function: %02X ValidBytes: %02X", 
            (int)packet.address, (int)packet.function, (int)packet.validBytes);
        return false;
    }
    uint16_t crc = DFROBOT_SEN0658::CRC16_2((uint8_t *)&packet, sizeof(WindPacket) - 2);
    if (crc != ((packet.crcHigh << 8) | packet.crcLow)) {
        MESH_DEBUG_PRINTLN("CRC mismatch %04X != %04X reading wind packet from SEN0658.", (int)crc, (int)((packet.crcHigh << 8) | packet.crcLow));
        return false;
    }
    sample.windSpeed = (((uint16_t)packet.windSpeedHigh << 8) | packet.windSpeedLow) / 100.0; 
    sample.windAngle = (float)(((uint16_t)packet.windAngleHigh << 8) | packet.windAngleLow);
    return true;
}

bool DFROBOT_SEN0658::readLight(DFROBOT_SEN0658_Sample &sample) {
    flushSerial();
    sendCommand(LightCom);
    LightPacket packet;
    if (!readBytes((uint8_t *)&packet, sizeof(LightPacket))) {
        MESH_DEBUG_PRINTLN("Could not read light packet from SEN0658.");
        return false;
    }
    if (packet.address != 0x01 || packet.function != 0x03 || packet.validBytes != sizeof(LightPacket) - 5) {
        MESH_DEBUG_PRINTLN(
            "Packet header mismatch reading light packet from SEN0658. Address: %02X Function: %02X ValidBytes: %02X", 
            (int)packet.address, (int)packet.function, (int)packet.validBytes);
        return false;
    }
    uint16_t crc = DFROBOT_SEN0658::CRC16_2((uint8_t *)&packet, sizeof(LightPacket) - 2);
    if (crc != ((packet.crcHigh << 8) | packet.crcLow)) {
        MESH_DEBUG_PRINTLN("CRC mismatch %04X != %04X reading light packet from SEN0658.", (int)crc, (int)((packet.crcHigh << 8) | packet.crcLow));
        return false;
    }
    sample.luminosity = (float)(((uint32_t)packet.light3 << 24) | ((uint32_t)packet.light2 << 16) | ((uint32_t)packet.light1 << 8) | packet.light0);
    return true;
}

bool DFROBOT_SEN0658::readTemperature(DFROBOT_SEN0658_Sample &sample) {
    flushSerial();
    sendCommand(TempCom);
    TemperaturePacket packet;
    if (!readBytes((uint8_t *)&packet, sizeof(TemperaturePacket))) {
        MESH_DEBUG_PRINTLN("Could not read temperature packet from SEN0658.");
        return false;
    }
    if (packet.address != 0x01 || packet.function != 0x03 || packet.validBytes != sizeof(TemperaturePacket) - 5) {
        MESH_DEBUG_PRINTLN(
            "Packet header mismatch reading temperature packet from SEN0658. Address: %02X Function: %02X ValidBytes: %02X", 
            (int)packet.address, (int)packet.function, (int)packet.validBytes);
        return false;
    }
    uint16_t crc = DFROBOT_SEN0658::CRC16_2((uint8_t *)&packet, sizeof(TemperaturePacket) - 2);
    if (crc != ((packet.crcHigh << 8) | packet.crcLow)) {
        MESH_DEBUG_PRINTLN("CRC mismatch %04X != %04X reading temperature packet from SEN0658.", (int)crc, (int)((packet.crcHigh << 8) | packet.crcLow));
        return false;
    }
    sample.humidity = (((uint16_t)packet.humidityHigh << 8) | packet.humidityLow) / 10.0;
    sample.temperature = (((uint16_t)packet.temperatureHigh << 8) | packet.temperatureLow) / 10.0;
    sample.noiseDb = (((uint16_t)packet.noiseHigh << 8) | packet.noiseLow) / 10.0;
    return true;
}

bool DFROBOT_SEN0658::readAir(DFROBOT_SEN0658_Sample &sample) {
    flushSerial();
    sendCommand(AirCom);
    AirPacket packet;
    if (!readBytes((uint8_t *)&packet, sizeof(AirPacket))) {
        MESH_DEBUG_PRINTLN("Could not read air packet from SEN0658.");
        return false;
    }
    if (packet.address != 0x01 || packet.function != 0x03 || packet.validBytes != sizeof(AirPacket) - 5) {
        MESH_DEBUG_PRINTLN(
            "Packet header mismatch reading air packet from SEN0658. Address: %02X Function: %02X ValidBytes: %02X", 
            (int)packet.address, (int)packet.function, (int)packet.validBytes);
        return false;
    }
    uint16_t crc = DFROBOT_SEN0658::CRC16_2((uint8_t *)&packet, sizeof(AirPacket) - 2);
    if (crc != ((packet.crcHigh << 8) | packet.crcLow)) {
        MESH_DEBUG_PRINTLN("CRC mismatch %04X != %04X reading air packet from SEN0658.", (int)crc, (int)((packet.crcHigh << 8) | packet.crcLow));
        return false;
    }
    sample.pm2_5 = (((uint16_t)packet.pm2_5High << 8) | packet.pm2_5Low);
    sample.pm10 = (((uint16_t)packet.pm10High << 8) | packet.pm10Low);
    sample.airPressure = (((uint16_t)packet.airPressureHigh << 8) | packet.airPressureLow) / 10.0;
    return true;
}

bool DFROBOT_SEN0658::readSample(DFROBOT_SEN0658_Sample &sample) {
    sample = {0};
    if (!readWind(sample)) {
        MESH_DEBUG_PRINTLN("Could not read wind data from SEN0658.");
        return false;
    }
    if (!readTemperature(sample)) {
        MESH_DEBUG_PRINTLN("Could not read temperature from SEN0658.");
        return false;
    }
    if (!readAir(sample)) {
        MESH_DEBUG_PRINTLN("Could not read air data from SEN0658.");
        return false;
    }
    if (!readLight(sample)) {
        MESH_DEBUG_PRINTLN("Could not read light data from SEN0658.");
        return false;
    }
    return true; // Return a dummy sample for this example.
}

void DFROBOT_SEN0658::sendCommand(const uint8_t command[6]) {
    WITH_SEN0658_SERIAL.write(command, 6);
    uint16_t crc = CRC16_2(&command[0], 6);
    WITH_SEN0658_SERIAL.write((crc >> 8) & 0xFF);
    WITH_SEN0658_SERIAL.write(crc & 0xFF);
}

uint16_t DFROBOT_SEN0658::CRC16_2(const uint8_t *buf, int len) {
    uint16_t crc = 0xFFFF;
    for (int pos = 0; pos < len; pos++) {
        crc ^= (uint16_t)buf[pos];
        for (int i = 8; i != 0; i--) {
        if ((crc & 0x0001) != 0) {
            crc >>= 1;
            crc ^= 0xA001;
        } else {
            crc >>= 1;
        }
        }
    }

    crc = ((crc & 0x00ff) << 8) | ((crc & 0xff00) >> 8);
    return crc;
}