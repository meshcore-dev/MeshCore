#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <Stream.h>
#include <string>

#include "Identity.h"

using namespace mesh;


class ConstantValueStream : public Stream {
public:
  const uint8_t *buffer_;
  size_t pos_, len_;

  ConstantValueStream(const uint8_t *b, size_t len)
  :buffer_(b),pos_(0),len_(len)
  {}

  int available() {
    return (int)(len_ - pos_);
  }
  MOCK_METHOD(size_t, write, (uint8_t c), (override));
  MOCK_METHOD(size_t, write, (const uint8_t *buffer, size_t size), (override));
  MOCK_METHOD(int, availableForWrite, (), (override));
  int read() {
    if (pos_ >= len_) {
      return 0;
    }
    return (int)buffer_[pos_++];
  }
  MOCK_METHOD(int, peek, (), (override));
};

TEST(IdentityTests, Identity)
{
  mesh::Identity id;
  const uint8_t pubhex[] =
    "87A47F423042DBEE25D1EA5CCC387FBAFE90FD435FA4A1237460E20C49D1EE74";

  mesh::Identity fromPubkey(&pubhex[0]);

  ConstantValueStream cs(&pubhex[0], 64);

  ASSERT_TRUE(id.readFrom(cs));
}
