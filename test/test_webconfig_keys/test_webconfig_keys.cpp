// Host tests for the WebConfig key allowlist / secret / slot-prefix helpers
// (src/helpers/WebConfigKeys.h). These parse attacker-supplied POST keys, so
// coverage of the length-guard and boundary cases matters for safety.
#include <gtest/gtest.h>
#include "helpers/WebConfigKeys.h"

// ---- allowlist ------------------------------------------------------------

TEST(WebConfigKeys, AllowsKnownScalarKeys) {
  EXPECT_TRUE(wcIsAllowedSetKey("name"));
  EXPECT_TRUE(wcIsAllowedSetKey("radio"));
  EXPECT_TRUE(wcIsAllowedSetKey("repeat"));
  EXPECT_TRUE(wcIsAllowedSetKey("wifi.ssid"));
  EXPECT_TRUE(wcIsAllowedSetKey("mqtt.iata"));
  EXPECT_TRUE(wcIsAllowedSetKey("mqtt.neighbors"));
  EXPECT_TRUE(wcIsAllowedSetKey("mqtt.neighbors.interval"));
  EXPECT_TRUE(wcIsAllowedSetKey("snmp.community"));
  EXPECT_TRUE(wcIsAllowedSetKey("timezone.offset"));
}

TEST(WebConfigKeys, AllowsPerSlotKeys) {
  EXPECT_TRUE(wcIsAllowedSetKey("mqtt1.preset"));
  EXPECT_TRUE(wcIsAllowedSetKey("mqtt1.server"));
  EXPECT_TRUE(wcIsAllowedSetKey("mqtt1.token"));
  EXPECT_TRUE(wcIsAllowedSetKey("mqtt2.filter"));
  EXPECT_TRUE(wcIsAllowedSetKey("mqtt6.audience"));  // MAX_MQTT_SLOTS == 6
}

TEST(WebConfigKeys, RejectsDangerousOrUnknownKeys) {
  EXPECT_FALSE(wcIsAllowedSetKey("erase"));
  EXPECT_FALSE(wcIsAllowedSetKey("password"));
  EXPECT_FALSE(wcIsAllowedSetKey("reboot"));
  EXPECT_FALSE(wcIsAllowedSetKey("bogus"));
  EXPECT_FALSE(wcIsAllowedSetKey("mqtt1.bogus"));   // unknown slot field
  EXPECT_FALSE(wcIsAllowedSetKey(""));
}

TEST(WebConfigKeys, AdminPasswordIsNotAnAllowlistedSetKey) {
  EXPECT_TRUE(wcIsAdminPasswordKey("password"));
  EXPECT_FALSE(wcIsAdminPasswordKey("admin.password"));
  EXPECT_FALSE(wcIsAdminPasswordKey(""));
  EXPECT_FALSE(wcIsAllowedSetKey("password"));  // never reachable as `set password`
}

TEST(WebConfigKeys, AdminPasswordFitsNodePrefsAndRejectsLineBreaks) {
  EXPECT_FALSE(wcIsValidAdminPassword(NULL));
  EXPECT_FALSE(wcIsValidAdminPassword(""));
  EXPECT_TRUE(wcIsValidAdminPassword("new-password"));
  EXPECT_TRUE(wcIsValidAdminPassword("123456789012345"));
  EXPECT_FALSE(wcIsValidAdminPassword("1234567890123456"));
  EXPECT_FALSE(wcIsValidAdminPassword("line\nbreak"));
  EXPECT_FALSE(wcIsValidAdminPassword("line\rbreak"));
}

TEST(WebConfigKeys, SlotIndexBoundsMatchMaxSlots) {
  EXPECT_FALSE(wcIsAllowedSetKey("mqtt0.preset"));  // slot 0 invalid
  EXPECT_TRUE(wcIsAllowedSetKey("mqtt6.preset"));   // last valid slot
  EXPECT_FALSE(wcIsAllowedSetKey("mqtt7.preset"));  // beyond MAX_MQTT_SLOTS
  EXPECT_FALSE(wcIsAllowedSetKey("mqtt9.preset"));
}

TEST(WebConfigKeys, IsCaseSensitive) {
  EXPECT_FALSE(wcIsAllowedSetKey("Name"));
  EXPECT_FALSE(wcIsAllowedSetKey("MQTT1.preset"));
}

// ---- short-key OOB guard --------------------------------------------------
// The slot-prefix probe indexes key[4..6]; these short strings must be rejected
// without ever reading past the terminator.

TEST(WebConfigKeys, ShortKeysRejectedSafely) {
  EXPECT_FALSE(wcIsSlotKeyPrefix(""));
  EXPECT_FALSE(wcIsSlotKeyPrefix("m"));
  EXPECT_FALSE(wcIsSlotKeyPrefix("mq"));
  EXPECT_FALSE(wcIsSlotKeyPrefix("mqt"));
  EXPECT_FALSE(wcIsSlotKeyPrefix("mqtt"));     // 4 chars — no digit/dot
  EXPECT_FALSE(wcIsSlotKeyPrefix("mqtt1"));    // 5 chars — no dot
  EXPECT_FALSE(wcIsSlotKeyPrefix("mqtt1."));   // 6 chars — no field char
  EXPECT_TRUE(wcIsSlotKeyPrefix("mqtt1.x"));   // 7 chars — minimum valid
  // Same guard via the public allowlist/secret entry points:
  EXPECT_FALSE(wcIsAllowedSetKey("mqtt"));
  EXPECT_FALSE(wcIsSecretKey("m"));
  EXPECT_FALSE(wcIsSecretKey("mqtt"));
}

TEST(WebConfigKeys, SlotPrefixDigitRange) {
  EXPECT_FALSE(wcIsSlotKeyPrefix("mqtt0.x"));
  EXPECT_TRUE(wcIsSlotKeyPrefix("mqtt6.x"));
  EXPECT_FALSE(wcIsSlotKeyPrefix("mqtt7.x"));
  EXPECT_FALSE(wcIsSlotKeyPrefix("mqttA.x"));  // non-digit
}

// ---- secret classification ------------------------------------------------

TEST(WebConfigKeys, SecretKeysDetected) {
  EXPECT_TRUE(wcIsSecretKey("wifi.pwd"));
  EXPECT_TRUE(wcIsSecretKey("mqtt1.password"));
  EXPECT_TRUE(wcIsSecretKey("mqtt3.token"));
  EXPECT_TRUE(wcIsSecretKey("mqtt6.password"));
}

TEST(WebConfigKeys, NonSecretKeysNotFlagged) {
  EXPECT_FALSE(wcIsSecretKey("wifi.ssid"));
  EXPECT_FALSE(wcIsSecretKey("mqtt1.username"));  // username is not masked
  EXPECT_FALSE(wcIsSecretKey("mqtt1.server"));
  EXPECT_FALSE(wcIsSecretKey("mqtt1.filter"));
  EXPECT_FALSE(wcIsSecretKey("mqtt.origin"));
  EXPECT_FALSE(wcIsSecretKey("name"));
  EXPECT_FALSE(wcIsSecretKey(""));
}

TEST(WebConfigKeys, EverySecretKeyIsAlsoAllowed) {
  // A secret key must be one the portal can actually set, or the masking is moot.
  const char* secrets[] = {"wifi.pwd", "mqtt1.password", "mqtt1.token",
                           "mqtt6.password", "mqtt6.token"};
  for (const char* k : secrets) {
    EXPECT_TRUE(wcIsSecretKey(k)) << k;
    EXPECT_TRUE(wcIsAllowedSetKey(k)) << k;
  }
}

// ---- CLI secret reads ----------------------------------------------------
// The web CLI runs commands with sender_timestamp 0, which is how CommonCLI
// recognises the serial console and answers secret getters in plaintext. These
// are the reads that must be masked back down for an HTTP caller.

TEST(WebConfigKeys, MasksEverySecretReadTheCliCanReach) {
  const char* masked[] = {
    "get prv.key",           // this node's identity — the worst one to leak
    "get wifi.pwd",          // grants the operator's LAN, not just the node
    "get guest.password",
    "get alert.psk",
    "get bridge.secret",
    "get mqtt1.password", "get mqtt1.token",
    "get mqtt6.password", "get mqtt6.token",
    "get  wifi.pwd",         // extra space after the verb
  };
  for (const char* c : masked) EXPECT_TRUE(wcIsSecretReadCommand(c)) << c;
}

TEST(WebConfigKeys, DoesNotMaskReadsThatCarryNoSecret) {
  const char* plain[] = {
    "get wifi.ssid", "get mqtt1.username", "get mqtt1.server", "get tx",
    "get public.key",        // public half, safe to read
    "get mqtt.owner",        // an owner's public key, not a credential
  };
  for (const char* c : plain) EXPECT_FALSE(wcIsSecretReadCommand(c)) << c;
}

TEST(WebConfigKeys, OnlyMasksReads) {
  // Writing a secret has always been the portal's job and reveals nothing;
  // only the read is restricted. Nor may a prefix be mistaken for a `get`.
  EXPECT_FALSE(wcIsSecretReadCommand("set wifi.pwd hunter2"));
  EXPECT_FALSE(wcIsSecretReadCommand("set prv.key aabb"));
  EXPECT_FALSE(wcIsSecretReadCommand("password hunter2"));
  EXPECT_FALSE(wcIsSecretReadCommand("getwifi.pwd"));
  EXPECT_FALSE(wcIsSecretReadCommand("get"));
  EXPECT_FALSE(wcIsSecretReadCommand(""));
}

// ---- request correlation -------------------------------------------------

TEST(WebConfigKeys, AcceptsExactHexRequestIds) {
  EXPECT_TRUE(wcIsValidReqId("0123456789abcdef"));
  EXPECT_TRUE(wcIsValidReqId("ABCDEF0123456789"));
}

TEST(WebConfigKeys, RejectsMissingMalformedOrWrongLengthRequestIds) {
  EXPECT_FALSE(wcIsValidReqId(NULL));
  EXPECT_FALSE(wcIsValidReqId(""));
  EXPECT_FALSE(wcIsValidReqId("0123456789abcde"));
  EXPECT_FALSE(wcIsValidReqId("0123456789abcdef0"));
  EXPECT_FALSE(wcIsValidReqId("0123456789abcdeg"));
  EXPECT_FALSE(wcIsValidReqId("01234567-9abcdef"));
  EXPECT_FALSE(wcIsValidReqId("01234567 9abcdef"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
