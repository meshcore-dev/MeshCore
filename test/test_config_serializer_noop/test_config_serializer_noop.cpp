// Runs only in [env:native_noop_config] (CONFIG_SERIALIZER_NOOP): boards that keep prefs in a binary record.
#include "helpers/ConfigSerializer.h"
#include "helpers/DynamicConfigSerializer.h"

#include <gtest/gtest.h>
#include <string.h>

namespace {
class Prefs : public ConfigSerializer {
public:
  char name[16];
  int32_t i32 = 1;
  int16_t i16 = 2;
  int8_t i8 = 3;
  uint32_t u32 = 4;
  uint16_t u16 = 5;
  uint8_t u8 = 6;
  float f = 7.0f;
  double d = 8.0;
  bool b = true;
  uint8_t blob[4] = { 1, 2, 3, 4 };
  void touch() { markDirty(); }

protected:
  void structure() override {
    def("name", name, sizeof(name));
    def("blob", blob, sizeof(blob));
    def("i32", i32);
    def("i16", i16);
    def("i8", i8);
    def("u32", u32);
    def("u16", u16);
    def("u8", u8);
    def("f", f);
    def("d", d);
    def("b", b);
  }
};

class OneKey : public KeyValueStore {
public:
  char v[8] = "old";
  bool setByKey(const char *key, const char *value) override {
    if (strcmp(key, "known") != 0) return false;
    strncpy(v, value, sizeof(v) - 1);
    return true;
  }
  bool getByKey(const char *key, char *value, size_t max_len) override {
    if (strcmp(key, "known") != 0) return false;
    strncpy(value, v, max_len);
    return true;
  }
};
} // namespace

TEST(ConfigSerializerNoop, DirtyFlagStillWorks) {
  Prefs p;
  EXPECT_FALSE(p.isDirty());
  p.touch();
  EXPECT_TRUE(p.isDirty());
  p.clearDirty();
  EXPECT_FALSE(p.isDirty());
}

TEST(DynamicConfigSerializerNoop, OnlyTheFallbackStoresValues) {
  OneKey fb;
  DynamicConfigSerializer d(&fb);
  char out[8];
  EXPECT_TRUE(d.setByKey("known", "new"));
  EXPECT_STREQ(fb.v, "new");
  EXPECT_TRUE(d.getByKey("known", out, sizeof(out) - 1));
  EXPECT_STREQ(out, "new");
  EXPECT_FALSE(d.setByKey("other", "x")); // no local text store in this build
  EXPECT_FALSE(d.getByKey("other", out, sizeof(out) - 1));
}

TEST(DynamicConfigSerializerNoop, HasNoTextBuffer) {
  EXPECT_LE(sizeof(DynamicConfigSerializer), 4 * sizeof(void *));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
