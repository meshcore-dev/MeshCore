#include "SerialEthernetInterface.h"

void SerialEthernetInterface::begin(int port) {
  // Ethernet hardware (Ethernet.init/begin) is brought up in setup();
  // here we only start the TCP server.
  server = new EthernetServer(port);
  server->begin();
}

void SerialEthernetInterface::enable() {
  if (_isEnabled) return;
  _isEnabled = true;
  send_queue_len = 0;
}

void SerialEthernetInterface::disable() {
  _isEnabled = false;
}

size_t SerialEthernetInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    ETH_DEBUG_PRINTLN("writeFrame(): frame too big, len=%d", (int)len);
    return 0;
  }
  if (!_connected || len == 0) return 0;

  if (send_queue_len >= ETH_FRAME_QUEUE_SIZE) {
    ETH_DEBUG_PRINTLN("writeFrame(): send_queue full (dropping code=0x%02x)", src[0]);
    return 0;
  }

  // PUSH codes (>= 0x80) go to all clients; command responses go to the
  // client that issued the most recent command.
  int8_t target = (src[0] >= 0x80) ? -1 : (int8_t)_last_rx;

  ETH_DEBUG_PRINTLN("TX code=0x%02x len=%d -> %s", src[0], (int)len,
                    target < 0 ? "all" : (target == 0 ? "slot0" : target == 1 ? "slot1" : "slot2"));

  send_queue[send_queue_len].target = target;
  send_queue[send_queue_len].len = (uint8_t)len;
  memcpy(send_queue[send_queue_len].buf, src, len);
  send_queue_len++;
  return len;
}

size_t SerialEthernetInterface::checkRecvFrame(uint8_t dest[]) {
  if (server == NULL) return 0;

  // ---- accept a new connection into a free slot --------------------------
  // accept() returns each new connection once and maintains the listen socket,
  // so it must be called every loop.
  EthernetClient nc = server->accept();
  if (nc) {
    int slot = -1;
    for (int i = 0; i < MAX_ETH_CLIENTS; i++) {
      if (!clients[i].connected()) { slot = i; break; }
    }
    if (slot >= 0) {
      clients[slot].stop();            // free any lingering socket in this slot
      clients[slot] = nc;
      rx_header[slot].type = 0;
      rx_header[slot].length = 0;
      ETH_DEBUG_PRINTLN("Got connection (slot %d)", slot);
    } else {
      nc.stop();                       // all slots busy — reject
      ETH_DEBUG_PRINTLN("Rejected connection (all %d slots busy)", MAX_ETH_CLIENTS);
    }
  }

  // ---- refresh connected state, free dropped sockets ---------------------
  bool any = false;
  for (int i = 0; i < MAX_ETH_CLIENTS; i++) {
    if (clients[i].connected()) {
      any = true;
    } else if (rx_header[i].type || rx_header[i].length) {
      // a client that was active just dropped — reset its parse state
      rx_header[i].type = 0;
      rx_header[i].length = 0;
      clients[i].stop();
      ETH_DEBUG_PRINTLN("Disconnected (slot %d)", i);
    }
  }
  _connected = any;

  // ---- drain the outbound queue ------------------------------------------
  while (send_queue_len > 0) {
    Frame &f = send_queue[0];
    uint8_t pkt[3 + MAX_FRAME_SIZE];
    pkt[0] = '>';
    pkt[1] = (f.len & 0xFF);
    pkt[2] = (f.len >> 8);
    memcpy(&pkt[3], f.buf, f.len);

    if (f.target < 0) {                          // broadcast (push)
      for (int i = 0; i < MAX_ETH_CLIENTS; i++) {
        if (clients[i].connected()) clients[i].write(pkt, 3 + f.len);
      }
    } else if (f.target < MAX_ETH_CLIENTS && clients[f.target].connected()) {
      clients[f.target].write(pkt, 3 + f.len);   // response to the requester
    }

    send_queue_len--;
    for (int i = 0; i < send_queue_len; i++) send_queue[i] = send_queue[i + 1];
  }

  // ---- read ONE inbound frame (round-robin across clients) ---------------
  for (int k = 0; k < MAX_ETH_CLIENTS; k++) {
    int i = (_rr + k) % MAX_ETH_CLIENTS;
    EthernetClient &c = clients[i];
    if (!c.connected()) continue;

    // frame header = [type][len_lo][len_hi]
    if (rx_header[i].type == 0 || rx_header[i].length == 0) {
      if (c.available() >= 3) {
        c.readBytes(&rx_header[i].type, 1);
        c.readBytes((uint8_t *)&rx_header[i].length, 2);
      }
    }

    if (rx_header[i].type != 0 && rx_header[i].length != 0) {
      int avail = c.available();
      int frame_type = rx_header[i].type;
      int frame_length = rx_header[i].length;

      if (frame_length > avail) continue;        // wait for the rest

      if (frame_length > MAX_FRAME_SIZE || frame_type != '<') {
        // oversized or unexpected type — discard
        while (frame_length > 0) {
          uint8_t skip[1];
          int n = c.read(skip, 1);
          if (n <= 0) break;
          frame_length -= n;
        }
        rx_header[i].type = 0;
        rx_header[i].length = 0;
        continue;
      }

      c.readBytes(dest, frame_length);
      rx_header[i].type = 0;
      rx_header[i].length = 0;
      _last_rx = i;                              // route responses back here
      _rr = (i + 1) % MAX_ETH_CLIENTS;           // fairness
      ETH_DEBUG_PRINTLN("RX[%d] cmd=0x%02x len=%d", i, dest[0], frame_length);
      return frame_length;
    }
  }

  return 0;
}
