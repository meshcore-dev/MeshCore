// Host contract tests for the complete MQTT publication-topic routing policy.
#define WITH_MQTT_BRIDGE 1
#define PROGMEM

#include <gtest/gtest.h>
#include <cstring>
#include <string>

#include "helpers/MQTTPresets.h"
#include "helpers/MQTTTopicRouter.h"

namespace {

constexpr const char* IATA = "DEN";
constexpr const char* DEVICE = "0123456789ABCDEF";
constexpr const char* TOKEN = "account-token";

struct TypeCase {
  int type;
  const char* name;
};

const TypeCase kTypes[] = {
  {MQTT_PUBLICATION_STATUS, "status"},
  {MQTT_PUBLICATION_PACKETS, "packets"},
  {MQTT_PUBLICATION_RAW, "raw"},
  {MQTT_PUBLICATION_NEIGHBORS, "neighbors"},
};

TEST(MQTTTopicRouter, EveryMeshCorePresetSupportsEveryPublicationType) {
  for (int preset_index = 0; preset_index < MQTT_PRESET_COUNT; ++preset_index) {
    const MQTTPresetDef& preset = MQTT_PRESETS[preset_index];
    if (preset.topic_style != MQTT_TOPIC_MESHCORE) continue;

    for (const TypeCase& type : kTypes) {
      char topic[128];
      ASSERT_TRUE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHCORE, type.type, nullptr,
                                            IATA, DEVICE, TOKEN, topic, sizeof(topic)))
          << preset.name << " / " << type.name;
      EXPECT_EQ(std::string("meshcore/DEN/0123456789ABCDEF/") + type.name, topic)
          << preset.name;
    }
  }
}

// Guards the raw exclusion: a merge from observer-firmware must not re-enable it.
TEST(MQTTTopicRouter, MeshRankTakesEveryTypeExceptRaw) {
  const MQTTPresetDef* preset = findMQTTPreset("meshrank");
  ASSERT_NE(nullptr, preset);
  ASSERT_EQ(MQTT_TOPIC_MESHRANK, preset->topic_style);

  for (const TypeCase& type : kTypes) {
    char topic[128];
    if (type.type == MQTT_PUBLICATION_RAW) {
      EXPECT_FALSE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHRANK, type.type, nullptr,
                                             IATA, DEVICE, TOKEN, topic, sizeof(topic)));
      EXPECT_STREQ("", topic);
      continue;
    }
    ASSERT_TRUE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHRANK, type.type, nullptr,
                                          IATA, DEVICE, TOKEN, topic, sizeof(topic)))
        << type.name;
    EXPECT_EQ(std::string("meshrank/uplink/account-token/0123456789ABCDEF/") + type.name,
              topic)
        << type.name;
  }
}

TEST(MQTTTopicRouter, MeshCoreRequiresUsableIataAndDevice) {
  char topic[64];
  const char* invalid_iatas[] = {nullptr, "", "XX", "XXXX", "X/X", "XXX"};
  for (const char* iata : invalid_iatas) {
    EXPECT_FALSE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHCORE, MQTT_PUBLICATION_STATUS,
                                           nullptr, iata, DEVICE, TOKEN, topic, sizeof(topic)));
    EXPECT_STREQ("", topic);
  }
  EXPECT_FALSE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHCORE, MQTT_PUBLICATION_STATUS,
                                         nullptr, IATA, nullptr, TOKEN, topic, sizeof(topic)));
  EXPECT_FALSE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHCORE, MQTT_PUBLICATION_STATUS,
                                         nullptr, IATA, "", TOKEN, topic, sizeof(topic)));
}

TEST(MQTTTopicRouter, MeshRankRequiresTokenAndDeviceButNotIata) {
  char topic[128];
  const char* missing_tokens[] = {nullptr, ""};
  for (const char* token : missing_tokens) {
    EXPECT_FALSE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHRANK, MQTT_PUBLICATION_PACKETS,
                                           nullptr, nullptr, DEVICE, token, topic, sizeof(topic)));
    EXPECT_STREQ("", topic);
  }
  EXPECT_FALSE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHRANK, MQTT_PUBLICATION_PACKETS,
                                         nullptr, nullptr, nullptr, TOKEN, topic, sizeof(topic)));
  EXPECT_TRUE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHRANK, MQTT_PUBLICATION_PACKETS,
                                        nullptr, nullptr, DEVICE, TOKEN, topic, sizeof(topic)));
}

TEST(MQTTTopicRouter, CustomTemplateExpandsEveryType) {
  for (const TypeCase& type : kTypes) {
    char topic[128];
    ASSERT_TRUE(mqttBuildPublicationTopic(
        MQTT_ROUTE_CUSTOM, type.type, "custom/{iata}/{token}/{device}/{type}",
        IATA, DEVICE, TOKEN, topic, sizeof(topic)));
    EXPECT_EQ(std::string("custom/DEN/account-token/0123456789ABCDEF/") + type.name, topic);
  }
}

TEST(MQTTTopicRouter, CustomLiteralDoesNotRequireIataTokenOrDevice) {
  char topic[32];
  ASSERT_TRUE(mqttBuildPublicationTopic(MQTT_ROUTE_CUSTOM, MQTT_PUBLICATION_RAW,
                                        "private/raw", nullptr, nullptr, nullptr,
                                        topic, sizeof(topic)));
  EXPECT_STREQ("private/raw", topic);
}

TEST(MQTTTopicRouter, EmptyCustomTemplateFailsRatherThanFallingBackImplicitly) {
  char topic[64];
  EXPECT_FALSE(mqttBuildPublicationTopic(MQTT_ROUTE_CUSTOM, MQTT_PUBLICATION_STATUS,
                                         "", IATA, DEVICE, TOKEN, topic, sizeof(topic)));
  EXPECT_STREQ("", topic);

  // MQTTBridge selects the MeshCore style explicitly for a custom slot whose
  // template is empty; make that default contract visible here.
  EXPECT_TRUE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHCORE, MQTT_PUBLICATION_STATUS,
                                        nullptr, IATA, DEVICE, TOKEN, topic, sizeof(topic)));
  EXPECT_STREQ("meshcore/DEN/0123456789ABCDEF/status", topic);
}

TEST(MQTTTopicRouter, FormattedTopicsRequireRoomForTerminator) {
  const char* expected = "meshcore/DEN/0123456789ABCDEF/status";
  const size_t exact_size = strlen(expected) + 1;
  char exact[64];
  ASSERT_LE(exact_size, sizeof(exact));
  EXPECT_TRUE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHCORE, MQTT_PUBLICATION_STATUS,
                                        nullptr, IATA, DEVICE, TOKEN, exact, exact_size));
  EXPECT_STREQ(expected, exact);

  char short_buf[64];
  memset(short_buf, 0x7f, sizeof(short_buf));
  EXPECT_FALSE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHCORE, MQTT_PUBLICATION_STATUS,
                                         nullptr, IATA, DEVICE, TOKEN,
                                         short_buf, exact_size - 1));
  EXPECT_EQ('\0', short_buf[exact_size - 2]);
}

TEST(MQTTTopicRouter, CustomTopicHonorsExactBoundary) {
  const char* expected = "custom/DEN/raw";
  char exact[15];
  static_assert(sizeof(exact) == 15, "fixture includes the terminator");
  EXPECT_TRUE(mqttBuildPublicationTopic(MQTT_ROUTE_CUSTOM, MQTT_PUBLICATION_RAW,
                                        "custom/{iata}/{type}", IATA, DEVICE, TOKEN,
                                        exact, sizeof(exact)));
  EXPECT_STREQ(expected, exact);

  char short_buf[14];
  EXPECT_FALSE(mqttBuildPublicationTopic(MQTT_ROUTE_CUSTOM, MQTT_PUBLICATION_RAW,
                                         "custom/{iata}/{type}", IATA, DEVICE, TOKEN,
                                         short_buf, sizeof(short_buf)));
  EXPECT_LT(strlen(short_buf), sizeof(short_buf));
}

TEST(MQTTTopicRouter, RejectsInvalidStyleTypeSlotAndOutput) {
  char topic[64] = "dirty";
  EXPECT_FALSE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHCORE, 99, nullptr,
                                         IATA, DEVICE, TOKEN, topic, sizeof(topic)));
  EXPECT_STREQ("", topic);
  EXPECT_FALSE(mqttBuildPublicationTopic(static_cast<MQTTTopicRouteStyle>(99),
                                         MQTT_PUBLICATION_STATUS, nullptr,
                                         IATA, DEVICE, TOKEN, topic, sizeof(topic)));
  EXPECT_FALSE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHCORE, MQTT_PUBLICATION_STATUS,
                                         nullptr, IATA, DEVICE, TOKEN, nullptr, 64));
  EXPECT_FALSE(mqttBuildPublicationTopic(MQTT_ROUTE_MESHCORE, MQTT_PUBLICATION_STATUS,
                                         nullptr, IATA, DEVICE, TOKEN, topic, 0));

  EXPECT_FALSE(mqttTopicSlotIndexValid(-1, RUNTIME_MQTT_SLOTS));
  EXPECT_TRUE(mqttTopicSlotIndexValid(0, RUNTIME_MQTT_SLOTS));
  EXPECT_TRUE(mqttTopicSlotIndexValid(RUNTIME_MQTT_SLOTS - 1, RUNTIME_MQTT_SLOTS));
  EXPECT_FALSE(mqttTopicSlotIndexValid(RUNTIME_MQTT_SLOTS, RUNTIME_MQTT_SLOTS));
  EXPECT_FALSE(mqttTopicSlotIndexValid(0, 0));
}

TEST(MQTTTopicRouter, PublicationTypeEnumValuesAreFrozen) {
  // The bridge passes MQTTBridge::MQTTMessageType to mqttBuildPublicationTopic
  // as an int; a compile-time static_assert in the bridge ties the two enums
  // together. Freeze the router side here so its values can't drift on their own.
  EXPECT_EQ(0, MQTT_PUBLICATION_STATUS);
  EXPECT_EQ(1, MQTT_PUBLICATION_PACKETS);
  EXPECT_EQ(2, MQTT_PUBLICATION_RAW);
  EXPECT_EQ(3, MQTT_PUBLICATION_NEIGHBORS);
  EXPECT_STREQ("status", mqttPublicationTypeName(MQTT_PUBLICATION_STATUS));
  EXPECT_STREQ("packets", mqttPublicationTypeName(MQTT_PUBLICATION_PACKETS));
  EXPECT_STREQ("raw", mqttPublicationTypeName(MQTT_PUBLICATION_RAW));
  EXPECT_STREQ("neighbors", mqttPublicationTypeName(MQTT_PUBLICATION_NEIGHBORS));
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
