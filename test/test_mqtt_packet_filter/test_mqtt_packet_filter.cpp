#include <gtest/gtest.h>

#include "helpers/MQTTPacketFilter.h"

namespace Filter = MQTTPacketFilter;

TEST(MQTTPacketFilter, EmptyAndAllMeanEveryType) {
  for (const char* value : {"", " ", "\tall\r\n"}) {
    uint16_t mask = 0;
    ASSERT_TRUE(Filter::parse(value, &mask)) << value;
    EXPECT_EQ(Filter::kAllPacketTypes, mask);
  }
}

TEST(MQTTPacketFilter, NoneMeansNoTypes) {
  uint16_t mask = Filter::kAllPacketTypes;
  ASSERT_TRUE(Filter::parse(" none ", &mask));
  EXPECT_EQ(0u, mask);
}

TEST(MQTTPacketFilter, ParsesDecimalCsvWithWhitespaceAndDuplicates) {
  uint16_t mask = 0;
  ASSERT_TRUE(Filter::parse(" 2, 4,\t15,2 ", &mask));
  EXPECT_EQ(static_cast<uint16_t>((1u << 2) | (1u << 4) | (1u << 15)), mask);
}

TEST(MQTTPacketFilter, FullNumericListCanonicalizesToAll) {
  uint16_t mask = 0;
  ASSERT_TRUE(Filter::parse("0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15", &mask));
  EXPECT_EQ(Filter::kAllPacketTypes, mask);
  char output[Filter::kFilterTextSize];
  ASSERT_TRUE(Filter::format(mask, output, sizeof(output)));
  EXPECT_STREQ("all", output);
}

TEST(MQTTPacketFilter, ParsesPayloadTypeNames) {
  uint16_t mask = 0;
  ASSERT_TRUE(Filter::parse("advert", &mask));
  EXPECT_EQ(static_cast<uint16_t>(1u << 4), mask);

  ASSERT_TRUE(Filter::parse(" txt_msg , grp_txt ", &mask));
  EXPECT_EQ(static_cast<uint16_t>((1u << 2) | (1u << 5)), mask);

  // Names and numbers are interchangeable within one list.
  ASSERT_TRUE(Filter::parse("advert,2,raw_custom", &mask));
  EXPECT_EQ(static_cast<uint16_t>((1u << 4) | (1u << 2) | (1u << 15)), mask);

  // Output stays numeric regardless of how the value was spelled.
  char output[Filter::kFilterTextSize];
  ASSERT_TRUE(Filter::format(mask, output, sizeof(output)));
  EXPECT_STREQ("2,4,15", output);
}

TEST(MQTTPacketFilter, EveryNamedTypeResolvesToItsPacketHeaderValue) {
  // Pins the table against src/Packet.h's PAYLOAD_TYPE_* values.
  const struct { const char* name; uint8_t type; } expected[] = {
    {"req", 0}, {"response", 1}, {"txt_msg", 2}, {"ack", 3}, {"advert", 4},
    {"grp_txt", 5}, {"grp_data", 6}, {"anon_req", 7}, {"path", 8},
    {"trace", 9}, {"multipart", 10}, {"control", 11}, {"raw_custom", 15},
  };
  for (const auto& e : expected) {
    uint16_t mask = 0;
    ASSERT_TRUE(Filter::parse(e.name, &mask)) << e.name;
    EXPECT_EQ(static_cast<uint16_t>(1u << e.type), mask) << e.name;
  }
  // Reserved types have no name and stay reachable by number only.
  size_t count = 0;
  const Filter::NamedPacketType* names = Filter::namedPacketTypes(&count);
  EXPECT_EQ(sizeof(expected) / sizeof(expected[0]), count);
  for (size_t i = 0; i < count; ++i) {
    EXPECT_TRUE(names[i].type < 12 || names[i].type == 15) << names[i].name;
  }
}

TEST(MQTTPacketFilter, RejectsMalformedOrOutOfRangeValuesWithoutChangingOutput) {
  const char* invalid[] = {
    ",", "1,", ",1", "1,,2", "1, ,2", "-1", "+1", "0x2", "16",
    "999999999999999999999", "ALL", "All", "all,2", "none,2", "2-4", "2x",
    // Names are lowercase, exact, and are not whole-value keywords.
    "ADVERT", "Advert", "advert_", "adver", "advertx", "reserved", "12_reserved",
    "2,all", "advert,none", "advert advert", "advert;2",
  };
  for (const char* value : invalid) {
    uint16_t mask = 0x1234;
    EXPECT_FALSE(Filter::parse(value, &mask)) << value;
    EXPECT_EQ(0x1234, mask) << value;
  }
  EXPECT_FALSE(Filter::parse(nullptr, nullptr));
}

TEST(MQTTPacketFilter, FormatsSubsetsInAscendingOrder) {
  const uint16_t mask = static_cast<uint16_t>(
      (1u << 15) | (1u << 2) | (1u << 10) | (1u << 0));
  char output[Filter::kFilterTextSize];
  ASSERT_TRUE(Filter::format(mask, output, sizeof(output)));
  EXPECT_STREQ("0,2,10,15", output);
}

TEST(MQTTPacketFilter, FormatsNoneAndHonorsExactBufferBoundaries) {
  char none[5];
  ASSERT_TRUE(Filter::format(0, none, sizeof(none)));
  EXPECT_STREQ("none", none);

  char too_small[4] = {'x', 'x', 'x', '\0'};
  EXPECT_FALSE(Filter::format(0, too_small, sizeof(too_small)));
  EXPECT_STREQ("", too_small);

  char all_types[Filter::kFilterTextSize];
  const uint16_t subset = static_cast<uint16_t>(Filter::kAllPacketTypes & ~(1u << 14));
  ASSERT_TRUE(Filter::format(subset, all_types, sizeof(all_types)));
  EXPECT_STREQ("0,1,2,3,4,5,6,7,8,9,10,11,12,13,15", all_types);
  char one_short[34];
  EXPECT_FALSE(Filter::format(subset, one_short, sizeof(one_short)));
  EXPECT_STREQ("", one_short);
}

TEST(MQTTPacketFilter, MembershipIsBoundedToFourBitTypes) {
  EXPECT_TRUE(Filter::allows(static_cast<uint16_t>(1u << 0), 0));
  EXPECT_TRUE(Filter::allows(static_cast<uint16_t>(1u << 15), 15));
  EXPECT_FALSE(Filter::allows(Filter::kAllPacketTypes, 16));
  EXPECT_FALSE(Filter::allows(0, 0));
}

TEST(MQTTPacketFilter, EligibilityIgnoresConnectionButRequiresTopicAndFilter) {
  const uint16_t only_advert = static_cast<uint16_t>(1u << 4);
  EXPECT_TRUE(Filter::slotEligible(true, true, only_advert, 4));
  EXPECT_FALSE(Filter::slotEligible(false, true, only_advert, 4));
  EXPECT_FALSE(Filter::slotEligible(true, false, only_advert, 4));
  EXPECT_FALSE(Filter::slotEligible(true, true, only_advert, 2));
}

TEST(MQTTPacketFilter, AllFilteredOrTopicIncompatibleTargetsCompleteIntentionally) {
  const uint16_t only_text = static_cast<uint16_t>(1u << 2);
  const bool filtered = Filter::slotEligible(true, true, only_text, 4);
  const bool topic_incompatible =
      Filter::slotEligible(true, false, Filter::kAllPacketTypes, 4);
  EXPECT_FALSE(filtered || topic_incompatible);
  EXPECT_TRUE(Filter::publishComplete(filtered || topic_incompatible, false));
}

TEST(MQTTPacketFilter, EligibleDisconnectedTargetStillRequiresRetry) {
  const uint16_t only_advert = static_cast<uint16_t>(1u << 4);
  // Connection state is intentionally absent from slotEligible(): the first
  // slot remains a target while disconnected. A second connected slot whose
  // filter rejects the advert cannot turn that into intentional completion.
  const bool disconnected_eligible =
      Filter::slotEligible(true, true, only_advert, 4);
  const bool connected_filtered =
      Filter::slotEligible(true, true, static_cast<uint16_t>(1u << 2), 4);
  ASSERT_TRUE(disconnected_eligible);
  ASSERT_FALSE(connected_filtered);
  EXPECT_FALSE(Filter::publishComplete(
      disconnected_eligible || connected_filtered, false));
}

TEST(MQTTPacketFilter, PublishFailureRetriesAndAnySuccessCompletes) {
  EXPECT_FALSE(Filter::publishComplete(true, false));
  EXPECT_TRUE(Filter::publishComplete(true, true));
  EXPECT_TRUE(Filter::publishComplete(false, true));  // defensive: success wins
}

TEST(MQTTPacketFilter, UnsupportedRawPathCannotHideEligiblePacketFailure) {
  const bool packet_eligible = true;
  const bool packet_published = false;
  const bool raw_eligible = false;  // e.g. MeshRank has no raw topic
  const bool raw_published = false;

  EXPECT_FALSE(Filter::publishComplete(
      packet_eligible || raw_eligible,
      packet_published || raw_published));
  EXPECT_TRUE(Filter::publishComplete(false, false));
}

TEST(MQTTPacketFilter, PacketAndRawCanShareTheSameMaskDecision) {
  const uint16_t text_and_advert = static_cast<uint16_t>((1u << 2) | (1u << 4));
  for (uint8_t type = 0; type <= 15; ++type) {
    const bool expected = type == 2 || type == 4;
    EXPECT_EQ(expected, Filter::allows(text_and_advert, type)) << unsigned(type);
  }
}

TEST(MQTTPacketFilter, CandidateIsTheTopicFreePartOfEligibility) {
  const uint16_t only_advert = static_cast<uint16_t>(1u << 4);
  EXPECT_TRUE(Filter::slotCandidate(true, only_advert, 4));
  EXPECT_FALSE(Filter::slotCandidate(false, only_advert, 4));
  EXPECT_FALSE(Filter::slotCandidate(true, only_advert, 2));
  // A candidate is exactly an eligible slot minus the topic-support term. The
  // bridge evaluates the cheap half first and only builds a topic for slots
  // that pass it, so the two must stay in this exact relationship.
  for (int enabled = 0; enabled <= 1; ++enabled) {
    for (int topic = 0; topic <= 1; ++topic) {
      for (uint8_t type = 0; type <= 15; ++type) {
        EXPECT_EQ(Filter::slotCandidate(enabled != 0, only_advert, type) && topic != 0,
                  Filter::slotEligible(enabled != 0, topic != 0, only_advert, type));
      }
    }
  }
}

// The gate has to resolve topic support before anything expensive runs: a
// connected slot that cannot form a topic (MeshRank raw, or a meshcore preset
// with no IATA) must reach "intentionally complete" without a serialisation.
TEST(MQTTPacketFilter, TopicIncompatibleTargetsAreCompleteWithoutPublishing) {
  const uint16_t all = Filter::kAllPacketTypes;
  for (uint8_t type = 0; type <= 15; ++type) {
    const bool eligible = Filter::slotEligible(true, false, all, type);
    EXPECT_FALSE(eligible) << unsigned(type);
    EXPECT_TRUE(Filter::publishComplete(eligible, false)) << unsigned(type);
  }
  // With topic support restored the same slot becomes a retry target again.
  EXPECT_TRUE(Filter::slotEligible(true, true, all, 4));
  EXPECT_FALSE(Filter::publishComplete(Filter::slotEligible(true, true, all, 4), false));
}

TEST(MQTTPacketFilter, EnabledUnionIgnoresDisabledSlotsAndGatesTheQueue) {
  const uint16_t masks[3] = {
    static_cast<uint16_t>(1u << 2),   // text only
    static_cast<uint16_t>(1u << 4),   // adverts only
    Filter::kAllPacketTypes,          // everything, but disabled below
  };
  const bool enabled[3] = {true, true, false};

  const uint16_t combined = Filter::enabledUnion(masks, enabled, 3);
  EXPECT_EQ(static_cast<uint16_t>((1u << 2) | (1u << 4)), combined);
  EXPECT_TRUE(Filter::allows(combined, 2));
  EXPECT_TRUE(Filter::allows(combined, 4));
  EXPECT_FALSE(Filter::allows(combined, 3));  // no enabled slot wants ACKs

  // A null `enabled` array counts every slot; no slots means no destination.
  EXPECT_EQ(Filter::kAllPacketTypes, Filter::enabledUnion(masks, nullptr, 3));
  EXPECT_EQ(0u, Filter::enabledUnion(masks, enabled, 0));
  EXPECT_EQ(0u, Filter::enabledUnion(nullptr, enabled, 3));

  // The union must never reject a type any single slot would have published.
  for (uint8_t type = 0; type <= 15; ++type) {
    const bool any_slot_allows =
        (enabled[0] && Filter::allows(masks[0], type)) ||
        (enabled[1] && Filter::allows(masks[1], type)) ||
        (enabled[2] && Filter::allows(masks[2], type));
    EXPECT_EQ(any_slot_allows, Filter::allows(combined, type)) << unsigned(type);
  }
}

TEST(MQTTPacketFilter, AllMasksDefaultDrivesTheShortPrefsPayload) {
  uint16_t masks[6];
  for (int i = 0; i < 6; ++i) masks[i] = Filter::kAllPacketTypes;
  EXPECT_TRUE(Filter::allMasksDefault(masks, 6));
  EXPECT_TRUE(Filter::allMasksDefault(nullptr, 6));
  EXPECT_TRUE(Filter::allMasksDefault(masks, 0));

  // Any slot off the default — including "none" — opts into the longer payload.
  for (int i = 0; i < 6; ++i) {
    masks[i] = 0;
    EXPECT_FALSE(Filter::allMasksDefault(masks, 6)) << i;
    masks[i] = static_cast<uint16_t>(Filter::kAllPacketTypes & ~1u);
    EXPECT_FALSE(Filter::allMasksDefault(masks, 6)) << i;
    masks[i] = Filter::kAllPacketTypes;
  }
  EXPECT_TRUE(Filter::allMasksDefault(masks, 6));
}

// countTypes() backs the diag line's fallback when the exact list won't fit,
// so it has to agree with what format() would have emitted.
TEST(MQTTPacketFilter, CountTypesMatchesTheFormattedList) {
  EXPECT_EQ(0, Filter::countTypes(0));
  EXPECT_EQ(16, Filter::countTypes(Filter::kAllPacketTypes));
  EXPECT_EQ(1, Filter::countTypes(static_cast<uint16_t>(1u << 15)));

  for (uint32_t mask = 0; mask <= 0xFFFFu; ++mask) {
    uint8_t expected = 0;
    for (uint8_t type = 0; type <= 15; ++type) {
      if ((mask & (1u << type)) != 0) expected++;
    }
    ASSERT_EQ(expected, Filter::countTypes(static_cast<uint16_t>(mask))) << mask;
  }
}

// The diag fallback exists because a clipped list parses as a real, different
// allowlist. Pin that hazard so the fallback is never "simplified" away.
TEST(MQTTPacketFilter, ClippedListStillParsesAsADifferentAllowlist) {
  const uint16_t configured = static_cast<uint16_t>(Filter::kAllPacketTypes & ~1u);
  char text[Filter::kFilterTextSize];
  ASSERT_TRUE(Filter::format(configured, text, sizeof(text)));
  ASSERT_STREQ("1,2,3,4,5,6,7,8,9,10,11,12,13,14,15", text);

  // Drop the trailing "15" the way a bounded reply buffer would.
  char clipped[Filter::kFilterTextSize];
  memcpy(clipped, text, sizeof(clipped));
  clipped[strlen("1,2,3,4,5,6,7,8,9,10,11,12,13,14,")] = '\0';

  uint16_t misread = 0;
  ASSERT_FALSE(Filter::parse(clipped, &misread));  // trailing comma is rejected
  clipped[strlen("1,2,3,4,5,6,7,8,9,10,11,12,13,14")] = '\0';
  ASSERT_TRUE(Filter::parse(clipped, &misread));   // but one char earlier reads clean
  EXPECT_NE(configured, misread) << "a clipped list must not be mistaken for the real one";
  EXPECT_EQ(14, Filter::countTypes(misread));
  EXPECT_EQ(15, Filter::countTypes(configured));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
