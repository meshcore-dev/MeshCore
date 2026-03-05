#include <Arduino.h>
#include "target.h"
#include "DFROBOT_SEN0658.h"

const uint8_t WindCom[] =    { 0x01, 0x03, 0x01, 0xF4, 0x00, 0x04 }; //Wind speed and direction
const uint8_t TempCom[] =    { 0x01, 0x03, 0x01, 0xF8, 0x00, 0x03 }; //Temperature, humidity, noise
const uint8_t AirCom[] =     { 0x01, 0x03, 0x01, 0xFB, 0x00, 0x03 }; //PM2.5, PM10, atmospheric pressure
const uint8_t LightCom[] =   { 0x01, 0x03, 0x01, 0xFE, 0x00, 0x02 }; //Light
const uint8_t BaudCom[] =    { 0x01, 0x06, 0x07, 0xD1, 0x00, 0x06 }; //115200 baud

struct PacketHeader {
  uint8_t address;
  uint8_t function;
  uint8_t validBytes;
} __attribute__((packed));

struct be_uint32_t {
    uint32_t wireBytes;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    operator uint32_t() const { return __builtin_bswap32(wireBytes); }
#else
    operator uint32_t() const { return wireBytes; }
#endif
} __attribute__((packed));

struct be_uint16_t {
    uint16_t wireBytes;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    operator uint16_t() const { return __builtin_bswap16(wireBytes); }
#else
    operator uint16_t() const { return wireBytes; }
#endif
} __attribute__((packed));

struct le_uint16_t {
    uint16_t wireBytes;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    operator uint16_t() const { return wireBytes; }
#else
    operator uint16_t() const { return __builtin_bswap16(wireBytes); }
#endif
} __attribute__((packed));

struct LightPacket {
  PacketHeader header;
  be_uint32_t light;
  le_uint16_t crc;
} __attribute__((packed));

struct WindPacket {
  PacketHeader header;
  be_uint16_t windSpeed;
  be_uint16_t Reserved;
  be_uint16_t windDirection;
  be_uint16_t windAngle;
  le_uint16_t crc;
} __attribute__((packed));

struct TemperaturePacket {
  PacketHeader header;
  be_uint16_t humidity;
  be_uint16_t temperature;
  be_uint16_t noise;
  le_uint16_t crc;
} __attribute__((packed));

struct AirPacket {
  PacketHeader header;
  be_uint16_t pm2_5;
  be_uint16_t pm10;
  be_uint16_t airPressure;
  le_uint16_t crc;
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

template<typename PacketType>
bool DFROBOT_SEN0658::readPacket(const uint8_t command[6], PacketType& packet) {
    flushSerial();
    sendCommand(command);
    if (!readBytes((uint8_t *)&packet, sizeof(PacketType))) {
        MESH_DEBUG_PRINTLN("Could not read packet from SEN0658.");
        return false;
    }
    if (packet.header.address != command[0] || packet.header.function != command[1] || packet.header.validBytes != sizeof(PacketType) - sizeof(PacketHeader) - sizeof(le_uint16_t)) {
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
    WindPacket packet;
    if (!readPacket<WindPacket>(WindCom, packet)) {
        MESH_DEBUG_PRINTLN("Could not read wind data from SEN0658.");
        return false;
    }
    sample.windSpeed = (uint16_t)packet.windSpeed / 100.0; 
    sample.windAngle = (float)(uint16_t)packet.windAngle;
    return true;
}

bool DFROBOT_SEN0658::readLight(DFROBOT_SEN0658_Sample &sample) {
    LightPacket packet;
    if (!readPacket<LightPacket>(LightCom, packet)) {
        MESH_DEBUG_PRINTLN("Could not read light data from SEN0658.");
        return false;
    }
    sample.luminosity = (float)(uint32_t)packet.light;
    return true;
}

bool DFROBOT_SEN0658::readTemperature(DFROBOT_SEN0658_Sample &sample) {
    TemperaturePacket packet;
    if (!readPacket<TemperaturePacket>(TempCom, packet)) {
        MESH_DEBUG_PRINTLN("Could not read temperature data from SEN0658.");
        return false;
    }
    sample.humidity = (uint16_t)packet.humidity / 10.0;
    sample.temperature = (uint16_t)packet.temperature / 10.0;
    sample.noiseDb = (uint16_t)packet.noise / 10.0;
    return true;
}

bool DFROBOT_SEN0658::readAir(DFROBOT_SEN0658_Sample &sample) {
    AirPacket packet;
    if (!readPacket<AirPacket>(AirCom, packet)) {
        MESH_DEBUG_PRINTLN("Could not read air data from SEN0658.");
        return false;
    }
    sample.pm2_5 = (uint16_t)packet.pm2_5;
    sample.pm10 = (uint16_t)packet.pm10;
    sample.airPressure = (uint16_t)packet.airPressure / 10.0;
    return true;
}

bool DFROBOT_SEN0658::readSample(DFROBOT_SEN0658_Sample &sample) {
    sample = {0};
    if (!readWind(sample)) {
        return false;
    }
    if (!readTemperature(sample)) {
        return false;
    }
    if (!readAir(sample)) {
        return false;
    }
    if (!readLight(sample)) {
        return false;
    }
    return true; // Return a dummy sample for this example.
}

void DFROBOT_SEN0658::sendCommand(const uint8_t command[6]) {
    WITH_SEN0658_SERIAL.write(command, 6);
    uint16_t crc = CRC16_2(&command[0], 6);
    WITH_SEN0658_SERIAL.write(crc & 0xFF);
    WITH_SEN0658_SERIAL.write((crc >> 8) & 0xFF);
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