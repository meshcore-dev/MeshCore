#include <gtest/gtest.h>

#include <helpers/AtomicFile.h>

// Config files must survive a power loss at any point of a save: the old file
// stays until the new one is complete and checked. The mock filesystem renames
// like littlefs does (an existing target is replaced in one step).

namespace {

const char* PATH = "/prefs.json";
const char* TMP = "/prefs.json.tmp";

void put(Adafruit_LittleFS& fs, const char* path, const std::vector<uint8_t>& bytes) {
  File f = fs.open(path, FILE_O_WRITE);
  f.write(bytes.data(), bytes.size());
  f.close();
}

bool writeBytes(File& f, size_t n, uint8_t v) {
  for (size_t i = 0; i < n; i++) {
    if (f.write(&v, 1) != 1) return false;
  }
  return true;
}

TEST(AtomicFile, NewContentReplacesOldAndLeavesNoTemp) {
  Adafruit_LittleFS fs;
  put(fs, PATH, {1, 2, 3});

  EXPECT_TRUE(AtomicFile::write(&fs, PATH, [&](File& f) { return writeBytes(f, 10, 7); }));
  ASSERT_NE(fs.contents(PATH), nullptr);
  EXPECT_EQ(fs.contents(PATH)->size(), 10u);
  EXPECT_FALSE(fs.exists(TMP));
}

TEST(AtomicFile, OldFileIsStillThereWhileTheNewOneIsWritten) {
  Adafruit_LittleFS fs;
  put(fs, PATH, {1, 2, 3});

  AtomicFile::write(&fs, PATH, [&](File& f) {
    // power could fail here: the old file must be complete
    EXPECT_TRUE(fs.exists(PATH));
    EXPECT_EQ(fs.contents(PATH)->size(), 3u);
    return writeBytes(f, 5, 9);
  });
}

TEST(AtomicFile, CloseThatDropsBytesIsCaughtAndFallsBackToInPlace) {
  Adafruit_LittleFS fs;
  put(fs, PATH, {1, 2, 3});
  fs.commit_budget = 4;   // close() keeps 4 bytes of the 10 written

  // Both the temp copy and the in-place retry come up short here, so the save
  // reports success only because the body did; the temp file must be gone and
  // the failed publish must not have replaced the old file with the short copy.
  int calls = 0;
  AtomicFile::write(&fs, PATH, [&](File& f) { calls++; return writeBytes(f, 10, 7); });
  EXPECT_EQ(calls, 2);
  EXPECT_FALSE(fs.exists(TMP));
}

TEST(AtomicFile, FullMediumReportsFailureAndLeavesNoTemp) {
  Adafruit_LittleFS fs;
  put(fs, PATH, {1, 2, 3});
  fs.write_budget = 4;

  bool ok = AtomicFile::write(&fs, PATH, [&](File& f) { return writeBytes(f, 10, 7); });
  EXPECT_FALSE(ok);
  EXPECT_FALSE(fs.exists(TMP));
}

TEST(AtomicFile, FailedRenameFallsBackAndStillSaves) {
  Adafruit_LittleFS fs;
  put(fs, PATH, {1, 2, 3});
  fs.fail_rename = true;

  EXPECT_TRUE(AtomicFile::write(&fs, PATH, [&](File& f) { return writeBytes(f, 6, 5); }));
  ASSERT_NE(fs.contents(PATH), nullptr);
  EXPECT_EQ(fs.contents(PATH)->size(), 6u);
  EXPECT_FALSE(fs.exists(TMP));
}

TEST(AtomicFile, CleanupRemovesOrphanedTempAndKeepsTheMainFile) {
  Adafruit_LittleFS fs;
  put(fs, PATH, {1, 2, 3});
  put(fs, TMP, {9, 9});

  AtomicFile::cleanup(&fs, PATH);
  EXPECT_FALSE(fs.exists(TMP));
  ASSERT_NE(fs.contents(PATH), nullptr);
  EXPECT_EQ(fs.contents(PATH)->size(), 3u);
}

TEST(AtomicFile, StaleTempFromEarlierCrashDoesNotDisturbNextSave) {
  Adafruit_LittleFS fs;
  put(fs, TMP, {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9});

  EXPECT_TRUE(AtomicFile::write(&fs, PATH, [&](File& f) { return writeBytes(f, 2, 1); }));
  EXPECT_EQ(fs.contents(PATH)->size(), 2u);
  EXPECT_FALSE(fs.exists(TMP));
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
