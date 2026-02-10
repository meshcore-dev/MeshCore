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

#define KISS_MAX_FRAME_SIZE  512
#define KISS_MAX_PACKET_SIZE 255

#define KISS_CMD_DATA        0x00
#define KISS_CMD_TXDELAY     0x01
#define KISS_CMD_PERSISTENCE 0x02
#define KISS_CMD_SLOTTIME    0x03
#define KISS_CMD_TXTAIL      0x04
#define KISS_CMD_FULLDUPLEX  0x05
#define KISS_CMD_SETHARDWARE 0x06
#define KISS_CMD_RETURN      0xFF

#define KISS_DEFAULT_TXDELAY     50
#define KISS_DEFAULT_PERSISTENCE 63
#define KISS_DEFAULT_SLOTTIME    10

#define HW_CMD_GET_IDENTITY      0x01
#define HW_CMD_GET_RANDOM        0x02
#define HW_CMD_VERIFY_SIGNATURE  0x03
#define HW_CMD_SIGN_DATA         0x04
#define HW_CMD_ENCRYPT_DATA      0x05
#define HW_CMD_DECRYPT_DATA      0x06
#define HW_CMD_KEY_EXCHANGE      0x07
#define HW_CMD_HASH              0x08
#define HW_CMD_SET_RADIO         0x09
#define HW_CMD_SET_TX_POWER      0x0A
#define HW_CMD_GET_RADIO         0x0B
#define HW_CMD_GET_TX_POWER      0x0C
#define HW_CMD_GET_CURRENT_RSSI  0x0D
#define HW_CMD_IS_CHANNEL_BUSY   0x0E
#define HW_CMD_GET_AIRTIME       0x0F
#define HW_CMD_GET_NOISE_FLOOR   0x10
#define HW_CMD_GET_VERSION       0x11
#define HW_CMD_GET_STATS         0x12
#define HW_CMD_GET_BATTERY       0x13
#define HW_CMD_GET_MCU_TEMP      0x14
#define HW_CMD_GET_SENSORS       0x15
#define HW_CMD_GET_DEVICE_NAME   0x16
#define HW_CMD_PING              0x17
#define HW_CMD_REBOOT            0x18
#define HW_CMD_SET_SIGNAL_REPORT 0x19
#define HW_CMD_GET_SIGNAL_REPORT 0x1A

/* Response code = command code | 0x80.  Generic / unsolicited use 0xF0+. */
#define HW_RESP(cmd)             ((cmd) | 0x80)

/* Generic responses (shared by multiple commands) */
#define HW_RESP_OK               0xF0
#define HW_RESP_ERROR            0xF1

/* Unsolicited notifications (no corresponding request) */
#define HW_RESP_TX_DONE          0xF8
#define HW_RESP_RX_META          0xF9

#define HW_ERR_INVALID_LENGTH    0x01
#define HW_ERR_INVALID_PARAM     0x02
#define HW_ERR_NO_CALLBACK       0x03
#define HW_ERR_MAC_FAILED        0x04
#define HW_ERR_UNKNOWN_CMD       0x05
#define HW_ERR_ENCRYPT_FAILED    0x06

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

enum TxState {
  TX_IDLE,
  TX_WAIT_CLEAR,
  TX_SLOT_WAIT,
  TX_DELAY,
  TX_SENDING
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

  uint8_t _txdelay;
  uint8_t _persistence;
  uint8_t _slottime;
  uint8_t _txtail;
  uint8_t _fullduplex;

  TxState _tx_state;
  uint32_t _tx_timer;

  SetRadioCallback _setRadioCallback;
  SetTxPowerCallback _setTxPowerCallback;
  GetCurrentRssiCallback _getCurrentRssiCallback;
  GetStatsCallback _getStatsCallback;

  RadioConfig _config;
  bool _signal_report_enabled;

  void writeByte(uint8_t b);
  void writeFrame(uint8_t type, const uint8_t* data, uint16_t len);
  void writeHardwareFrame(uint8_t sub_cmd, const uint8_t* data, uint16_t len);
  void writeHardwareError(uint8_t error_code);
  void processFrame();
  void handleHardwareCommand(uint8_t sub_cmd, const uint8_t* data, uint16_t len);
  void processTx();

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
  void handleGetMCUTemp();
  void handleReboot();
  void handleGetDeviceName();
  void handleSetSignalReport(const uint8_t* data, uint16_t len);
  void handleGetSignalReport();

public:
  KissModem(Stream& serial, mesh::LocalIdentity& identity, mesh::RNG& rng,
            mesh::Radio& radio, mesh::MainBoard& board, SensorManager& sensors);

  void begin();
  void loop();

  void setRadioCallback(SetRadioCallback cb) { _setRadioCallback = cb; }
  void setTxPowerCallback(SetTxPowerCallback cb) { _setTxPowerCallback = cb; }
  void setGetCurrentRssiCallback(GetCurrentRssiCallback cb) { _getCurrentRssiCallback = cb; }
  void setGetStatsCallback(GetStatsCallback cb) { _getStatsCallback = cb; }

  void onPacketReceived(int8_t snr, int8_t rssi, const uint8_t* packet, uint16_t len);
  bool isTxBusy() const { return _tx_state != TX_IDLE; }
  /** True only when radio is actually transmitting; use to skip recvRaw in main loop. */
  bool isActuallyTransmitting() const { return _tx_state == TX_SENDING; }
};
