#include <gtest/gtest.h>

#include <cstdlib>
#include <vector>

#include "helpers/MQTTRuntimeBufferLifecycle.h"

namespace RuntimeBuffers = MQTTRuntimeBufferLifecycle;

TEST(MQTTRuntimeBufferLifecycle, PartialAllocationKeepsOtherBuffersAndRetriesOnlyMissing) {
  std::vector<size_t> allocation_sizes;
  int allocation_attempt = 0;
  const auto allocate = [&allocation_sizes, &allocation_attempt](size_t size) -> void* {
    allocation_sizes.push_back(size);
    allocation_attempt++;
    if (allocation_attempt == 2) {
      return nullptr;
    }
    return std::malloc(size);
  };

  void* raw = nullptr;
  void* publish = nullptr;
  void* status = nullptr;

  raw = RuntimeBuffers::allocateIfMissing(raw, 256, allocate);
  publish = RuntimeBuffers::allocateIfMissing(publish, 2048, allocate);
  status = RuntimeBuffers::allocateIfMissing(status, 768, allocate);

  ASSERT_NE(nullptr, raw);
  EXPECT_EQ(nullptr, publish);
  ASSERT_NE(nullptr, status);
  ASSERT_EQ(3U, allocation_sizes.size());
  void* const initial_raw = raw;
  void* const initial_status = status;

  raw = RuntimeBuffers::allocateIfMissing(raw, 256, allocate);
  publish = RuntimeBuffers::allocateIfMissing(publish, 2048, allocate);
  status = RuntimeBuffers::allocateIfMissing(status, 768, allocate);

  EXPECT_EQ(4U, allocation_sizes.size());
  EXPECT_EQ(2048U, allocation_sizes.back());
  EXPECT_EQ(initial_raw, raw);
  EXPECT_EQ(initial_status, status);
  EXPECT_NE(nullptr, publish);

  int releases = 0;
  const auto release = [&releases](void* allocation) {
    releases++;
    std::free(allocation);
  };
  raw = RuntimeBuffers::release(raw, release);
  publish = RuntimeBuffers::release(publish, release);
  status = RuntimeBuffers::release(status, release);

  EXPECT_EQ(nullptr, raw);
  EXPECT_EQ(nullptr, publish);
  EXPECT_EQ(nullptr, status);
  EXPECT_EQ(3, releases);

  raw = RuntimeBuffers::release(raw, release);
  publish = RuntimeBuffers::release(publish, release);
  status = RuntimeBuffers::release(status, release);
  EXPECT_EQ(3, releases);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
