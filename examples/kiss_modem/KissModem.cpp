#include "KissModem.h"
#include <CayenneLPP.h>

KissModem::KissModem(Stream& serial, mesh::LocalIdentity& identity, mesh::RNG& rng,
                     mesh::Radio& radio, mesh::MainBoard& board, SensorManager& sensors)
  : _serial(serial), _identity(identity), _rng(rng), _radio(radio), _board(board), _sensors(sensors) {
  _rx_len = 0;
  _rx_escaped = false;
  _rx_active = false;
  _has_pending_tx = false;
  _pending_tx_len = 0;
  _setRadioCallback = nullptr;
  _setTxPowerCallback = nullptr;
  _getCurrentRssiCallback = nullptr;
  _getStatsCallback = nullptr;
  _config = {0, 0, 0, 0, 0};
}

void KissModem::begin() {
  _rx_len = 0;
  _rx_escaped = false;
  _rx_active = false;
  _has_pending_tx = false;
}

void KissModem::writeByte(uint8_t b) {
  if (b == KISS_FEND) {
    _serial.write(KISS_FESC);
    _serial.write(KISS_TFEND);
  } else if (b == KISS_FESC) {
    _serial.write(KISS_FESC);
    _serial.write(KISS_TFESC);
  } else {
    _serial.write(b);
  }
}

void KissModem::writeFrame(uint8_t cmd, const uint8_t* data, uint16_t len) {
  _serial.write(KISS_FEND);
  writeByte(cmd);
  for (uint16_t i = 0; i < len; i++) {
    writeByte(data[i]);
  }
  _serial.write(KISS_FEND);
}

void KissModem::writeErrorFrame(uint8_t error_code) {
  writeFrame(RESP_ERROR, &error_code, 1);
}

void KissModem::loop() {
  while (_serial.available()) {
    uint8_t b = _serial.read();
    
    if (b == KISS_FEND) {
      if (_rx_active && _rx_len > 0) {
        processFrame();
      }
      _rx_len = 0;
      _rx_escaped = false;
      _rx_active = true;
      continue;
    }
    
    if (!_rx_active) continue;
    
    if (b == KISS_FESC) {
      _rx_escaped = true;
      continue;
    }
    
    if (_rx_escaped) {
      _rx_escaped = false;
      if (b == KISS_TFEND) b = KISS_FEND;
      else if (b == KISS_TFESC) b = KISS_FESC;
    }
    
    if (_rx_len < KISS_MAX_FRAME_SIZE) {
      _rx_buf[_rx_len++] = b;
    }
  }
}

void KissModem::processFrame() {
  if (_rx_len < 1) return;
  
  uint8_t cmd = _rx_buf[0];
  const uint8_t* data = &_rx_buf[1];
  uint16_t data_len = _rx_len - 1;
  
  switch (cmd) {
    case CMD_DATA:
      if (data_len < 2) {
        writeErrorFrame(ERR_INVALID_LENGTH);
      } else if (data_len > KISS_MAX_PACKET_SIZE) {
        writeErrorFrame(ERR_INVALID_LENGTH);
      } else if (_has_pending_tx) {
        writeErrorFrame(ERR_TX_PENDING);
      } else {
        memcpy(_pending_tx, data, data_len);
        _pending_tx_len = data_len;
        _has_pending_tx = true;
      }
      break;
    case CMD_GET_IDENTITY:
      handleGetIdentity();
      break;
    case CMD_GET_RANDOM:
      handleGetRandom(data, data_len);
      break;
    case CMD_VERIFY_SIGNATURE:
      handleVerifySignature(data, data_len);
      break;
    case CMD_SIGN_DATA:
      handleSignData(data, data_len);
      break;
    case CMD_ENCRYPT_DATA:
      handleEncryptData(data, data_len);
      break;
    case CMD_DECRYPT_DATA:
      handleDecryptData(data, data_len);
      break;
    case CMD_KEY_EXCHANGE:
      handleKeyExchange(data, data_len);
      break;
    case CMD_HASH:
      handleHash(data, data_len);
      break;
    case CMD_SET_RADIO:
      handleSetRadio(data, data_len);
      break;
    case CMD_SET_TX_POWER:
      handleSetTxPower(data, data_len);
      break;
    case CMD_GET_RADIO:
      handleGetRadio();
      break;
    case CMD_GET_TX_POWER:
      handleGetTxPower();
      break;
    case CMD_GET_VERSION:
      handleGetVersion();
      break;
    case CMD_GET_CURRENT_RSSI:
      handleGetCurrentRssi();
      break;
    case CMD_IS_CHANNEL_BUSY:
      handleIsChannelBusy();
      break;
    case CMD_GET_AIRTIME:
      handleGetAirtime(data, data_len);
      break;
    case CMD_GET_NOISE_FLOOR:
      handleGetNoiseFloor();
      break;
    case CMD_GET_STATS:
      handleGetStats();
      break;
    case CMD_GET_BATTERY:
      handleGetBattery();
      break;
    case CMD_PING:
      handlePing();
      break;
    case CMD_GET_SENSORS:
      handleGetSensors(data, data_len);
      break;
    default:
      writeErrorFrame(ERR_UNKNOWN_CMD);
      break;
  }
}

void KissModem::handleGetIdentity() {
  writeFrame(RESP_IDENTITY, _identity.pub_key, PUB_KEY_SIZE);
}

void KissModem::handleGetRandom(const uint8_t* data, uint16_t len) {
  if (len < 1) {
    writeErrorFrame(ERR_INVALID_LENGTH);
    return;
  }
  
  uint8_t requested = data[0];
  if (requested < 1 || requested > 64) {
    writeErrorFrame(ERR_INVALID_PARAM);
    return;
  }
  
  uint8_t buf[64];
  _rng.random(buf, requested);
  writeFrame(RESP_RANDOM, buf, requested);
}

void KissModem::handleVerifySignature(const uint8_t* data, uint16_t len) {
  if (len < PUB_KEY_SIZE + SIGNATURE_SIZE + 1) {
    writeErrorFrame(ERR_INVALID_LENGTH);
    return;
  }
  
  mesh::Identity signer(data);
  const uint8_t* signature = data + PUB_KEY_SIZE;
  const uint8_t* msg = data + PUB_KEY_SIZE + SIGNATURE_SIZE;
  uint16_t msg_len = len - PUB_KEY_SIZE - SIGNATURE_SIZE;
  
  uint8_t result = signer.verify(signature, msg, msg_len) ? 0x01 : 0x00;
  writeFrame(RESP_VERIFY, &result, 1);
}

void KissModem::handleSignData(const uint8_t* data, uint16_t len) {
  if (len < 1) {
    writeErrorFrame(ERR_INVALID_LENGTH);
    return;
  }
  
  uint8_t signature[SIGNATURE_SIZE];
  _identity.sign(signature, data, len);
  writeFrame(RESP_SIGNATURE, signature, SIGNATURE_SIZE);
}

void KissModem::handleEncryptData(const uint8_t* data, uint16_t len) {
  if (len < PUB_KEY_SIZE + 1) {
    writeErrorFrame(ERR_INVALID_LENGTH);
    return;
  }
  
  const uint8_t* key = data;
  const uint8_t* plaintext = data + PUB_KEY_SIZE;
  uint16_t plaintext_len = len - PUB_KEY_SIZE;
  
  uint8_t buf[KISS_MAX_FRAME_SIZE];
  int encrypted_len = mesh::Utils::encryptThenMAC(key, buf, plaintext, plaintext_len);
  
  if (encrypted_len > 0) {
    writeFrame(RESP_ENCRYPTED, buf, encrypted_len);
  } else {
    writeErrorFrame(ERR_ENCRYPT_FAILED);
  }
}

void KissModem::handleDecryptData(const uint8_t* data, uint16_t len) {
  if (len < PUB_KEY_SIZE + CIPHER_MAC_SIZE + 1) {
    writeErrorFrame(ERR_INVALID_LENGTH);
    return;
  }
  
  const uint8_t* key = data;
  const uint8_t* ciphertext = data + PUB_KEY_SIZE;
  uint16_t ciphertext_len = len - PUB_KEY_SIZE;
  
  uint8_t buf[KISS_MAX_FRAME_SIZE];
  int decrypted_len = mesh::Utils::MACThenDecrypt(key, buf, ciphertext, ciphertext_len);
  
  if (decrypted_len > 0) {
    writeFrame(RESP_DECRYPTED, buf, decrypted_len);
  } else {
    writeErrorFrame(ERR_MAC_FAILED);
  }
}

void KissModem::handleKeyExchange(const uint8_t* data, uint16_t len) {
  if (len < PUB_KEY_SIZE) {
    writeErrorFrame(ERR_INVALID_LENGTH);
    return;
  }
  
  uint8_t shared_secret[PUB_KEY_SIZE];
  _identity.calcSharedSecret(shared_secret, data);
  writeFrame(RESP_SHARED_SECRET, shared_secret, PUB_KEY_SIZE);
}

void KissModem::handleHash(const uint8_t* data, uint16_t len) {
  if (len < 1) {
    writeErrorFrame(ERR_INVALID_LENGTH);
    return;
  }
  
  uint8_t hash[32];
  mesh::Utils::sha256(hash, 32, data, len);
  writeFrame(RESP_HASH, hash, 32);
}

bool KissModem::getPacketToSend(uint8_t* packet, uint16_t* len) {
  if (!_has_pending_tx) return false;
  
  memcpy(packet, _pending_tx, _pending_tx_len);
  *len = _pending_tx_len;
  _has_pending_tx = false;
  return true;
}

void KissModem::onPacketReceived(int8_t snr, int8_t rssi, const uint8_t* packet, uint16_t len) {
  uint8_t buf[2 + KISS_MAX_PACKET_SIZE];
  buf[0] = (uint8_t)snr;
  buf[1] = (uint8_t)rssi;
  memcpy(&buf[2], packet, len);
  writeFrame(CMD_DATA, buf, 2 + len);
}

void KissModem::handleSetRadio(const uint8_t* data, uint16_t len) {
  if (len < 10) {
    writeErrorFrame(ERR_INVALID_LENGTH);
    return;
  }
  if (!_setRadioCallback) {
    writeErrorFrame(ERR_NO_CALLBACK);
    return;
  }
  
  uint32_t freq_hz, bw_hz;
  memcpy(&freq_hz, data, 4);
  memcpy(&bw_hz, data + 4, 4);
  uint8_t sf = data[8];
  uint8_t cr = data[9];
  
  _config.freq_hz = freq_hz;
  _config.bw_hz = bw_hz;
  _config.sf = sf;
  _config.cr = cr;
  
  float freq = freq_hz / 1000000.0f;
  float bw = bw_hz / 1000.0f;
  
  _setRadioCallback(freq, bw, sf, cr);
  writeFrame(RESP_OK, nullptr, 0);
}

void KissModem::handleSetTxPower(const uint8_t* data, uint16_t len) {
  if (len < 1) {
    writeErrorFrame(ERR_INVALID_LENGTH);
    return;
  }
  if (!_setTxPowerCallback) {
    writeErrorFrame(ERR_NO_CALLBACK);
    return;
  }
  
  _config.tx_power = data[0];
  _setTxPowerCallback(data[0]);
  writeFrame(RESP_OK, nullptr, 0);
}

void KissModem::handleGetRadio() {
  uint8_t buf[10];
  memcpy(buf, &_config.freq_hz, 4);
  memcpy(buf + 4, &_config.bw_hz, 4);
  buf[8] = _config.sf;
  buf[9] = _config.cr;
  writeFrame(RESP_RADIO, buf, 10);
}

void KissModem::handleGetTxPower() {
  writeFrame(RESP_TX_POWER, &_config.tx_power, 1);
}

void KissModem::handleGetVersion() {
  uint8_t buf[2];
  buf[0] = KISS_FIRMWARE_VERSION;
  buf[1] = 0;
  writeFrame(RESP_VERSION, buf, 2);
}

void KissModem::onTxComplete(bool success) {
  uint8_t result = success ? 0x01 : 0x00;
  writeFrame(RESP_TX_DONE, &result, 1);
}

void KissModem::handleGetCurrentRssi() {
  if (!_getCurrentRssiCallback) {
    writeErrorFrame(ERR_NO_CALLBACK);
    return;
  }
  
  float rssi = _getCurrentRssiCallback();
  int8_t rssi_byte = (int8_t)rssi;
  writeFrame(RESP_CURRENT_RSSI, (uint8_t*)&rssi_byte, 1);
}

void KissModem::handleIsChannelBusy() {
  uint8_t busy = _radio.isReceiving() ? 0x01 : 0x00;
  writeFrame(RESP_CHANNEL_BUSY, &busy, 1);
}

void KissModem::handleGetAirtime(const uint8_t* data, uint16_t len) {
  if (len < 1) {
    writeErrorFrame(ERR_INVALID_LENGTH);
    return;
  }
  
  uint8_t packet_len = data[0];
  uint32_t airtime = _radio.getEstAirtimeFor(packet_len);
  writeFrame(RESP_AIRTIME, (uint8_t*)&airtime, 4);
}

void KissModem::handleGetNoiseFloor() {
  int16_t noise_floor = _radio.getNoiseFloor();
  writeFrame(RESP_NOISE_FLOOR, (uint8_t*)&noise_floor, 2);
}

void KissModem::handleGetStats() {
  if (!_getStatsCallback) {
    writeErrorFrame(ERR_NO_CALLBACK);
    return;
  }
  
  uint32_t rx, tx, errors;
  _getStatsCallback(&rx, &tx, &errors);
  uint8_t buf[12];
  memcpy(buf, &rx, 4);
  memcpy(buf + 4, &tx, 4);
  memcpy(buf + 8, &errors, 4);
  writeFrame(RESP_STATS, buf, 12);
}

void KissModem::handleGetBattery() {
  uint16_t mv = _board.getBattMilliVolts();
  writeFrame(RESP_BATTERY, (uint8_t*)&mv, 2);
}

void KissModem::handlePing() {
  writeFrame(RESP_PONG, nullptr, 0);
}

void KissModem::handleGetSensors(const uint8_t* data, uint16_t len) {
  if (len < 1) {
    writeErrorFrame(ERR_INVALID_LENGTH);
    return;
  }
  
  uint8_t permissions = data[0];
  CayenneLPP telemetry(255);
  if (_sensors.querySensors(permissions, telemetry)) {
    writeFrame(RESP_SENSORS, telemetry.getBuffer(), telemetry.getSize());
  } else {
    writeFrame(RESP_SENSORS, nullptr, 0);
  }
}
