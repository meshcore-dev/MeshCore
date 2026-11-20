#include <gtest/gtest.h>
#include <cstring>
#include <ed_25519.h>

#include "Dispatcher.cpp"
#include "Mesh.cpp"
#include "helpers/AdvertDataHelpers.cpp"
#include "helpers/BaseChatMesh.cpp"

void StrHelper::strncpy(char* dest, const char* src, size_t buf_sz) {
  if (buf_sz == 0) return;
  std::strncpy(dest, src, buf_sz - 1);
  dest[buf_sz - 1] = 0;
}

namespace mesh {

Identity::Identity() {
  memset(pub_key, 0, sizeof(pub_key));
}

bool Identity::verify(const uint8_t*, const uint8_t*, int) const {
  return true;
}

LocalIdentity::LocalIdentity() : Identity() {
  memset(prv_key, 0, sizeof(prv_key));
}

void LocalIdentity::sign(uint8_t* sig, const uint8_t*, int) const {
  memset(sig, 0, SIGNATURE_SIZE);
}

void LocalIdentity::calcSharedSecret(uint8_t* secret, const uint8_t* other_pub_key) const {
  ed25519_key_exchange(secret, other_pub_key, prv_key);
}

}  // namespace mesh

namespace {

class DummyRadio : public mesh::Radio {
public:
  int recvRaw(uint8_t*, int) override { return 0; }
  uint32_t getEstAirtimeFor(int) override { return 10; }
  float packetScore(float, int) override { return 1.0f; }
  bool startSendRaw(const uint8_t*, int) override { return true; }
  bool isSendComplete() override { return true; }
  void onSendFinished() override {}
  bool isInRecvMode() const override { return true; }
};

class DummyMillisecondClock : public mesh::MillisecondClock {
public:
  unsigned long getMillis() override { return 1000; }
};

class DummyRNG : public mesh::RNG {
public:
  void random(uint8_t* dest, size_t len) override { memset(dest, 0x42, len); }
};

class DummyRTCClock : public mesh::RTCClock {
  uint32_t _now = 1700000000;
public:
  uint32_t getCurrentTime() override { return _now; }
  void setCurrentTime(uint32_t time) override { _now = time; }
};

class DummyPacketManager : public mesh::PacketManager {
public:
  mesh::Packet* allocNew() override { return nullptr; }
  void free(mesh::Packet*) override {}
  void queueOutbound(mesh::Packet*, uint8_t, uint32_t) override {}
  mesh::Packet* getNextOutbound(uint32_t) override { return nullptr; }
  int getOutboundCount(uint32_t) const override { return 0; }
  int getOutboundTotal() const override { return 0; }
  int getFreeCount() const override { return 16; }
  mesh::Packet* getOutboundByIdx(int) override { return nullptr; }
  mesh::Packet* removeOutboundByIdx(int) override { return nullptr; }
  void queueInbound(mesh::Packet*, uint32_t) override {}
  mesh::Packet* getNextInbound(uint32_t) override { return nullptr; }
};

class DummyMeshTables : public mesh::MeshTables {
public:
  bool wasSeen(const mesh::Packet*) override { return false; }
  void markSeen(const mesh::Packet*) override {}
  void clear(const mesh::Packet*) override {}
};

class TestChatMesh : public BaseChatMesh {
public:
  int messages_received = 0;

  TestChatMesh(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng,
               mesh::RTCClock& rtc, mesh::PacketManager& mgr, mesh::MeshTables& tables)
      : BaseChatMesh(radio, ms, rng, rtc, mgr, tables) {}

  void onDiscoveredContact(ContactInfo&, bool, uint8_t, const uint8_t*) override {}
  ContactInfo* processAck(const uint8_t*) override { return nullptr; }
  void onContactPathUpdated(const ContactInfo&) override {}
  void onMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {
    messages_received++;
  }
  void onCommandDataRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}
  void onCLICommandRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*, char*) override {}
  void onSignedMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t, const uint8_t*, const char*) override {}
  void onChannelMessageRecv(const mesh::GroupChannel&, mesh::Packet*, uint32_t, const char*) override {}
  uint8_t onContactRequest(const ContactInfo&, uint32_t, const uint8_t*, uint8_t, uint8_t*) override { return 0; }
  void onContactResponse(const ContactInfo&, const uint8_t*, uint8_t) override {}
  uint32_t calcFloodTimeoutMillisFor(uint32_t) const override { return 1000; }
  uint32_t calcDirectTimeoutMillisFor(uint32_t, uint8_t) const override { return 1000; }
  void onSendTimeout() override {}

  using BaseChatMesh::resetContacts;
  using BaseChatMesh::searchPeersByHash;
  using BaseChatMesh::getPeerSharedSecret;
  using mesh::Mesh::onRecvPacket;
};

struct MeshFixture {
  DummyRadio radio;
  DummyMillisecondClock ms;
  DummyRNG rng;
  DummyRTCClock rtc;
  DummyPacketManager mgr;
  DummyMeshTables tables;
  TestChatMesh chat{radio, ms, rng, rtc, mgr, tables};
};

}  // namespace

TEST(EmptyContactSlots, ZeroPubKeyProducesAllZeroSharedSecretForAnyPrivateKey) {
  ContactInfo empty_slot;
  memset(&empty_slot, 0, sizeof(empty_slot));
  EXPECT_TRUE(empty_slot.isEmptySlot());

  uint8_t raw_prv[PRV_KEY_SIZE];
  for (int i = 0; i < PRV_KEY_SIZE; i++) {
    raw_prv[i] = static_cast<uint8_t>(0xA5 ^ (i * 13 + 7));
  }
  uint8_t secret[PUB_KEY_SIZE];
  ed25519_key_exchange(secret, empty_slot.id.pub_key, raw_prv);

  uint8_t expected_zeroes[PUB_KEY_SIZE] = { 0 };
  EXPECT_EQ(0, memcmp(secret, expected_zeroes, PUB_KEY_SIZE));
}

TEST(EmptyContactSlots, NullKeyPeerPacketIsRejectedWhenAnonSlotsAreEmpty) {
  MeshFixture fx;
  fx.chat.self_id.pub_key[0] = 0x42;

  // Craft a PAYLOAD_TYPE_TXT_MSG addressed to self_id (dest_hash=0x42) with src_hash=0x00,
  // encrypted and MAC'd under the all-zero 32-byte key that an empty ContactInfo slot derives.
  uint8_t zero_secret[PUB_KEY_SIZE] = { 0 };
  uint8_t plaintext[16] = { 0 };
  uint32_t ts = 1700000001;
  memcpy(plaintext, &ts, 4);
  plaintext[4] = (TXT_TYPE_PLAIN << 2);
  strcpy(reinterpret_cast<char*>(&plaintext[5]), "forged");

  mesh::Packet pkt;
  pkt.header = ROUTE_TYPE_FLOOD | (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
  pkt.path_len = 0;
  pkt.payload[0] = 0x42;  // dest_hash (matches fx.chat.self_id)
  pkt.payload[1] = 0x00;  // src_hash (matches all-zero pub_key if empty slots are not skipped)
  int enc_len = mesh::Utils::encryptThenMAC(zero_secret, &pkt.payload[2], plaintext, 12);
  pkt.payload_len = static_cast<uint8_t>(2 + enc_len);

  fx.chat.onRecvPacket(&pkt);
  EXPECT_EQ(0, fx.chat.messages_received);
}

TEST(EmptyContactSlots, SearchPeersByHashSkipsEmptyAnonSlotsAndFindsRealZeroPrefixPeer) {
  MeshFixture fx;
  const uint8_t zero_hash = 0x00;

  // Freshly reset contacts has MAX_ANON_CONTACTS (8) zeroed transient slots.
  EXPECT_EQ(0, fx.chat.searchPeersByHash(&zero_hash));

  // Add a legitimate permanent contact whose public key starts with 0x00.
  ContactInfo peer;
  memset(&peer, 0, sizeof(peer));
  peer.type = ADV_TYPE_CHAT;
  peer.lastmod = 1700000100;
  strcpy(peer.name, "Alice");
  peer.id.pub_key[0] = 0x00;
  for (int i = 1; i < PUB_KEY_SIZE; i++) {
    peer.id.pub_key[i] = static_cast<uint8_t>(i);
  }
  ASSERT_TRUE(fx.chat.addContact(peer));

  // Only the populated contact at index MAX_ANON_CONTACTS should match.
  EXPECT_EQ(1, fx.chat.searchPeersByHash(&zero_hash));

  // Lookup by pubkey and name prefix must also skip the empty transient slots.
  uint8_t zero_prefix[6] = { 0 };
  EXPECT_EQ(nullptr, fx.chat.lookupContactByPubKey(zero_prefix, sizeof(zero_prefix)));

  ContactInfo* found = fx.chat.lookupContactByPubKey(peer.id.pub_key, 6);
  ASSERT_NE(nullptr, found);
  EXPECT_STREQ("Alice", found->name);

  ContactInfo* by_name = fx.chat.searchContactsByPrefix("");
  ASSERT_NE(nullptr, by_name);
  EXPECT_STREQ("Alice", by_name->name);
}

TEST(EmptyContactSlots, PopulatedTransientAnonSlotStillMatchesEvenWhenLastmodIsZero) {
  MeshFixture fx;
  const uint8_t zero_hash = 0x00;

  ContactInfo anon;
  memset(&anon, 0, sizeof(anon));
  anon.type = ADV_TYPE_NONE;
  anon.lastmod = 0;  // e.g. RTC not yet initialised on nRF52
  anon.id.pub_key[0] = 0x00;
  anon.id.pub_key[1] = 0x7F;
  ASSERT_TRUE(fx.chat.addContact(anon));

  EXPECT_FALSE(anon.isEmptySlot());
  EXPECT_EQ(1, fx.chat.searchPeersByHash(&zero_hash));
  EXPECT_NE(nullptr, fx.chat.lookupContactByPubKey(anon.id.pub_key, PUB_KEY_SIZE));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
