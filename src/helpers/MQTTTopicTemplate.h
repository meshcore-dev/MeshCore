#pragma once

#include <stddef.h>
#include <string.h>

// Expand the {iata} {device} {token} {type} placeholders in a custom MQTT topic
// template. Factored out of MQTTBridge::substituteTopicTemplate so the (bounded)
// string expansion can be unit-tested on the host; the bridge passes its cached
// _iata / _device_id, the slot token, and the message-type string.
//
// Returns false on buffer overflow or an empty result. buf is always
// NUL-terminated. A null value substitutes as empty; an unknown "{...}" token is
// copied through verbatim.
static inline bool mqttSubstituteTopic(const char* tmpl, const char* iata,
                                       const char* device, const char* token,
                                       const char* type_str, char* buf, size_t buf_size) {
  if (!buf || buf_size == 0) return false;
  if (!iata) iata = "";
  if (!device) device = "";
  if (!token) token = "";
  if (!type_str) type_str = "";

  size_t out = 0;
  const char* p = tmpl ? tmpl : "";
  while (*p && out < buf_size - 1) {
    const char* sub = NULL;
    size_t adv = 0;
    if (strncmp(p, "{iata}", 6) == 0) {
      sub = iata; adv = 6;
    } else if (strncmp(p, "{device}", 8) == 0) {
      sub = device; adv = 8;
    } else if (strncmp(p, "{token}", 7) == 0) {
      sub = token; adv = 7;
    } else if (strncmp(p, "{type}", 6) == 0) {
      sub = type_str; adv = 6;
    }
    if (sub) {
      size_t len = strlen(sub);
      if (out + len >= buf_size) {
        buf[out] = '\0';  // keep buf terminated even on the overflow path
        return false;
      }
      memcpy(buf + out, sub, len);
      out += len;
      p += adv;
    } else {
      buf[out++] = *p++;
    }
  }
  buf[out] = '\0';
  // The loop also stops when the output buffer is full. If input remains,
  // report overflow just as we do for an oversized placeholder substitution;
  // callers must never publish a silently truncated topic.
  if (*p) return false;
  return out > 0;
}
