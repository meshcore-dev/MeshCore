#include "SerialWifiInterface.h"
#include <WiFi.h>

static bool readClientExact(WiFiClient& client, uint8_t* dest, size_t len) {
  if (len == 0) return true;
  return client.readBytes(dest, len) == len;
}

static bool discardClientExact(WiFiClient& client, size_t len) {
  uint8_t skip[16];
  while (len > 0) {
    size_t chunk = len > sizeof(skip) ? sizeof(skip) : len;
    int n = client.read(skip, chunk);
    if (n <= 0) return false;
    len -= n;
  }
  return true;
}

void SerialWifiInterface::begin(int port) {
  // wifi setup is handled outside of this class, only starts the server
  server.begin(port);
}

// ---------- public methods
void SerialWifiInterface::enable() { 
  if (_isEnabled) return;

  _isEnabled = true;
  clearBuffers();
}

void SerialWifiInterface::disable() {
  _isEnabled = false;
}

size_t SerialWifiInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    WIFI_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d\n", len);
    _drop_count++;
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      WIFI_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      _drop_count++;
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

bool SerialWifiInterface::isWriteBusy() const {
  return false;
}

bool SerialWifiInterface::hasReceivedFrameHeader() {
  return received_frame_header.type != 0 && received_frame_header.length != 0;
}

void SerialWifiInterface::resetReceivedFrameHeader() {
  received_frame_header.type = 0;
  received_frame_header.length = 0;
}

size_t SerialWifiInterface::checkRecvFrame(uint8_t dest[]) {
  // check if new client connected
  auto newClient = server.available();
  if (newClient) {

    // disconnect existing client
    deviceConnected = false;
    client.stop();

    // switch active connection to new client
    client = newClient;

    // forget received frame header
    resetReceivedFrameHeader();
    
  }

  if (client.connected()) {
    if (!deviceConnected) {
      WIFI_DEBUG_PRINTLN("Got connection");
      deviceConnected = true;
    }
  } else {
    if (deviceConnected) {
      deviceConnected = false;
      WIFI_DEBUG_PRINTLN("Disconnected");
    }
  }

  if (deviceConnected) {
    if (send_queue_len > 0) {   // first, check send queue
      
      _last_write = millis();
      int len = send_queue[0].len;

      uint8_t pkt[3+len]; // use same header as serial interface so client can delimit frames
      pkt[0] = '>';
      pkt[1] = (len & 0xFF);  // LSB
      pkt[2] = (len >> 8);    // MSB
      memcpy(&pkt[3], send_queue[0].buf, send_queue[0].len);
      size_t written = client.write(pkt, 3 + len);
      if (written == (size_t)(3 + len)) {
        send_queue_len--;
        for (int i = 0; i < send_queue_len; i++) {   // delete top item from queue
          send_queue[i] = send_queue[i + 1];
        }
      } else if (written > 0) {
        WIFI_DEBUG_PRINTLN("Partial write: wrote %d of %d bytes, disconnecting to resync framing", (int)written, 3 + len);
        _drop_count += send_queue_len;
        client.stop();
        deviceConnected = false;
        clearBuffers();
        resetReceivedFrameHeader();
      } else {
        WIFI_DEBUG_PRINTLN("Write returned 0 bytes, keeping frame for retry");
      }
    } else {

      // check if we are waiting for a frame header
      if(!hasReceivedFrameHeader()){

        // make sure we have received enough bytes for a frame header
        // 3 bytes frame header = (1 byte frame type) + (2 bytes frame length as unsigned 16-bit little endian)
        int frame_header_length = 3;
        if(client.available() >= frame_header_length){

          // read frame header
          bool ok = readClientExact(client, &received_frame_header.type, 1);
          ok = ok && readClientExact(client, (uint8_t*)&received_frame_header.length, 2);
          if (!ok) {
            WIFI_DEBUG_PRINTLN("Failed to read frame header exactly");
            _drop_count++;
            client.stop();
            deviceConnected = false;
            clearBuffers();
            resetReceivedFrameHeader();
            return 0;
          }

        }

      }

      // check if we have received a frame header
      if(hasReceivedFrameHeader()){

        // make sure we have received enough bytes for the required frame length
        int available = client.available();
        int frame_type = received_frame_header.type;
        int frame_length = received_frame_header.length;
        if(frame_length > available){
          WIFI_DEBUG_PRINTLN("Waiting for %d more bytes", frame_length - available);
          return 0;
        }

        // skip frames that are larger than MAX_FRAME_SIZE
        if(frame_length > MAX_FRAME_SIZE){
          WIFI_DEBUG_PRINTLN("Skipping frame: length=%d is larger than MAX_FRAME_SIZE=%d", frame_length, MAX_FRAME_SIZE);
          if (!discardClientExact(client, frame_length)) {
            WIFI_DEBUG_PRINTLN("Failed to discard oversized frame");
            _drop_count++;
            client.stop();
            deviceConnected = false;
            clearBuffers();
            resetReceivedFrameHeader();
            return 0;
          }
          _drop_count++;
          resetReceivedFrameHeader();
          return 0;
        }

        // skip frames that are not expected type
        // '<' is 0x3c which indicates a frame sent from app to radio
        if(frame_type != '<'){
          WIFI_DEBUG_PRINTLN("Skipping frame: type=0x%x is unexpected", frame_type);
          if (!discardClientExact(client, frame_length)) {
            WIFI_DEBUG_PRINTLN("Failed to discard unexpected frame");
            _drop_count++;
            client.stop();
            deviceConnected = false;
            clearBuffers();
            resetReceivedFrameHeader();
            return 0;
          }
          _drop_count++;
          resetReceivedFrameHeader();
          return 0;
        }

        // read frame data to provided buffer
        if (!readClientExact(client, dest, frame_length)) {
          WIFI_DEBUG_PRINTLN("Failed to read frame payload exactly");
          _drop_count++;
          client.stop();
          deviceConnected = false;
          clearBuffers();
          resetReceivedFrameHeader();
          return 0;
        }

        // ready for next frame
        resetReceivedFrameHeader();
        return frame_length;

      }
      
    }
  }

  return 0;
}

bool SerialWifiInterface::isConnected() const {
  return deviceConnected;  //pServer != NULL && pServer->getConnectedCount() > 0;
}
