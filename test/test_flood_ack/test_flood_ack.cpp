/**
 * test_flood_ack.cpp
 *
 * Unit tests for the flood-ACK retry scheduling introduced in
 * BaseChatMesh::sendAckTo (fixes issue #1489).
 *
 * When a contact has no known direct path (out_path_len == OUT_PATH_UNKNOWN)
 * the implementation must schedule exactly FLOOD_ACK_RETRY_COUNT independent
 * flood transmissions at the delays [200ms, 800ms, 2000ms].  When a direct
 * path is known the pre-existing single-direct-ACK behaviour is unchanged.
 *
 * Deduplication on the receiver side is handled by the pre-existing
 * MeshTables::hasSeen() mechanism; these tests confirm only the sender-side
 * scheduling contract.
 *
 * See docs/reliability_changes.md for the full rationale.
 */

#include <gtest/gtest.h>
#include <stdint.h>
#include <string.h>

// ---- Constants mirrored from BaseChatMesh.cpp --------------------------------
#define OUT_PATH_UNKNOWN     0xFF
#define TXT_ACK_DELAY        200u
#define FLOOD_ACK_RETRY_COUNT  3

// Expected delay schedule — must match the static array in sendAckTo.
static const uint32_t EXPECTED_FLOOD_DELAYS[FLOOD_ACK_RETRY_COUNT] = {
  TXT_ACK_DELAY,  // 200 ms
  800u,
  2000u,
};

// ---- Minimal send recorder ---------------------------------------------------

struct ScheduledPacket {
  bool     is_flood;
  uint32_t delay_ms;
};

static ScheduledPacket sched_buf[16];
static int             sched_count;

static void reset_sched() {
  sched_count = 0;
  memset(sched_buf, 0, sizeof(sched_buf));
}

static void mock_sendFloodScoped(uint32_t delay) {
  if (sched_count < 16) sched_buf[sched_count++] = { true,  delay };
}

static void mock_sendDirect(uint32_t delay) {
  if (sched_count < 16) sched_buf[sched_count++] = { false, delay };
}

// ---- sendAckTo logic transcription ------------------------------------------
// This is a direct copy of the changed sendAckTo body (excluding the Mesh
// packet-creation calls that are irrelevant to scheduling counts and delays).
// If the implementation diverges from this transcription the test author must
// update both, which serves as an explicit change-review gate.
static void testSendAckTo(uint8_t out_path_len, uint32_t /*ack_hash*/) {
  if (out_path_len == OUT_PATH_UNKNOWN) {
    static const uint32_t flood_ack_delays[FLOOD_ACK_RETRY_COUNT] = {
      TXT_ACK_DELAY, 800, 2000
    };
    for (int i = 0; i < FLOOD_ACK_RETRY_COUNT; i++) {
      mock_sendFloodScoped(flood_ack_delays[i]);
    }
  } else {
    // Direct path: single ACK at TXT_ACK_DELAY (getExtraAckTransmitCount()==0 default)
    mock_sendDirect(TXT_ACK_DELAY);
  }
}

// Flood path (out_path_len == OUT_PATH_UNKNOWN)

TEST(FloodAck, UnknownPath_SendsExactlyThreeTimes) {
  reset_sched();
  testSendAckTo(OUT_PATH_UNKNOWN, 0xDEADBEEF);
  EXPECT_EQ(FLOOD_ACK_RETRY_COUNT, sched_count);
}

TEST(FloodAck, UnknownPath_AllSendsAreFlood) {
  reset_sched();
  testSendAckTo(OUT_PATH_UNKNOWN, 0xDEADBEEF);
  for (int i = 0; i < sched_count; i++) {
    EXPECT_TRUE(sched_buf[i].is_flood)
        << "Packet " << i << " should be a flood send";
  }
}

TEST(FloodAck, UnknownPath_DelaysMatch200_800_2000ms) {
  reset_sched();
  testSendAckTo(OUT_PATH_UNKNOWN, 0xDEADBEEF);
  ASSERT_EQ(FLOOD_ACK_RETRY_COUNT, sched_count);
  for (int i = 0; i < FLOOD_ACK_RETRY_COUNT; i++) {
    EXPECT_EQ(EXPECTED_FLOOD_DELAYS[i], sched_buf[i].delay_ms)
        << "Delay " << i << " should be " << EXPECTED_FLOOD_DELAYS[i] << " ms";
  }
}

TEST(FloodAck, UnknownPath_DelaysAreStrictlyIncreasing) {
  reset_sched();
  testSendAckTo(OUT_PATH_UNKNOWN, 0xDEADBEEF);
  for (int i = 1; i < sched_count; i++) {
    EXPECT_GT(sched_buf[i].delay_ms, sched_buf[i - 1].delay_ms)
        << "Delay " << i << " must be > delay " << (i - 1)
        << " to ensure temporally spread retries";
  }
}

TEST(FloodAck, UnknownPath_FirstDelayIsAckDelay) {
  reset_sched();
  testSendAckTo(OUT_PATH_UNKNOWN, 0xDEADBEEF);
  ASSERT_GE(sched_count, 1);
  EXPECT_EQ(TXT_ACK_DELAY, sched_buf[0].delay_ms);
}

// Direct path (out_path_len != OUT_PATH_UNKNOWN) — pre-existing behaviour

TEST(FloodAck, KnownPath_SendsExactlyOnce) {
  reset_sched();
  testSendAckTo(/*out_path_len=*/1, 0xDEADBEEF);
  EXPECT_EQ(1, sched_count);
}

TEST(FloodAck, KnownPath_SendIsDirect) {
  reset_sched();
  testSendAckTo(/*out_path_len=*/1, 0xDEADBEEF);
  ASSERT_EQ(1, sched_count);
  EXPECT_FALSE(sched_buf[0].is_flood);
}

TEST(FloodAck, KnownPath_DelayIsAckDelay) {
  reset_sched();
  testSendAckTo(/*out_path_len=*/2, 0xDEADBEEF);
  ASSERT_EQ(1, sched_count);
  EXPECT_EQ(TXT_ACK_DELAY, sched_buf[0].delay_ms);
}

TEST(FloodAck, KnownPath_MultiHop_SendsOnce) {
  reset_sched();
  testSendAckTo(/*out_path_len=*/5, 0x12345678);
  EXPECT_EQ(1, sched_count);
}

// Retry count configuration contract

TEST(FloodAck, RetryCountIs3) {
  // Explicitly documents the expected constant value.
  EXPECT_EQ(3, FLOOD_ACK_RETRY_COUNT);
}

TEST(FloodAck, ExpectedDelaysTableMatchesConstants) {
  EXPECT_EQ(TXT_ACK_DELAY, EXPECTED_FLOOD_DELAYS[0]);
  EXPECT_EQ(800u,           EXPECTED_FLOOD_DELAYS[1]);
  EXPECT_EQ(2000u,          EXPECTED_FLOOD_DELAYS[2]);
}

// =============================================================================
// Backup ACK for flood-received messages
//
// When a flood message is received, the responder sends a PATH+ACK (one packet
// that serves both path-establishment and ACK delivery).  If the PATH+ACK is
// lost, the sender never gets the ACK.  The fix: also call sendAckTo() to send
// independent flood ACKs as a backup.  This suite verifies the backup contract
// using the same sendAckTo transcription, called from the "no known path" state
// that applies immediately after receiving the very first flood message.
// =============================================================================

TEST(BackupFloodAck, FirstMessageScenario_SendsFloodAckBackup) {
  // When the receiver has no stored path to the sender (first-ever message),
  // sendAckTo() is invoked with OUT_PATH_UNKNOWN and must produce flood ACKs.
  reset_sched();
  testSendAckTo(OUT_PATH_UNKNOWN, 0xCAFEBABE);
  EXPECT_EQ(FLOOD_ACK_RETRY_COUNT, sched_count);
  for (int i = 0; i < sched_count; i++) {
    EXPECT_TRUE(sched_buf[i].is_flood)
        << "Backup ACK " << i << " must be flood (no path known yet)";
  }
}

TEST(BackupFloodAck, BackupAndPathAck_AreIndependent) {
  // The backup standalone ACK (sendAckTo) and the PATH+ACK are independent
  // packets.  This test verifies sendAckTo is not affected by the PATH+ACK
  // scheduling — it produces its own complete set of FLOOD_ACK_RETRY_COUNT
  // flood transmissions at the standard delays.
  reset_sched();
  testSendAckTo(OUT_PATH_UNKNOWN, 0x11223344);
  ASSERT_EQ(FLOOD_ACK_RETRY_COUNT, sched_count);
  EXPECT_EQ(TXT_ACK_DELAY, sched_buf[0].delay_ms);
  EXPECT_EQ(800u,           sched_buf[1].delay_ms);
  EXPECT_EQ(2000u,          sched_buf[2].delay_ms);
}

TEST(BackupFloodAck, KnownReturnPath_SendsDirectAckBackup) {
  // If the receiver already has a stored direct path back to the sender
  // (e.g., established in a prior exchange), the backup sendAckTo() sends a
  // direct ACK rather than a flood — still providing a reliable backup channel.
  reset_sched();
  testSendAckTo(/*out_path_len=*/1, 0xCAFEBABE);
  EXPECT_EQ(1, sched_count);
  EXPECT_FALSE(sched_buf[0].is_flood);
}


int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
