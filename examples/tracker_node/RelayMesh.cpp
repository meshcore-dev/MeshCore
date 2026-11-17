#include "RelayMesh.h"

RelayMesh::RelayMesh(mesh::Radio& radio, mesh::RNG& rng,
                     mesh::RTCClock& rtc, mesh::MeshTables& tables)
    : mesh::Mesh(radio, *new ArduinoMillis(), rng, rtc,
                 *new StaticPoolPacketManager(32), tables)
{
    memset(&_stats, 0, sizeof(_stats));
    _boot_ms = 0;
}

void RelayMesh::begin() {
    _boot_ms = millis();
    mesh::Mesh::begin();
    // GPS stays off in relay mode — no sensors.begin() call here
}

/* ------------------------------------------------------------------ */
/* Packet filtering                                                     */
/* ------------------------------------------------------------------ */

bool RelayMesh::allowPacketForward(const mesh::Packet* packet) {
    uint8_t type = packet->getPayloadType();

    // Forward Kestrel encrypted traffic and team comms
    if (type == PAYLOAD_TYPE_RAW_CUSTOM) return true;
    if (type == PAYLOAD_TYPE_GRP_DATA)   return true;
    if (type == PAYLOAD_TYPE_TXT_MSG)    return true;

    // Drop everything else — ADVERTs, REQs, etc.
    return false;
}

/* ------------------------------------------------------------------ */
/* Stats hooks                                                          */
/* ------------------------------------------------------------------ */

void RelayMesh::logRx(mesh::Packet* pkt, int len, float score) {
    _stats.n_rx++;
}

void RelayMesh::logTx(mesh::Packet* pkt, int len) {
    _stats.n_tx++;
    // Every TX from relay is a forward (we never originate traffic)
    _stats.n_forwarded++;
}

/* ------------------------------------------------------------------ */
/* Main loop                                                            */
/* ------------------------------------------------------------------ */

void RelayMesh::loop() {
    mesh::Mesh::loop();
}

/* ------------------------------------------------------------------ */
/* CLI                                                                  */
/* ------------------------------------------------------------------ */

void RelayMesh::handleCommand(char* command, char* reply) {
    while (*command == ' ') command++;

    if (strcmp(command, "status") == 0) {
        unsigned long uptime_secs = (millis() - _boot_ms) / 1000;
        sprintf(reply,
                "mode=relay uptime=%lus rx=%lu tx=%lu fwd=%lu",
                (unsigned long)uptime_secs,
                (unsigned long)_stats.n_rx,
                (unsigned long)_stats.n_tx,
                (unsigned long)_stats.n_forwarded);
    } else {
        strcpy(reply, "relay commands: status");
    }
}
