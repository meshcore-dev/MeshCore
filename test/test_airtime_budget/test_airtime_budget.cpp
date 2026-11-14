#include <gtest/gtest.h>
#include <AirtimeBudget.h>
#include <Dispatcher.h>
#include <vector>

using namespace mesh;

// ---------------------------------------------------------------------------
// AirtimeBudget: the rolling-window ledger itself
// ---------------------------------------------------------------------------

TEST(AirtimeBudget, StartsEmptyAndAccumulates) {
  AirtimeBudget budget;
  budget.begin(3600000, 36000, 0, AirtimeBudget::MAX_SLOTS);

  EXPECT_TRUE(budget.isEnabled());
  EXPECT_EQ(0u, budget.getUsed());
  EXPECT_EQ(36000u, budget.getRemaining());

  budget.record(1000, 0);
  EXPECT_EQ(1000u, budget.getUsed());
  EXPECT_EQ(35000u, budget.getRemaining());
}

TEST(AirtimeBudget, DeniesSpendBeyondLimit) {
  AirtimeBudget budget;
  budget.begin(3600000, 1000, 0, AirtimeBudget::MAX_SLOTS);

  budget.record(900, 0);
  EXPECT_FALSE(budget.canSpend(200, 0));
  EXPECT_TRUE(budget.canSpend(100, 0));

  budget.record(100, 0);
  EXPECT_EQ(1000u, budget.getUsed());
  EXPECT_EQ(0u, budget.getRemaining());
  EXPECT_FALSE(budget.canSpend(1, 0));
}

TEST(AirtimeBudget, IsNotEnabledWhenLimitIsFullWindow) {
  AirtimeBudget budget;
  budget.begin(3600000, 3600000, 0, AirtimeBudget::MAX_SLOTS);   // 100% = no limit

  EXPECT_FALSE(budget.isEnabled());
  EXPECT_TRUE(budget.canSpend(3600000, 0));
}

// Usage is only forgotten once the slot holding it lies entirely outside the
// window, so the ledger over-holds rather than releasing airtime too early.
TEST(AirtimeBudget, ForgetsUsageOnlyAfterTheWholeWindow) {
  AirtimeBudget budget;
  budget.begin(1000, 500, 0, 10);   // 10 x 100 ms slots

  budget.record(500, 0);
  budget.record(500, 600);
  EXPECT_EQ(1000u, budget.getUsed());

  budget.update(1000);   // the record at t=0 has now fallen out of the window
  EXPECT_EQ(500u, budget.getUsed());

  budget.update(1600);   // and now the record at t=600 has too
  EXPECT_EQ(0u, budget.getUsed());
  EXPECT_TRUE(budget.canSpend(500, 1600));
}

TEST(AirtimeBudget, MillisWrapIsHandled) {
  AirtimeBudget budget;
  uint32_t start = 0xFFFFFF00u;
  budget.begin(1000, 500, start, 10);

  budget.record(300, start);
  uint32_t now = start + 0x150;   // wraps past zero
  EXPECT_EQ(300u, budget.getUsed());
  EXPECT_TRUE(budget.canSpend(200, now));
  EXPECT_FALSE(budget.canSpend(300, now));
}

// ---------------------------------------------------------------------------
// Dispatcher: forwarding admission and relay accounting
// ---------------------------------------------------------------------------

namespace {

class FakeClock : public MillisecondClock {
public:
  uint32_t now = 0;
  unsigned long getMillis() override { return now; }
};

class FakeRadio : public Radio {
public:
  uint32_t airtime_per_tx = 1000;
  bool send_complete = false;
  int sends_started = 0;

  int recvRaw(uint8_t* bytes, int sz) override { return 0; }
  uint32_t getEstAirtimeFor(int len_bytes) override { return airtime_per_tx; }
  float packetScore(float snr, int packet_len) override { return 0; }
  bool startSendRaw(const uint8_t* bytes, int len) override { sends_started++; send_complete = false; return true; }
  bool isSendComplete() override { return send_complete; }
  void onSendFinished() override { }
  bool isInRecvMode() const override { return true; }
};

class FakeManager : public PacketManager {
  std::vector<Packet*> _pool;
  Packet* _outbound = nullptr;
  uint32_t _outbound_at = 0;
  Packet* _inbound = nullptr;
  uint32_t _inbound_at = 0;

  bool scheduled(uint32_t at, uint32_t now) const { return (int32_t)(now - at) >= 0; }

public:
  int n_freed = 0;

  ~FakeManager() {
    for (auto p : _pool) delete p;
  }

  Packet* allocNew() override {
    auto p = new Packet();
    _pool.push_back(p);
    return p;
  }
  void free(Packet* packet) override {
    n_freed++;
    if (packet == _outbound) _outbound = nullptr;
    if (packet == _inbound) _inbound = nullptr;
  }

  void queueOutbound(Packet* packet, uint8_t priority, uint32_t scheduled_for) override {
    _outbound = packet;
    _outbound_at = scheduled_for;
  }
  Packet* getNextOutbound(uint32_t now) override {
    if (_outbound && scheduled(_outbound_at, now)) {
      auto p = _outbound;
      _outbound = nullptr;
      return p;
    }
    return nullptr;
  }
  int getOutboundCount(uint32_t now) const override { return (_outbound && scheduled(_outbound_at, now)) ? 1 : 0; }
  int getOutboundTotal() const override { return _outbound ? 1 : 0; }
  int getFreeCount() const override { return 4; }
  Packet* getOutboundByIdx(int i) override { return i == 0 ? _outbound : nullptr; }
  Packet* removeOutboundByIdx(int i) override {
    auto p = _outbound;
    _outbound = nullptr;
    return p;
  }
  void queueInbound(Packet* packet, uint32_t scheduled_for) override {
    _inbound = packet;
    _inbound_at = scheduled_for;
  }
  Packet* getNextInbound(uint32_t now) override {
    if (_inbound && scheduled(_inbound_at, now)) {
      auto p = _inbound;
      _inbound = nullptr;
      return p;
    }
    return nullptr;
  }
};

class TestDispatcher : public Dispatcher {
public:
  float fwd_factor = 0.0f;
  unsigned long window_ms = 3600000;

  TestDispatcher(Radio& radio, MillisecondClock& ms, PacketManager& mgr) : Dispatcher(radio, ms, mgr) { }

  float getForwardAirtimeBudgetFactor() const override { return fwd_factor; }

protected:
  unsigned long getDutyCycleWindowMs() const override { return window_ms; }
  DispatcherAction onRecvPacket(Packet* pkt) override { return ACTION_RETRANSMIT(3); }
};

void queueInbound(FakeManager& mgr, FakeClock& clock) {
  auto pkt = mgr.allocNew();
  pkt->header = ROUTE_TYPE_FLOOD | (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
  pkt->payload_len = 8;
  mgr.queueInbound(pkt, clock.now);
}

}   // namespace

TEST(ForwardAirtime, ForwardIsAdmittedAndMeasuredAirtimeIsRecorded) {
  FakeClock clock;
  clock.now = 1000;   // firmware never starts the dispatcher at millis()==0
  FakeRadio radio;
  FakeManager mgr;
  TestDispatcher dispatcher(radio, clock, mgr);
  radio.airtime_per_tx = 300;
  dispatcher.begin();

  queueInbound(mgr, clock);
  clock.now += 1;      // let the scheduler's 'next_tx_time' pass
  dispatcher.loop();   // admit + queue + start the send
  EXPECT_EQ(1, radio.sends_started);
  EXPECT_EQ(0, mgr.n_freed);

  radio.send_complete = true;
  clock.now += 400;    // radio was busy 400 ms (measured airtime, not the estimate)
  dispatcher.loop();

  EXPECT_EQ(0u, dispatcher.getNumForwardDropped());
  EXPECT_EQ(400u, dispatcher.getForwardAirTime());
  EXPECT_EQ(400u, dispatcher.getForwardBudgetUsed());
}

TEST(ForwardAirtime, ForwardIsDroppedWhenBudgetIsSpent) {
  FakeClock clock;
  clock.now = 1000;
  FakeRadio radio;
  FakeManager mgr;
  TestDispatcher dispatcher(radio, clock, mgr);
  dispatcher.window_ms = 600000;   // 10 minutes
  dispatcher.fwd_factor = 9.0f;    // 10% of the window may be relayed
  radio.airtime_per_tx = 70000;    // one forward alone exceeds that
  dispatcher.begin();

  queueInbound(mgr, clock);
  dispatcher.loop();

  EXPECT_EQ(0, radio.sends_started);
  EXPECT_EQ(1, mgr.n_freed);
  EXPECT_EQ(1u, dispatcher.getNumForwardDropped());
  EXPECT_EQ(0u, dispatcher.getForwardAirTime());
}

TEST(ForwardAirtime, OwnTrafficDoesNotConsumeTheRelayBudget) {
  FakeClock clock;
  clock.now = 1000;
  FakeRadio radio;
  FakeManager mgr;
  TestDispatcher dispatcher(radio, clock, mgr);
  dispatcher.window_ms = 600000;
  dispatcher.fwd_factor = 9.0f;
  radio.airtime_per_tx = 1000;
  dispatcher.begin();

  auto pkt = mgr.allocNew();
  pkt->header = ROUTE_TYPE_FLOOD | (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
  dispatcher.sendPacket(pkt, 0);
  EXPECT_FALSE(pkt->_forwarded);

  clock.now += 1;
  dispatcher.loop();
  EXPECT_EQ(1, radio.sends_started);
  radio.send_complete = true;
  clock.now += 900;
  dispatcher.loop();

  EXPECT_EQ(0u, dispatcher.getForwardAirTime());
  EXPECT_EQ(0u, dispatcher.getForwardBudgetUsed());
}

TEST(ForwardAirtime, RelayBudgetFreesUpAfterTheWindow) {
  FakeClock clock;
  clock.now = 1000;
  FakeRadio radio;
  FakeManager mgr;
  TestDispatcher dispatcher(radio, clock, mgr);
  dispatcher.window_ms = 1000;
  dispatcher.fwd_factor = 1.0f;   // 50% of the window
  radio.airtime_per_tx = 400;
  dispatcher.begin();

  queueInbound(mgr, clock);
  clock.now += 1;
  dispatcher.loop();
  EXPECT_EQ(1, radio.sends_started);
  radio.send_complete = true;
  clock.now += 400;
  dispatcher.loop();
  EXPECT_EQ(400u, dispatcher.getForwardBudgetUsed());

  queueInbound(mgr, clock);
  dispatcher.loop();   // 400 + 400 > 500: refused
  EXPECT_EQ(1u, dispatcher.getNumForwardDropped());

  clock.now += 1000;   // let the whole window pass
  queueInbound(mgr, clock);
  dispatcher.loop();
  EXPECT_EQ(2, radio.sends_started);   // admitted again
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
