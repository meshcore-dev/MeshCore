#pragma once

#include <Arduino.h>
#include <Identity.h>
#include <Utils.h>
#include <Mesh.h>
#include <helpers/SensorManager.h>

#define KISS_FEND  0xC0
#define KISS_FESC  0xDB
#define KISS_TFEND 0xDC
#define KISS_TFESC 0xDD

#define KISS_MAX_FRAME_SIZE 512
#define KISS_MAX_PACKET_SIZE 255

#define CMD_DATA              0x00
#define CMD_GET_IDENTITY      0x01
#define CMD_GET_RANDOM        0x02
#define CMD_VERIFY_SIGNATURE  0x03
#define CMD_SIGN_DATA         0x04
#define CMD_ENCRYPT_DATA      0x05
#define CMD_DECRYPT_DATA      0x06
#define CMD_KEY_EXCHANGE      0x07
#define CMD_HASH              0x08
#define CMD_SET_RADIO         0x09
#define CMD_SET_TX_POWER      0x0A
#define CMD_GET_RADIO         0x0C
#define CMD_GET_TX_POWER      0x0D
#define CMD_GET_VERSION       0x0F
#define CMD_GET_CURRENT_RSSI  0x10
#define CMD_IS_CHANNEL_BUSY   0x11
#define CMD_GET_AIRTIME       0x12
#define CMD_GET_NOISE_FLOOR   0x13
#define CMD_GET_STATS         0x14
#define CMD_GET_BATTERY       0x15
#define CMD_PING              0x16
#define CMD_GET_SENSORS       0x17

#define RESP_IDENTITY         0x21
#define RESP_RANDOM           0x22
#define RESP_VERIFY           0x23
#define RESP_SIGNATURE        0x24
#define RESP_ENCRYPTED        0x25
#define RESP_DECRYPTED        0x26
#define RESP_SHARED_SECRET    0x27
#define RESP_HASH             0x28
#define RESP_OK               0x29
#define RESP_RADIO            0x2A
#define RESP_TX_POWER         0x2B
#define RESP_VERSION          0x2D
#define RESP_ERROR            0x2E
#define RESP_TX_DONE          0x2F
#define RESP_CURRENT_RSSI     0x30
#define RESP_CHANNEL_BUSY     0x31
#define RESP_AIRTIME          0x32
#define RESP_NOISE_FLOOR      0x33
#define RESP_STATS            0x34
#define RESP_BATTERY          0x35
#define RESP_PONG             0x36
#define RESP_SENSORS          0x37

#define ERR_INVALID_LENGTH    0x01
#define ERR_INVALID_PARAM     0x02
#define ERR_NO_CALLBACK       0x03
#define ERR_MAC_FAILED        0x04
#define ERR_UNKNOWN_CMD       0x05
#define ERR_ENCRYPT_FAILED    0x06
#define ERR_TX_PENDING        0x07

#define KISS_FIRMWARE_VERSION 1

typedef void (*SetRadioCallback)(float freq, float bw, uint8_t sf, uint8_t cr);
typedef void (*SetTxPowerCallback)(uint8_t power);
typedef float (*GetCurrentRssiCallback)();
typedef void (*GetStatsCallback)(uint32_t* rx, uint32_t* tx, uint32_t* errors);

struct RadioConfig {
  uint32_t freq_hz;
  uint32_t bw_hz;
  uint8_t sf;
  uint8_t cr;
  uint8_t tx_power;
};

class KissModem {
  Stream& _serial;
  mesh::LocalIdentity& _identity;
  mesh::RNG& _rng;
  mesh::Radio& _radio;
  mesh::MainBoard& _board;
  SensorManager& _sensors;
  
  uint8_t _rx_buf[KISS_MAX_FRAME_SIZE];
  uint16_t _rx_len;
  bool _rx_escaped;
  bool _rx_active;
  
  uint8_t _pending_tx[KISS_MAX_PACKET_SIZE];
  uint16_t _pending_tx_len;
  bool _has_pending_tx;

  SetRadioCallback _setRadioCallback;
  SetTxPowerCallback _setTxPowerCallback;
  GetCurrentRssiCallback _getCurrentRssiCallback;
  GetStatsCallback _getStatsCallback;
  
  RadioConfig _config;

  void writeByte(uint8_t b);
  void writeFrame(uint8_t cmd, const uint8_t* data, uint16_t len);
  void writeErrorFrame(uint8_t error_code);
  void processFrame();
  
  void handleGetIdentity();
  void handleGetRandom(const uint8_t* data, uint16_t len);
  void handleVerifySignature(const uint8_t* data, uint16_t len);
  void handleSignData(const uint8_t* data, uint16_t len);
  void handleEncryptData(const uint8_t* data, uint16_t len);
  void handleDecryptData(const uint8_t* data, uint16_t len);
  void handleKeyExchange(const uint8_t* data, uint16_t len);
  void handleHash(const uint8_t* data, uint16_t len);
  void handleSetRadio(const uint8_t* data, uint16_t len);
  void handleSetTxPower(const uint8_t* data, uint16_t len);
  void handleGetRadio();
  void handleGetTxPower();
  void handleGetVersion();
  void handleGetCurrentRssi();
  void handleIsChannelBusy();
  void handleGetAirtime(const uint8_t* data, uint16_t len);
  void handleGetNoiseFloor();
  void handleGetStats();
  void handleGetBattery();
  void handlePing();
  void handleGetSensors(const uint8_t* data, uint16_t len);

public:
  KissModem(Stream& serial, mesh::LocalIdentity& identity, mesh::RNG& rng,
            mesh::Radio& radio, mesh::MainBoard& board, SensorManager& sensors);
  
  void begin();
  void loop();
  
  void setRadioCallback(SetRadioCallback cb) { _setRadioCallback = cb; }
  void setTxPowerCallback(SetTxPowerCallback cb) { _setTxPowerCallback = cb; }
  void setGetCurrentRssiCallback(GetCurrentRssiCallback cb) { _getCurrentRssiCallback = cb; }
  void setGetStatsCallback(GetStatsCallback cb) { _getStatsCallback = cb; }
  
  bool getPacketToSend(uint8_t* packet, uint16_t* len);
  void onPacketReceived(int8_t snr, int8_t rssi, const uint8_t* packet, uint16_t len);
  void onTxComplete(bool success);
};
