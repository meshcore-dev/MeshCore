#pragma once

#include <stddef.h>
#include <string.h>

// Pure, dependency-free validators for the observer's CLI/web configuration
// inputs. Factored out of CommonCLI_Observer.cpp so the exact logic the setters
// enforce can be unit-tested on the host (see test/test_observer_validation)
// rather than only through the full CLI object.

// IATA region code: exactly three ASCII alphanumerics. The value is placed
// directly into MQTT topic paths (meshcore/{iata}/...), so anything else (wrong
// length, spaces, topic separators) is rejected. Case is preserved here; the
// setter uppercases after validation.
static inline bool mqttIataValid(const char* s) {
  if (!s || strlen(s) != 3) return false;
  for (int i = 0; i < 3; i++) {
    char c = s[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
      return false;
    }
  }
  return true;
}

// Owner public key: exactly 64 hex characters (a 32-byte Ed25519 key), any case.
static inline bool mqttOwnerKeyValid(const char* s) {
  if (!s || strlen(s) != 64) return false;
  for (int i = 0; i < 64; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

// NTP hostname: non-empty, <= 63 chars, made of letters/digits/'.'/'-', with no
// leading or trailing dot. ("none" is handled as a clear by the caller.)
static inline bool mqttNtpHostnameValid(const char* host) {
  if (!host || host[0] == '\0') return false;
  size_t len = strlen(host);
  if (len > 63) return false;
  if (host[0] == '.' || host[len - 1] == '.') return false;
  for (size_t i = 0; i < len; i++) {
    char c = host[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '-')) {
      return false;
    }
  }
  return true;
}

// A value fits its fixed destination buffer, which must hold the string plus a
// NUL terminator (so the usable length is bufsize - 1). Used to reject an
// over-long submission up front instead of silently truncating it.
static inline bool mqttValueFits(const char* s, size_t bufsize) {
  return s != NULL && bufsize > 0 && strlen(s) < bufsize;
}
