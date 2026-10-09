#include <gtest/gtest.h>
#include <helpers/room_history/HistoryTypes.h>
#include <helpers/room_history/HistoryStorage.h>
#include "FakeStorage.h"
class HistoryTestFS { public: bool mkdir(const char*) { return true; } };
#define FILESYSTEM HistoryTestFS
#include <helpers/CommonCLI.h>
#include <helpers/room_history/HistoryPreferences.h>
#include <helpers/room_history/RoomHistory.h>
#include <helpers/room_history/HistoryStartup.h>
#include <helpers/room_history/HistoryMembers.h>
#include <helpers/room_history/HistoryClock.h>
#include <helpers/room_history/HistoryPlayback.h>
#include <helpers/room_history/HistorySubmission.h>
#include <helpers/room_history/HistoryAdmin.h>
#include <helpers/room_history/HistoryReceive.h>
#include <helpers/room_history/HistoryLogin.h>
#include <helpers/room_history/HistoryWrite.h>
#include <string>
class PrefStream : public room_history::PreferenceBuffer {
  template<class T> size_t number(T n) { return Print::print(std::to_string(n).c_str()); }
public:
  explicit PrefStream(const char* input = "") { length = strlen(input); memcpy(data, input, length); }
  size_t print(unsigned char n, int) override { return number(n); }
  size_t print(int n, int) override { return number(n); }
  size_t print(unsigned int n, int) override { return number(n); }
  size_t print(long n, int) override { return number(n); }
  size_t print(unsigned long n, int) override { return number(n); }
  size_t print(long long n, int) override { return number(n); }
  size_t print(unsigned long long n, int) override { return number(n); }
  size_t print(double n, int precision) override {
    char tmp[48]; snprintf(tmp, sizeof(tmp), "%.*f", precision, n); return Print::print(tmp);
  }
};
using namespace room_history;
TEST(HistoryWrite, UnsupportedSyncRequiresSuccessfulWriteFlushAndBackendClose) {
  for (int syncError : {0, ENOSYS, EIO, ENOSPC, EBADF}) for (int failedStage : {-1, 0, 1, 2}) {
    std::string order;
    bool saved = checkedSpiffsWrite(
      [&]() { order += 'W'; return failedStage != 0; },
      [&]() { order += 'F'; return failedStage != 1; },
      [&]() { order += 'S'; return SyncResult{syncError ? -1 : 0, syncError}; },
      [&]() { order += 'C'; return failedStage != 2; });
    EXPECT_EQ("WFSC", order); // Close is checked even after earlier failures.
    EXPECT_EQ(failedStage == -1 && (syncError == 0 || syncError == ENOSYS), saved);
  }
}
TEST(HistoryContracts, StrictPlaybackAndRollback) {
  uint16_t value = DefaultPlayback; int saves = 0;
  for (const char* bad : {"", "0", "-1", "+1", "2001", "100x", " 100", "100 ", "1.5", "NaN", "18446744073709551616"}) {
    EXPECT_EQ(Result::Invalid, changePlayback(value, bad, [&]() { ++saves; return true; }));
    EXPECT_EQ(100, value);
  }
  EXPECT_EQ(0, saves);
  for (const char* valid : {"1", "2000", "100", "default"})
    EXPECT_EQ(Result::Ok, changePlayback(value, valid, [&]() { ++saves; return true; }));
  EXPECT_EQ(100, value);
  EXPECT_EQ(Result::Io, changePlayback(value, "2000", []() { return false; }));
  EXPECT_EQ(100, value);
}
TEST(HistoryContracts, FullIntegerParsingAndBudgets) {
  uint64_t n = 9;
  EXPECT_TRUE(parseUnsigned("18446744073709551615", UINT64_MAX, n)); EXPECT_EQ(UINT64_MAX, n);
  EXPECT_FALSE(parseUnsigned("18446744073709551616", UINT64_MAX, n));
  EXPECT_FALSE(parseUnsigned("9", 1, n));
  EXPECT_LE(sizeof(IndexEntry) * Capacity, 80000u);
  EXPECT_LE(sizeof(Member) * MemberCapacity, 32768u);
  EXPECT_EQ(655360u, ArchiveBudget + MemberBudget);
}
#if defined(XIAO_WIO_ROOM_HISTORY) && XIAO_WIO_ROOM_HISTORY
TEST(HistoryPreferences, ActualRoomSerializerLegacyMissingAndBoundaryValues) {
  NodePrefs prefs; PrefStream old("{name:\"Existing room\",room:{rd_only:1},power:{batt_connected:1},radio:{freq:917.375}} ");
  ASSERT_TRUE(prefs.loadSerial(old)); EXPECT_EQ(100, prefs.history_playback);
  EXPECT_EQ(1, prefs.allow_read_only); EXPECT_EQ(1, prefs.battery_connected);
  for (uint16_t value : {1, 100, 2000}) {
    prefs.history_playback = value; PrefStream out; ASSERT_TRUE(prefs.saveSerial(out));
    NodePrefs restored; out.position = 0; ASSERT_TRUE(restored.loadSerial(out));
    EXPECT_EQ(value, restored.history_playback); EXPECT_STREQ("Existing room", restored.node_name);
    EXPECT_EQ(1, restored.allow_read_only); EXPECT_EQ(1, restored.battery_connected);
    EXPECT_FLOAT_EQ(917.375f, restored.freq);
  }
}
TEST(HistoryPreferences, InvalidRestoredValuesDoNotWrapIntoValidRange) {
  for (const char* value : {"0", "-1", "2001", "65537", "4294967396", "1.5", "nan", "\"100x\""}) {
    NodePrefs prefs; std::string json = std::string("{room:{hist_playback:") + value + "}}";
    PrefStream input(json.c_str()); ASSERT_TRUE(prefs.loadSerial(input)); EXPECT_EQ(100, prefs.history_playback) << value;
    EXPECT_TRUE(prefs.usedBoundedFallback());
  }
  NodePrefs prefs; PrefStream damaged("{room:{hist_playback:2000}"); EXPECT_FALSE(prefs.loadSerial(damaged));
}
TEST(HistoryPreferences, TransactionPreservesUnrelatedSettingsAndKeepsBackup) {
  FakeStorage fs; NodePrefs prefs; strcpy(prefs.node_name, "Room"); prefs.history_playback = 1;
  PrefStream b; ASSERT_TRUE(savePreferences(fs, prefs, b));
  auto original = fs.files["/prefs.json"];
  prefs.history_playback = 2000; ASSERT_TRUE(savePreferences(fs, prefs, b));
  EXPECT_EQ(original, fs.files["/prefs.previous"]);
  NodePrefs restored; PrefStream input; input.length = fs.files["/prefs.json"].size();
  memcpy(input.data, fs.files["/prefs.json"].data(), input.length);
  ASSERT_TRUE(restored.loadSerial(input)); EXPECT_EQ(2000, restored.history_playback); EXPECT_STREQ("Room", restored.node_name);
}
TEST(HistoryPreferences, FailedStageOrSwitchPreservesSolePreviousFile) {
  for (int cut = 1; cut <= 3; ++cut) {
    FakeStorage fs; NodePrefs prefs; PrefStream b; ASSERT_TRUE(savePreferences(fs, prefs, b));
    auto original = fs.files["/prefs.json"]; fs.operations = 0; fs.failAt = cut; fs.shortWrite = 12;
    EXPECT_FALSE(savePreferences(fs, prefs, b));
    EXPECT_TRUE((fs.files.count("/prefs.json") && fs.files["/prefs.json"] == original) ||
                (fs.files.count("/prefs.previous") && fs.files["/prefs.previous"] == original));
  }
}
#else
TEST(HistoryPreferences, DisabledFeatureDoesNotSerializeHistorySetting) {
  NodePrefs p; PrefStream input("{room:{hist_playback:2000,rd_only:1}}");
  ASSERT_TRUE(p.loadSerial(input)); EXPECT_EQ(1, p.allow_read_only);
  PrefStream output; ASSERT_TRUE(p.saveSerial(output));
  std::string text((char*)output.data, output.length); EXPECT_EQ(std::string::npos, text.find("hist_playback"));
}
#endif
TEST(HistoryStartup, EveryPartitionByteMustBeErasedAndReadable) {
  size_t inspected = 0;
  auto blank = [&](size_t offset, uint8_t* b, size_t n) { EXPECT_EQ(inspected, offset); memset(b, 255, n); inspected += n; return true; };
  EXPECT_TRUE(partitionIsErased(0x180000, blank)); EXPECT_EQ(0x180000u, inspected);
  EXPECT_FALSE(partitionIsErased(0x180000, [](size_t offset, uint8_t* b, size_t n) {
    memset(b, 255, n); if (offset + n == 0x180000) b[n - 1] = 0; return true;
  }));
  EXPECT_FALSE(partitionIsErased(0x180000, [](size_t, uint8_t*, size_t) { return false; }));
}
TEST(HistoryStartup, OnlyBlankFailedMountMayFormatAndExistingIdentityNeverRegenerates) {
  struct Backend {
    int mounts = 0, formats = 0; bool healthy = false, blank = false, formatOK = true;
    bool mount() { ++mounts; return healthy || (formats && formatOK); }
    bool erased() { return blank; } bool format() { ++formats; return formatOK; }
  };
  Backend nonblank; EXPECT_FALSE(safeMount(nonblank)); EXPECT_EQ(0, nonblank.formats);
  Backend fresh; fresh.blank = true; EXPECT_TRUE(safeMount(fresh)); EXPECT_EQ(1, fresh.formats);
  Backend broken; broken.blank = true; broken.formatOK = false; EXPECT_FALSE(safeMount(broken));
  Backend normal; normal.healthy = true; EXPECT_TRUE(safeMount(normal)); EXPECT_EQ(0, normal.formats);
  EXPECT_EQ(IdentityAction::Provision, identityAction(0, false, false));
  EXPECT_EQ(IdentityAction::Load, identityAction(10, true, true));
  EXPECT_EQ(IdentityAction::Recovery, identityAction(10, false, false));
  EXPECT_EQ(IdentityAction::Recovery, identityAction(1, true, false));
}
TEST(HistoryStartup, PreferencesRestoreLastValidGenerationWithoutDiscardingEvidence) {
  auto valid = [](FakeStorage& fs, const char* name) { return fs.files[name] == std::vector<uint8_t>({'o','l','d'}); };
  for (bool current : {false, true}) {
    FakeStorage fs; fs.files["/prefs.previous"] = {'o','l','d'}; fs.files["/prefs.pending"] = {'t'};
    if (current) fs.files["/prefs.json"] = {'b','a','d'};
    EXPECT_TRUE(recoverPreferences(fs, [&](const char* n) { return valid(fs, n); }));
    EXPECT_EQ(std::vector<uint8_t>({'o','l','d'}), fs.files["/prefs.json"]);
    if (current) EXPECT_EQ(std::vector<uint8_t>({'b','a','d'}), fs.files["/prefs.failed"]);
  }
  FakeStorage bad; bad.files["/prefs.json"] = {'b'}; auto before = bad.files;
  EXPECT_FALSE(recoverPreferences(bad, [&](const char* n) { return valid(bad, n); })); EXPECT_EQ(before, bad.files);
}
TEST(HistoryCodec, WireSchemaCrcAndExactBounds) {
  EXPECT_EQ(0xcbf43926u, crc32("123456789", 9));
  Post p; p.sequence = 0x0102030405060708ull; p.timestamp = 42; p.senderTimestamp = 12; p.author[0] = 3;
  strcpy(p.text, "hello"); p.length = 5; uint8_t b[RecordBytes]; ASSERT_TRUE(encodePost(p, b));
  EXPECT_EQ(8, b[12]); EXPECT_EQ(1, b[19]); EXPECT_EQ(224, get16(b + 8));
  Post out; ASSERT_TRUE(decodePost(b, out)); EXPECT_STREQ("hello", out.text); EXPECT_EQ(p.sequence, out.sequence);
  for (size_t i = 0; i < sizeof(b); ++i) { b[i] ^= 1; EXPECT_FALSE(decodePost(b, out)) << i; b[i] ^= 1; }
  p.kind = 1; EXPECT_FALSE(encodePost(p, b)); p.senderTimestamp = 0; EXPECT_TRUE(encodePost(p, b));
}
TEST(HistoryJournal, RetainsExactlyNewest2000AndRecoversWithoutHealthyRewrites) {
  for (int n : {0, 1, 32, 100, 255, 256, 1999, 2000, 2001, 4096}) {
    FakeStorage fs; uint8_t key[32] = {1}, author[32] = {2};
    { RoomHistory history(fs); ASSERT_EQ(Result::Ok, history.begin(key)); Post out;
      for (int i = 1; i <= n; ++i) ASSERT_EQ(Result::Ok, history.append(author, i, "post", 0, 100, out)) << i;
      EXPECT_EQ(std::min(n, 2000), history.count()); size_t bytes; EXPECT_TRUE(history.bytes(bytes)); EXPECT_LE(bytes, ArchiveBudget);
    }
    auto files = fs.files; int writes = fs.operations;
    RoomHistory recovered(fs); ASSERT_EQ(Result::Ok, recovered.begin(key)); EXPECT_EQ(std::min(n, 2000), recovered.count());
    EXPECT_EQ(files, fs.files); EXPECT_EQ(writes, fs.operations);
    Post p; uint32_t previous = 0;
    for (uint16_t i = 0; i < recovered.count(); ++i) {
      ASSERT_EQ(Result::Ok, recovered.read(recovered.at(i), p)); EXPECT_GT(p.timestamp, previous); previous = p.timestamp;
      EXPECT_EQ(uint64_t(std::max(1, n - 1999) + i), p.sequence); EXPECT_STREQ("post", p.text); EXPECT_EQ(2, p.author[0]);
    }
  }
}
TEST(HistoryJournal, DurableDuplicateConflictAndSystemPostSemantics) {
  FakeStorage fs; uint8_t key[32] = {1}, author[32] = {2}; Post p;
  { RoomHistory h(fs); ASSERT_EQ(Result::Ok, h.begin(key)); ASSERT_EQ(Result::Ok, h.append(author, 42, "one", 0, 100, p)); }
  RoomHistory h(fs); ASSERT_EQ(Result::Ok, h.begin(key));
  EXPECT_EQ(Result::Duplicate, h.append(author, 42, "one", 0, 1, p)); EXPECT_EQ(1, h.count());
  EXPECT_EQ(Result::Conflict, h.append(author, 42, "two", 0, 1, p)); EXPECT_EQ(1, h.count());
  EXPECT_EQ(Result::Ok, h.append(key, 0, "same", 1, 0, p)); uint32_t t = p.timestamp;
  EXPECT_EQ(Result::Ok, h.append(key, 0, "same", 1, 0, p)); EXPECT_GT(p.timestamp, t); EXPECT_EQ(3, h.count());
}
TEST(HistoryJournal, AllFirstAppendCutStagesPreserveReservationOrExplicitRecovery) {
  for (int cut = 1; cut <= 3; ++cut) for (size_t prefix : {size_t(0), size_t(12), SIZE_MAX}) {
    FakeStorage fs; uint8_t key[32] = {1}, author[32] = {2}; Post p;
    { RoomHistory h(fs); ASSERT_EQ(Result::Ok, h.begin(key)); fs.operations = 0; fs.failAt = cut; fs.shortWrite = prefix;
      EXPECT_NE(Result::Ok, h.append(author, 1, "one", 0, 100, p)); EXPECT_EQ(State::Recovery, h.state()); }
    fs.failAt = -1; RoomHistory recovered(fs); auto outcome = recovered.begin(key);
    if (outcome == Result::Ok) {
      uint32_t floor = recovered.timestampFloor(); EXPECT_EQ(Result::Ok, recovered.append(author, 2, "two", 0, 0, p)); EXPECT_GT(p.timestamp, floor);
    } else EXPECT_EQ(State::Recovery, recovered.state());
  }
}
TEST(HistoryJournal, CorruptMiddleUnknownSchemaForeignIdentityAndReadFailureFailClosed) {
  FakeStorage baseline; uint8_t key[32] = {1}, author[32] = {2}; Post p;
  { RoomHistory h(baseline); ASSERT_EQ(Result::Ok, h.begin(key)); for (int i = 1; i <= 5; ++i) ASSERT_EQ(Result::Ok, h.append(author, i, "one", 0, 100, p)); }
  FakeStorage middle = baseline; middle.files["/rh_s_00000001"][HeaderBytes + 2 * RecordBytes + 70] ^= 1;
  RoomHistory h1(middle); EXPECT_EQ(Result::Recovery, h1.begin(key));
  FakeStorage schema = baseline; schema.files["/rh_s_00000001"][4] = 2;
  RoomHistory h2(schema); EXPECT_EQ(Result::Recovery, h2.begin(key));
  uint8_t foreign[32] = {8}; RoomHistory h3(baseline); EXPECT_EQ(Result::Recovery, h3.begin(foreign));
  FakeStorage unreadable = baseline; unreadable.failReads = true; RoomHistory h4(unreadable); EXPECT_EQ(Result::Recovery, h4.begin(key));
}
TEST(HistoryJournal, RotationCutsRetentionAndUnlinkFailuresAreBounded) {
  FakeStorage baseline; uint8_t key[32] = {1}, author[32] = {2}; Post p;
  { RoomHistory h(baseline); ASSERT_EQ(Result::Ok, h.begin(key)); for (int i = 1; i <= 64; ++i) ASSERT_EQ(Result::Ok, h.append(author, i, "post", 0, 100, p)); }
  for (int cut = 1; cut <= 3; ++cut) for (size_t prefix : {size_t(0), size_t(10), SIZE_MAX}) {
    FakeStorage fs = baseline;
    { RoomHistory h(fs); ASSERT_EQ(Result::Ok, h.begin(key)); fs.operations = 0; fs.failAt = cut; fs.shortWrite = prefix;
      EXPECT_NE(Result::Ok, h.append(author, 65, "rotation", 0, 100, p)); }
    fs.failAt = -1; RoomHistory recovered(fs); auto result = recovered.begin(key);
    if (result == Result::Ok) { EXPECT_GE(recovered.count(), 64); EXPECT_GE(recovered.timestampFloor(), 163u); }
    else EXPECT_EQ(State::Recovery, recovered.state());
  }
  RoomHistory h(baseline); ASSERT_EQ(Result::Ok, h.begin(key));
  for (int i = 65; i <= 2063; ++i) ASSERT_EQ(Result::Ok, h.append(author, i, "post", 0, 100, p));
  baseline.failUnlink = true; EXPECT_EQ(Result::Ok, h.append(author, 2064, "last", 0, 100, p));
  EXPECT_EQ(State::Recovery, h.state()); EXPECT_GT(h.failures, 0u); EXPECT_EQ(2000, h.count());
}
TEST(HistoryJournal, CapacityAllocationAndClockOverflowNeverReportCommit) {
  uint8_t key[32] = {1}, author[32] = {2}; Post p;
  FakeStorage full; full.foreignUsed = full.total; RoomHistory h(full); ASSERT_EQ(Result::Ok, h.begin(key));
  EXPECT_EQ(Result::Full, h.append(author, 1, "post", 0, 100, p)); EXPECT_EQ(0, h.count());
  FakeStorage memory; memory.failAlloc = true; RoomHistory noIndex(memory); EXPECT_EQ(Result::Full, noIndex.begin(key));
  FakeStorage time; RoomHistory clock(time); ASSERT_EQ(Result::Ok, clock.begin(key));
  ASSERT_EQ(Result::Ok, clock.append(author, 1, "max", 0, UINT32_MAX, p));
  EXPECT_EQ(Result::Full, clock.append(author, 2, "overflow", 0, 0, p)); EXPECT_EQ(1, clock.count());
}
TEST(HistoryMembers, DurableFullIdentityFloorsCursorAndUnknownRecentLogin) {
  FakeStorage fs; uint8_t room[32] = {1}, key[32] = {2}; Member* m;
  uint64_t incarnation;
  { HistoryMembers users(fs); ASSERT_EQ(Result::Ok, users.begin(room));
    ASSERT_EQ(Result::Ok, users.login(key, 80, true, 1700000000, m)); incarnation = m->incarnation;
    ASSERT_EQ(Result::Ok, users.delivered(key, incarnation, 95, 999));
    ASSERT_EQ(Result::Ok, users.login(key, 100, false, 0, m));
    EXPECT_EQ(80u, m->joinFloor); EXPECT_EQ(95u, m->delivered); EXPECT_FALSE(m->loginKnown); }
  auto files = fs.files; int mutations = fs.operations;
  HistoryMembers users(fs); ASSERT_EQ(Result::Ok, users.begin(room)); EXPECT_EQ(files, fs.files); EXPECT_EQ(mutations, fs.operations);
  m = users.find(key); ASSERT_NE(nullptr, m); EXPECT_EQ(incarnation, m->incarnation); EXPECT_EQ(999u, m->deliveredTimestamp);
  EXPECT_FALSE(m->loginKnown); EXPECT_EQ(1700000000u, m->lastLogin);
  EXPECT_EQ(Result::Duplicate, users.delivered(key, incarnation, 95, 999));
  EXPECT_EQ(Result::NotFound, users.delivered(key, incarnation + 1, 100, 1000));
}
TEST(HistoryMembers, CapacityNoEvictionCompactionAndAtomicPurges) {
  FakeStorage fs; uint8_t room[32] = {1}, key[32] = {}; Member* m;
  { HistoryMembers users(fs); ASSERT_EQ(Result::Ok, users.begin(room));
    for (unsigned i = 0; i < MemberCapacity; ++i) { key[0] = i; key[1] = 1; ASSERT_EQ(Result::Ok, users.login(key, i, false, 0, m)); }
    key[1] = 2; EXPECT_EQ(Result::Full, users.login(key, 500, false, 0, m)); EXPECT_EQ(256, users.count());
    key[1] = 1;
    for (int i = 0; i < 700; ++i) ASSERT_EQ(Result::Ok, users.login(key, 500, true, 1700000000 + i, m));
    size_t bytes; ASSERT_TRUE(users.bytes(bytes)); EXPECT_LE(bytes, MemberBudget); EXPECT_LE(users.memoryBytes(), 32768u);
  }
  HistoryMembers users(fs); ASSERT_EQ(Result::Ok, users.begin(room)); EXPECT_EQ(256, users.count());
  PurgeCounts count; EXPECT_EQ(Result::Invalid, users.forget("", count)); EXPECT_EQ(256, users.count());
  ASSERT_EQ(Result::Ok, users.forget("all", count)); EXPECT_EQ(256, count.removed); EXPECT_EQ(0, users.count());
  HistoryMembers reboot(fs); ASSERT_EQ(Result::Ok, reboot.begin(room)); EXPECT_EQ(0, reboot.count());
  ASSERT_EQ(Result::Ok, reboot.login(key, 900, false, 0, m)); EXPECT_EQ(900u, m->joinFloor); EXPECT_EQ(0u, m->delivered);
}
TEST(HistoryMembers, PrefixAmbiguityTombstoneAndStaleAckCannotResurrect) {
  FakeStorage fs; uint8_t room[32] = {1}, a[32] = {0xab}, b[32] = {0xab}, selected[32]; b[31] = 1; Member* m;
  HistoryMembers users(fs); ASSERT_EQ(Result::Ok, users.begin(room)); ASSERT_EQ(Result::Ok, users.login(a, 40, false, 0, m));
  uint64_t incarnation = m->incarnation; ASSERT_EQ(Result::Ok, users.login(b, 40, false, 0, m));
  EXPECT_EQ(Result::Ambiguous, users.select("ab0000000000", selected));
  EXPECT_EQ(Result::Invalid, users.select("ab00000000", selected)); EXPECT_EQ(Result::Invalid, users.select("zz0000000000", selected));
  PurgeCounts n; ASSERT_EQ(Result::Ok, users.forget("ab00000000000000000000000000000000000000000000000000000000000000", n));
  EXPECT_EQ(Result::NotFound, users.delivered(a, incarnation, 100, 200));
  HistoryMembers reboot(fs); ASSERT_EQ(Result::Ok, reboot.begin(room)); EXPECT_EQ(1, reboot.count());
  ASSERT_EQ(Result::Ok, reboot.login(a, 150, false, 0, m)); EXPECT_NE(incarnation, m->incarnation); EXPECT_EQ(150u, m->joinFloor);
  EXPECT_EQ(Result::NotFound, reboot.delivered(a, incarnation, 200, 300));
}
TEST(HistoryMembers, FaultsAtEveryGenerationSwitchAndMutationRemainOldOrNew) {
  FakeStorage baseline; uint8_t room[32] = {1}, key[32] = {2}; Member* m;
  { HistoryMembers users(baseline); ASSERT_EQ(Result::Ok, users.begin(room));
    ASSERT_EQ(Result::Ok, users.login(key, 5, false, 0, m)); ASSERT_EQ(Result::Ok, users.delivered(key, m->incarnation, 10, 100)); }
  for (int cut = 1; cut <= 5; ++cut) for (size_t prefix : {size_t(0), size_t(13), SIZE_MAX}) {
    FakeStorage fs = baseline;
    { HistoryMembers users(fs); ASSERT_EQ(Result::Ok, users.begin(room)); fs.operations = 0; fs.failAt = cut; fs.shortWrite = prefix;
      PurgeCounts n; Result r = users.forget("all", n); if (r != Result::Ok) EXPECT_EQ(0, n.removed); }
    fs.failAt = -1; HistoryMembers reboot(fs); Result r = reboot.begin(room);
    if (r == Result::Ok) { EXPECT_LE(reboot.count(), 1); if (reboot.count()) EXPECT_EQ(10u, reboot.at(0).delivered); }
    else EXPECT_EQ(State::Recovery, reboot.state());
  }
  for (size_t prefix : {size_t(0), size_t(13), SIZE_MAX}) {
    FakeStorage fs = baseline;
    { HistoryMembers users(fs); ASSERT_EQ(Result::Ok, users.begin(room)); uint64_t inc = users.find(key)->incarnation;
      fs.operations = 0; fs.failAt = 1; fs.shortWrite = prefix;
      EXPECT_NE(Result::Ok, users.delivered(key, inc, 11, 101)); EXPECT_EQ(10u, users.find(key)->delivered); }
    fs.failAt = -1; HistoryMembers reboot(fs); ASSERT_EQ(Result::Ok, reboot.begin(room));
    EXPECT_TRUE(reboot.find(key)->delivered == 10 || reboot.find(key)->delivered == 11);
  }
}
TEST(HistoryMembers, NonemptyPurgeSnapshotCutsNeverPartiallyForgetMembers) {
  FakeStorage baseline; uint8_t room[32] = {1}, keys[4][32] = {{2}, {3}, {4}, {5}}; Member* m;
  {
    HistoryMembers users(baseline); ASSERT_EQ(Result::Ok, users.begin(room));
    for (unsigned i = 0; i < 4; ++i) {
      ASSERT_EQ(Result::Ok, users.login(keys[i], 5, true, i ? 1700172800 : 1700000000, m));
      ASSERT_EQ(Result::Ok, users.delivered(keys[i], m->incarnation, 10 + i, 100 + i));
    }
  }
  // Header, three retained rows, new log, then both old-generation removals.
  for (int cut = 1; cut <= 7; ++cut) for (size_t prefix : {size_t(0), size_t(13), SIZE_MAX}) {
    FakeStorage fs = baseline;
    {
      HistoryMembers users(fs); ASSERT_EQ(Result::Ok, users.begin(room));
      fs.operations = 0; fs.failAt = cut; fs.shortWrite = prefix; PurgeCounts n;
      Result r = users.forgetInactive(1, true, 1700172800, n);
      if (r == Result::Ok) { EXPECT_EQ(1, n.removed); EXPECT_EQ(3, users.count()); }
      else EXPECT_EQ(0, n.removed);
    }
    fs.failAt = -1; HistoryMembers reboot(fs); ASSERT_EQ(Result::Ok, reboot.begin(room));
    EXPECT_TRUE(reboot.count() == 3 || reboot.count() == 4);
    EXPECT_EQ(reboot.count() == 4, reboot.find(keys[0]) != nullptr);
    for (unsigned i = 1; i < 4; ++i) {
      const Member* retained = reboot.find(keys[i]); ASSERT_NE(nullptr, retained);
      EXPECT_EQ(10u + i, retained->delivered); EXPECT_EQ(100u + i, retained->deliveredTimestamp);
      EXPECT_EQ(5u, retained->joinFloor); EXPECT_EQ(1700172800u, retained->lastLogin);
    }
  }
}
TEST(HistoryMembers, MiddleCorruptionUnknownSchemaAndForeignIdentityFailClosed) {
  FakeStorage baseline; uint8_t room[32] = {1}, key[32] = {2}; Member* m;
  {
    HistoryMembers users(baseline); ASSERT_EQ(Result::Ok, users.begin(room));
    ASSERT_EQ(Result::Ok, users.login(key, 5, true, 1700000000, m));
    ASSERT_EQ(Result::Ok, users.delivered(key, m->incarnation, 10, 100));
  }
  for (unsigned fault = 0; fault < 3; ++fault) {
    FakeStorage fs = baseline; auto& log = fs.files["/rhu_l_00000001"];
    if (fault == 0) log[HeaderBytes + 10] ^= 1; // Bad middle record must not be sealed as a tail.
    if (fault == 1) { log[HeaderBytes + 4] = 2; put32(log.data() + HeaderBytes + 120, crc32(log.data() + HeaderBytes, 120)); }
    if (fault == 2) { log[8] ^= 1; put32(log.data() + 60, crc32(log.data(), 60)); }
    auto before = fs.files; HistoryMembers reboot(fs);
    EXPECT_EQ(Result::Recovery, reboot.begin(room)); EXPECT_EQ(State::Recovery, reboot.state());
    EXPECT_EQ(before, fs.files);
  }
}
TEST(HistoryClock, TrustedAnchorWrapBackwardCorrectionAndInactiveBoundaries) {
  HistoryClock clock; uint64_t now; EXPECT_FALSE(clock.now(0, now)); EXPECT_FALSE(clock.set(0, 0));
  ASSERT_TRUE(clock.set(1700000000, UINT32_MAX - 500ull)); ASSERT_TRUE(clock.now(UINT32_MAX + 1500ull, now)); EXPECT_EQ(1700000002u, now);
  EXPECT_FALSE(clock.now(UINT32_MAX - 1000ull, now)); ASSERT_TRUE(clock.set(1600000000, UINT32_MAX + 1500ull));
  ASSERT_TRUE(clock.now(UINT32_MAX + 2500ull, now)); EXPECT_EQ(1600000001u, now);
  FakeStorage fs; uint8_t room[32] = {1}, a[32] = {2}, b[32] = {3}, c[32] = {4}; Member* m;
  HistoryMembers users(fs); ASSERT_EQ(Result::Ok, users.begin(room));
  ASSERT_EQ(Result::Ok, users.login(a, 1, true, 1700000000, m));
  ASSERT_EQ(Result::Ok, users.login(b, 1, false, 0, m));
  ASSERT_EQ(Result::Ok, users.login(c, 1, true, 1800000000, m)); PurgeCounts counts;
  EXPECT_EQ(Result::ClockUnset, users.forgetInactive(1, false, 1700086400, counts)); EXPECT_EQ(3, users.count());
  ASSERT_EQ(Result::Ok, users.forgetInactive(1, true, 1700086399, counts)); EXPECT_EQ(0, counts.removed);
  ASSERT_EQ(Result::Ok, users.forgetInactive(1, true, 1700086400, counts)); EXPECT_EQ(1, counts.removed); EXPECT_EQ(1, counts.unknown); EXPECT_EQ(1, counts.future);
  EXPECT_EQ(Result::Invalid, users.forgetInactive(36501, true, 1700086400, counts));
}
struct PlaybackFixture : ::testing::Test {
  FakeStorage fs;
  uint8_t room[32] = {1}, user[32] = {2}, author[32] = {3};
  RoomHistory history{fs}; HistoryMembers members{fs}; HistoryPlayback playback{history, members}; Member* m = nullptr;
  void SetUp() override {
    ASSERT_EQ(Result::Ok, history.begin(room)); ASSERT_EQ(Result::Ok, members.begin(room));
    ASSERT_EQ(Result::Ok, members.login(user, 0, false, 0, m));
  }
  void posts(int n, bool own = false) {
    for (int i = 0; i < n; ++i) {
      Post p; ASSERT_EQ(Result::Ok, history.append(own ? user : author, uint32_t(history.highWater() + 1), "message", 0, 100, p));
    }
    history.tick(6000);
  }
  void acceptNext(uint64_t sequence) {
    Post p; ASSERT_EQ(Result::Ok, playback.next(user, p, 6000)); EXPECT_EQ(sequence, p.sequence);
    playback.pending(user, p, uint32_t(sequence + 1)); EXPECT_EQ(Result::Ok, playback.acknowledge(user, uint32_t(sequence + 1)));
  }
};
TEST_F(PlaybackFixture, Newest100ChronologicalWithNoEndlessBatchesAndLiveExtra) {
  posts(1500); m = members.find(user); ASSERT_EQ(Result::Ok, members.delivered(user, m->incarnation, 1000, history.at(999).timestamp));
  ASSERT_EQ(Result::Ok, playback.login(*members.find(user), 0, 100)); EXPECT_EQ(100, playback.count(user));
  for (uint64_t seq = 1401; seq <= 1500; ++seq) { playback.keepAlive(user, 0); acceptNext(seq); }
  EXPECT_EQ(0, playback.count(user)); posts(2); EXPECT_EQ(2, playback.count(user)); acceptNext(1501); acceptNext(1502);
  ASSERT_EQ(Result::Ok, playback.login(*members.find(user), 0, 2000)); EXPECT_EQ(0, playback.count(user));
}
TEST_F(PlaybackFixture, OwnAuthorExclusionBeforeCapAndCountSaturation) {
  posts(200); posts(120, true); ASSERT_EQ(Result::Ok, playback.login(*members.find(user), 0, 100));
  EXPECT_EQ(100, playback.count(user)); acceptNext(101);
  ASSERT_EQ(Result::Ok, playback.login(*members.find(user), 0, 2000)); EXPECT_EQ(99, playback.count(user));
  EXPECT_EQ(255, HistoryPlayback::wireCount(255)); EXPECT_EQ(255, HistoryPlayback::wireCount(256)); EXPECT_EQ(255, HistoryPlayback::wireCount(2000));
}
TEST_F(PlaybackFixture, TrulyNewPurgedRejoiningAndStaleAck) {
  posts(100); uint8_t other[32] = {4}; Member* fresh;
  ASSERT_EQ(Result::Ok, members.login(other, history.highWater(), false, 0, fresh));
  ASSERT_EQ(Result::Ok, playback.login(*fresh, 0, 2000)); EXPECT_EQ(0, playback.count(other));
  ASSERT_EQ(Result::Ok, playback.login(*members.find(user), 0, 100)); Post pending;
  ASSERT_EQ(Result::Ok, playback.next(user, pending, 6000)); playback.pending(user, pending, 123);
  PurgeCounts counts; ASSERT_EQ(Result::Ok, members.forget("020000000000", counts)); playback.reconcile();
  EXPECT_EQ(Result::NotFound, playback.acknowledge(user, 123)); posts(20);
  ASSERT_EQ(Result::Ok, members.login(user, history.highWater(), false, 0, m));
  ASSERT_EQ(Result::Ok, playback.login(*m, 0, 2000)); EXPECT_EQ(0, playback.count(user)); posts(1); acceptNext(121);
}
TEST_F(PlaybackFixture, PendingKeepAliveHintsAndFailedDurableAckDoNotSkip) {
  posts(20); ASSERT_EQ(Result::Ok, playback.login(*m, 0, 20)); Post p;
  ASSERT_EQ(Result::Ok, playback.next(user, p, 6000)); playback.pending(user, p, 77);
  playback.keepAlive(user, 0); EXPECT_EQ(77u, playback.session(user)->ack);
  playback.keepAlive(user, UINT32_MAX); EXPECT_EQ(20, playback.count(user));
  EXPECT_EQ(Result::NotFound, playback.acknowledge(user, 76));
  fs.operations = 0; fs.failAt = 1; fs.shortWrite = 10;
  EXPECT_EQ(Result::Io, playback.acknowledge(user, 77)); EXPECT_EQ(0u, members.find(user)->delivered); EXPECT_EQ(77u, playback.session(user)->ack);
  fs.failAt = -1;
  HistoryMembers recovered(fs); ASSERT_EQ(Result::Ok, recovered.begin(room)); EXPECT_EQ(0u, recovered.find(user)->delivered);
}
TEST_F(PlaybackFixture, TwoClientsAclEvictionRetentionAndSnapshotLivePosts) {
  uint8_t other[32] = {4}; Member* another; ASSERT_EQ(Result::Ok, members.login(other, 0, false, 0, another));
  posts(2000); ASSERT_EQ(Result::Ok, playback.login(*members.find(user), 0, 2000));
  ASSERT_EQ(Result::Ok, playback.login(*members.find(other), 0, 1));
  Post p; ASSERT_EQ(Result::Ok, playback.next(user, p, 6000)); playback.pending(user, p, 8);
  posts(100); EXPECT_EQ(2000, playback.count(user)); EXPECT_EQ(101, playback.count(other));
  playback.timeout(user); ASSERT_EQ(Result::Ok, playback.next(user, p, 6000)); EXPECT_EQ(101u, p.sequence); EXPECT_EQ(1u, playback.expired);
  playback.retainSessions([&](const uint8_t* key) { return !memcmp(key, other, 32); });
  EXPECT_EQ(nullptr, playback.session(user)); EXPECT_NE(nullptr, playback.session(other)); EXPECT_EQ(2, members.count());
}
TEST_F(PlaybackFixture, FreshDelayUsesMonotonicClockEvenWhenWireTimeIsInFuture) {
  Post p; ASSERT_EQ(Result::Ok, history.append(author, 1, "future", 0, 2000000000, p, UINT32_MAX - 1000));
  ASSERT_EQ(Result::Ok, playback.login(*m, 0, 100));
  EXPECT_EQ(Result::NotFound, playback.next(user, p, UINT32_MAX - 900));
  EXPECT_EQ(Result::Ok, playback.next(user, p, 5000));
  { RoomHistory reboot(fs); ASSERT_EQ(Result::Ok, reboot.begin(room)); HistoryPlayback restored(reboot, members);
    ASSERT_EQ(Result::Ok, restored.login(*m, 0, 100)); EXPECT_EQ(Result::Ok, restored.next(user, p, 0)); }
}
TEST_F(PlaybackFixture, ReceiveCommitDuplicateRestartFailuresAndPermissions) {
  uint32_t last = 10; Post p;
  fs.operations = 0; fs.failAt = 3; fs.shortWrite = 0;
  EXPECT_EQ(Result::Io, acceptSubmission(history, author, 11, "one", last, true, 100, 0, p)); EXPECT_EQ(10u, last);
  fs.failAt = -1; RoomHistory recovered(fs); ASSERT_EQ(Result::Ok, recovered.begin(room));
  EXPECT_EQ(Result::Ok, acceptSubmission(recovered, author, 11, "one", last, true, 100, 0, p)); EXPECT_EQ(11u, last);
  last = 30; EXPECT_EQ(Result::Duplicate, acceptSubmission(recovered, author, 11, "one", last, true, 100, 0, p));
  EXPECT_EQ(Result::Conflict, acceptSubmission(recovered, author, 11, "changed", last, true, 100, 0, p));
  EXPECT_EQ(Result::Invalid, acceptSubmission(recovered, author, 31, "guest", last, false, 100, 0, p));
  EXPECT_EQ(Result::Invalid, acceptSubmission(recovered, author, 25, "replay", last, true, 100, 0, p));
  EXPECT_EQ(1, recovered.count()); EXPECT_EQ(30u, last);
}
TEST_F(PlaybackFixture, ActualAdminCommandsBoundsParsingPaginationAndArchivePreservation) {
  HistoryClock clock; HistoryAdmin cli(history, members, clock); uint16_t cap = 100;
  struct Buffer { char reply[157]; char sentinel[4] = {'x','x','x','x'}; } b;
  auto send = [&](const char* command) { EXPECT_TRUE(cli.handle(command, b.reply, sizeof(b.reply), cap, 1000, [](){ return true; })); EXPECT_EQ('x', b.sentinel[0]); return std::string(b.reply); };
  posts(10); auto archive = fs.files["/rh_s_00000001"];
  EXPECT_EQ("OK", send("set history.playback 2000")); EXPECT_EQ(2000, cap);
  EXPECT_EQ("ERR invalid value", send("set history.playback 2000x")); EXPECT_EQ(2000, cap);
  EXPECT_EQ("ERR invalid value", send("history.users.purge")); EXPECT_EQ(1, members.count());
  EXPECT_EQ("ERR invalid value", send("history.users.purge.inactive 0"));
  EXPECT_EQ("ERR clock not set", send("history.users.purge.inactive 1"));
  EXPECT_EQ("OK", send("history.clock.set 1700000000")); EXPECT_NE(std::string::npos, send("get history.clock").find("trusted"));
  EXPECT_NE(std::string::npos, send("history.users.list").find("page=0/1"));
  uint8_t another[32] = {6}; ASSERT_EQ(Result::Ok, members.login(another, 10, true, 1700000000, m));
  EXPECT_EQ(0u, send("history.users.list 1").find("ERR list changed"));
  EXPECT_EQ("OK removed=2", send("history.users.purge all")); EXPECT_EQ(0, members.count()); EXPECT_EQ(archive, fs.files["/rh_s_00000001"]);
  EXPECT_EQ(10, history.count());
}
TEST_F(PlaybackFixture, ProductionPlainReceiveOnlyEmitsAckAfterVerifiedCommitAndExactRetry) {
  uint8_t wire[184] = {}; uint32_t sender = 11, replay = 10; memcpy(wire, &sender, 4); strcpy((char*)wire + 5, "one");
  int acks = 0; Post post;
  auto ack = [&](uint32_t hash) {
    ++acks; EXPECT_EQ(1, history.count()); Post found;
    EXPECT_EQ(Result::Duplicate, history.findSubmission(author, sender, "one", found));
    uint32_t expected; mesh::Utils::sha256((uint8_t*)&expected, 4, wire, 8, author, 32); EXPECT_EQ(expected, hash);
  };
  fs.operations = 0; fs.failAt = 3; fs.shortWrite = 0;
  EXPECT_EQ(Result::Io, receivePlain(history, author, 2, replay, wire, 8, 100, 0, post, ack)); EXPECT_EQ(0, acks); EXPECT_EQ(10u, replay);
  fs.failAt = -1; ASSERT_EQ(Result::Ok, history.begin(room));
  EXPECT_EQ(Result::Ok, receivePlain(history, author, 2, replay, wire, 8, 100, 0, post, ack)); EXPECT_EQ(1, acks); EXPECT_EQ(11u, replay);
  replay = 40; wire[4] = 1; EXPECT_EQ(Result::Duplicate, receivePlain(history, author, 2, replay, wire, 8, 100, 0, post, ack)); EXPECT_EQ(2, acks);
  EXPECT_EQ(Result::Invalid, receivePlain(history, author, 0, replay, wire, 8, 100, 0, post, ack)); EXPECT_EQ(2, acks);
  EXPECT_EQ(Result::Invalid, receivePlain(history, author, 1, replay, wire, 8, 100, 0, post, ack)); EXPECT_EQ(2, acks);
  EXPECT_EQ(Result::Invalid, receivePlain(history, author, 5, replay, wire, 8, 100, 0, post, ack)); EXPECT_EQ(2, acks);
  wire[5] = 'X'; EXPECT_EQ(Result::Conflict, receivePlain(history, author, 2, replay, wire, 8, 100, 0, post, ack)); EXPECT_EQ(2, acks);
}
TEST_F(PlaybackFixture, PrivateRoomHistoryNeverGrantsAuthAndCommandsLookingLikePostsStayText) {
  uint8_t perm;
  EXPECT_FALSE(authenticateLogin("wrong", false, 0, "admin", "secret", false, perm));
  EXPECT_FALSE(authenticateLogin("", false, 0, "admin", "secret", false, perm));
  // The member registry is intentionally not an input to authenticateLogin.
  ASSERT_NE(nullptr, members.find(user)); EXPECT_FALSE(authenticateLogin("wrong", false, 0, "admin", "secret", false, perm));
  EXPECT_TRUE(authenticateLogin("secret", false, 0, "admin", "secret", false, perm)); EXPECT_EQ(2, perm);
  EXPECT_TRUE(authenticateLogin("admin", false, 0, "admin", "secret", false, perm)); EXPECT_EQ(3, perm);
  EXPECT_TRUE(authenticateLogin("wrong", false, 0, "admin", "secret", true, perm)); EXPECT_EQ(0, perm);
  EXPECT_TRUE(authenticateLogin("", true, 2, "admin", "secret", false, perm)); EXPECT_EQ(2, perm);
  uint8_t data[184] = {}; uint32_t timestamp = 1, replay = 0; memcpy(data, &timestamp, 4);
  strcpy((char*)data + 5, "history.users.purge all"); Post p;
  ASSERT_EQ(Result::Ok, receivePlain(history, user, 2, replay, data, 5 + strlen((char*)data + 5), 100, 0, p, [](uint32_t){}));
  EXPECT_EQ(1, members.count()); EXPECT_STREQ("history.users.purge all", p.text);
  uint8_t cliFlag = 2; data[4] = cliFlag << 2;
  EXPECT_EQ(Result::Invalid, receivePlain(history, user, 2, replay, data, 30, 100, 0, p, [](uint32_t){}));
}
int main(int argc, char** argv) { ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS(); }
