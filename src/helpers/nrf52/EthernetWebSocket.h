#pragma once

// Minimal WebSocket (RFC 6455) server-side helpers for the Ethernet CLI.
// Lets a browser (e.g. the MeshCore web config tool) talk to the same CLI that
// is exposed on the telnet port, since browsers cannot open raw TCP sockets.
// Only compiled into builds with ETHERNET_ENABLED (included from EthernetCLI.h).

#include <Arduino.h>
#include <string.h>

// ---------- SHA-1 (only used for the Sec-WebSocket-Accept handshake) ----------

static inline uint32_t ws_rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void ws_sha1_block(uint32_t h[5], const uint8_t blk[64]) {
  uint32_t w[80];
  for (int i = 0; i < 16; i++) {
    w[i] = ((uint32_t)blk[i*4] << 24) | ((uint32_t)blk[i*4+1] << 16) | ((uint32_t)blk[i*4+2] << 8) | blk[i*4+3];
  }
  for (int i = 16; i < 80; i++) w[i] = ws_rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
    else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
    else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
    else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
    uint32_t t = ws_rol(a, 5) + f + e + k + w[i];
    e = d; d = c; c = ws_rol(b, 30); b = a; a = t;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

static void ws_sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
  uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
  uint8_t blk[64];
  size_t i = 0;
  for (; i + 64 <= len; i += 64) ws_sha1_block(h, data + i);
  size_t rem = len - i;
  memset(blk, 0, 64);
  memcpy(blk, data + i, rem);
  blk[rem] = 0x80;
  if (rem >= 56) { ws_sha1_block(h, blk); memset(blk, 0, 64); }
  uint64_t bits = (uint64_t)len * 8;
  for (int j = 0; j < 8; j++) blk[63 - j] = (uint8_t)(bits >> (j * 8));
  ws_sha1_block(h, blk);
  for (int j = 0; j < 5; j++) {
    out[j*4] = h[j] >> 24; out[j*4+1] = h[j] >> 16; out[j*4+2] = h[j] >> 8; out[j*4+3] = h[j];
  }
}

static void ws_base64(const uint8_t* in, size_t len, char* out) {
  static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16;
    if (i + 1 < len) v |= (uint32_t)in[i+1] << 8;
    if (i + 2 < len) v |= in[i+2];
    out[o++] = tbl[(v >> 18) & 63];
    out[o++] = tbl[(v >> 12) & 63];
    out[o++] = i + 1 < len ? tbl[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < len ? tbl[v & 63] : '=';
  }
  out[o] = 0;
}

// Sec-WebSocket-Accept = base64(sha1(key + GUID)); out must hold >= 29 chars
static void ws_accept_key(const char* key, char* out) {
  char buf[96];
  snprintf(buf, sizeof(buf), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
  uint8_t digest[20];
  ws_sha1((const uint8_t*)buf, strlen(buf), digest);
  ws_base64(digest, 20, out);
}

// ---------- Origin check ----------
// Browsers let any web page open a WebSocket to any host, so without this a
// random site you visit could send admin commands to the node. Only pages served
// from localhost are accepted by default; add your own with
//   -D ETHERNET_WS_ALLOWED_ORIGIN='"http://192.168.1.10:8000"'
// or disable the check entirely with -D ETHERNET_WS_ALLOW_ANY_ORIGIN=1.
// Requests without an Origin header (non-browser clients) are accepted, since
// such clients can already use the telnet port. An Origin header that is present
// (even empty) must match, otherwise the request is rejected.

static bool ws_origin_is_local(const char* origin) {
  const char* host;
  if (strncmp(origin, "http://", 7) == 0) host = origin + 7;
  else if (strncmp(origin, "https://", 8) == 0) host = origin + 8;
  else return false;
  static const char* const local_hosts[] = { "localhost", "127.0.0.1", "[::1]" };
  for (const char* lh : local_hosts) {
    size_t n = strlen(lh);
    if (strncmp(host, lh, n) == 0 && (host[n] == 0 || host[n] == ':')) return true;
  }
  return false;
}

static bool ws_origin_allowed(const char* origin) {
#if defined(ETHERNET_WS_ALLOW_ANY_ORIGIN) && ETHERNET_WS_ALLOW_ANY_ORIGIN
  return true;
#endif
  if (ws_origin_is_local(origin)) return true;
#ifdef ETHERNET_WS_ALLOWED_ORIGIN
  if (strcmp(origin, ETHERNET_WS_ALLOWED_ORIGIN) == 0) return true;
#endif
  return false;
}

// ---------- Handshake + frame decoding ----------

#define WS_HANDSHAKE_MAX   1024
#define WS_HANDSHAKE_TIMEOUT_MS 5000
#define WS_RX_BUF_SIZE     512
#define WS_CTRL_MAX        125
#define WS_TX_CHUNK        512  // max payload per outgoing frame

enum WsState : uint8_t { WS_IDLE, WS_OPEN };
enum WsFrameStep : uint8_t { WSF_HDR0, WSF_HDR1, WSF_EXTLEN, WSF_MASK, WSF_PAYLOAD };

// Pending HTTP upgrade request (kept separate so it can run while another session is open)
struct WsHandshake {
  char req[WS_HANDSHAKE_MAX];
  uint16_t req_len = 0;
  uint32_t started = 0;
  void begin() { req_len = 0; started = millis(); }
};

struct WsSession {
  WsState state = WS_IDLE;

  // frame decoder
  WsFrameStep step = WSF_HDR0;
  uint8_t opcode = 0;
  bool in_message = false;  // inside a fragmented text/binary message
  uint8_t ext_needed = 0, ext_got = 0;
  uint64_t payload_len = 0, payload_got = 0;
  uint8_t mask[4]; uint8_t mask_got = 0;
  uint8_t ctrl[WS_CTRL_MAX]; uint8_t ctrl_len = 0;

  // decoded application data waiting to be consumed by the CLI line reader
  uint8_t rx[WS_RX_BUF_SIZE];
  uint16_t rx_head = 0, rx_tail = 0;

  void reset() {
    state = WS_IDLE; step = WSF_HDR0; opcode = 0; in_message = false;
    ext_needed = ext_got = 0; payload_len = payload_got = 0; mask_got = 0; ctrl_len = 0;
    rx_head = rx_tail = 0;
  }
  int available() const { return (rx_head - rx_tail + WS_RX_BUF_SIZE) % WS_RX_BUF_SIZE; }
  int rx_free() const { return WS_RX_BUF_SIZE - 1 - available(); }
  int read() {
    if (rx_head == rx_tail) return -1;
    uint8_t c = rx[rx_tail]; rx_tail = (rx_tail + 1) % WS_RX_BUF_SIZE; return c;
  }
  bool rx_full() const { return (rx_head + 1) % WS_RX_BUF_SIZE == rx_tail; }
  void rx_push(uint8_t c) {
    if (rx_full()) return;  // safety only: ws_receive never reads more than rx_free()
    rx[rx_head] = c; rx_head = (rx_head + 1) % WS_RX_BUF_SIZE;
  }
};

// Send one frame (server frames are never masked)
template <class C>
static void ws_send_frame(C& client, uint8_t opcode, const uint8_t* data, size_t len) {
  if (len > WS_TX_CHUNK) len = WS_TX_CHUNK;
  uint8_t hdr[4];
  size_t hlen;
  hdr[0] = 0x80 | opcode;  // FIN
  if (len < 126) { hdr[1] = len; hlen = 2; }
  else { hdr[1] = 126; hdr[2] = len >> 8; hdr[3] = len & 0xFF; hlen = 4; }
  // Header and payload go out in ONE write: every write blocks until the peer
  // ACKs it, so two writes would cost two round trips (and delayed-ACK stalls).
  static uint8_t out[4 + WS_TX_CHUNK];
  memcpy(out, hdr, hlen);
  memcpy(out + hlen, data, len);
  client.write(out, hlen + len);
}

template <class C>
static void ws_send_close(C& client, uint16_t code) {
  uint8_t p[2] = { (uint8_t)(code >> 8), (uint8_t)(code & 0xFF) };
  ws_send_frame(client, 0x8, p, 2);
}

template <class C>
static void ws_send_data(C& client, const uint8_t* data, size_t len) {
  while (len > 0) {
    size_t n = len > WS_TX_CHUNK ? WS_TX_CHUNK : len;
    ws_send_frame(client, 0x2 /* binary */, data, n);
    data += n; len -= n;
  }
}

// Case-insensitive search for header "name:" in the request; copies trimmed value.
// Returns 0 = absent, 1 = found and copied, -1 = found but too long for out (out = "").
static int ws_header_value(const char* req, const char* name, char* out, size_t out_size) {
  size_t nlen = strlen(name);
  out[0] = 0;
  for (const char* p = req; *p; ) {
    const char* eol = strstr(p, "\r\n");
    if (!eol) break;
    if ((size_t)(eol - p) > nlen && strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
      const char* v = p + nlen + 1;
      while (*v == ' ' || *v == '\t') v++;
      size_t vlen = eol - v;
      while (vlen > 0 && (v[vlen-1] == ' ' || v[vlen-1] == '\t')) vlen--;
      if (vlen >= out_size) return -1;
      memcpy(out, v, vlen); out[vlen] = 0;
      return 1;
    }
    p = eol + 2;
  }
  return 0;
}

// One print per reply (each write blocks until ACKed)
template <class C>
static void ws_http_reply(C& client, const char* status, const char* extra_hdr) {
  char resp[160];
  snprintf(resp, sizeof(resp), "HTTP/1.1 %s\r\n%sContent-Length: 0\r\nConnection: close\r\n\r\n", status, extra_hdr);
  client.print(resp);
}

// Feed handshake bytes. Returns: 0 = need more, 1 = upgraded (101 sent), -1 = rejected (close it).
template <class C>
static int ws_handshake_step(WsHandshake& h, C& client) {
  int room = client.available();
  if (room > WS_HANDSHAKE_MAX - 1 - h.req_len) room = WS_HANDSHAKE_MAX - 1 - h.req_len;
  uint16_t old_len = h.req_len;
  if (room > 0) {
    int n = client.read((uint8_t*)h.req + h.req_len, room);
    if (n > 0) h.req_len += n;
  }
  h.req[h.req_len] = 0;

  const char* end = strstr(h.req + (old_len > 3 ? old_len - 3 : 0), "\r\n\r\n");
  if (end) {
    end += 4;
    char key[32], origin[128], upgrade[16], version[8];
    if (end != h.req + h.req_len) {  // client sent data before our 101 (RFC 6455 4.1)
      ws_http_reply(client, "400 Bad Request", "");
      return -1;
    }
    int has_key = ws_header_value(h.req, "Sec-WebSocket-Key", key, sizeof(key));
    int has_upg = ws_header_value(h.req, "Upgrade", upgrade, sizeof(upgrade));
    if (strncmp(h.req, "GET ", 4) != 0 || has_upg != 1 || strcasecmp(upgrade, "websocket") != 0 ||
        has_key != 1 || strlen(key) != 24) {  // key is a base64 16-byte nonce
      ws_http_reply(client, "400 Bad Request", "");
      return -1;
    }
    if (ws_header_value(h.req, "Sec-WebSocket-Version", version, sizeof(version)) != 1 || strcmp(version, "13") != 0) {
      ws_http_reply(client, "426 Upgrade Required", "Sec-WebSocket-Version: 13\r\n");
      return -1;
    }
    int has_org = ws_header_value(h.req, "Origin", origin, sizeof(origin));
    if (has_org < 0 || (has_org > 0 && !ws_origin_allowed(origin))) {
      Serial.printf("ETH: WebSocket origin rejected: %s\n", has_org < 0 ? "(too long)" : origin);
      ws_http_reply(client, "403 Forbidden", "");
      return -1;
    }
    char accept[32];
    ws_accept_key(key, accept);
    char resp[160];
    snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
    client.print(resp);
    return 1;
  }
  if (h.req_len >= WS_HANDSHAKE_MAX - 1) {  // buffer full, no terminator
    ws_http_reply(client, "431 Request Header Fields Too Large", "");
    return -1;
  }
  if (millis() - h.started > WS_HANDSHAKE_TIMEOUT_MS) return -1;
  return 0;
}

// Decode incoming frames into s.rx; handles ping/close. Returns false if the
// connection should be closed.
template <class C>
static bool ws_receive(WsSession& s, C& client) {
  auto fail = [&]() { ws_send_close(client, 1002); return false; };  // protocol error
  uint8_t buf[64];
  // Bounded work per call, so a peer flooding control frames can't hold up loop()
  for (int budget = WS_RX_BUF_SIZE; budget > 0; ) {
    // Never read more than the rx ring can hold, so no payload byte is ever dropped;
    // the rest stays in the socket until the CLI has consumed some.
    int n = client.available();
    if (n > (int)sizeof(buf)) n = sizeof(buf);
    if (n > s.rx_free()) n = s.rx_free();
    if (n <= 0) break;
    n = client.read(buf, n);
    if (n <= 0) break;
    budget -= n;

    for (int i = 0; i < n; i++) {
      uint8_t b = buf[i];
      switch (s.step) {
        case WSF_HDR0: {
          bool fin = b & 0x80;
          uint8_t op = b & 0x0F;
          if (b & 0x70) return fail();                    // no extensions negotiated
          if ((op > 0x2 && op < 0x8) || op > 0xA) return fail();  // reserved opcodes
          if (op >= 0x8) { if (!fin) return fail(); }     // control frames can't be fragmented
          else {
            if ((op == 0x0) != s.in_message) return fail();  // continuation only inside a message
            s.in_message = !fin;
          }
          s.opcode = op;
          s.step = WSF_HDR1;
          break;
        }
        case WSF_HDR1:
          if (!(b & 0x80)) return fail();  // client frames must be masked
          s.payload_len = b & 0x7F; s.payload_got = 0; s.mask_got = 0; s.ctrl_len = 0;
          if (s.opcode >= 0x8 && s.payload_len > WS_CTRL_MAX) return fail();
          if (s.payload_len == 126) { s.ext_needed = 2; s.ext_got = 0; s.payload_len = 0; s.step = WSF_EXTLEN; }
          else if (s.payload_len == 127) { s.ext_needed = 8; s.ext_got = 0; s.payload_len = 0; s.step = WSF_EXTLEN; }
          else s.step = WSF_MASK;
          break;
        case WSF_EXTLEN:
          if (s.ext_needed == 8 && s.ext_got == 0 && (b & 0x80)) return fail();  // MSB of 64-bit length
          s.payload_len = (s.payload_len << 8) | b;
          if (++s.ext_got == s.ext_needed) s.step = WSF_MASK;
          break;
        case WSF_MASK:
          s.mask[s.mask_got++] = b;
          if (s.mask_got == 4) s.step = WSF_PAYLOAD;
          break;
        case WSF_PAYLOAD: {
          uint8_t c = b ^ s.mask[s.payload_got & 3];
          if (s.opcode <= 0x2) s.rx_push(c);                       // continuation/text/binary
          else if (s.ctrl_len < WS_CTRL_MAX) s.ctrl[s.ctrl_len++] = c;  // control frame payload
          s.payload_got++;
          break;
        }
      }

      // A frame is complete once its payload has been fully read
      // (also covers zero-length payloads right after the mask).
      if (s.step == WSF_PAYLOAD && s.payload_got == s.payload_len) {
        if (s.opcode == 0x8) {         // close: echo the status code (if any), then drop the connection
          ws_send_frame(client, 0x8, s.ctrl, s.ctrl_len >= 2 ? 2 : 0);
          return false;
        } else if (s.opcode == 0x9) {  // ping -> pong
          ws_send_frame(client, 0xA, s.ctrl, s.ctrl_len);
        }                              // unsolicited pong: ignored
        s.step = WSF_HDR0;
      }
    }
  }
  return true;
}
