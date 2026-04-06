#pragma once

#if defined(ESP32) && defined(TCP_CONSOLE_PORT) && defined(ADMIN_PASSWORD)

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <helpers/CommonCLI.h>  // for NodePrefs

#ifndef TCP_CONSOLE_MAX_CLIENTS
  #define TCP_CONSOLE_MAX_CLIENTS 2
#endif

#ifndef TCP_CONSOLE_TIMEOUT_MS
  #define TCP_CONSOLE_TIMEOUT_MS 300000  // 5 minutes inactivity timeout
#endif

class TCPConsole {
  WiFiServer _server;
  WiFiClient _clients[TCP_CONSOLE_MAX_CLIENTS];
  bool _authenticated[TCP_CONSOLE_MAX_CLIENTS];
  unsigned long _last_active[TCP_CONSOLE_MAX_CLIENTS];
  char _cmd_buf[TCP_CONSOLE_MAX_CLIENTS][160];
  int _cmd_len[TCP_CONSOLE_MAX_CLIENTS];
  NodePrefs* _prefs;  // pointer to live NodePrefs — password is read at runtime

  void sendToClient(int i, const char* msg) {
    if (_clients[i] && _clients[i].connected()) {
      _clients[i].print(msg);
    }
  }

  void disconnectClient(int i) {
    _clients[i].stop();
    _authenticated[i] = false;
    memset(_cmd_buf[i], 0, sizeof(_cmd_buf[i]));
    _cmd_len[i] = 0;
  }

public:
  TCPConsole(NodePrefs* prefs)
    : _server(TCP_CONSOLE_PORT), _prefs(prefs) {
    for (int i = 0; i < TCP_CONSOLE_MAX_CLIENTS; i++) {
      _authenticated[i] = false;
      memset(_cmd_buf[i], 0, sizeof(_cmd_buf[i]));
      _cmd_len[i] = 0;
      _last_active[i] = 0;
    }
  }

  void begin() {
    _server.begin();
    Serial.printf("TCP Console listening on port %d\n", TCP_CONSOLE_PORT);
  }

  void setPrefs(NodePrefs* prefs) { _prefs = prefs; }

  // Call this from loop(), passing the mesh's handleCommand function
  template<typename T>
  void loop(T& mesh) {
    // Accept new clients
    WiFiClient newClient = _server.available();
    if (newClient) {
      bool found = false;
      for (int i = 0; i < TCP_CONSOLE_MAX_CLIENTS; i++) {
        if (!_clients[i] || !_clients[i].connected()) {
          _clients[i] = newClient;
          _authenticated[i] = false;
          memset(_cmd_buf[i], 0, sizeof(_cmd_buf[i]));
          _cmd_len[i] = 0;
          _last_active[i] = millis();
          sendToClient(i, "MeshCore Console\r\nPassword: ");
          found = true;
          break;
        }
      }
      if (!found) {
        newClient.print("Server busy. Try again later.\r\n");
        newClient.stop();
      }
    }

    // Handle connected clients
    for (int i = 0; i < TCP_CONSOLE_MAX_CLIENTS; i++) {
      if (!_clients[i] || !_clients[i].connected()) continue;

      // Inactivity timeout
      if (millis() - _last_active[i] > TCP_CONSOLE_TIMEOUT_MS) {
        sendToClient(i, "\r\nTimeout. Disconnecting.\r\n");
        disconnectClient(i);
        continue;
      }

      // Read available data
      while (_clients[i].available()) {
        _last_active[i] = millis();
        char c = _clients[i].read();

        if (c == '\n') continue;  // ignore LF, handle CR only

        if (c != '\r' && _cmd_len[i] < 158) {
          _cmd_buf[i][_cmd_len[i]++] = c;
          _cmd_buf[i][_cmd_len[i]] = 0;
          if (!_authenticated[i]) {
            sendToClient(i, "*");  // mask password input
          } else {
            _clients[i].print(c);  // echo command
          }
          continue;
        }

        // Got CR — process command
        sendToClient(i, "\r\n");
        _cmd_buf[i][_cmd_len[i]] = 0;

        if (!_authenticated[i]) {
          // Compare full password field with memcmp to avoid short-circuit timing
          bool ok = _prefs != nullptr &&
                    _cmd_len[i] == (int)strnlen(_prefs->password, sizeof(_prefs->password)) &&
                    memcmp(_cmd_buf[i], _prefs->password, sizeof(_prefs->password)) == 0;
          if (ok) {
            _authenticated[i] = true;
            char welcome[80];
            snprintf(welcome, sizeof(welcome), "Welcome to %s console.\r\n> ", _prefs->node_name);
            sendToClient(i, welcome);
          } else {
            sendToClient(i, "Wrong password. Disconnecting.\r\n");
            disconnectClient(i);
          }
        } else {
          // Execute command
          if (strlen(_cmd_buf[i]) > 0) {
            char reply[160];
            reply[0] = 0;
            mesh.handleCommand(0, _cmd_buf[i], reply);
            if (reply[0]) {
              sendToClient(i, "  -> ");
              sendToClient(i, reply);
              sendToClient(i, "\r\n");
            }
          }
          sendToClient(i, "> ");
        }

        memset(_cmd_buf[i], 0, sizeof(_cmd_buf[i]));
        _cmd_len[i] = 0;
      }
    }
  }
};

#endif  // ESP32 && TCP_CONSOLE_PORT && ADMIN_PASSWORD
