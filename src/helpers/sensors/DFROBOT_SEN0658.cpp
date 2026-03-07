#include <Arduino.h>
#include "target.h"
#include "DFROBOT_SEN0658.h"
#include "EndianTypes.h"

struct PacketHeader {
  uint8_t address;
  uint8_t function;
  uint8_t validBytes;
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

    bool result;
    int i;
    for(i = 0; i < 100; i++) {
    
        WITH_SEN0658_SERIAL.begin(4800);
        sendWriteCommand(0x07D1, 0x0006); // set baud rate to 115200
        flushSerial();

        WITH_SEN0658_SERIAL.end();
        WITH_SEN0658_SERIAL.begin(115200);

        DFROBOT_SEN0658_Sample sample = {0};
        if(result = readSample(sample)) {
            break;
        }

        delay(100);
    }

    if (result) {
        MESH_DEBUG_PRINTLN("SEN0658 started after %i attempts.", i+1);
    } else {
        MESH_DEBUG_PRINTLN("SEN0658 failed to start after %i attempts.", i+1);
    }
        
    return result;
}

bool DFROBOT_SEN0658::hasPendingWork() {
    #if defined(WITH_SEN0658_EN)
    return idleShutdownTime != 0;
    #endif
    
    return false;
}

template<typename PacketType>
bool DFROBOT_SEN0658::readRegisters(uint16_t registerStart, PacketType& packet) {
    const uint16_t dataBytes = sizeof(PacketType) - sizeof(PacketHeader) - sizeof(le_uint16_t);
    const uint16_t registerLength = dataBytes / 2;
    flushSerial();
    sendCommand(0x03, registerStart, registerLength);
    if (!readBytes((uint8_t *)&packet, sizeof(PacketType))) {
        MESH_DEBUG_PRINTLN("Could not read packet from SEN0658.");
        return false;
    }
    if (packet.header.address != 1 || packet.header.function != 3 || packet.header.validBytes != dataBytes) {
        MESH_DEBUG_PRINTLN(
            "Packet header mismatch reading packet from SEN0658. Address: %02X Function: %02X ValidBytes: %02X", 
            (int)packet.header.address, (int)packet.header.function, (int)packet.header.validBytes);
        return false;
    }
    uint16_t crc = DFROBOT_SEN0658::CRC16_2((uint8_t *)&packet, sizeof(PacketType) - sizeof(le_uint16_t));
    if (crc != (uint16_t)packet.crc) {
        MESH_DEBUG_PRINTLN("CRC mismatch %04X != %04X reading packet from SEN0658.", (int)crc, (int)packet.crc);
        return false;
    }
    return true;
}

bool DFROBOT_SEN0658::readWind(DFROBOT_SEN0658_Sample &sample) {
    struct WindPacket {
        PacketHeader header;
        be_uint16_t windSpeed;
        be_uint16_t Reserved;
        be_uint16_t ignoredWindDirection;
        be_uint16_t windAngle;
        le_uint16_t crc;
    } __attribute__((packed)) packet;

    if (!readRegisters<WindPacket>(0x01F4, packet)) {
        MESH_DEBUG_PRINTLN("Could not read wind data from SEN0658.");
        return false;
    }
    // Wind is sometimes really all zeros
    // if (packet.windSpeed == 0 && packet.windAngle == 0) {
    //     MESH_DEBUG_PRINTLN("WindPacket is all zeros.");
    //     return false;
    // }
    sample.windSpeed = (uint16_t)packet.windSpeed / 100.0; 
    sample.windAngle = (float)(uint16_t)packet.windAngle;
    return true;
}

bool DFROBOT_SEN0658::readLight(DFROBOT_SEN0658_Sample &sample) {
    struct LightPacket {
        PacketHeader header;
        be_uint32_t light;
        le_uint16_t crc;
    } __attribute__((packed)) packet;

    if (!readRegisters<LightPacket>(0x01FE, packet)) {
        MESH_DEBUG_PRINTLN("Could not read light data from SEN0658.");
        return false;
    }

    if (packet.light == 0) {
        MESH_DEBUG_PRINTLN("LightPacket is all zeros.");
        return false;
    }

    sample.luminosity = (float)(uint32_t)packet.light;
    return true;
}

bool DFROBOT_SEN0658::readTemperature(DFROBOT_SEN0658_Sample &sample) {
    struct TemperaturePacket {
        PacketHeader header;
        be_uint16_t humidity;
        be_uint16_t temperature;
        be_uint16_t noise;
        le_uint16_t crc;
    } __attribute__((packed)) packet;

    if (!readRegisters<TemperaturePacket>(0x01F8, packet)) {
        MESH_DEBUG_PRINTLN("Could not read temperature data from SEN0658.");
        return false;
    }
    if (packet.humidity == 0 && packet.temperature == 0 && packet.noise == 0) {
        MESH_DEBUG_PRINTLN("TemperaturePacket is all zeros.");
        return false;
    }
    sample.humidity = (uint16_t)packet.humidity / 10.0;
    sample.temperature = (uint16_t)packet.temperature / 10.0;
    sample.noiseDb = (uint16_t)packet.noise / 10.0;
    return true;
}

bool DFROBOT_SEN0658::readAir(DFROBOT_SEN0658_Sample &sample) {
    struct AirPacket {
        PacketHeader header;
        be_uint16_t pm2_5;
        be_uint16_t pm10;
        be_uint16_t airPressure;
        le_uint16_t crc;
    } __attribute__((packed)) packet;

    if (!readRegisters<AirPacket>(0x01FB, packet)) {
        MESH_DEBUG_PRINTLN("Could not read air data from SEN0658.");
        return false;
    }
    if (packet.pm2_5 == 0 && packet.pm10 == 0 && packet.airPressure == 0) {
        MESH_DEBUG_PRINTLN("AirPacket is all zeros.");
        return false;
    }
    sample.pm2_5 = (uint16_t)packet.pm2_5;
    sample.pm10 = (uint16_t)packet.pm10;
    sample.airPressure = (uint16_t)packet.airPressure / 10.0;
    return true;
}

bool DFROBOT_SEN0658::readSample(DFROBOT_SEN0658_Sample &sample) {
    sample = {0};

    bool result;
    int i;
    for(i = 0; i < 100; i++) {
        if(result = readTemperature(sample)) break;
        delay(100);
    }
    if (!result) return false;
    MESH_DEBUG_PRINTLN("SEN0658 readTemperature after %i attempts.", i+1);

    for(i = 0; i < 100; i++) {
        if(result = readAir(sample)) break;
        delay(100);
    }
    if (!result) return false;
    MESH_DEBUG_PRINTLN("SEN0658 readAir after %i attempts.", i+1);

    for(i = 0; i < 100; i++) {
        if(result = readLight(sample)) break;
        delay(100);
    }
    if (!result) return false;
    MESH_DEBUG_PRINTLN("SEN0658 readLight after %i attempts.", i+1);

    for(i = 0; i < 100; i++) {
        if(result = readWind(sample)) break;
        delay(100);
    }
    if (!result) return false;
    MESH_DEBUG_PRINTLN("SEN0658 readWind after %i attempts.", i+1);

    return result;
}

bool DFROBOT_SEN0658::readWindDirectionOffset(uint16_t &offset) {
    struct OffsetPacket {
        PacketHeader header;
        be_uint16_t windOffset;
        le_uint16_t crc;
    } __attribute__((packed)) packet;

    if (!readRegisters<OffsetPacket>(0x6000, packet)) {
        MESH_DEBUG_PRINTLN("Could not read wind direction offset from SEN0658.");
        return false;
    }
    offset = (uint16_t)packet.windOffset;
    return true;
}

bool DFROBOT_SEN0658::writeRegister(uint16_t registerIndex, uint16_t value) {
    sendWriteCommand(registerIndex, value);

    struct SingleRegisterPacket {
        uint8_t address;
        uint8_t function;
        be_uint16_t regiterIndex;
        be_uint16_t registerValue;
        le_uint16_t crc;
    } __attribute__((packed)) packet;

    if (!readBytes((uint8_t *)&packet, sizeof(SingleRegisterPacket))) {
        MESH_DEBUG_PRINTLN("Could not read packet from SEN0658.");
        return false;
    }
    if (packet.address != 1 || packet.function != 6 || packet.regiterIndex != registerIndex) {
        MESH_DEBUG_PRINTLN(
            "Packet header mismatch reading packet from SEN0658. Address: %02X Function: %02X RegisterIndex: %02X", 
            (int)packet.address, (int)packet.function, (int)packet.regiterIndex);
        return false;
    }
    uint16_t crc = DFROBOT_SEN0658::CRC16_2((uint8_t *)&packet, sizeof(SingleRegisterPacket) - sizeof(le_uint16_t));
    if (crc != (uint16_t)packet.crc) {
        MESH_DEBUG_PRINTLN("CRC mismatch %04X != %04X reading packet from SEN0658.",
            (int)crc, (int)packet.crc);
        return false;
    }

    if (packet.registerValue != value) {
        MESH_DEBUG_PRINTLN("Verification mismatch after writing to SEN0658. Wrote %i but read back %i.", 
            (int)value, (int)packet.registerValue);
        return false;
    }

    return true;
}

bool DFROBOT_SEN0658::writeWindDirectionOffset(uint16_t offset) {
    if (offset > 1) {
        MESH_DEBUG_PRINTLN("Invalid wind direction offset value %i. Must be 0 or 1.", (int)offset);
        return false;
    }
    return writeRegister(0x6000, offset);
}

bool DFROBOT_SEN0658::zeroWindSpeed() {
    return writeRegister(0x6001, 0x00AA);
}

bool DFROBOT_SEN0658::zeroRainfall() {
    return writeRegister(0x6002, 0x005A);
}

void DFROBOT_SEN0658::sendCommand(uint8_t function, uint16_t registerStart, uint16_t registerLengthOrValue) {
    uint8_t command[6];
    command[0] = 1;
    command[1] = function;
    command[2] = (registerStart >> 8) & 0xFF;
    command[3] = registerStart & 0xFF;
    command[4] = (registerLengthOrValue >> 8) & 0xFF;
    command[5] = registerLengthOrValue & 0xFF;

#if defined(WITH_SEN0658_EN)
    uint32_t previousIdleShutdownTime = idleShutdownTime;
    uint32_t now = millis();
    idleShutdownTime = now + 30000; // keep enabled for 30 seconds after command
    if (previousIdleShutdownTime == 0) {
        MESH_DEBUG_PRINTLN("SEN0658 power on: idleShutdownTime set to %lu (currently %lu).", idleShutdownTime, now);
        digitalWrite(WITH_SEN0658_EN, HIGH);
        delay(100); // wait for sensor to power up
    }
#endif

    WITH_SEN0658_SERIAL.write(command, 6);
    uint16_t crc = CRC16_2(&command[0], 6);
    WITH_SEN0658_SERIAL.write(crc & 0xFF);
    WITH_SEN0658_SERIAL.write((crc >> 8) & 0xFF);
}


void DFROBOT_SEN0658::sendWriteCommand(uint16_t registerIndex, uint16_t value) {
    sendCommand(0x06, registerIndex, value);
}

void DFROBOT_SEN0658::loop() {
#if defined(WITH_SEN0658_EN)
    uint32_t now = millis();
    if (idleShutdownTime != 0 && now > idleShutdownTime) {
        MESH_DEBUG_PRINTLN("SEN0658 idle shutdown at %lu (was set to %lu).", now, idleShutdownTime);
        digitalWrite(WITH_SEN0658_EN, LOW);
        idleShutdownTime = 0;
    }
#endif
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
    return crc;
}