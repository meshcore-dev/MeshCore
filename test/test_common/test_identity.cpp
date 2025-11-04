#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <Stream.h>
#include <string>

#include <ed_25519.h>
#include "mock_streams.h"
#include "Identity.h"
#include "Utils.h"

using namespace mesh;

TEST(IdentityTests, Identity)
{
  mesh::Identity id;
  const uint8_t pubhex[] =
    "87A47F423042DBEE25D1EA5CCC387FBA";

  mesh::Identity fromPubkey(&pubhex[0]);

  ConstantValueStream cs(&pubhex[0], 64);

  ASSERT_TRUE(id.readFrom(cs));

  uint8_t buffer[80];
  memset(buffer, 0, sizeof(buffer));
  MockStream bs(&buffer[0]);
  ASSERT_TRUE(id.writeTo(bs));
  ASSERT_STREQ((const char *)bs.buffer, (const char *)pubhex);
}

#define ZERO_PUB_KEY \
  "\x3B\x6A\x27\xBC\xCE\xB6\xA4\x2D\x62\xA3\xA8\xD0\x2A\x6F\x0D" \
  "\x73\x65\x32\x15\x77\x1D\xE2\x43\xA6\x3A\xC0\x48\xA1\x8B\x59"
#define ZERO_PRV_KEY \
  "\x50\x46\xAD\xC1\xDB\xA8\x38\x86\x7B\x2B\xBB\xFD\xD0\xC3\x42" \
  "\x3E\x58\xB5\x79\x70\xB5\x26\x7A\x90\xF5\x79\x60\x92\x4A\x87" \
  "\xF1\x56\x0A\x6A\x85\xEA\xA6\x42\xDA\xC8\x35\x42\x4B\x5D\x7C" \
  "\x8D\x63\x7C\x00\x40\x8C\x7A\x73\xDA\x67\x2B\x7F\x49\x85\x21" \
  "\x42\x0B\x6D\xD3"

TEST(IdentityTests, LocalIdentity)
{
  // create a zero identity
  uint8_t pub_key[PUB_KEY_SIZE], prv_key[PRV_KEY_SIZE], seed[SEED_SIZE];
  memset(seed, 0, SEED_SIZE);
  ed25519_create_keypair(pub_key, prv_key, seed);

  // create a Stream containing that identity
  uint8_t stored_key[PUB_KEY_SIZE+PRV_KEY_SIZE+SEED_SIZE];
  memcpy(stored_key, pub_key, PUB_KEY_SIZE);
  memcpy(stored_key+PUB_KEY_SIZE, prv_key, PRV_KEY_SIZE);
  // we're not saving seeds yet
  memset(stored_key+PUB_KEY_SIZE+PRV_KEY_SIZE, 0, SEED_SIZE);
  ConstantValueStream skf(stored_key, sizeof(stored_key));

  mesh::LocalIdentity id;
  ASSERT_TRUE(id.readFrom(skf));
  ASSERT_EQ(skf.pos, PUB_KEY_SIZE + PRV_KEY_SIZE);

  uint8_t buffer[1024];
  MockStream dump(&buffer[0]);

  ASSERT_TRUE(id.writeTo(dump));
  // Correct serialization is pubkey || prvkey (for now)
  ASSERT_TRUE(memcmp(buffer, ZERO_PUB_KEY, PUB_KEY_SIZE));
  ASSERT_TRUE(memcmp(buffer+PUB_KEY_SIZE, ZERO_PRV_KEY, PRV_KEY_SIZE));
  // ... and for the moment, nothing else
  ASSERT_EQ(dump.pos, PUB_KEY_SIZE + PRV_KEY_SIZE);

}
