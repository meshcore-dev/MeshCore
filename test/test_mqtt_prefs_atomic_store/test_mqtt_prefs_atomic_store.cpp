#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "helpers/MQTTPrefsAtomicStore.h"
#include "helpers/MQTTPrefsRecovery.h"

namespace AtomicStore = MQTTPrefsAtomicStore;
namespace Recovery = MQTTPrefsRecovery;

namespace {

enum class FailurePoint {
  None,
  Begin,
  HeaderWrite,
  PayloadWrite,
  ImageWrite,
  Finish,
  Commit,
};

class InMemoryStore {
public:
  explicit InMemoryStore(FailurePoint failure, bool preexisting_recovery_temp = false)
      : _failure(failure), _preexisting_recovery_temp(preexisting_recovery_temp) {
    _files["/mqtt_prefs"] = {'o', 'l', 'd', '-', 'p', 'r', 'e', 'f', 's'};
    if (_preexisting_recovery_temp) _files["/mqtt_prefs.tmp"] = {'r', 'e', 'c', 'o', 'v', 'e', 'r'};
  }

  bool begin() {
    ++begin_calls;
    if (_preexisting_recovery_temp) return false;
    _files.erase("/mqtt_prefs.tmp");
    _open = _failure != FailurePoint::Begin;
    _owns_temp = _open;
    return _open;
  }

  size_t write(const uint8_t* bytes, size_t size) {
    ++write_calls;
    if (!_open) return 0;
    const bool should_fail = (write_calls == 1 && _failure == FailurePoint::HeaderWrite) ||
        (write_calls == 2 && _failure == FailurePoint::PayloadWrite);
    const size_t written = should_fail && size > 0 ? size - 1 : size;
    _staging.insert(_staging.end(), bytes, bytes + written);
    return written;
  }

  bool finish() {
    ++finish_calls;
    _open = false;
    if (_failure == FailurePoint::Finish) return false;
    _files["/mqtt_prefs.tmp"] = _staging;
    _finished = true;
    return true;
  }

  bool commit() {
    ++commit_calls;
    if (_failure == FailurePoint::Commit) return false;
    _files["/mqtt_prefs"] = _files["/mqtt_prefs.tmp"];
    _files.erase("/mqtt_prefs.tmp");
    _finished = false;
    return true;
  }

  void abort() {
    ++abort_calls;
    _open = false;
    _staging.clear();
    // Mirrors MQTTPrefsFileStore: after finish(), a failed commit may already
    // have moved the old primary to .bak, so the verified temp is recovery
    // data rather than disposable staging.
    if (_owns_temp && !_finished) _files.erase("/mqtt_prefs.tmp");
    _finished = false;
    _owns_temp = false;
  }

  const std::vector<uint8_t>& source() const { return _files.at("/mqtt_prefs"); }
  bool tempExists() const { return _files.count("/mqtt_prefs.tmp") != 0; }

  int begin_calls = 0;
  int write_calls = 0;
  int finish_calls = 0;
  int commit_calls = 0;
  int abort_calls = 0;

private:
  FailurePoint _failure;
  bool _preexisting_recovery_temp = false;
  bool _open = false;
  bool _finished = false;
  bool _owns_temp = false;
  std::vector<uint8_t> _staging;
  std::map<std::string, std::vector<uint8_t>> _files;
};

AtomicStore::Result run(InMemoryStore* store) {
  const uint8_t header[] = {0xf5, 'M', 'Q', 'P', 1, 0, 0x09, 0x00};
  const uint8_t payload[] = {'n', 'e', 'w', '-', 'p', 'r', 'e', 'f', 's'};
  return AtomicStore::write(*store, header, sizeof(header), payload, sizeof(payload));
}

AtomicStore::Result runWithObserverTail(InMemoryStore* store) {
  const uint8_t header[] = {0xf5, 'M', 'Q', 'P', 1, 0, 0x0a, 0x00};
  // The final three bytes stand in for the observer tail transferred from a
  // legacy /com_prefs file. They must be committed before that source is compacted.
  const uint8_t payload[] = {'m', 'i', 'g', 'r', 'a', 't', 'e', 0x91, 0x7e, 0xa5};
  return AtomicStore::write(*store, header, sizeof(header), payload, sizeof(payload));
}

class LegacyComPrefs {
public:
  LegacyComPrefs() : bytes({'l', 'e', 'g', 'a', 'c', 'y', '-', 'c', 'o', 'm'}) {}

  void compactAfterMqttCommit(const std::vector<uint8_t>& mqtt_bytes) {
    const std::vector<uint8_t> observer_tail = {0x91, 0x7e, 0xa5};
    mqtt_tail_present_before_compaction = mqtt_bytes.size() >= observer_tail.size() &&
        std::equal(observer_tail.rbegin(), observer_tail.rend(), mqtt_bytes.rbegin());
    bytes = {'c', 'o', 'm', 'p', 'a', 'c', 't'};
    ++compact_calls;
  }

  std::vector<uint8_t> bytes;
  bool mqtt_tail_present_before_compaction = false;
  int compact_calls = 0;
};

class LegacyNodePrefs {
public:
  LegacyNodePrefs() : bytes({'l', 'e', 'g', 'a', 'c', 'y', '-', 'n', 'o', 'd', 'e'}) {}

  void migrateAfterMqttCommit(const std::vector<uint8_t>& mqtt_bytes) {
    const std::vector<uint8_t> observer_tail = {0x91, 0x7e, 0xa5};
    mqtt_tail_present_before_removal = mqtt_bytes.size() >= observer_tail.size() &&
        std::equal(observer_tail.rbegin(), observer_tail.rend(), mqtt_bytes.rbegin());
    bytes.clear();  // model removal after current-layout /com_prefs is written
    ++migration_calls;
  }

  std::vector<uint8_t> bytes;
  bool mqtt_tail_present_before_removal = false;
  int migration_calls = 0;
};

// Models the final old-name migration separately from the MQTT transaction:
// /com_prefs is absent while /node_prefs is authoritative. A failed temp write
// or rename must leave that source as the only usable preference image.
class InMemoryCommonPrefsStore {
public:
  explicit InMemoryCommonPrefsStore(FailurePoint failure) : _failure(failure) {
    _files["/node_prefs"] = {'l', 'e', 'g', 'a', 'c', 'y', '-', 'n', 'o', 'd', 'e'};
  }

  bool begin() {
    ++begin_calls;
    _files.erase("/com_prefs.tmp");
    _staging.clear();
    _open = _failure != FailurePoint::Begin;
    return _open;
  }

  size_t write(const uint8_t* bytes, size_t size) {
    ++write_calls;
    if (!_open) return 0;
    const bool should_fail = _failure == FailurePoint::ImageWrite && write_calls == 2;
    const size_t written = should_fail && size > 0 ? size - 1 : size;
    _staging.insert(_staging.end(), bytes, bytes + written);
    return written;
  }

  bool finish() {
    ++finish_calls;
    _open = false;
    if (_failure == FailurePoint::Finish) return false;
    _files["/com_prefs.tmp"] = _staging;
    return true;
  }

  bool commit() {
    ++commit_calls;
    if (_failure == FailurePoint::Commit) return false;
    _files["/com_prefs"] = _files["/com_prefs.tmp"];
    _files.erase("/com_prefs.tmp");
    return true;
  }

  void abort() {
    ++abort_calls;
    _open = false;
    _staging.clear();
    _files.erase("/com_prefs.tmp");
  }

  void removeNodeSource() { _files.erase("/node_prefs"); }
  const std::vector<uint8_t>& nodeSource() const { return _files.at("/node_prefs"); }
  const std::vector<uint8_t>& destination() const { return _files.at("/com_prefs"); }
  bool destinationExists() const { return _files.count("/com_prefs") != 0; }
  bool tempExists() const { return _files.count("/com_prefs.tmp") != 0; }
  bool nodeSourceIsPreferred() const {
    return _files.count("/node_prefs") != 0 && _files.count("/com_prefs") == 0;
  }
  bool nodeSourceExists() const { return _files.count("/node_prefs") != 0; }

  int begin_calls = 0;
  int write_calls = 0;
  int finish_calls = 0;
  int commit_calls = 0;
  int abort_calls = 0;

private:
  FailurePoint _failure;
  bool _open = false;
  std::vector<uint8_t> _staging;
  std::map<std::string, std::vector<uint8_t>> _files;
};

AtomicStore::ImageResult runCommonPrefsImage(InMemoryCommonPrefsStore* store) {
  const uint8_t core[] = {'c', 'o', 'm', '-', 'p', 'r', 'e', 'f', 's'};
  const uint8_t tail[] = {0x19, 0xa4, 0x7e};
  return AtomicStore::writeImage(*store, [&core, &tail](InMemoryCommonPrefsStore& target) {
    return target.write(core, sizeof(core)) == sizeof(core) &&
        target.write(tail, sizeof(tail)) == sizeof(tail);
  });
}

// Models the exact SPIFFS transaction used by MQTTPrefsFileStore. Files only
// move by rename: SPIFFS rejects a destination that already exists, so the old
// primary must remain available as .bak until the new temp owns the primary.
class SpiffsMqttTransaction {
public:
  enum class Boundary {
    BeforeBackupRename,
    AfterBackupRename,
    AfterPrimaryRename,
    AfterBackupCleanup,
  };

  SpiffsMqttTransaction() {
    _files["/mqtt_prefs"] = oldImage();
  }

  void writeVerifiedTemp() { _files["/mqtt_prefs.tmp"] = newImage(); }
  void cutDuringTempWrite() { _files["/mqtt_prefs.tmp"] = {'n'}; }

  void cutAt(Boundary boundary) {
    writeVerifiedTemp();
    if (boundary == Boundary::BeforeBackupRename) return;
    rename("/mqtt_prefs", "/mqtt_prefs.bak");
    if (boundary == Boundary::AfterBackupRename) return;
    rename("/mqtt_prefs.tmp", "/mqtt_prefs");
    if (boundary == Boundary::AfterPrimaryRename) return;
    _files.erase("/mqtt_prefs.bak");
  }

  // Inject ordinary operation failures (as distinct from a power cut). A
  // failed temp rename leaves both the verified temp and old backup intact.
  bool publish(bool fail_backup_rename, bool fail_temp_rename, bool fail_cleanup) {
    writeVerifiedTemp();
    if (fail_backup_rename || !rename("/mqtt_prefs", "/mqtt_prefs.bak")) return false;
    if (fail_temp_rename || !rename("/mqtt_prefs.tmp", "/mqtt_prefs")) return false;
    if (!fail_cleanup) _files.erase("/mqtt_prefs.bak");
    return true;  // backup cleanup is intentionally non-fatal after publish
  }

  void recover(Recovery::FileState primary = Recovery::FileState::Usable,
               Recovery::FileState temp = Recovery::FileState::Usable,
               Recovery::FileState backup = Recovery::FileState::Usable) {
    const bool had_primary = _files.count("/mqtt_prefs") != 0;
    const auto stateFor = [&](const char* path, Recovery::FileState readable) {
      return _files.count(path) == 0 ? Recovery::FileState::Missing : readable;
    };
    const Recovery::Action action = Recovery::select(
        stateFor("/mqtt_prefs", primary), stateFor("/mqtt_prefs.tmp", temp),
        stateFor("/mqtt_prefs.bak", backup));
    if (action == Recovery::Action::PromoteTemp) {
      rename("/mqtt_prefs.tmp", "/mqtt_prefs");
      // Match production: once a usable temp becomes primary, every backup is
      // stale and is cleared so a second save can start this boot.
      if (temp == Recovery::FileState::Usable && backup != Recovery::FileState::Missing) {
        _files.erase("/mqtt_prefs.bak");
      }
      return;
    }
    if (action == Recovery::Action::PromoteBackup) {
      rename("/mqtt_prefs.bak", "/mqtt_prefs");
      if (backup == Recovery::FileState::Usable && temp != Recovery::FileState::Missing) {
        _files.erase("/mqtt_prefs.tmp");
      }
      return;
    }

    // A usable primary is authoritative, so production cleans every stale or
    // incomplete transaction artifact. It only preserves artifacts when the
    // primary itself is opaque.
    if (had_primary && primary == Recovery::FileState::Usable) {
      _files.erase("/mqtt_prefs.tmp");
      _files.erase("/mqtt_prefs.bak");
    }
  }

  bool has(const char* path) const { return _files.count(path) != 0; }
  bool canStartSave() const { return !has("/mqtt_prefs.tmp") && !has("/mqtt_prefs.bak"); }
  const std::vector<uint8_t>& primary() const { return _files.at("/mqtt_prefs"); }
  static std::vector<uint8_t> oldImage() { return {'o', 'l', 'd'}; }
  static std::vector<uint8_t> newImage() { return {'n', 'e', 'w'}; }

private:
  bool rename(const char* from, const char* to) {
    if (_files.count(from) == 0 || _files.count(to) != 0) return false;
    _files[to] = _files[from];
    _files.erase(from);
    return true;
  }

  std::map<std::string, std::vector<uint8_t>> _files;
};

}  // namespace

TEST(MQTTPrefsAtomicStore, CommitPublishesExactHeaderThenPayload) {
  InMemoryStore store(FailurePoint::None);

  EXPECT_EQ(AtomicStore::Result::Committed, run(&store));
  EXPECT_EQ((std::vector<uint8_t>{0xf5, 'M', 'Q', 'P', 1, 0, 0x09, 0x00,
                                  'n', 'e', 'w', '-', 'p', 'r', 'e', 'f', 's'}),
            store.source());
  EXPECT_FALSE(store.tempExists());
  EXPECT_EQ(1, store.begin_calls);
  EXPECT_EQ(2, store.write_calls);
  EXPECT_EQ(1, store.finish_calls);
  EXPECT_EQ(1, store.commit_calls);
  EXPECT_EQ(0, store.abort_calls);
}

TEST(MQTTPrefsAtomicStore, AnyFailureAbortsAndPreservesExistingSource) {
  const std::vector<uint8_t> source = {'o', 'l', 'd', '-', 'p', 'r', 'e', 'f', 's'};
  const struct {
    FailurePoint point;
    AtomicStore::Result expected;
    int writes;
    int finishes;
    int commits;
  } cases[] = {
      {FailurePoint::Begin, AtomicStore::Result::BeginFailed, 0, 0, 0},
      {FailurePoint::HeaderWrite, AtomicStore::Result::HeaderWriteFailed, 1, 0, 0},
      {FailurePoint::PayloadWrite, AtomicStore::Result::PayloadWriteFailed, 2, 0, 0},
      {FailurePoint::Finish, AtomicStore::Result::FinishFailed, 2, 1, 0},
      {FailurePoint::Commit, AtomicStore::Result::CommitFailed, 2, 1, 1},
  };

  for (const auto& test_case : cases) {
    InMemoryStore store(test_case.point);
    EXPECT_EQ(test_case.expected, run(&store));
    EXPECT_EQ(source, store.source());
    EXPECT_EQ(test_case.point == FailurePoint::Commit, store.tempExists());
    EXPECT_EQ(1, store.begin_calls);
    EXPECT_EQ(test_case.writes, store.write_calls);
    EXPECT_EQ(test_case.finishes, store.finish_calls);
    EXPECT_EQ(test_case.commits, store.commit_calls);
    EXPECT_EQ(1, store.abort_calls);
  }
}

TEST(MQTTPrefsAtomicStore, BeginFailureDoesNotErasePreexistingRecoveryTemp) {
  InMemoryStore store(FailurePoint::Begin, true);

  EXPECT_EQ(AtomicStore::Result::BeginFailed, run(&store));
  EXPECT_TRUE(store.tempExists());
  EXPECT_EQ(1, store.abort_calls);
}

TEST(MQTTPrefsAtomicStore, LegacyCrossFileUpgradeCommitsTailBeforeCompactingComPrefs) {
  InMemoryStore mqtt_store(FailurePoint::None);
  LegacyComPrefs com_prefs;
  AtomicStore::LegacyUpgradeGate gate(true);
  gate.requireMqttRewrite();

  const AtomicStore::Result result = runWithObserverTail(&mqtt_store);
  gate.recordMqttSave(AtomicStore::committed(result));
  ASSERT_TRUE(gate.mayRewriteComPrefs());

  com_prefs.compactAfterMqttCommit(mqtt_store.source());
  gate.recordComPrefsRewrite();

  EXPECT_TRUE(com_prefs.mqtt_tail_present_before_compaction);
  EXPECT_EQ(1, com_prefs.compact_calls);
  EXPECT_EQ((std::vector<uint8_t>{'c', 'o', 'm', 'p', 'a', 'c', 't'}), com_prefs.bytes);
  EXPECT_FALSE(gate.mayRewriteComPrefs());
}

TEST(MQTTPrefsAtomicStore, LegacyCrossFilePowerCutPreservesBothSources) {
  const std::vector<uint8_t> legacy_mqtt = {'o', 'l', 'd', '-', 'p', 'r', 'e', 'f', 's'};
  const std::vector<uint8_t> legacy_com = {'l', 'e', 'g', 'a', 'c', 'y', '-', 'c', 'o', 'm'};
  for (const FailurePoint point : {FailurePoint::Begin, FailurePoint::HeaderWrite,
                                   FailurePoint::PayloadWrite, FailurePoint::Finish,
                                   FailurePoint::Commit}) {
    InMemoryStore mqtt_store(point);
    LegacyComPrefs com_prefs;
    AtomicStore::LegacyUpgradeGate gate(true);
    gate.requireMqttRewrite();

    const AtomicStore::Result result = runWithObserverTail(&mqtt_store);
    gate.recordMqttSave(AtomicStore::committed(result));
    if (gate.mayRewriteComPrefs()) {
      com_prefs.compactAfterMqttCommit(mqtt_store.source());
      gate.recordComPrefsRewrite();
    }

    EXPECT_FALSE(AtomicStore::committed(result));
    EXPECT_EQ(legacy_mqtt, mqtt_store.source());
    EXPECT_EQ(legacy_com, com_prefs.bytes);
    EXPECT_EQ(0, com_prefs.compact_calls);
    EXPECT_TRUE(gate.blocksComPrefsRewrite());
  }
}

TEST(MQTTPrefsAtomicStore, LegacyNodePrefsMigrationWaitsForObserverTailCommit) {
  InMemoryStore mqtt_store(FailurePoint::None);
  LegacyNodePrefs node_prefs;
  AtomicStore::LegacyUpgradeGate gate(true);
  gate.requireMqttRewrite();

  const AtomicStore::Result result = runWithObserverTail(&mqtt_store);
  gate.recordMqttSave(AtomicStore::committed(result));
  ASSERT_TRUE(gate.mayRewriteComPrefs());

  node_prefs.migrateAfterMqttCommit(mqtt_store.source());
  gate.recordComPrefsRewrite();

  EXPECT_TRUE(node_prefs.mqtt_tail_present_before_removal);
  EXPECT_EQ(1, node_prefs.migration_calls);
  EXPECT_TRUE(node_prefs.bytes.empty());
}

TEST(MQTTPrefsAtomicStore, LegacyNodePrefsPowerCutPreservesSource) {
  const std::vector<uint8_t> legacy_mqtt = {'o', 'l', 'd', '-', 'p', 'r', 'e', 'f', 's'};
  const std::vector<uint8_t> legacy_node = {
      'l', 'e', 'g', 'a', 'c', 'y', '-', 'n', 'o', 'd', 'e'};
  for (const FailurePoint point : {FailurePoint::Begin, FailurePoint::HeaderWrite,
                                   FailurePoint::PayloadWrite, FailurePoint::Finish,
                                   FailurePoint::Commit}) {
    InMemoryStore mqtt_store(point);
    LegacyNodePrefs node_prefs;
    AtomicStore::LegacyUpgradeGate gate(true);
    gate.requireMqttRewrite();

    const AtomicStore::Result result = runWithObserverTail(&mqtt_store);
    gate.recordMqttSave(AtomicStore::committed(result));
    if (gate.mayRewriteComPrefs()) {
      node_prefs.migrateAfterMqttCommit(mqtt_store.source());
      gate.recordComPrefsRewrite();
    }

    EXPECT_FALSE(AtomicStore::committed(result));
    EXPECT_EQ(legacy_mqtt, mqtt_store.source());
    EXPECT_EQ(legacy_node, node_prefs.bytes);
    EXPECT_EQ(0, node_prefs.migration_calls);
    EXPECT_TRUE(gate.blocksComPrefsRewrite());
  }
}

TEST(MQTTPrefsAtomicStore, NodePrefsMigrationPublishesComPrefsBeforeRemovingSource) {
  InMemoryCommonPrefsStore store(FailurePoint::None);

  ASSERT_EQ(AtomicStore::ImageResult::Committed, runCommonPrefsImage(&store));
  EXPECT_TRUE(store.nodeSourceExists());  // caller removes it only after commit
  EXPECT_EQ((std::vector<uint8_t>{'c', 'o', 'm', '-', 'p', 'r', 'e', 'f', 's', 0x19, 0xa4, 0x7e}),
            store.destination());
  EXPECT_FALSE(store.tempExists());

  store.removeNodeSource();
  EXPECT_FALSE(store.nodeSourceExists());
  EXPECT_TRUE(store.destinationExists());
}

TEST(MQTTPrefsAtomicStore, NodePrefsMigrationFailurePreservesSourceAndNeverPrefersPartialDestination) {
  const std::vector<uint8_t> legacy_node = {
      'l', 'e', 'g', 'a', 'c', 'y', '-', 'n', 'o', 'd', 'e'};
  const struct {
    FailurePoint point;
    AtomicStore::ImageResult expected;
    int writes;
    int finishes;
    int commits;
  } cases[] = {
      {FailurePoint::Begin, AtomicStore::ImageResult::BeginFailed, 0, 0, 0},
      {FailurePoint::ImageWrite, AtomicStore::ImageResult::WriteFailed, 2, 0, 0},
      {FailurePoint::Finish, AtomicStore::ImageResult::FinishFailed, 2, 1, 0},
      {FailurePoint::Commit, AtomicStore::ImageResult::CommitFailed, 2, 1, 1},
  };

  for (const auto& test_case : cases) {
    InMemoryCommonPrefsStore store(test_case.point);
    EXPECT_EQ(test_case.expected, runCommonPrefsImage(&store));
    EXPECT_EQ(legacy_node, store.nodeSource());
    EXPECT_TRUE(store.nodeSourceIsPreferred());
    EXPECT_FALSE(store.destinationExists());
    EXPECT_FALSE(store.tempExists());
    EXPECT_EQ(1, store.begin_calls);
    EXPECT_EQ(test_case.writes, store.write_calls);
    EXPECT_EQ(test_case.finishes, store.finish_calls);
    EXPECT_EQ(test_case.commits, store.commit_calls);
    EXPECT_EQ(1, store.abort_calls);
  }
}

TEST(MQTTPrefsAtomicStore, SpiffsPowerCutsAtEveryPublishBoundaryLeaveRecoverableImage) {
  const struct {
    SpiffsMqttTransaction::Boundary boundary;
    std::vector<uint8_t> expected_after_reboot;
  } cases[] = {
      // Temp has not become the committed image yet, so the old primary wins.
      {SpiffsMqttTransaction::Boundary::BeforeBackupRename, SpiffsMqttTransaction::oldImage()},
      // Old primary is .bak and verified new temp wins the recovery race.
      {SpiffsMqttTransaction::Boundary::AfterBackupRename, SpiffsMqttTransaction::newImage()},
      {SpiffsMqttTransaction::Boundary::AfterPrimaryRename, SpiffsMqttTransaction::newImage()},
      {SpiffsMqttTransaction::Boundary::AfterBackupCleanup, SpiffsMqttTransaction::newImage()},
  };

  for (const auto& test_case : cases) {
    SpiffsMqttTransaction store;
    store.cutAt(test_case.boundary);
    store.recover();
    ASSERT_TRUE(store.has("/mqtt_prefs"));
    EXPECT_EQ(test_case.expected_after_reboot, store.primary());
    EXPECT_FALSE(store.has("/mqtt_prefs.tmp"));
    EXPECT_FALSE(store.has("/mqtt_prefs.bak"));
  }
}

TEST(MQTTPrefsAtomicStore, PowerCutDuringTempWriteKeepsPrimaryAndAllowsNextSave) {
  SpiffsMqttTransaction store;
  store.cutDuringTempWrite();

  // The partial temp is opaque to the codec, but the existing primary is the
  // only committed image. Recovery discards the incomplete transaction rather
  // than blocking every later config save behind /mqtt_prefs.tmp.
  store.recover(Recovery::FileState::Usable, Recovery::FileState::Preserve);
  EXPECT_EQ(SpiffsMqttTransaction::oldImage(), store.primary());
  EXPECT_FALSE(store.has("/mqtt_prefs.tmp"));
  EXPECT_TRUE(store.canStartSave());
}

TEST(MQTTPrefsAtomicStore, RecoveredUsablePrimaryClearsOpaqueTransactionArtifacts) {
  {
    SpiffsMqttTransaction store;
    store.cutAt(SpiffsMqttTransaction::Boundary::AfterBackupRename);
    // A current-format temp wins; the old backup need not be decodable to be
    // stale once that usable temp owns the primary name.
    store.recover(Recovery::FileState::Usable, Recovery::FileState::Usable,
                  Recovery::FileState::Preserve);
    EXPECT_EQ(SpiffsMqttTransaction::newImage(), store.primary());
    EXPECT_TRUE(store.canStartSave());
  }
  {
    SpiffsMqttTransaction store;
    store.cutAt(SpiffsMqttTransaction::Boundary::AfterBackupRename);
    // Conversely, when the usable backup becomes primary, an opaque temp was
    // never published and must not leave saves permanently blocked.
    store.recover(Recovery::FileState::Usable, Recovery::FileState::Preserve,
                  Recovery::FileState::Usable);
    EXPECT_EQ(SpiffsMqttTransaction::oldImage(), store.primary());
    EXPECT_TRUE(store.canStartSave());
  }
}

TEST(MQTTPrefsAtomicStore, SpiffsRenameAndCleanupFailuresRemainRecoverable) {
  {
    SpiffsMqttTransaction store;
    EXPECT_FALSE(store.publish(true, false, false));
    store.recover();
    EXPECT_EQ(SpiffsMqttTransaction::oldImage(), store.primary());
    EXPECT_FALSE(store.has("/mqtt_prefs.tmp"));
    EXPECT_FALSE(store.has("/mqtt_prefs.bak"));
  }
  {
    SpiffsMqttTransaction store;
    EXPECT_FALSE(store.publish(false, true, false));
    EXPECT_FALSE(store.has("/mqtt_prefs"));
    EXPECT_TRUE(store.has("/mqtt_prefs.tmp"));
    EXPECT_TRUE(store.has("/mqtt_prefs.bak"));
    store.recover();
    EXPECT_EQ(SpiffsMqttTransaction::newImage(), store.primary());
    EXPECT_FALSE(store.has("/mqtt_prefs.tmp"));
    EXPECT_FALSE(store.has("/mqtt_prefs.bak"));
  }
  {
    SpiffsMqttTransaction store;
    EXPECT_TRUE(store.publish(false, false, true));
    EXPECT_EQ(SpiffsMqttTransaction::newImage(), store.primary());
    EXPECT_TRUE(store.has("/mqtt_prefs.bak"));
    store.recover();
    EXPECT_FALSE(store.has("/mqtt_prefs.bak"));
  }
}

TEST(MQTTPrefsAtomicStore, RecoveryNeverOverwritesOpaqueNewerLayout) {
  // An unreadable primary owns its name, even if an older usable backup and a
  // verified temp exist. This is the downgrade-preservation invariant.
  EXPECT_EQ(Recovery::Action::KeepPrimary,
            Recovery::select(Recovery::FileState::Preserve, Recovery::FileState::Usable,
                             Recovery::FileState::Usable));
  // If there is no primary, a usable backup wins over an opaque temp. Once
  // promoted, production treats the backup as authoritative and clears temp.
  EXPECT_EQ(Recovery::Action::PromoteBackup,
            Recovery::select(Recovery::FileState::Missing, Recovery::FileState::Preserve,
                             Recovery::FileState::Usable));
  // With no other image, an opaque backup is renamed into the empty primary
  // name so CommonCLI will hold it rather than silently replace it with defaults.
  EXPECT_EQ(Recovery::Action::PromoteBackup,
            Recovery::select(Recovery::FileState::Missing, Recovery::FileState::Missing,
                             Recovery::FileState::Preserve));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
