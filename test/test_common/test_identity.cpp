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
    "87A47F423042DBEE25D1EA5CCC387FBAFE90FD435FA4A1237460E20C49D1EE74";

  mesh::Identity fromPubkey(&pubhex[0]);

  ConstantValueStream cs(&pubhex[0], 64);

  ASSERT_TRUE(id.readFrom(cs));
}
