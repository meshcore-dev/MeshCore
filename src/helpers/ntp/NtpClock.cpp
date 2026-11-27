#include "NtpClock.h"

#include <Arduino.h>
#include <atomic>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <helpers/KeyValueStore.h>
#include <lwip/apps/sntp.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>

namespace {
constexpr char fallbackHost[] = "pool.ntp.org";
constexpr uint8_t fallbackSlot = CONFIG_LWIP_DHCP_MAX_NTP_SERVERS;
static_assert(fallbackSlot < SNTP_MAX_SERVERS, "SNTP needs a slot after the DHCP servers");

enum NtpRequest : uintptr_t { Online = 1, Enabled = 2, SyncNow = 4 };

KeyValueStore* prefs = nullptr;
bool available = false;
bool enabled = true;
bool syncNow = false;
uint32_t nextPoll = 0;
int64_t minimumEpoch = 0;
ntp::LastReply last;
std::atomic<uint32_t> receivedTime{0};

// The following state is used only by the lwIP thread.
bool wasOnline = false;
bool resolving = false;
uint32_t nextLookup = 0;
ip_addr_t fallbackAddress{};

void fallbackResolved(const char*, const ip_addr_t* address, void*) {
  resolving = false;
  if (address && wasOnline) fallbackAddress = *address;
}

void pollNtp(void* arg) {
  const auto request = reinterpret_cast<uintptr_t>(arg);
  const bool online = request & Online;
  if (wasOnline && !online) {
    for (uint8_t slot = 0; slot < fallbackSlot; ++slot) sntp_setserver(slot, nullptr);
    ip_addr_set_zero(&fallbackAddress);
    nextLookup = sys_now();
  }
  wasOnline = online;

  if (!online || !(request & Enabled)) {
    if (sntp_enabled()) sntp_stop();
    return;
  }

  const bool hasDhcp = !ip_addr_isany(sntp_getserver(0));
  const uint32_t now = sys_now();
  if (!hasDhcp && !resolving && ((request & SyncNow) || static_cast<int32_t>(now - nextLookup) >= 0)) {
    // Resolve outside SNTP so stopping SNTP cannot leave it with a pending DNS callback.
    ip_addr_t address;
    const err_t result = dns_gethostbyname_addrtype(fallbackHost, &address, fallbackResolved,
                                                  nullptr, LWIP_DNS_ADDRTYPE_IPV4);
    resolving = result == ERR_INPROGRESS;
    if (result == ERR_OK) fallbackAddress = address;
    nextLookup = now + (ip_addr_isany(&fallbackAddress) ? 30000 : sntp_get_sync_interval());
  }

  // DHCP may replace the server on a lease renewal.
  const ip_addr_t* fallback = hasDhcp ? IP_ANY_TYPE : &fallbackAddress;
  if (!ip_addr_cmp(sntp_getserver(fallbackSlot), fallback)) sntp_setserver(fallbackSlot, fallback);
  if ((request & SyncNow) && sntp_enabled()) sntp_stop();
  if (!sntp_enabled() && (hasDhcp || !ip_addr_isany(&fallbackAddress))) {
    sntp_init();
  }
}
}

// Apply NTP replies through the MeshCore clock on the firmware loop.
extern "C" void sntp_sync_time(struct timeval* time) {
  if (time->tv_sec >= minimumEpoch) receivedTime.store(static_cast<uint32_t>(time->tv_sec));
}

namespace ntp {

void begin(uint32_t minEpoch) {
  minimumEpoch = minEpoch;
  available = esp_netif_init() == ESP_OK;
  if (available) esp_sntp_servermode_dhcp(true);
}

void loadPreferences(KeyValueStore* store) {
  prefs = store;
  char value[8] = {};
  enabled = !prefs || !prefs->getByKey("ntp", value, sizeof(value) - 1) || strcmp(value, "off") != 0;
  nextPoll = millis();
}

void loop(mesh::RTCClock& clock, bool networkReady) {
  if (!available) return;
  const uint32_t time = receivedTime.exchange(0);
  if (enabled && networkReady && time) last.accept(clock, time);

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextPoll) < 0) return;
  const uintptr_t request = (networkReady ? Online : 0) | (enabled ? Enabled : 0)
                            | (syncNow ? SyncNow : 0);
  if (tcpip_try_callback(pollNtp, reinterpret_cast<void*>(request)) == ERR_OK) syncNow = false;
  nextPoll = now + 1000;
}

bool handleCommand(const char* command, char* reply, size_t replySize, const char* waitingFor) {
  if (strcmp(command, "ntp") == 0 || strcmp(command, "ntp status") == 0) {
    if (!available) snprintf(reply, replySize, "NTP unavailable");
    else if (!enabled) snprintf(reply, replySize, "NTP off");
    else if (waitingFor) snprintf(reply, replySize, "NTP on, waiting for %s", waitingFor);
    else if (!last.received) snprintf(reply, replySize, "NTP on, waiting for server");
    else {
      snprintf(reply, replySize, "NTP on, last reply %lu UTC epoch", (unsigned long)last.received);
    }
  } else if (strcmp(command, "ntp on") == 0 || strcmp(command, "ntp off") == 0) {
    const bool turnOn = strcmp(command, "ntp on") == 0;
    if (!available) snprintf(reply, replySize, "NTP unavailable");
    else if (enabled != turnOn && (!prefs || !prefs->setByKey("ntp", turnOn ? "on" : "off"))) {
      snprintf(reply, replySize, "Error saving NTP setting");
    } else {
      if (enabled != turnOn) {
        enabled = turnOn;
        last = {};
        receivedTime.exchange(0);
        syncNow = false;
        nextPoll = millis();
      }
      snprintf(reply, replySize, "OK - NTP %s", turnOn ? "on" : "off");
    }
  } else if (strcmp(command, "ntp sync") == 0) {
    if (!available) snprintf(reply, replySize, "NTP unavailable");
    else if (!enabled) snprintf(reply, replySize, "Enable NTP with ntp on");
    else {
      syncNow = true;
      nextPoll = millis();
      snprintf(reply, replySize, "OK - NTP sync requested");
    }
  } else {
    return false;
  }
  return true;
}
}
