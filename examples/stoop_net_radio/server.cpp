#include "server.h"

#ifdef WIFI_SSID
#ifdef ESP32

#include <helpers/esp32/SerialWifiInterface.h> // WIFI_DEBUG_PRINTLN, pulls in WiFi.h
#include <helpers/MultiSerialInterface.h>
#include "MyMesh.h"
#include "wifi_manager.h" // server, rate_limiter, wifiClientModeActive()
#include <web_assets.h>
#include <ElegantOTA.h>

#ifndef STOOP_NODE_NAME
#define STOOP_NODE_NAME "stoop"
#endif

#ifndef ADMIN_PASSWORD
#define ADMIN_PASSWORD "password"
#endif

// TODO(Heidt) unclear if there's a limit in the FW on username length,
// but https://github.com/meshcore-dev/MeshCore/issues/2613 suggests 23 is the max
#define MAX_USERNAME_LEN 23
// save up to 8 bytes
#define STOOP_MAX_USERNAME_LEN 15 // assume 8 bytes for node name, Meshcore max username is 31 bytes
#define STOOP_MAX_NODENAME_LEN (MAX_USERNAME_LEN - STOOP_MAX_USERNAME_LEN - 1 /* @ */)

// TODO(Heidt) maybe want to have a static size for the node name, TBR
#define STOOP_SENDER_OVERHEAD (STOOP_MAX_USERNAME_LEN + 1 /* @ */ + STOOP_MAX_NODENAME_LEN + 2 /* ": " */)
#define STOOP_MAX_POST_BYTES (MAX_TEXT_LEN - STOOP_SENDER_OVERHEAD)

#define STOOP_SESSION_HEADER "X-Stoop-Session"

// defined in main.cpp
extern MyMesh the_mesh;
extern MultiSerialInterface interface_manager;

// escape a string for safe embedding inside a JSON string literal
static void appendJsonEscaped(String& out, const char* s) {
  for (const char* p = s; *p; p++) {
    switch (*p) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      default:
        out += ((uint8_t)*p < 0x20) ? ' ' : *p;
    }
  }
}

static bool getSessionTokenFromRequest(uint8_t out_token[STOOP_SESSION_TOKEN_LEN]) {
  if (!server.hasHeader(STOOP_SESSION_HEADER)) return false;
  return mesh::Utils::fromHex(out_token, STOOP_SESSION_TOKEN_LEN, server.header(STOOP_SESSION_HEADER).c_str());
}

// shared by GET /api/limits and POST /api/post, send the current rate limiting status
// as json
static void sendLimitsJson(int code, const uint8_t session_token[STOOP_SESSION_TOKEN_LEN],
                            uint32_t user_tokens, uint32_t user_next_ms,
                            uint32_t node_tokens, uint32_t node_next_ms) {
  char token_hex[STOOP_SESSION_TOKEN_LEN * 2 + 1];
  mesh::Utils::toHex(token_hex, session_token, STOOP_SESSION_TOKEN_LEN);
  server.sendHeader(STOOP_SESSION_HEADER, token_hex);
  uint32_t next_ms = user_next_ms > node_next_ms ? user_next_ms : node_next_ms;

  String json = "{\"tokens_remaining\":";
  json += user_tokens;
  json += ",\"next_token_ms\":";
  json += next_ms;
  json += ",\"node_tokens_remaining\":";
  json += node_tokens;
  json += ",\"max_post_bytes\":";
  json += STOOP_MAX_POST_BYTES;
  json += "}";
  server.send(code, "application/json", json);
}

void configureServer() {
  static const char* collected_headers[] = { STOOP_SESSION_HEADER };
  server.collectHeaders(collected_headers, 1);

  auto serveIndex = [](){
    WIFI_DEBUG_PRINTLN("Serving index.html: host=%s uri=%s", server.hostHeader().c_str(), server.uri().c_str());
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", (const char*)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
  };
  server.on("/", HTTP_GET, serveIndex);

  // Captive portals for various platforms
  server.on("/generate_204", HTTP_GET, serveIndex);           // Android / Chrome
  server.on("/gen_204", HTTP_GET, serveIndex);                // older Android
  server.on("/hotspot-detect.html", HTTP_GET, serveIndex);    // iOS / macOS
  server.on("/library/test/success.html", HTTP_GET, serveIndex); // older iOS / macOS
  server.on("/connecttest.txt", HTTP_GET, serveIndex);        // Windows
  server.on("/ncsi.txt", HTTP_GET, serveIndex);               // Windows (older NCSI)
  server.on("/success.txt", HTTP_GET, serveIndex);            // Firefox

  server.on("/chat.html", HTTP_GET, [](){
    WIFI_DEBUG_PRINTLN("Serving chat.html");
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", (const char*)CHAT_HTML_GZ, CHAT_HTML_GZ_LEN);
  });
  server.on("/stoop.html", HTTP_GET, [](){
    WIFI_DEBUG_PRINTLN("Serving stoop.html");
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", (const char*)STOOP_HTML_GZ, STOOP_HTML_GZ_LEN);
  });
  server.on("/admin", HTTP_GET, [](){
    WIFI_DEBUG_PRINTLN("Serving admin.html");
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", (const char*)ADMIN_HTML_GZ, ADMIN_HTML_GZ_LEN);
  });

  // enables/disables interface_manager (BLE/USB/WiFi companion interfaces)
  // when given the correct ADMIN_PASSWORD
  server.on("/api/admin", HTTP_POST, [](){
    String password = server.arg("password");
    String action = server.arg("action");
    if (password != ADMIN_PASSWORD) {
      server.send(403, "text/plain", "forbidden");
      return;
    }

    if (action == "enable") {
      interface_manager.enable();
    } else if (action == "disable") {
      interface_manager.disable();
    } else {
      server.send(400, "text/plain", "bad request");
      return;
    }

    String json = "{\"enabled\":";
    json += interface_manager.isEnabled() ? "true" : "false";
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/stoop/max_message_bytes", HTTP_GET, [](){
    server.send(200, "text/plain", String(STOOP_MAX_POST_BYTES));
  });

  // returns recent #stoop channel messages as JSON: [{"ts":123,"text":"name: hi"}, ...]
  server.on("/api/stoop/messages", HTTP_GET, [](){
    int count = the_mesh.stoop_log_count;
    int n = count < MyMesh::STOOP_LOG_SIZE ? count : MyMesh::STOOP_LOG_SIZE;
    int start = count - n;
    String json = "[";
    for (int i = 0; i < n; i++) {
      auto& msg = the_mesh.stoop_log[(start + i) % MyMesh::STOOP_LOG_SIZE];
      if (i > 0) json += ",";
      json += "{\"ts\":";
      json += msg.timestamp;
      json += ",\"text\":\"";
      appendJsonEscaped(json, msg.text);
      json += "\"}";
    }
    json += "]";
    server.send(200, "application/json", json);
  });

  // reports the caller's current rate-limit status without consuming a token
  server.on("/api/limits", HTTP_GET, [](){
    uint8_t token[STOOP_SESSION_TOKEN_LEN];
    bool token_valid = getSessionTokenFromRequest(token);

    uint8_t out_token[STOOP_SESSION_TOKEN_LEN];
    uint32_t user_tokens, user_next_ms, node_tokens, node_next_ms;
    rate_limiter.getStatus(token, token_valid, millis(), out_token,
                            user_tokens, user_next_ms, node_tokens, node_next_ms);
    sendLimitsJson(200, out_token, user_tokens, user_next_ms, node_tokens, node_next_ms);
  });

  // sends a message on the #stoop channel as "username@<node name>: text",
  // subject to the per-user and per-node token buckets
  server.on("/api/post", HTTP_POST, [](){
    String text = server.arg("text");
    String username = server.arg("username");
    if (text.length() == 0 || (int)text.length() > (int)STOOP_MAX_POST_BYTES) {
      server.send(400, "text/plain", "bad request");
      return;
    }

    uint8_t token[STOOP_SESSION_TOKEN_LEN];
    bool token_valid = getSessionTokenFromRequest(token);

    uint8_t out_token[STOOP_SESSION_TOKEN_LEN];
    uint32_t user_tokens, user_next_ms, node_tokens, node_next_ms;
    bool allowed = rate_limiter.tryConsume(token, token_valid, millis(), out_token,
                                            user_tokens, user_next_ms, node_tokens, node_next_ms);
    if (!allowed) {
      sendLimitsJson(429, out_token, user_tokens, user_next_ms, node_tokens, node_next_ms);
      return;
    }

    if (username.length() == 0) username = "anon";
    if (username.length() > STOOP_MAX_USERNAME_LEN) username = username.substring(0, STOOP_MAX_USERNAME_LEN);

    char sender[STOOP_MAX_USERNAME_LEN + 1 + sizeof(STOOP_NODE_NAME) + 1];
    snprintf(sender, sizeof(sender), "%s@%s", username.c_str(), STOOP_NODE_NAME);

    ChannelDetails channel;
    uint32_t ts = the_mesh.getRTCClock()->getCurrentTimeUnique();
    if (the_mesh.getChannel(0, channel) &&
        the_mesh.sendGroupMessage(ts, channel.channel, sender, text.c_str(), text.length())) {
      char full[sizeof(sender) + MAX_TEXT_LEN + 2];
      snprintf(full, sizeof(full), "%s: %s", sender, text.c_str());
      the_mesh.logStoopMsg(ts, full);
      sendLimitsJson(200, out_token, user_tokens, user_next_ms, node_tokens, node_next_ms);
    } else {
      // note: the token was already spent even though the radio-level send failed;
      // this is a rare failure path, not something worth refunding for
      server.send(500, "text/plain", "send failed");
    }
  });

  // Any request for a page that we don't host is treated as a captive portal probe
  // and redirected to the main page but only in AP mode
  server.onNotFound([](){
    WIFI_DEBUG_PRINTLN("onNotFound: method=%d host=%s uri=%s",
                       (int)server.method(), server.hostHeader().c_str(), server.uri().c_str());
    if (wifiClientModeActive()) {
      server.send(404, "text/plain", "Not found");
      return;
    }
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });

  ElegantOTA.begin(&server, "admin", ADMIN_PASSWORD);
  server.begin();
}

#endif // ESP32
#endif // WIFI_SSID
