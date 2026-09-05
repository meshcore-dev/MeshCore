#ifdef ESP_PLATFORM

// Link-time workaround for an off-by-one heap overflow in ESP-IDF v4.4's
// WebSocket transport (components/tcp_transport/transport_ws.c), which ships
// PRECOMPILED in the Arduino-ESP32 2.x SDK (libtcp_transport.a) and cannot be
// patched at source level.
//
// The bug (transport_ws.c, ws_connect() response-read loop):
//
//     header_len += len;
//     ws->buffer[header_len] = '\0';        // header_len can reach WS_BUFFER_SIZE
//     } while (... && header_len < WS_BUFFER_SIZE);
//
// ws->buffer is malloc(WS_BUFFER_SIZE) (1024). When a wss:// endpoint answers
// the WebSocket upgrade with >= 1024 bytes of HTTP response before the blank
// line terminator (typical for a down/misconfigured broker behind a proxy or
// CDN that serves a large HTML error page), the final iteration writes one
// '\0' one byte past the block. With heap poisoning enabled that zeroes the
// LSB of the tail canary (0xbaad5678 -> 0xbaad5600); the corruption then sits
// silent until the block is freed — which happens in ws_destroy() during
// esp_mqtt_client_destroy(), i.e. MQTTBridge::end() — and the free asserts:
//
//     CORRUPT HEAP: Bad tail at 0x.... Expected 0xbaad5678 got 0xbaad5600
//     assert failed: multi_heap_free multi_heap_poisoning.c:259
//
// On observer builds that teardown runs at the start of the deferred
// `ota update`, so a single down wss broker made every online OTA panic and
// reboot before the download began (backtrace decoded from a Heltec V3 on
// v1.16.0.11: free <- ws_destroy <- esp_transport_list_destroy <-
// esp_mqtt_client_destroy <- ~PsychicMqttClient <- destroySlotClients <-
// MQTTBridge::end <- MyMesh::setBridgeState <- MyMesh::loop).
//
// Fix: [esp32_base] adds `-Wl,--wrap=esp_transport_ws_init`, so every
// creation of a WS transport (esp-mqtt does one per wss slot) is routed
// through __wrap_esp_transport_ws_init below, which replaces the freshly
// allocated 1024-byte buffer with a (WS_BUFFER_SIZE + 1)-byte one. The
// out-of-bounds index WS_BUFFER_SIZE then lands on our extra byte and the
// handshake fails cleanly ("Upgrade" header not found) instead of corrupting
// the heap. Upstream fixed this in ESP-IDF 5.x, so this file compiles to a
// pass-through there and can be deleted (together with the --wrap flag) when
// the fork moves to Arduino core 3.x.
//
// transport_ws_t below is copied verbatim from ESP-IDF release/v4.4
// transport_ws.c (the struct is file-private, so it is not in any shipped
// header). Source fidelity was verified against the shipped binary: addr2line
// on the crash backtrace resolves to the exact line numbers of that file
// (e.g. free(ws->buffer) at transport_ws.c:546). Only the first two members
// (path, buffer) are dereferenced here.

#include "esp_idf_version.h"

#if ESP_IDF_VERSION_MAJOR == 4

#include <stdlib.h>
#include "sdkconfig.h"
#include "esp_transport.h"
#include "esp_transport_ws.h"

#ifndef CONFIG_WS_BUFFER_SIZE
#define CONFIG_WS_BUFFER_SIZE 1024
#endif

// --- copied from ESP-IDF release/v4.4 components/tcp_transport/transport_ws.c ---
typedef struct {
    uint8_t opcode;
    char mask_key[4];
    int payload_len;
    int bytes_remaining;
    bool header_received;
} ws_transport_frame_state_t;

typedef struct {
    char *path;
    char *buffer;
    char *sub_protocol;
    char *user_agent;
    char *headers;
    bool propagate_control_frames;
    ws_transport_frame_state_t frame_state;
    esp_transport_handle_t parent;
} transport_ws_t;
// --------------------------------------------------------------------------------

extern "C" {

esp_transport_handle_t __real_esp_transport_ws_init(esp_transport_handle_t parent_handle);

esp_transport_handle_t __wrap_esp_transport_ws_init(esp_transport_handle_t parent_handle) {
  esp_transport_handle_t t = __real_esp_transport_ws_init(parent_handle);
  if (t != nullptr) {
    transport_ws_t* ws = (transport_ws_t*)esp_transport_get_context_data(t);
    if (ws != nullptr && ws->buffer != nullptr) {
      // The buffer is untouched at this point (allocated moments ago inside
      // __real_esp_transport_ws_init), so a swap is safe.
      char* padded = (char*)malloc(CONFIG_WS_BUFFER_SIZE + 1);
      if (padded != nullptr) {
        free(ws->buffer);
        ws->buffer = padded;
      }
      // On alloc failure keep the original buffer: same behavior as before
      // this fix, which is still strictly better than failing init here.
    }
  }
  return t;
}

}  // extern "C"

#else  // ESP_IDF_VERSION_MAJOR != 4

// IDF 5.x fixed the overflow upstream; keep a pass-through so the --wrap flag
// (set for all ESP32 envs in [esp32_base]) still links if anything references
// the symbol.

#include "esp_transport.h"
#include "esp_transport_ws.h"

extern "C" {

esp_transport_handle_t __real_esp_transport_ws_init(esp_transport_handle_t parent_handle);

esp_transport_handle_t __wrap_esp_transport_ws_init(esp_transport_handle_t parent_handle) {
  return __real_esp_transport_ws_init(parent_handle);
}

}  // extern "C"

#endif  // ESP_IDF_VERSION_MAJOR

#endif  // ESP_PLATFORM
