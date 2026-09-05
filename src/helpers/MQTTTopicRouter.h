#pragma once

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "MQTTObserverValidation.h"
#include "MQTTTopicTemplate.h"

// Pure MQTT publication-topic policy shared by MQTTBridge and the native tests.
// Keep these values aligned with MQTTBridge::MQTTMessageType; the bridge passes
// its enum value as an int so this helper stays independent of ESP/Arduino types.
enum MQTTPublicationType {
  MQTT_PUBLICATION_STATUS = 0,
  MQTT_PUBLICATION_PACKETS = 1,
  MQTT_PUBLICATION_RAW = 2,
  MQTT_PUBLICATION_NEIGHBORS = 3,
};

enum MQTTTopicRouteStyle {
  MQTT_ROUTE_MESHCORE,
  MQTT_ROUTE_MESHRANK,
  MQTT_ROUTE_CUSTOM,
};

static inline bool mqttTopicSlotIndexValid(int index, size_t slot_count) {
  return index >= 0 && (size_t)index < slot_count;
}

static inline const char* mqttPublicationTypeName(int type) {
  switch (type) {
    case MQTT_PUBLICATION_STATUS: return "status";
    case MQTT_PUBLICATION_PACKETS: return "packets";
    case MQTT_PUBLICATION_RAW: return "raw";
    case MQTT_PUBLICATION_NEIGHBORS: return "neighbors";
    default: return NULL;
  }
}

static inline bool mqttWriteTopic(char* buf, size_t buf_size, const char* format,
                                  const char* first, const char* second,
                                  const char* third) {
  if (!buf || buf_size == 0 || !format || !first || !second || !third) return false;
  buf[0] = '\0';
  int written = snprintf(buf, buf_size, format, first, second, third);
  return written > 0 && (size_t)written < buf_size;
}

// Build the complete topic for one publication. MeshRank takes status, packets,
// and neighbors under meshrank/uplink/{token}/{device}/, using the same type
// suffixes as the MeshCore layout, and requires a per-slot token rather than an
// IATA. MeshCore routes require a configured IATA and device id. Custom
// templates may omit either placeholder, so their individual values are allowed
// to be empty.
static inline bool mqttBuildPublicationTopic(MQTTTopicRouteStyle style, int type,
                                             const char* custom_template,
                                             const char* iata, const char* device,
                                             const char* token,
                                             char* buf, size_t buf_size) {
  if (!buf || buf_size == 0) return false;
  buf[0] = '\0';

  const char* type_name = mqttPublicationTypeName(type);
  if (!type_name) return false;

  switch (style) {
    case MQTT_ROUTE_MESHCORE:
      if (!mqttIataValid(iata) || strcmp(iata, "XXX") == 0 || !device || device[0] == '\0') {
        return false;
      }
      return mqttWriteTopic(buf, buf_size, "meshcore/%s/%s/%s", iata, device, type_name);

    case MQTT_ROUTE_MESHRANK:
      // Raw is deliberately withheld: highest-volume topic, and the broker does
      // not consume it. observer-firmware still sends it — keep this on merge.
      if (type == MQTT_PUBLICATION_RAW || !token || token[0] == '\0' ||
          !device || device[0] == '\0') {
        return false;
      }
      return mqttWriteTopic(buf, buf_size, "meshrank/uplink/%s/%s/%s",
                            token, device, type_name);

    case MQTT_ROUTE_CUSTOM:
      return mqttSubstituteTopic(custom_template, iata, device, token, type_name,
                                 buf, buf_size);

    default:
      return false;
  }
}

