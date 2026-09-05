// Host tests for the MQTT observer preset table and lookup helpers
// (src/helpers/MQTTPresets.h). Pure logic — no ESP/radio dependencies.
//
// The preset table and lookup functions are compiled only for observer builds,
// so opt into that feature flag before including the header (the definitions are
// pure C++/data with no ESP dependencies).
#define WITH_MQTT_BRIDGE 1
#define PROGMEM  // host build: the CA-cert strings in MQTTPresets.h are PROGMEM-qualified
#include <gtest/gtest.h>
#include <cstring>
#include <set>
#include <string>
#include "helpers/MQTTPresets.h"

// ---- findMQTTPreset -------------------------------------------------------

TEST(MQTTPresets, FindKnownPreset) {
  const MQTTPresetDef* p = findMQTTPreset("analyzer-us");
  ASSERT_NE(nullptr, p);
  EXPECT_STREQ("analyzer-us", p->name);
  EXPECT_EQ(MQTT_AUTH_JWT, p->auth_type);
  EXPECT_EQ(MQTT_TOPIC_MESHCORE, p->topic_style);
}

TEST(MQTTPresets, FindReturnsTablePointer) {
  // The returned pointer must be into the table, not a copy.
  const MQTTPresetDef* p = findMQTTPreset("meshrank");
  ASSERT_NE(nullptr, p);
  bool in_table = false;
  for (int i = 0; i < MQTT_PRESET_COUNT; i++) {
    if (p == &MQTT_PRESETS[i]) { in_table = true; break; }
  }
  EXPECT_TRUE(in_table);
}

TEST(MQTTPresets, UnknownAndEmptyReturnNull) {
  EXPECT_EQ(nullptr, findMQTTPreset("does-not-exist"));
  EXPECT_EQ(nullptr, findMQTTPreset(""));
  EXPECT_EQ(nullptr, findMQTTPreset(nullptr));
}

TEST(MQTTPresets, NoneAndCustomAreNotTablePresets) {
  // "none"/"custom" are virtual presets handled by the CLI, not table entries.
  EXPECT_EQ(nullptr, findMQTTPreset(MQTT_PRESET_NONE));
  EXPECT_EQ(nullptr, findMQTTPreset(MQTT_PRESET_CUSTOM));
  EXPECT_STREQ("none", MQTT_PRESET_NONE);
  EXPECT_STREQ("custom", MQTT_PRESET_CUSTOM);
}

TEST(MQTTPresets, LookupIsCaseSensitive) {
  EXPECT_EQ(nullptr, findMQTTPreset("Analyzer-US"));
}

// ---- table integrity ------------------------------------------------------

TEST(MQTTPresets, EveryNameIsUniqueAndNonEmpty) {
  std::set<std::string> names;
  for (int i = 0; i < MQTT_PRESET_COUNT; i++) {
    ASSERT_NE(nullptr, MQTT_PRESETS[i].name) << "preset " << i << " has null name";
    EXPECT_NE('\0', MQTT_PRESETS[i].name[0]) << "preset " << i << " has empty name";
    auto res = names.insert(MQTT_PRESETS[i].name);
    EXPECT_TRUE(res.second) << "duplicate preset name: " << MQTT_PRESETS[i].name;
  }
  EXPECT_EQ((size_t)MQTT_PRESET_COUNT, names.size());
}

TEST(MQTTPresets, EveryPresetHasAServerUrl) {
  for (int i = 0; i < MQTT_PRESET_COUNT; i++) {
    ASSERT_NE(nullptr, MQTT_PRESETS[i].server_url) << MQTT_PRESETS[i].name;
    EXPECT_NE('\0', MQTT_PRESETS[i].server_url[0]) << MQTT_PRESETS[i].name;
  }
}

TEST(MQTTPresets, JwtPresetsCarryAnAudience) {
  // JWT auth needs an audience (the field doubles as the broker host here).
  for (int i = 0; i < MQTT_PRESET_COUNT; i++) {
    if (MQTT_PRESETS[i].auth_type == MQTT_AUTH_JWT) {
      EXPECT_NE(nullptr, MQTT_PRESETS[i].jwt_audience)
          << MQTT_PRESETS[i].name << " is JWT but has no audience";
    }
  }
}

TEST(MQTTPresets, NamesFitTheSlotPresetBuffer) {
  // Stored preset name goes into mqtt_slot_preset[MAX][24]; keep < 24 chars.
  for (int i = 0; i < MQTT_PRESET_COUNT; i++) {
    EXPECT_LT(strlen(MQTT_PRESETS[i].name), (size_t)24)
        << MQTT_PRESETS[i].name << " too long for slot-preset buffer";
  }
}

// ---- mqttPresetNeedsSlotCredentials ---------------------------------------

TEST(MQTTPresets, EmbeddedUserpassDoesNotNeedSlotCredentials) {
  // tennmesh ships an embedded username+password.
  const MQTTPresetDef* p = findMQTTPreset("tennmesh");
  ASSERT_NE(nullptr, p);
  EXPECT_EQ(MQTT_AUTH_USERPASS, p->auth_type);
  EXPECT_FALSE(mqttPresetNeedsSlotCredentials(p));
}

TEST(MQTTPresets, UserpassWithoutEmbeddedCredsNeedsSlotCredentials) {
  // inwmesh is USERPASS with null user/pass -> must come from mqttN.username/password.
  const MQTTPresetDef* p = findMQTTPreset("inwmesh");
  ASSERT_NE(nullptr, p);
  EXPECT_EQ(MQTT_AUTH_USERPASS, p->auth_type);
  EXPECT_TRUE(mqttPresetNeedsSlotCredentials(p));
  EXPECT_TRUE(mqttPresetNeedsSlotUsername(p));
  EXPECT_TRUE(mqttPresetNeedsSlotPassword(p));
  EXPECT_FALSE(mqttPresetUsesDevicePubkeyUsername(p));
}

TEST(MQTTPresets, NonUserpassNeverNeedsSlotCredentials) {
  for (int i = 0; i < MQTT_PRESET_COUNT; i++) {
    if (MQTT_PRESETS[i].auth_type != MQTT_AUTH_USERPASS) {
      EXPECT_FALSE(mqttPresetNeedsSlotCredentials(&MQTT_PRESETS[i]))
          << MQTT_PRESETS[i].name;
    }
  }
  EXPECT_FALSE(mqttPresetNeedsSlotCredentials(nullptr));
}

TEST(MQTTPresets, MeshrankIsTokenStyleNoAuth) {
  const MQTTPresetDef* p = findMQTTPreset("meshrank");
  ASSERT_NE(nullptr, p);
  EXPECT_EQ(MQTT_TOPIC_MESHRANK, p->topic_style);
  EXPECT_EQ(MQTT_AUTH_NONE, p->auth_type);
}

TEST(MQTTPresets, MeshChaun14UsesPubkeyUsernameAndNeedsPassword) {
  const MQTTPresetDef* p = findMQTTPreset("mesh-chaun14");
  ASSERT_NE(nullptr, p);
  EXPECT_EQ(MQTT_AUTH_USERPASS, p->auth_type);
  EXPECT_EQ(MQTT_TOPIC_MESHCORE, p->topic_style);
  EXPECT_STREQ("mqtt://mqtt.mesh.chaun14.fr:1884", p->server_url);
  EXPECT_EQ(nullptr, p->ca_cert);
  EXPECT_EQ(60, p->keepalive);
  EXPECT_TRUE(mqttPresetUsesDevicePubkeyUsername(p));
  EXPECT_STREQ(MQTT_USERPASS_USERNAME_PUBKEY, p->userpass_username);
  EXPECT_FALSE(mqttPresetNeedsSlotUsername(p));
  EXPECT_TRUE(mqttPresetNeedsSlotPassword(p));
  EXPECT_TRUE(mqttPresetNeedsSlotCredentials(p));
}

TEST(MQTTPresets, WcmeshIsJwtWithIsrgRootX1) {
  const MQTTPresetDef* p = findMQTTPreset("wcmesh");
  ASSERT_NE(nullptr, p);
  EXPECT_EQ(MQTT_AUTH_JWT, p->auth_type);
  EXPECT_EQ(MQTT_TOPIC_MESHCORE, p->topic_style);
  EXPECT_STREQ("wss://mqtt.wcmesh.com:443", p->server_url);
  EXPECT_STREQ("mqtt.wcmesh.com", p->jwt_audience);
  EXPECT_EQ(ISRG_ROOT_X1, p->ca_cert);
  EXPECT_NE(GTS_ROOT_R4, p->ca_cert);
  EXPECT_FALSE(mqttPresetNeedsSlotCredentials(p));
  EXPECT_FALSE(mqttPresetUsesDevicePubkeyUsername(p));
}

// ---- slot count constants -------------------------------------------------

TEST(MQTTPresets, SlotCountsAreSane) {
  EXPECT_GT(RUNTIME_MQTT_SLOTS, 0);
  EXPECT_LE(RUNTIME_MQTT_SLOTS, MAX_MQTT_SLOTS);
  EXPECT_EQ(6, MAX_MQTT_SLOTS);  // persisted layout — must not drift without migration
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
