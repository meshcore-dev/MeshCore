#include "LifeLineBle.h"

#ifndef MAX_LORA_TX_POWER
  #define MAX_LORA_TX_POWER LORA_TX_POWER
#endif

LifeLineBle::LifeLineBle(MyMesh& mesh, mesh::MainBoard& board) : _mesh(mesh), _board(board) {
  _name[0] = 0;
  _phone_lat = _phone_lon = 0;
}

void LifeLineBle::begin(uint32_t pin_code) {
  // name = prefix + first 2 public key bytes in hex, same scheme as wristband_sim
  const uint8_t* pub = _mesh.self_id.pub_key;
  snprintf(_name, sizeof(_name), "%02X%02X", pub[0], pub[1]);
  _ble.begin(LIFELINE_BLE_NAME_PREFIX, _name, pin_code);
  _ble.enable();
}

void LifeLineBle::loop() {
  size_t len = _ble.checkRecvFrame(_in);
  if (len > 0) {
    handleFrame(_in, len);
  }
}

void LifeLineBle::handleFrame(const uint8_t* frame, size_t len) {
  switch (frame[0]) {
    case LL_CMD_APP_START:
      writeSelfInfo();
      break;
    case LL_CMD_GET_BATT:
      writeBattery();
      break;
    case LL_CMD_SET_LOCATION: {   // [F0][lat int32 x1e6][lon int32 x1e6]
      if (len < 9) { writeErr(LL_ERR_ILLEGAL_ARG); break; }
      int32_t lat, lon;
      memcpy(&lat, &frame[1], 4);
      memcpy(&lon, &frame[5], 4);
      if (lat < -90000000 || lat > 90000000 || lon < -180000000 || lon > 180000000) {
        writeErr(LL_ERR_ILLEGAL_ARG);
        break;
      }
      _phone_lat = lat;
      _phone_lon = lon;
      writeOk();
      break;
    }
    case LL_CMD_SOS_ACK:      // [F1][seq uint16]  TODO: stop SOS retries (no SOS button yet)
    case LL_CMD_SOS_CANCEL:   // [F3][seq uint16]
      if (len < 3) { writeErr(LL_ERR_ILLEGAL_ARG); break; }
      writeOk();
      break;
    case LL_CMD_SOS_RESULT:   // [F2][seq uint16][sent uint8][total uint8]  TODO: LED feedback
      if (len < 5) { writeErr(LL_ERR_ILLEGAL_ARG); break; }
      writeOk();
      break;
    default:
      writeErr(LL_ERR_UNSUPPORTED_CMD);
      break;
  }
}

void LifeLineBle::writeOk() {
  _out[0] = LL_RESP_OK;
  _ble.writeFrame(_out, 1);
}

void LifeLineBle::writeErr(uint8_t code) {
  _out[0] = LL_RESP_ERR;
  _out[1] = code;
  _ble.writeFrame(_out, 2);
}

// MeshCore SELF_INFO layout, so the LifeLine app parses it unchanged.
void LifeLineBle::writeSelfInfo() {
  NodePrefs* prefs = _mesh.getNodePrefs();
  int i = 0;
  _out[i++] = LL_RESP_SELF_INFO;
  _out[i++] = ADV_TYPE_REPEATER;
  _out[i++] = prefs->tx_power_dbm;
  _out[i++] = MAX_LORA_TX_POWER;
  memcpy(&_out[i], _mesh.self_id.pub_key, PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  memcpy(&_out[i], &_phone_lat, 4); i += 4;
  memcpy(&_out[i], &_phone_lon, 4); i += 4;
  _out[i++] = 0;   // multi acks
  _out[i++] = 0;   // advert location policy
  _out[i++] = 0;   // telemetry modes
  _out[i++] = 0;   // manual add contacts
  uint32_t freq = prefs->freq * 1000;
  memcpy(&_out[i], &freq, 4); i += 4;
  uint32_t bw = prefs->bw * 1000;
  memcpy(&_out[i], &bw, 4); i += 4;
  _out[i++] = prefs->sf;
  _out[i++] = prefs->cr;
  int n = snprintf((char*)&_out[i], sizeof(_out) - i, "%s%s", LIFELINE_BLE_NAME_PREFIX, _name);
  i += n;
  _ble.writeFrame(_out, i);
}

// MeshCore BATT_AND_STORAGE: [0C][mV uint16][used KB uint32][total KB uint32]
void LifeLineBle::writeBattery() {
  uint16_t mv = _board.getBattMilliVolts();
  uint32_t zero = 0;
  _out[0] = LL_RESP_BATT;
  memcpy(&_out[1], &mv, 2);
  memcpy(&_out[3], &zero, 4);
  memcpy(&_out[7], &zero, 4);
  _ble.writeFrame(_out, 11);
}
