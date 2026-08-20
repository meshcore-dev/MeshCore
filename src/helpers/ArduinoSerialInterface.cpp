#include "ArduinoSerialInterface.h"

#define RECV_STATE_IDLE        0
#define RECV_STATE_HDR_FOUND   1
#define RECV_STATE_LEN1_FOUND  2
#define RECV_STATE_LEN2_FOUND  3

void ArduinoSerialInterface::enable() { 
  _isEnabled = true;
  _state = RECV_STATE_IDLE;
}
void ArduinoSerialInterface::disable() {
  _isEnabled = false;
}

bool ArduinoSerialInterface::isConnected() const {
  if (_conn_check) return _conn_check();
  return true;   // no way of knowing, so assume yes
}

bool ArduinoSerialInterface::isWriteBusy() const {
  if (_flow_ctl && isConnected()) {
    return const_cast<Stream*>(_serial)->availableForWrite() < (int)(MAX_FRAME_SIZE + 3);
  }
  // while nobody drains the port the TX buffer stays full, so never report
  // busy in that case: it would stall the paced streams on all interfaces
  return false;
}

size_t ArduinoSerialInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    // frame is too big!
    return 0;
  }
  if (_flow_ctl) {
    if (!isConnected()) {
      return len;   // nobody is listening, drop instead of filling the TX buffer
    }
    if (_serial->availableForWrite() < (int)(len + 3)) {
      // a short write would tear the length prefixed framing, and as there is
      // neither a checksum nor a resync marker the receiver would stay out of
      // sync forever - so drop the whole frame instead
      return 0;
    }
  }

  uint8_t hdr[3];
  hdr[0] = '>';
  hdr[1] = (len & 0xFF);  // LSB
  hdr[2] = (len >> 8);    // MSB

  _serial->write(hdr, 3);
  return _serial->write(src, len);
}

size_t ArduinoSerialInterface::checkRecvFrame(uint8_t dest[]) {
  while (_serial->available()) {
    int c = _serial->read();
    if (c < 0) break;

    switch (_state) {
      case RECV_STATE_IDLE:
        if (c == '<') {
          _state = RECV_STATE_HDR_FOUND;
        }
        break;
      case RECV_STATE_HDR_FOUND:
        _frame_len = (uint8_t)c;   // LSB
        _state = RECV_STATE_LEN1_FOUND;
        break;
      case RECV_STATE_LEN1_FOUND:
        _frame_len |= ((uint16_t)c) << 8;   // MSB
        rx_len = 0;
        _state = _frame_len > 0 ? RECV_STATE_LEN2_FOUND : RECV_STATE_IDLE;
        break;
      default:
        if (rx_len < MAX_FRAME_SIZE) {
          rx_buf[rx_len] = (uint8_t)c;   // rest of frame will be discarded if > MAX
        }
        rx_len++;
        if (rx_len >= _frame_len) {  // received a complete frame?
          if (_frame_len > MAX_FRAME_SIZE) _frame_len = MAX_FRAME_SIZE;    // truncate
          memcpy(dest, rx_buf, _frame_len);
          _state = RECV_STATE_IDLE;  // reset state, for next frame
          _last_frame_ms = millis();   // a real client is talking to us
          return _frame_len;
        }
    }
  }
  return 0;
}
