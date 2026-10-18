#include "SerialEthernetInterface.h"

#define RECV_STATE_IDLE        0
#define RECV_STATE_HDR_FOUND   1
#define RECV_STATE_LEN1_FOUND  2
#define RECV_STATE_LEN2_FOUND  3

bool SerialEthernetInterface::begin() {
  return true;
}

void SerialEthernetInterface::enable() {
  if (_isEnabled) return;
  _isEnabled = true;
  clearBuffers();
}

void SerialEthernetInterface::disable() {
  _isEnabled = false;
}

size_t SerialEthernetInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    ETHERNET_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d\n", len);
    return 0;
  }

  if (isConnected() && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      ETHERNET_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

bool SerialEthernetInterface::isWriteBusy() const {
  return false;
}

void SerialEthernetInterface::onClientConnected() {
  _state = RECV_STATE_IDLE;
  _frame_len = 0;
  _rx_len = 0;
}

size_t SerialEthernetInterface::checkRecvFrame(uint8_t dest[]) {

  if (isConnected()) {
    if (send_queue_len > 0) {   // first, check send queue

      _last_write = millis();
      int len = send_queue[0].len;

#if ETHERNET_RAW_LINE
      ETHERNET_DEBUG_PRINTLN("TX line len=%d", len);
      client.write(send_queue[0].buf, len);
      client.write("\r\n", 2);
#else
      uint8_t pkt[3+len]; // use same header as serial interface so client can delimit frames
      pkt[0] = '>';
      pkt[1] = (len & 0xFF);  // LSB
      pkt[2] = (len >> 8);    // MSB
      memcpy(&pkt[3], send_queue[0].buf, send_queue[0].len);
      ETHERNET_DEBUG_PRINTLN("Sending frame len=%d", len);
      #if ETHERNET_DEBUG_LOGGING && ARDUINO
      ETHERNET_DEBUG_PRINTLN("TX frame len=%d", len);
      #endif
      write(pkt, 3 + len);
#endif
      send_queue_len--;
      for (int i = 0; i < send_queue_len; i++) {   // delete top item from queue
        send_queue[i] = send_queue[i + 1];
      }
    } else {
      while (available()) {
        int c = read();
        if (c < 0) break;

#if ETHERNET_RAW_LINE
        if (c == '\r' || c == '\n') {
          if (_rx_len == 0) {
            continue;
          }
          uint16_t out_len = _rx_len;
          if (out_len > MAX_FRAME_SIZE) out_len = MAX_FRAME_SIZE;
          memcpy(dest, _rx_buf, out_len);
          _rx_len = 0;
          return out_len;
        }
        if (_rx_len < MAX_FRAME_SIZE) {
          _rx_buf[_rx_len] = (uint8_t)c;
          _rx_len++;
        }
#else
        switch (_state) {
          case RECV_STATE_IDLE:
            if (c == '<') {
              _state = RECV_STATE_HDR_FOUND;
            }
            break;
          case RECV_STATE_HDR_FOUND:
            _frame_len = (uint8_t)c;
            _state = RECV_STATE_LEN1_FOUND;
            break;
          case RECV_STATE_LEN1_FOUND:
            _frame_len |= ((uint16_t)c) << 8;
            _rx_len = 0;
            _state = _frame_len > 0 ? RECV_STATE_LEN2_FOUND : RECV_STATE_IDLE;
            break;
          default:
            if (_rx_len < MAX_FRAME_SIZE) {
              _rx_buf[_rx_len] = (uint8_t)c;
            }
            _rx_len++;
            if (_rx_len >= _frame_len) {
              if (_frame_len > MAX_FRAME_SIZE) {
                _frame_len = MAX_FRAME_SIZE;
              }
              #if ETHERNET_DEBUG_LOGGING && ARDUINO
              ETHERNET_DEBUG_PRINTLN("RX frame len=%d", _frame_len);
              #endif
              memcpy(dest, _rx_buf, _frame_len);
              _state = RECV_STATE_IDLE;
              return _frame_len;
            }
        }
#endif
      }
    }
  }

  return 0;
}
