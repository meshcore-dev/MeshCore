#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <Stream.h>
#include <string>

#include <ed_25519.h>
#include "mock_streams.h"
#include "Identity.h"

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
  MockStream bs(&buffer[0]);
  ASSERT_TRUE(id.writeTo(bs));
  ASSERT_STREQ((const char *)bs.buffer, (const char *)pubhex);
}

TEST(IdentityTests, LocalIdentity)
{
  uint8_t pub_key[PUB_KEY_SIZE], prv_key[PRV_KEY_SIZE], seed[SEED_SIZE];
  memset(seed, 0, SEED_SIZE);
  ed25519_create_keypair(pub_key, prv_key, seed);

  uint8_t stored_key[PUB_KEY_SIZE+PRV_KEY_SIZE+SEED_SIZE];
  memcpy(stored_key, pub_key, PUB_KEY_SIZE);
  memcpy(stored_key+PUB_KEY_SIZE, prv_key, PRV_KEY_SIZE);
  // we're not saving seeds yet
  memset(stored_key+PUB_KEY_SIZE+PRV_KEY_SIZE, 0, SEED_SIZE);

  ConstantValueStream skf(stored_key, sizeof(stored_key));
  mesh::LocalIdentity id;
  ASSERT_TRUE(id.readFrom(skf));

}
