#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include <RTClib.h>
#include <target.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
#endif

#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/IdentityStore.h>

#include "TrackerPrefs.h"

// Packet type counters
struct RelayStats {
    uint32_t n_rx;
    uint32_t n_tx;
    uint32_t n_forwarded;
};

class RelayMesh : public mesh::Mesh {
public:
    RelayMesh(mesh::Radio& radio, mesh::RNG& rng,
              mesh::RTCClock& rtc, mesh::MeshTables& tables);

    void begin();
    void loop();

    void handleCommand(char* command, char* reply);

    // Our identity — set by main before begin()
    mesh::LocalIdentity self_id;

    // Stats accessors
    const RelayStats& getStats() const { return _stats; }

protected:
    // Forward Kestrel encrypted traffic (RAW_CUSTOM), group data, and text messages
    bool allowPacketForward(const mesh::Packet* packet) override;

    // Relay everything we can decode routing for
    bool filterRecvFloodPacket(mesh::Packet* pkt) override { return false; }

    // Minimal airtime budget — relay only, no own transmissions
    float getAirtimeBudgetFactor() const override { return 0.5f; }

    // Logging hooks — update stats
    void logRx(mesh::Packet* pkt, int len, float score) override;
    void logTx(mesh::Packet* pkt, int len) override;

    // Required Mesh peer-resolution stubs (no contacts in relay mode)
    int searchPeersByHash(const uint8_t* hash) override { return 0; }
    void getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) override {}
    void onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx,
                        const uint8_t* secret, uint8_t* data, size_t len) override {}
    bool onPeerPathRecv(mesh::Packet* packet, int sender_idx, const uint8_t* secret,
                        uint8_t* path, uint8_t path_len,
                        uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override { return false; }
    void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id,
                      uint32_t timestamp, const uint8_t* app_data, size_t app_data_len) override {}
    void onGroupDataRecv(mesh::Packet* packet, uint8_t type, const mesh::GroupChannel& channel,
                         uint8_t* data, size_t len) override {}
    void onAckRecv(mesh::Packet* packet, uint32_t ack_crc) override {}

private:
    RelayStats    _stats;
    unsigned long _boot_ms;
};
