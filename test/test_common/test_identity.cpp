#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <Stream.h>
#include <string>

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
