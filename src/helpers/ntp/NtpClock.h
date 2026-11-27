#pragma once

#include <stddef.h>
#include <MeshCore.h>

class KeyValueStore;

namespace ntp {

struct LastReply {
  uint32_t received = 0;

  void accept(mesh::RTCClock& clock, uint32_t epoch) {
    received = epoch;
    if (clock.getCurrentTime() != epoch) clock.setCurrentTime(epoch);
  }
};

// Owns the ESP32 SNTP client. Call before starting the network to accept DHCP servers.
void begin(uint32_t minimumEpoch);
void loadPreferences(KeyValueStore* prefs);

// Pass true when the Ethernet or Wi-Fi interface has a usable IP address.
void loop(mesh::RTCClock& clock, bool networkReady);

// Supply the interface name while waiting for an IP address, or nullptr once ready.
bool handleCommand(const char* command, char* reply, size_t replySize, const char* waitingFor);

}
