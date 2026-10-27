#include <Dispatcher.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/SimpleMeshTables.h>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

struct Clock : mesh::MillisecondClock {
  uint32_t now = 100;
  unsigned long getMillis() override { return now; }
};

struct Radio : mesh::Radio {
  mesh::RadioProfiles config;
  bool expose_profiles = true, prepare_busy = false, complete = true, start_success = true;
  uint8_t selected = 0, busy_mask = 0;
  int failed_profile = -1;
  unsigned loop_count = 0;
  uint32_t airtime = 10;
  std::vector<uint8_t> prepared, sent, incoming;
  std::vector<std::vector<uint8_t>> raw_sent;
  Radio() {
    config.primary.freq = 909.5f; config.primary.bw = 62.5f;
    config.primary.sf = 7; config.primary.cr = 5;
    config.secondary.params = config.primary;
    config.secondary.params.freq = 910.5f;
    config.secondary.params.bw = 500;
    config.secondary.mode = mesh::RadioProfileMode::RxTx;
    config.cross = mesh::RadioCrossMode::Off;
  }
  mesh::RadioProfiles* profiles() override { return expose_profiles ? &config : nullptr; }
  const mesh::RadioProfiles* profiles() const override { return expose_profiles ? &config : nullptr; }
  uint8_t receiveProfile() const override { return selected; }
  void loop() override { ++loop_count; }
  int recvRaw(uint8_t* bytes, int maximum) override {
    assert(incoming.size() <= size_t(maximum));
    const int size = incoming.size();
    if (size) memcpy(bytes, incoming.data(), size);
    incoming.clear();
    return size;
  }
  uint32_t getEstAirtimeFor(int) override { return airtime; }
  uint32_t getProfileAirtime(uint8_t, int, uint8_t = 0) override { return airtime; }
  float packetScore(float, int) override { return 1; }
  mesh::RadioParamApplyResult prepareTransmitProfile(uint8_t target) override {
    prepared.push_back(target);
    if (prepare_busy) return mesh::RadioParamApplyResult::BUSY;
    if (failed_profile == target) return mesh::RadioParamApplyResult::FAILED;
    selected = target;
    return mesh::RadioParamApplyResult::APPLIED;
  }
  bool startSendRaw(const uint8_t* bytes, int size) override {
    if (!start_success) return false;
    sent.push_back(selected);
    raw_sent.emplace_back(bytes, bytes + size);
    return true;
  }
  bool isSendComplete() override { return complete; }
  void onSendFinished() override { }
  bool isInRecvMode() const override { return true; }
  bool isReceiving() override { return busy_mask & (1U << selected); }
};

struct Node : mesh::Dispatcher {
  bool reply = false, forward = false;
  int rx_delay = 0;
  unsigned received = 0;
  uint8_t received_profile = 0xff;
  float budget_factor = 0;
  uint32_t budget_window = 100000;
  std::vector<std::pair<uint32_t, bool>> results;
  Node(Radio& radio, Clock& clock, mesh::PacketManager& manager)
      : Dispatcher(radio, clock, manager) { }
  mesh::DispatcherAction onRecvPacket(mesh::Packet* packet) override {
    ++received;
    received_profile = packet->radio_profile;
    if (reply) {
      auto* response = obtainNewPacket();
      assert(response);
      fill(response);
      sendPacket(response, 0);
    }
    return forward ? ACTION_RETRANSMIT(0) : ACTION_RELEASE;
  }
  int calcRxDelay(float, uint32_t) const override { return rx_delay; }
  float getAirtimeBudgetFactor() const override { return budget_factor; }
  unsigned long getDutyCycleWindowMs() const override { return budget_window; }
  void onTransmitResult(uint32_t token, bool success) override { results.emplace_back(token, success); }
  static void fill(mesh::Packet* packet) {
    packet->header = ROUTE_TYPE_DIRECT | (PAYLOAD_TYPE_RESPONSE << PH_TYPE_SHIFT);
    packet->path_len = 0;
    packet->payload_len = 2;
    packet->payload[0] = 0x55; packet->payload[1] = 0x66;
  }
  mesh::Packet* local(uint32_t delay = 0) {
    auto* packet = obtainNewPacket();
    assert(packet);
    fill(packet);
    sendPacket(packet, 0, delay);
    return packet;
  }
  void replyOn(uint8_t profile, uint32_t generation, uint32_t delay = 0) {
    ReceiveProfileScope context(*this, profile, generation);
    local(delay);
  }
};

void tick(Node& node, Clock& clock, unsigned elapsed = 1) {
  clock.now += elapsed;
  node.loop();
}

void receive(Radio& radio, uint8_t profile, bool flood = false) {
  radio.selected = profile;
  radio.incoming = {uint8_t((flood ? ROUTE_TYPE_FLOOD : ROUTE_TYPE_DIRECT)
      | (PAYLOAD_TYPE_REQ << PH_TYPE_SHIFT)), 0, 0x55, 0x66};
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  if (scenario == "affinity") {
    Clock clock; Radio radio; StaticPoolPacketManager manager(12); Node node(radio, clock, manager);
    node.begin(); node.reply = true;
    receive(radio, 1); tick(node, clock);
    assert(node.received == 1 && node.received_profile == 1);
    assert(radio.sent == std::vector<uint8_t>({1}));
    assert(radio.config.rx_packets[1] == 1);
    assert(node.hasPendingTransmissions());
    tick(node, clock);
    assert(!node.hasPendingTransmissions() && radio.config.tx_packets[1] == 1);
    assert(manager.getFreeCount() == 12);
    node.local(); tick(node, clock);
    assert(radio.sent.back() == 0); // RX affinity did not leak to local work.
    tick(node, clock);
    node.rx_delay = 100;
    receive(radio, 1, true); tick(node, clock);
    radio.selected = 0; // Scanner moved before the delayed request was handled.
    tick(node, clock, 101); tick(node, clock);
    assert(node.received == 2 && radio.sent.back() == 1);
    assert(manager.getFreeCount() == 12);
  } else if (scenario == "matrix") {
    for (unsigned temporary = 0; temporary < 4; ++temporary)
      for (unsigned cross = 0; cross < 3; ++cross)
        for (unsigned mode = 1; mode <= 2; ++mode)
          for (unsigned origin = 0; origin < 2; ++origin) {
            Clock clock; Radio radio; StaticPoolPacketManager manager(6); Node node(radio, clock, manager);
            radio.config.primary_temporary = temporary & 1;
            radio.config.secondary_temporary = temporary & 2;
            radio.config.cross = mesh::RadioCrossMode(cross);
            radio.config.secondary.mode = mesh::RadioProfileMode(mode);
            node.begin();
            node.replyOn(origin, radio.config.generation[origin]);
            const bool crossing = cross == 1 || (cross == 0 && bool(temporary & 1) == bool(temporary & 2));
            unsigned expected = origin == 0 || mode == 2 ? 1U << origin : 0;
            if (crossing && ((origin ^ 1) == 0 || mode == 2)) expected |= 1U << (origin ^ 1);
            for (unsigned i = 0; i < 5; ++i) tick(node, clock);
            unsigned actual = 0;
            for (auto profile : radio.sent) { assert(!(actual & (1U << profile))); actual |= 1U << profile; }
            assert(actual == expected);
            assert(manager.getFreeCount() == 6);
          }
  } else if (scenario == "stale") {
    Clock clock; Radio radio; StaticPoolPacketManager manager(8); Node node(radio, clock, manager);
    node.begin(); radio.config.cross = mesh::RadioCrossMode::On;
    node.local(100);
    ++radio.config.generation[0]; // Invalidates origin and both queued copies.
    tick(node, clock, 101); tick(node, clock);
    assert(radio.sent.empty() && manager.getFreeCount() == 8);
    node.local();
    radio.config.cross = mesh::RadioCrossMode::Off;
    tick(node, clock); tick(node, clock); tick(node, clock);
    assert(radio.sent == std::vector<uint8_t>({0}) && manager.getFreeCount() == 8);
    node.replyOn(1, radio.config.generation[1], 100);
    ++radio.config.generation[1];
    tick(node, clock, 101);
    assert(radio.sent.size() == 1 && manager.getFreeCount() == 8);
    node.rx_delay = 100;
    receive(radio, 1, true); tick(node, clock);
    ++radio.config.generation[1];
    tick(node, clock, 101);
    assert(node.received == 0 && manager.getFreeCount() == 8);
  } else if (scenario == "cad") {
    Clock clock; Radio radio; StaticPoolPacketManager manager(8); Node node(radio, clock, manager);
    radio.config.cross = mesh::RadioCrossMode::On;
    node.begin(); radio.busy_mask = 1; node.local();
    tick(node, clock);
    assert(radio.sent.empty() && manager.getOutboundTotal() == 2);
    tick(node, clock);
    assert(radio.sent == std::vector<uint8_t>({1})); // Busy primary did not block secondary.
    tick(node, clock);
    radio.busy_mask = 0; tick(node, clock, 201); tick(node, clock);
    assert(radio.sent == std::vector<uint8_t>({1, 0}) && manager.getFreeCount() == 8);
  } else if (scenario == "busy") {
    Clock clock; Radio radio; StaticPoolPacketManager manager(6); Node node(radio, clock, manager);
    node.begin(); node.local(); radio.prepare_busy = true;
    tick(node, clock);
    assert(radio.sent.empty() && manager.getOutboundTotal() == 1 && !node.hasOutbound());
    radio.prepare_busy = false; tick(node, clock); tick(node, clock);
    assert(radio.sent == std::vector<uint8_t>({0}) && manager.getFreeCount() == 6);
    radio.failed_profile = 0; node.local(); tick(node, clock);
    assert(radio.sent.size() == 1 && manager.getFreeCount() == 6);
  } else if (scenario == "cad_generation") {
    Clock clock; Radio radio; StaticPoolPacketManager manager(6); Node node(radio, clock, manager);
    node.begin(); radio.busy_mask = 1; node.local(); tick(node, clock);
    ++radio.config.generation[0]; node.local();
    tick(node, clock, 5000); // Discard the old session's deferred queue item.
    tick(node, clock); // The new session starts a fresh CAD interval.
    assert(radio.sent.empty() && manager.getOutboundTotal() == 1);
  } else if (scenario == "budget") {
    Clock clock; Radio radio; StaticPoolPacketManager manager(6); Node node(radio, clock, manager);
    node.budget_factor = 99; node.budget_window = 1000; radio.airtime = 100;
    radio.config.cross = mesh::RadioCrossMode::On;
    node.begin(); node.local(); tick(node, clock); tick(node, clock, 10);
    assert(radio.prepared.empty() && radio.sent.empty() && radio.loop_count == 2);
    assert(manager.getOutboundTotal() == 2); // No tune before airtime credit.
  } else if (scenario == "shared_budget") {
    Clock clock; Radio radio; StaticPoolPacketManager manager(6); Node node(radio, clock, manager);
    node.budget_factor = 1; node.budget_window = 400; radio.airtime = 40;
    radio.config.cross = mesh::RadioCrossMode::On;
    node.begin(); node.local(); tick(node, clock);
    tick(node, clock, 40); // First completion; the shared TX timestamp is now due.
    tick(node, clock); // Next-profile start on the following dispatch pass.
    tick(node, clock, 40);
    assert(radio.sent == std::vector<uint8_t>({0, 1}));
    assert(node.getTotalAirTime() == 80);
    assert(node.getRemainingTxBudget() == 140); // One common credit balance.
    assert(manager.getFreeCount() == 6);
  } else if (scenario == "queue") {
    mesh::Packet a, b;
    PacketQueue queue(1);
    assert(queue.add(&a, 0, 0xfffffff0));
    assert(!queue.add(&b, 0, 0));
    assert(queue.peek(0xfffffff1) == &a);
    assert(queue.reschedule(&a, 10));
    assert(queue.peek(0xfffffff1) == nullptr);
    assert(queue.get(11) == &a && queue.count() == 0);
    Clock clock; Radio radio; StaticPoolPacketManager manager(1); Node node(radio, clock, manager);
    radio.config.cross = mesh::RadioCrossMode::On;
    node.begin(); node.local();
    assert(manager.getOutboundTotal() == 1 && manager.getFreeCount() == 0);
    tick(node, clock); tick(node, clock);
    assert(radio.sent == std::vector<uint8_t>({0}) && manager.getFreeCount() == 1);
  } else if (scenario == "completion") {
    {
      struct RejectManager : StaticPoolPacketManager {
        RejectManager() : StaticPoolPacketManager(1) { }
        bool queueOutbound(mesh::Packet*, uint8_t, uint32_t) override { return false; }
      } rejected;
      Clock clock; Radio radio; Node node(radio, clock, rejected); node.begin();
      auto* packet = node.obtainNewPacket(); assert(packet); Node::fill(packet);
      const uint32_t token = node.reserveTransmitToken(); node.setTransmitToken(packet, token);
      node.sendPacket(packet, 0);
      assert(node.results == (std::vector<std::pair<uint32_t, bool>>{{token, false}}));
      assert(rejected.getFreeCount() == 1);
    }
    Clock clock; Radio radio; StaticPoolPacketManager manager(6); Node node(radio, clock, manager);
    node.begin();
    auto tagged = [&]() {
      auto* packet = node.obtainNewPacket(); assert(packet); Node::fill(packet);
      const uint32_t token = node.reserveTransmitToken(); assert(token);
      node.setTransmitToken(packet, token); node.sendPacket(packet, 0);
      return token;
    };
    uint32_t token = tagged(); tick(node, clock);
    assert(node.results.empty()); // Starting a TX is not a successful response.
    tick(node, clock);
    assert(node.results == (std::vector<std::pair<uint32_t, bool>>{{token, true}}));
    node.local(); tick(node, clock); tick(node, clock);
    assert(node.results.size() == 1); // An unrelated packet cannot satisfy a barrier.
    radio.start_success = false; token = tagged(); tick(node, clock);
    assert(node.results.back() == std::make_pair(token, false));
    radio.start_success = true; radio.complete = false; token = tagged(); tick(node, clock);
    tick(node, clock, 100);
    assert(node.results.back() == std::make_pair(token, false)); // TX timeout.
    radio.complete = true; radio.failed_profile = 0; token = tagged(); tick(node, clock);
    assert(node.results.back() == std::make_pair(token, false)); // Failed tuple preparation.
    radio.failed_profile = -1; token = tagged(); ++radio.config.generation[0]; tick(node, clock);
    assert(node.results.back() == std::make_pair(token, false)); // Stale session rejection.
    radio.config.cross = mesh::RadioCrossMode::On; radio.failed_profile = 0;
    token = tagged(); tick(node, clock); tick(node, clock); tick(node, clock);
    assert(node.results[node.results.size() - 2] == std::make_pair(token, false));
    assert(node.results.back() == std::make_pair(token, true)); // Other crossed copy succeeds.
    assert(manager.getFreeCount() == 6 && !node.hasPendingTransmissions());
  } else if (scenario == "dedup") {
    SimpleMeshTables table;
    mesh::Packet primary; Node::fill(&primary);
    primary.radio_profile = 0; primary.radio_generation = 1;
    mesh::Packet secondary = primary;
    secondary.radio_profile = 1; secondary.radio_generation = 99;
    assert(!table.wasSeen(&primary)); table.markSeen(&primary);
    // Profiles share one Mesh node and one history, including when crossover
    // is disabled. Changing route/session metadata does not create a new hash.
    assert(table.wasSeen(&secondary));
    table.clear(&secondary);
    assert(!table.wasSeen(&primary));
    secondary.header = (secondary.header & ~PH_ROUTE_MASK) | ROUTE_TYPE_FLOOD;
    table.markSeen(&secondary);
    assert(table.wasSeen(&primary)); // Route changes cannot create bridge loops.
  } else if (scenario == "wire") {
    mesh::Packet packet; Node::fill(&packet);
    uint8_t raw[255], changed[255];
    uint8_t hash_before[MAX_HASH_SIZE], hash_after[MAX_HASH_SIZE];
    packet.calculatePacketHash(hash_before);
    const unsigned length = packet.writeTo(raw);
    packet.radio_profile = 1; packet.radio_origin = 1;
    packet.radio_generation = 99; packet.radio_origin_generation = 77; packet.radio_bound = true;
    packet.transmit_token = 123;
    assert(packet.writeTo(changed) == length && memcmp(raw, changed, length) == 0);
    packet.calculatePacketHash(hash_after);
    assert(memcmp(hash_before, hash_after, sizeof(hash_before)) == 0);
    assert(packet.readFrom(raw, length));
    assert(packet.radio_profile == 0 && packet.radio_origin == 0 && !packet.radio_bound);
    assert(packet.radio_generation == 0 && packet.radio_origin_generation == 0);
    assert(packet.transmit_token == 0);
    Clock clock; Radio radio; StaticPoolPacketManager manager(3); Node node(radio, clock, manager);
    radio.expose_profiles = false; node.begin(); node.local(); tick(node, clock); tick(node, clock);
    assert(radio.sent == std::vector<uint8_t>({0}) && manager.getFreeCount() == 3);
  } else if (scenario == "wrap") {
    Clock clock; clock.now = 0xfffffff0;
    Radio radio; StaticPoolPacketManager manager(3); Node node(radio, clock, manager);
    node.begin(); node.local(32);
    tick(node, clock, 31); assert(radio.sent.empty());
    tick(node, clock, 2); assert(radio.sent == std::vector<uint8_t>({0}));
    tick(node, clock); assert(manager.getFreeCount() == 3);
  } else {
    assert(!"Unknown scenario");
  }
}
