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
#if defined(NRF52_PLATFORM) && defined(USE_TINYUSB)
  // Adafruit_USBD_CDC::operator bool() reports DTR/line-state. Assumes
  // _serial is bound to the global Serial (the TinyUSB CDC), which is true
  // for every current caller — see begin().
  // Caveat: tools that don't assert DTR (raw `cat`, some picocom configs)
  // will appear disconnected; outgoing frames will be skipped rather than
  // queued. Drivers/apps that open the port normally (Linux CDC ACM, the
  // MeshCore companion apps, screen, default picocom/minicom) assert DTR.
  return (bool) Serial;
#else
  return true;   // HardwareSerial / other Streams have no connection concept
#endif
}

bool ArduinoSerialInterface::isWriteBusy() const {
#if defined(NRF52_PLATFORM) && defined(USE_TINYUSB)
  // Pace callers (e.g. the contact iterator in MyMesh::checkSerialInterface)
  // so we don't blow past the 256-byte TinyUSB CDC TX FIFO. A worst-case
  // frame is MAX_FRAME_SIZE + 3 header bytes.
  return _serial->availableForWrite() < MAX_FRAME_SIZE + 3;
#else
  // UART-backed TX rings on other platforms are often smaller than a single
  // frame (RP2040: 32 B, STM32: 64 B), so this threshold would stall the
  // iterator. Keep the old no-pacing behavior until a per-transport
  // threshold is introduced.
  return false;
#endif
}

size_t ArduinoSerialInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    // frame is too big!
    return 0;
  }
  if (!isConnected()) {
    return 0;   // don't push data into a closed USB CDC pipe
  }

  // Single contiguous write so a partial transfer can't leave a header on the
  // wire with a truncated payload (which would desync the host parser).
  uint8_t buf[MAX_FRAME_SIZE + 3];
  buf[0] = '>';
  buf[1] = (len & 0xFF);  // LSB
  buf[2] = (len >> 8);    // MSB
  memcpy(&buf[3], src, len);

  size_t written = _serial->write(buf, len + 3);
  return written >= len + 3 ? len : 0;
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
          return _frame_len;
        }
    }
  }
  return 0;
}
