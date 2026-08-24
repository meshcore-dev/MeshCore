#include <gtest/gtest.h>

#include <helpers/bridges/BridgeTxQueue.h>

#include <string.h>
#include <vector>

namespace {

const uint16_t MAX_FRAME = 250;
const uint8_t MAX_ATTEMPTS = 3;
const uint32_t ACK_TIMEOUT_MS = 100;
const uint32_t RETRY_DELAY_MS = 20;

/** Stands in for the radio. Records every hand-off and can be told to refuse. */
class FakeSender : public BridgeFrameSender {
public:
  std::vector<std::vector<uint8_t>> sent;
  bool accept = true;

  bool sendFrame(const uint8_t *frame, size_t len) override {
    if (!accept) return false;
    sent.push_back(std::vector<uint8_t>(frame, frame + len));
    return true;
  }
};

std::vector<uint8_t> frameOf(size_t len, uint8_t tag) {
  std::vector<uint8_t> f(len);
  for (size_t i = 0; i < len; i++) {
    f[i] = (uint8_t)(tag + i);
  }
  return f;
}

BridgeTxQueue *makeQueue(FakeSender &sender, uint8_t capacity = 4) {
  auto *q = new BridgeTxQueue(capacity, MAX_FRAME, MAX_ATTEMPTS, ACK_TIMEOUT_MS, RETRY_DELAY_MS);
  q->setSender(&sender);
  return q;
}

}  // namespace

TEST(BridgeTxQueue, SendsNothingWhileTheQueueIsEmpty) {
  FakeSender sender;
  auto q = makeQueue(sender);

  q->loop(0);
  q->loop(1000);

  EXPECT_TRUE(sender.sent.empty());
  EXPECT_TRUE(q->isIdle());
  delete q;
}

TEST(BridgeTxQueue, SendsAnEnqueuedFrameOnTheNextLoop) {
  FakeSender sender;
  auto q = makeQueue(sender);
  auto f = frameOf(20, 0xA0);

  ASSERT_TRUE(q->enqueue(f.data(), f.size()));
  q->loop(0);

  ASSERT_EQ(sender.sent.size(), 1u);
  EXPECT_EQ(sender.sent[0], f);
  delete q;
}

TEST(BridgeTxQueue, KeepsOnlyOneFrameInFlightUntilItCompletes) {
  FakeSender sender;
  auto q = makeQueue(sender);
  auto a = frameOf(10, 0xA0);
  auto b = frameOf(10, 0xB0);

  ASSERT_TRUE(q->enqueue(a.data(), a.size()));
  ASSERT_TRUE(q->enqueue(b.data(), b.size()));

  q->loop(0);
  q->loop(1);
  q->loop(2);

  EXPECT_EQ(sender.sent.size(), 1u) << "a second frame must not be handed over while one is in flight";
  delete q;
}

TEST(BridgeTxQueue, SendsTheNextFrameOnceTheRadioConfirms) {
  FakeSender sender;
  auto q = makeQueue(sender);
  auto a = frameOf(10, 0xA0);
  auto b = frameOf(10, 0xB0);

  ASSERT_TRUE(q->enqueue(a.data(), a.size()));
  ASSERT_TRUE(q->enqueue(b.data(), b.size()));

  q->loop(0);
  q->onSendComplete(true);
  q->loop(1);
  EXPECT_EQ(q->getSent(), 1u);

  q->loop(2);
  ASSERT_EQ(sender.sent.size(), 2u);
  EXPECT_EQ(sender.sent[0], a);
  EXPECT_EQ(sender.sent[1], b);

  q->onSendComplete(true);
  q->loop(3);
  EXPECT_EQ(q->getSent(), 2u) << "getSent() counts frames the radio confirmed";
  EXPECT_TRUE(q->isIdle());
  delete q;
}

// esp_now_send() returns ESP_ERR_ESPNOW_NO_MEM under burst load. The old code
// logged that and threw the packet away; it must survive to be retried.
TEST(BridgeTxQueue, RetriesTheSameFrameWhenTheRadioRefusesIt) {
  FakeSender sender;
  auto q = makeQueue(sender);
  auto f = frameOf(20, 0xA0);

  sender.accept = false;
  ASSERT_TRUE(q->enqueue(f.data(), f.size()));
  q->loop(0);
  EXPECT_TRUE(sender.sent.empty());
  EXPECT_EQ(q->getRadioRefusals(), 1u);

  sender.accept = true;
  q->loop(RETRY_DELAY_MS);

  ASSERT_EQ(sender.sent.size(), 1u) << "the refused frame must be retried, not dropped";
  EXPECT_EQ(sender.sent[0], f);
  EXPECT_EQ(q->getRetries(), 1u);
  delete q;
}

TEST(BridgeTxQueue, WaitsTheRetryDelayBeforeTryingAgain) {
  FakeSender sender;
  auto q = makeQueue(sender);
  auto f = frameOf(20, 0xA0);

  sender.accept = false;
  ASSERT_TRUE(q->enqueue(f.data(), f.size()));
  q->loop(0);

  sender.accept = true;
  q->loop(RETRY_DELAY_MS - 1);
  EXPECT_TRUE(sender.sent.empty()) << "must not retry before the backoff has elapsed";

  q->loop(RETRY_DELAY_MS);
  EXPECT_EQ(sender.sent.size(), 1u);
  delete q;
}

TEST(BridgeTxQueue, GivesUpOnAFrameAfterMaxAttemptsAndMovesOn) {
  FakeSender sender;
  auto q = makeQueue(sender);
  auto a = frameOf(10, 0xA0);
  auto b = frameOf(10, 0xB0);

  sender.accept = false;
  ASSERT_TRUE(q->enqueue(a.data(), a.size()));
  ASSERT_TRUE(q->enqueue(b.data(), b.size()));

  uint32_t now = 0;
  for (int i = 0; i < MAX_ATTEMPTS; i++) {
    q->loop(now);
    now += RETRY_DELAY_MS;
  }
  q->loop(now);

  EXPECT_EQ(q->getFailed(), 1u);

  sender.accept = true;
  now += RETRY_DELAY_MS;
  q->loop(now);

  ASSERT_EQ(sender.sent.size(), 1u) << "the queue must move on to the next frame";
  EXPECT_EQ(sender.sent[0], b);
  delete q;
}

// If the radio's completion callback is never invoked, nothing else will ever
// clear the in-flight state. Without a timeout the bridge goes permanently silent.
TEST(BridgeTxQueue, RecoversWhenTheCompletionCallbackNeverFires) {
  FakeSender sender;
  auto q = makeQueue(sender);
  auto f = frameOf(20, 0xA0);

  ASSERT_TRUE(q->enqueue(f.data(), f.size()));
  q->loop(0);
  ASSERT_EQ(sender.sent.size(), 1u);

  // no onSendComplete() ever arrives
  q->loop(ACK_TIMEOUT_MS - 1);
  EXPECT_EQ(sender.sent.size(), 1u);
  EXPECT_EQ(q->getTimeouts(), 0u);

  q->loop(ACK_TIMEOUT_MS);
  EXPECT_EQ(q->getTimeouts(), 1u);

  q->loop(ACK_TIMEOUT_MS + RETRY_DELAY_MS);
  EXPECT_EQ(sender.sent.size(), 2u) << "the frame must be retried after the completion timed out";
  delete q;
}

TEST(BridgeTxQueue, DoesNotStayWedgedWhenEveryCompletionIsLost) {
  FakeSender sender;
  auto q = makeQueue(sender);
  auto a = frameOf(10, 0xA0);
  auto b = frameOf(10, 0xB0);

  ASSERT_TRUE(q->enqueue(a.data(), a.size()));
  ASSERT_TRUE(q->enqueue(b.data(), b.size()));

  for (uint32_t now = 0; now <= 2000; now += 10) {
    q->loop(now);
  }

  EXPECT_TRUE(q->isIdle()) << "the queue must drain even if no completion ever arrives";
  EXPECT_GT(q->getTimeouts(), 0u);
  delete q;
}

TEST(BridgeTxQueue, RetriesWhenTheRadioReportsTheSendFailed) {
  FakeSender sender;
  auto q = makeQueue(sender);
  auto f = frameOf(20, 0xA0);

  ASSERT_TRUE(q->enqueue(f.data(), f.size()));
  q->loop(0);
  ASSERT_EQ(sender.sent.size(), 1u);

  q->onSendComplete(false);
  q->loop(1);
  q->loop(1 + RETRY_DELAY_MS);

  ASSERT_EQ(sender.sent.size(), 2u);
  EXPECT_EQ(sender.sent[1], f);
  EXPECT_EQ(q->getRetries(), 1u);
  delete q;
}

TEST(BridgeTxQueue, PreservesOrderAcrossRetries) {
  FakeSender sender;
  auto q = makeQueue(sender);
  auto a = frameOf(10, 0xA0);
  auto b = frameOf(10, 0xB0);
  auto c = frameOf(10, 0xC0);

  ASSERT_TRUE(q->enqueue(a.data(), a.size()));
  ASSERT_TRUE(q->enqueue(b.data(), b.size()));
  ASSERT_TRUE(q->enqueue(c.data(), c.size()));

  uint32_t now = 0;
  // a is refused once, then everything flows
  sender.accept = false;
  q->loop(now);
  sender.accept = true;

  for (int i = 0; i < 10; i++) {
    now += RETRY_DELAY_MS;
    q->loop(now);
    q->onSendComplete(true);
  }

  ASSERT_EQ(sender.sent.size(), 3u);
  EXPECT_EQ(sender.sent[0], a);
  EXPECT_EQ(sender.sent[1], b);
  EXPECT_EQ(sender.sent[2], c);
  delete q;
}

TEST(BridgeTxQueue, ReportsWhenAFrameIsDroppedBecauseTheQueueIsFull) {
  FakeSender sender;
  auto q = makeQueue(sender, 2);
  auto f = frameOf(10, 0xA0);

  EXPECT_TRUE(q->enqueue(f.data(), f.size()));
  EXPECT_TRUE(q->enqueue(f.data(), f.size()));
  EXPECT_FALSE(q->enqueue(f.data(), f.size()));

  EXPECT_EQ(q->queue().getDroppedFull(), 1u);
  delete q;
}

TEST(BridgeTxQueue, ResetDiscardsQueuedAndInFlightFrames) {
  FakeSender sender;
  auto q = makeQueue(sender);
  auto f = frameOf(10, 0xA0);

  ASSERT_TRUE(q->enqueue(f.data(), f.size()));
  ASSERT_TRUE(q->enqueue(f.data(), f.size()));
  q->loop(0);
  ASSERT_EQ(sender.sent.size(), 1u);

  q->reset();
  EXPECT_TRUE(q->isIdle());

  q->loop(1000);
  EXPECT_EQ(sender.sent.size(), 1u) << "nothing queued should survive a reset";
  delete q;
}

TEST(BridgeTxQueue, SurvivesLoopWithNoSenderConfigured) {
  BridgeTxQueue q(4, MAX_FRAME, MAX_ATTEMPTS, ACK_TIMEOUT_MS, RETRY_DELAY_MS);
  auto f = frameOf(10, 0xA0);

  ASSERT_TRUE(q.enqueue(f.data(), f.size()));
  q.loop(0);
  q.loop(1000);

  SUCCEED() << "no crash";
}

TEST(BridgeTxQueue, HandlesMillisRollover) {
  FakeSender sender;
  auto q = makeQueue(sender);
  auto f = frameOf(20, 0xA0);

  const uint32_t near_max = 0xFFFFFFFFu - 5;

  ASSERT_TRUE(q->enqueue(f.data(), f.size()));
  q->loop(near_max);
  ASSERT_EQ(sender.sent.size(), 1u);

  // millis() wraps past zero while the frame is in flight
  q->loop(near_max + ACK_TIMEOUT_MS);  // wraps
  EXPECT_EQ(q->getTimeouts(), 1u) << "the ack timeout must survive a millis() rollover";
  delete q;
}

// The radio owes exactly one completion per accepted send. If an attempt times
// out, its completion is still coming; crediting it to the retry would confirm a
// frame that was never acknowledged, and would advance the queue past a frame
// whose real completion is still outstanding.
TEST(BridgeTxQueue, IgnoresACompletionThatArrivesAfterItsAttemptTimedOut) {
  FakeSender sender;
  auto q = makeQueue(sender);
  auto a = frameOf(10, 0xA0);
  auto b = frameOf(10, 0xB0);

  ASSERT_TRUE(q->enqueue(a.data(), a.size()));
  ASSERT_TRUE(q->enqueue(b.data(), b.size()));

  q->loop(0);
  ASSERT_EQ(sender.sent.size(), 1u);

  q->loop(ACK_TIMEOUT_MS);
  ASSERT_EQ(q->getTimeouts(), 1u);

  q->loop(ACK_TIMEOUT_MS + RETRY_DELAY_MS);
  ASSERT_EQ(sender.sent.size(), 2u) << "frame a is retried";

  // attempt 1's completion finally lands, long after it was given up on
  q->onSendComplete(true);
  q->loop(ACK_TIMEOUT_MS + RETRY_DELAY_MS + 1);

  EXPECT_EQ(q->getSent(), 0u) << "a stale completion must not confirm the live attempt";
  EXPECT_EQ(sender.sent.size(), 2u) << "and must not advance the queue to the next frame";

  // attempt 2's real completion still works
  q->onSendComplete(true);
  q->loop(ACK_TIMEOUT_MS + RETRY_DELAY_MS + 2);
  EXPECT_EQ(q->getSent(), 1u);
  delete q;
}

// reset() happens on `set bridge.channel` / `set bridge.secret`, which restart the
// bridge under a send that is still in flight.
TEST(BridgeTxQueue, IgnoresACompletionOwedFromBeforeAReset) {
  FakeSender sender;
  auto q = makeQueue(sender);
  auto a = frameOf(10, 0xA0);
  auto b = frameOf(10, 0xB0);

  ASSERT_TRUE(q->enqueue(a.data(), a.size()));
  q->loop(0);
  ASSERT_EQ(sender.sent.size(), 1u) << "frame a is in flight";

  q->reset();

  ASSERT_TRUE(q->enqueue(b.data(), b.size()));
  q->loop(1);
  ASSERT_EQ(sender.sent.size(), 2u) << "frame b is now in flight";

  // frame a's completion, dispatched before the reset, arrives now
  q->onSendComplete(true);
  q->loop(2);

  EXPECT_EQ(q->getSent(), 0u) << "a pre-reset completion must not confirm frame b";
  delete q;
}
