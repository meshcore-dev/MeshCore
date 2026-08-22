/*
 * Copyright (c) 2026 Inhero GmbH
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Adafruit_LittleFS.h>
#include <Arduino.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

// Mini preferences library compatible with Arduino Preferences API
// Provides simple file-based key-value storage using LittleFS backend
class SimplePreferences {
private:
  String _namespace;
  bool _started = false;

  // Builds filename "/namespace/key.txt"
  String getFilePath(const char* key) {
    String path = "/" + _namespace;
    // Create namespace folder if it doesn't exist
    InternalFS.mkdir(path.c_str());
    path += "/";
    path += key;
    path += ".txt";
    return path;
  }

public:
  SimplePreferences() {}

  bool begin(const char* name) {
    _namespace = name;
    _started = true;
    return InternalFS.begin();
  }

  void end() { _started = false; }

  // Store string value
  size_t putString(const char* key, const char* value) {
    if (!_started || value == nullptr) return 0;

    String path = getFilePath(key);

    // Remove existing file (clean overwrite)
    InternalFS.remove(path.c_str());

    File file = InternalFS.open(path.c_str(), FILE_O_WRITE);
    if (!file) return 0;

    // file.print accepts const char* directly — no extra copy
    size_t len = file.print(value);

    file.close();
    return len;
  }

  size_t putInt(const char* key, const uint16_t val) {
    char buffer[10];
    snprintf(buffer, sizeof(buffer), "%u", val);
    return putString(key, buffer);
  }

  // Read string value into the caller-provided buffer
  size_t getString(const char* key, char* buffer, size_t maxLen, const char* defaultValue = "") {
    if (!_started) {
      // Copy fallback
      strncpy(buffer, defaultValue, maxLen);
      buffer[maxLen - 1] = '\0'; // Safety
      return strlen(buffer);
    }

    String path = getFilePath(key);

    if (!InternalFS.exists(path.c_str())) {
      strncpy(buffer, defaultValue, maxLen);
      buffer[maxLen - 1] = '\0';
      return strlen(buffer);
    }

    File file = InternalFS.open(path.c_str(), FILE_O_READ);
    if (!file) {
      strncpy(buffer, defaultValue, maxLen);
      return strlen(buffer);
    }

    size_t bytesRead = file.readBytes(buffer, maxLen - 1);
    buffer[bytesRead] = '\0'; // Set null-terminator manually

    // Trim trailing whitespace and newline characters
    while (bytesRead > 0 &&
           (buffer[bytesRead - 1] == '\r' || buffer[bytesRead - 1] == '\n' || buffer[bytesRead - 1] == ' ')) {
      buffer[bytesRead - 1] = '\0';
      bytesRead--;
    }

    file.close();
    return bytesRead;
  }

  bool containsKey(const char* key) {
    if (!_started) return false;
    String path = getFilePath(key);
    return InternalFS.exists(path.c_str());
  }
};
