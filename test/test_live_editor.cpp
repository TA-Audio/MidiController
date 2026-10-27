#include <gtest/gtest.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <map>
#include <string>
#include "performance_validation.h"

namespace {

constexpr int O_READ = 1, O_RDONLY = 1, O_RDWR = 2, O_WRONLY = 4, O_CREAT = 8, O_TRUNC = 16;
std::map<std::string, std::string> files;
int failRename = 0;
int failRemove = 0;
bool failSync = false;
uint32_t now = 0;
uint32_t millis() { return now; }
using std::min;

struct FakeSD {
  bool exists(const char *name) { return files.count(name) != 0; }
  bool remove(const char *name) {
    if (failRemove && --failRemove == 0) return false;
    return files.erase(name) != 0;
  }
} SD;

class FsFile {
  std::string name;
  size_t position = 0;
public:
  bool open(const char *path, int mode) {
    name = path;
    position = 0;
    if (name == "/") return true;
    if (!files.count(name) && !(mode & O_CREAT)) return false;
    if (mode & O_TRUNC) files[name].clear();
    return true;
  }
  size_t fileSize() { return files[name].size(); }
  int read(char *buffer, size_t count) {
    const auto &text = files.at(name);
    count = min(count, text.size() - position);
    memcpy(buffer, text.data() + position, count);
    position += count;
    return static_cast<int>(count);
  }
  size_t write(const char *buffer, size_t count) {
    files[name].append(buffer, count);
    return count;
  }
  bool sync() { return !failSync; }
  bool rename(const char *target) {
    if (failRename && --failRename == 0) return false;
    if (files.count(target)) return false;
    files[target] = files.at(name);
    files.erase(name);
    name = target;
    return true;
  }
  void close() {}
  bool openNext(FsFile *directory, int mode) {
    if (directory->position >= files.size()) return false;
    auto iterator = files.begin();
    std::advance(iterator, directory->position++);
    return open(iterator->first.c_str(), mode);
  }
  void getName(char *buffer, size_t count) { snprintf(buffer, count, "%s", name.c_str()); }
  bool isDirectory() { return name == "/"; }
  bool getError() { return false; }
};

struct FakeUsb {
  void sendSysEx(int, uint8_t *) {}
  void send_now() {}
  void read() {}
  void setHandleSysEx(void (*)(const uint8_t *, uint16_t, bool)) {}
} usbMIDI;

bool hasLoaded = true, playingMidiFile = false, stoppingMidiFile = false, activePresetValid = true;
bool liveStorageFault = false, livePresetDetached = false;
bool livePresetHasSlot = true;
uint32_t activePresetSignature = 0;
int currentPreset = 0, presetCount = 2, prefetchedPresetIndex = -1;
bool prefetchRequested = false;
StaticJsonDocument<presetDocumentCapacity> prefetchedPresetDoc;
char presetList[maxPresetListSize][maxPresetNameLength] = {"1.json", "2.json"};
int activations = 0, triggers = 0;
void changePreset() { ++activations; livePresetDetached = false; livePresetHasSlot = true; }
void executeSwitchLogic(int) { ++triggers; }
bool hasJsonExtension(const char *name) {
  return strlen(name) >= 5 && strcmp(name + strlen(name) - 5, ".json") == 0;
}
void loadPresetList() {
  presetCount = 0;
  for (const auto &file : files) {
    if (hasJsonExtension(file.first.c_str())) snprintf(presetList[presetCount++], maxPresetNameLength, "%s", file.first.c_str());
  }
  qsort(presetList, presetCount, sizeof(presetList[0]), [](const void *a, const void *b) {
    return atoi(static_cast<const char *>(a)) - atoi(static_cast<const char *>(b));
  });
  prefetchedPresetIndex = -1;
  prefetchRequested = false;
}
void saveControllerStateNow() {}
void showError(const char *, const char *) {}

#define USB_MIDI_SERIAL
#include "live_editor.h"

class LiveEditor : public ::testing::Test {
protected:
  const std::string original = R"({"Name":"Original"})";
  const std::string updated = R"({"Name":"Updated","Switch1":{"TapTempo":true}})";

  void SetUp() override {
    files = {{"1.json", original}, {"2.json", original}, {"song.mid", "MIDI"}};
    failRename = 0;
    failRemove = 0;
    failSync = false;
    now = 0;
    liveExpectedBytes = 0;
    liveRequestLength = 0;
    liveResponding = false;
    liveSetActive = liveStorageFault = livePresetDetached = false;
    livePresetHasSlot = true;
    presetCount = 2;
    snprintf(presetList[0], maxPresetNameLength, "1.json");
    snprintf(presetList[1], maxPresetNameLength, "2.json");
    hasLoaded = activePresetValid = true;
    playingMidiFile = stoppingMidiFile = false;
    currentPreset = activations = triggers = 0;
  }

  bool request(uint8_t command, const std::string &text = "", int sequence = 0) {
    liveRequestLength = encodeLivePacket(liveRequest, 1, command, sequence, true,
      reinterpret_cast<const uint8_t *>(text.data()), text.size());
    EXPECT_GE(liveRequestLength, 0);
    serviceLiveRequest();
    liveResponding = false;
    return liveResponseCommand != 0x7f;
  }

  bool upload(const std::string &text) {
    const auto revision = liveRevision(files["1.json"].data(), files["1.json"].size());
    const std::string begin = "{\"name\":\"1.json\",\"size\":" + std::to_string(text.size()) +
      ",\"revision\":" + std::to_string(revision) + "}";
    if (!request(4, begin)) return false;
    for (size_t offset = 0; offset < text.size(); offset += liveChunkBytes) {
      if (!request(5, text.substr(offset, liveChunkBytes), offset / liveChunkBytes)) return false;
    }
    return true;
  }

  void expectClean() {
    EXPECT_FALSE(files.count(liveStageName));
    EXPECT_FALSE(files.count(liveBackupName));
    EXPECT_FALSE(files.count(liveJournalName));
    EXPECT_EQ(files["song.mid"], "MIDI");
    EXPECT_EQ(files["2.json"], original);
  }
};

TEST_F(LiveEditor, SavesExistingFileWithoutActivatingAndInvalidatesPrefetch) {
  prefetchedPresetIndex = 0;
  prefetchRequested = true;
  ASSERT_TRUE(upload(updated));
  ASSERT_TRUE(request(6)) << liveResponse;
  EXPECT_EQ(files["1.json"], updated);
  EXPECT_EQ(activations, 0);
  EXPECT_EQ(prefetchedPresetIndex, -1);
  EXPECT_FALSE(prefetchRequested);
  expectClean();
}

TEST_F(LiveEditor, ConflictingRevisionAndMalformedActionsCannotOverwrite) {
  EXPECT_FALSE(request(4, R"({"name":"1.json","size":20,"revision":0})"));
  EXPECT_EQ(files["1.json"], original);
  ASSERT_TRUE(upload(R"({"Switch1":{"Toggle":true,"TapTempo":true}})"));
  EXPECT_FALSE(request(6));
  EXPECT_EQ(files["1.json"], original);
  expectClean();
}

TEST_F(LiveEditor, RechecksRevisionAtCommit) {
  ASSERT_TRUE(upload(updated));
  files["1.json"] = R"({"Name":"External edit"})";
  EXPECT_FALSE(request(6));
  EXPECT_EQ(files["1.json"], R"({"Name":"External edit"})");
  expectClean();
}

TEST_F(LiveEditor, InterruptedRenameRestoresOriginal) {
  ASSERT_TRUE(upload(updated));
  failRename = 2;
  EXPECT_FALSE(request(6));
  EXPECT_EQ(files["1.json"], original);
  expectClean();
}

TEST_F(LiveEditor, FailedStageSyncRetainsOriginal) {
  ASSERT_TRUE(upload(updated));
  failSync = true;
  EXPECT_FALSE(request(6));
  EXPECT_EQ(files["1.json"], original);
  EXPECT_FALSE(files.count(liveBackupName));
  EXPECT_FALSE(files.count(liveJournalName));
  failSync = false;
  ASSERT_TRUE(recoverLiveSave());
  expectClean();
}

TEST_F(LiveEditor, RecoversBothSidesOfInterruptedReplacement) {
  files[liveStageName] = updated;
  files[liveJournalName] = "1.json";
  files[liveBackupName] = original;
  files.erase("1.json");
  ASSERT_TRUE(recoverLiveSave());
  EXPECT_EQ(files["1.json"], original);
  expectClean();
  files[liveStageName] = updated;
  files[liveJournalName] = "1.json";
  files[liveBackupName] = original;
  files["1.json"] = updated;
  ASSERT_TRUE(recoverLiveSave());
  EXPECT_EQ(files["1.json"], updated);
  expectClean();
}

TEST_F(LiveEditor, InvalidJournalWithoutBackupIsSafeButAmbiguousBackupIsPreserved) {
  files[liveJournalName] = "";
  files[liveStageName] = updated;
  ASSERT_TRUE(recoverLiveSave());
  EXPECT_EQ(files["1.json"], original);
  expectClean();
  files[liveBackupName] = original;
  EXPECT_FALSE(recoverLiveSave());
  EXPECT_EQ(files[liveBackupName], original);
}

TEST_F(LiveEditor, RejectsReorderedUploadAndExpiresIdleUpload) {
  ASSERT_TRUE(upload(updated));
  EXPECT_FALSE(request(5, "extra", 99));
  EXPECT_TRUE(request(9));
  EXPECT_FALSE(request(6));
  ASSERT_TRUE(upload(updated));
  now = 5000;
  serviceLiveEditor();
  EXPECT_FALSE(request(6));
  EXPECT_EQ(files["1.json"], original);
}

TEST_F(LiveEditor, PlaybackBlocksSavesButAllowsTestingTheActiveFileSwitch) {
  playingMidiFile = true;
  EXPECT_FALSE(request(3, R"({"name":"1.json"})"));
  EXPECT_TRUE(request(8, R"({"switch":1,"name":"1.json"})"));
  EXPECT_EQ(triggers, 1);
  EXPECT_FALSE(request(8, R"({"switch":1,"name":"2.json"})"));
}

TEST_F(LiveEditor, ActivationAndTestsRequireTheSelectedFilename) {
  EXPECT_FALSE(request(7, R"({"index":0,"name":"2.json"})"));
  EXPECT_EQ(activations, 0);
  EXPECT_TRUE(request(7, R"({"index":1,"name":"2.json"})"));
  EXPECT_EQ(activations, 1);
  EXPECT_EQ(currentPreset, 1);
  EXPECT_FALSE(request(8, R"({"switch":1,"name":"1.json"})"));
  EXPECT_EQ(triggers, 0);
}

TEST_F(LiveEditor, FullSetReordersAddsDeletesAndPreservesRuntimeAndMidi) {
  uint32_t revision = 0;
  ASSERT_TRUE(liveTreeRevision(revision));
  EXPECT_EQ(revision, 3620011878UL);
  ASSERT_TRUE(request(10, "{\"count\":3,\"revision\":" + std::to_string(revision) + "}")) << liveResponse;
  const std::string texts[] = {updated, original, R"({"Name":"New"})"};
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(request(4, "{\"index\":" + std::to_string(i) + ",\"size\":" + std::to_string(texts[i].size()) + "}")) << liveResponse;
    ASSERT_TRUE(request(5, texts[i]));
    ASSERT_TRUE(request(6)) << liveResponse;
    EXPECT_EQ(files["1.json"], original);
  }
  ASSERT_TRUE(request(11)) << liveResponse;
  EXPECT_EQ(files["1.json"], updated);
  EXPECT_EQ(files["2.json"], original);
  EXPECT_EQ(files["3.json"], texts[2]);
  EXPECT_EQ(files["song.mid"], "MIDI");
  EXPECT_EQ(presetCount, 3);
  EXPECT_EQ(activations, 0);
  EXPECT_TRUE(livePresetDetached);
  EXPECT_FALSE(request(8, R"({"switch":1,"name":"1.json"})"));
  EXPECT_TRUE(request(7, R"({"index":0,"name":"1.json"})"));
  EXPECT_TRUE(request(8, R"({"switch":1,"name":"1.json"})"));
}

TEST_F(LiveEditor, FullSetCanDeleteAllPresetsAndThenCreateFromEmpty) {
  uint32_t revision = 0;
  ASSERT_TRUE(liveTreeRevision(revision));
  ASSERT_TRUE(request(10, "{\"count\":0,\"revision\":" + std::to_string(revision) + "}"));
  ASSERT_TRUE(request(11)) << liveResponse;
  EXPECT_EQ(presetCount, 0);
  EXPECT_EQ(files.size(), 1u);
  ASSERT_TRUE(liveTreeRevision(revision));
  ASSERT_TRUE(request(10, "{\"count\":1,\"revision\":" + std::to_string(revision) + "}"));
  ASSERT_TRUE(request(4, "{\"index\":0,\"size\":" + std::to_string(updated.size()) + "}"));
  ASSERT_TRUE(request(5, updated));
  ASSERT_TRUE(request(6));
  ASSERT_TRUE(request(11)) << liveResponse;
  EXPECT_EQ(files["1.json"], updated);
  EXPECT_EQ(files["song.mid"], "MIDI");
  EXPECT_EQ(presetCount, 1);
}

TEST_F(LiveEditor, FullSetChecksRevisionBeforeBeginAndCommit) {
  uint32_t revision = 0;
  ASSERT_TRUE(liveTreeRevision(revision));
  EXPECT_FALSE(request(10, "{\"count\":0,\"revision\":" + std::to_string(revision ^ 1) + "}"));
  ASSERT_TRUE(request(10, "{\"count\":0,\"revision\":" + std::to_string(revision) + "}"));
  files["2.json"] = updated;
  EXPECT_FALSE(request(11));
  EXPECT_EQ(files["1.json"], original);
  EXPECT_EQ(files["2.json"], updated);
  EXPECT_TRUE(request(9));
}

TEST_F(LiveEditor, FullSetRejectsOutOfOrderStagesAndAbortsWithoutMutatingOriginals) {
  uint32_t revision = 0;
  ASSERT_TRUE(liveTreeRevision(revision));
  EXPECT_FALSE(request(10, "{\"count\":151,\"revision\":" + std::to_string(revision) + "}"));
  ASSERT_TRUE(request(10, "{\"count\":2,\"revision\":" + std::to_string(revision) + "}"));
  EXPECT_FALSE(request(4, R"({"index":1,"size":20})"));
  EXPECT_FALSE(request(11));
  EXPECT_TRUE(request(9));
  EXPECT_EQ(files["1.json"], original);
  expectClean();
}

TEST_F(LiveEditor, FullSetRollsBackInterruptedBackupRenames) {
  LiveSetManifest manifest = {};
  manifest.oldCount = 2;
  manifest.newCount = 1;
  strcpy(manifest.names[0], "1.json");
  strcpy(manifest.names[1], "2.json");
  files["syn001.tmp"] = updated;
  ASSERT_TRUE(publishLiveSetManifest(manifest));
  ASSERT_TRUE(liveRename("1.json", "syb001.bak"));
  ASSERT_TRUE(recoverLiveSet());
  EXPECT_EQ(files["1.json"], original);
  EXPECT_EQ(files["2.json"], original);
  EXPECT_FALSE(files.count("syn001.tmp"));
  EXPECT_FALSE(files.count(liveSetJournal));
}

TEST_F(LiveEditor, FullSetRollsForwardPartiallyInstalledNumberedSwaps) {
  LiveSetManifest manifest = {};
  manifest.oldCount = 2;
  manifest.newCount = 2;
  strcpy(manifest.names[0], "1.json");
  strcpy(manifest.names[1], "2.json");
  files["syn001.tmp"] = updated;
  files["syn002.tmp"] = R"({"Name":"Other"})";
  ASSERT_TRUE(publishLiveSetManifest(manifest));
  ASSERT_TRUE(liveRename("1.json", "syb001.bak"));
  ASSERT_TRUE(liveRename("2.json", "syb002.bak"));
  files[liveSetReady] = "";
  ASSERT_TRUE(liveRename("syn001.tmp", "1.json"));
  ASSERT_TRUE(recoverLiveSet());
  EXPECT_EQ(files["1.json"], updated);
  EXPECT_EQ(files["2.json"], R"({"Name":"Other"})");
  EXPECT_EQ(files["song.mid"], "MIDI");
  EXPECT_FALSE(files.count(liveSetJournal));
  EXPECT_FALSE(files.count(liveSetReady));
  EXPECT_FALSE(files.count("syb001.bak"));
  EXPECT_TRUE(recoverLiveSet());
}

TEST_F(LiveEditor, CorruptSetManifestAndOrphanBackupsArePreserved) {
  files[liveSetJournal] = "partial";
  files["syb001.bak"] = original;
  EXPECT_FALSE(recoverLiveSet());
  EXPECT_EQ(files["syb001.bak"], original);
  files.erase(liveSetJournal);
  EXPECT_FALSE(recoverLiveSet());
  EXPECT_EQ(files["syb001.bak"], original);
}

TEST_F(LiveEditor, SetRevisionDetectsAddedFilesAndUnreadableOrOversizedPresets) {
  uint32_t revision = 0;
  files["extra.json"] = original;
  EXPECT_FALSE(liveTreeRevision(revision));
  files.erase("extra.json");
  files["1.json"] = std::string(8193, 'x');
  EXPECT_FALSE(liveTreeRevision(revision));
}

TEST_F(LiveEditor, EveryRenameFailureRecoversOneCompleteSet) {
  for (int failure = 1; failure <= 6; ++failure) {
    SetUp();
    LiveSetManifest manifest = {};
    manifest.oldCount = 2;
    manifest.newCount = 2;
    strcpy(manifest.names[0], "1.json");
    strcpy(manifest.names[1], "2.json");
    files["syn001.tmp"] = updated;
    files["syn002.tmp"] = R"({"Name":"Second new"})";
    failRename = failure;
    EXPECT_FALSE(installLiveSet(manifest)) << failure;
    ASSERT_TRUE(recoverLiveSet()) << failure;
    const bool oldSet = files["1.json"] == original && files["2.json"] == original;
    const bool newSet = files["1.json"] == updated && files["2.json"] == R"({"Name":"Second new"})";
    EXPECT_TRUE(oldSet || newSet) << failure;
    EXPECT_EQ(files.size(), 3u) << failure;
    EXPECT_EQ(files["song.mid"], "MIDI");
  }
}

TEST_F(LiveEditor, FullSetReplacesOriginalNamedFilesRatherThanLeavingDuplicates) {
  files.erase("1.json");
  files.erase("2.json");
  files["Lead.json"] = original;
  files["Clean.json"] = updated;
  strcpy(presetList[0], "Clean.json");
  strcpy(presetList[1], "Lead.json");
  uint32_t revision = 0;
  ASSERT_TRUE(liveTreeRevision(revision));
  ASSERT_TRUE(request(10, "{\"count\":1,\"revision\":" + std::to_string(revision) + "}"));
  ASSERT_TRUE(request(4, "{\"index\":0,\"size\":" + std::to_string(updated.size()) + "}"));
  ASSERT_TRUE(request(5, updated));
  ASSERT_TRUE(request(6));
  ASSERT_TRUE(request(11)) << liveResponse;
  EXPECT_EQ(files["1.json"], updated);
  EXPECT_FALSE(files.count("Lead.json"));
  EXPECT_FALSE(files.count("Clean.json"));
  EXPECT_EQ(files.size(), 2u);
}

TEST_F(LiveEditor, IdleSetUploadExpiresWithoutChangingOriginals) {
  uint32_t revision = 0;
  ASSERT_TRUE(liveTreeRevision(revision));
  ASSERT_TRUE(request(10, "{\"count\":1,\"revision\":" + std::to_string(revision) + "}"));
  ASSERT_TRUE(request(4, "{\"index\":0,\"size\":" + std::to_string(updated.size()) + "}"));
  ASSERT_TRUE(request(5, updated));
  ASSERT_TRUE(request(6));
  now = 30000;
  serviceLiveEditor();
  EXPECT_FALSE(liveSetActive);
  EXPECT_FALSE(files.count("syn001.tmp"));
  EXPECT_EQ(files["1.json"], original);
}

TEST_F(LiveEditor, FullSetTracksTheActivePresetAcrossReorderingWithoutSendingMessages) {
  uint32_t revision = 0;
  ASSERT_TRUE(liveTreeRevision(revision));
  ASSERT_TRUE(request(10, "{\"count\":2,\"revision\":" + std::to_string(revision) + "}"));
  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(request(4, "{\"index\":" + std::to_string(i) + ",\"size\":" + std::to_string(updated.size()) +
      ",\"source\":\"" + std::to_string(2 - i) + ".json\"}"));
    ASSERT_TRUE(request(5, updated));
    ASSERT_TRUE(request(6));
  }
  ASSERT_TRUE(request(11));
  EXPECT_EQ(currentPreset, 1);
  EXPECT_TRUE(livePresetDetached);
  EXPECT_EQ(activations, 0);
  EXPECT_EQ(triggers, 0);
}

TEST_F(LiveEditor, FullSetSupportsExactlyOneHundredAndFiftyPresets) {
  uint32_t revision = 0;
  ASSERT_TRUE(liveTreeRevision(revision));
  ASSERT_TRUE(request(10, "{\"count\":150,\"revision\":" + std::to_string(revision) + "}"));
  for (int i = 0; i < 150; ++i) {
    ASSERT_TRUE(request(4, "{\"index\":" + std::to_string(i) + ",\"size\":" + std::to_string(updated.size()) + "}"));
    ASSERT_TRUE(request(5, updated));
    ASSERT_TRUE(request(6));
  }
  ASSERT_TRUE(request(11)) << liveResponse;
  EXPECT_EQ(presetCount, 150);
  EXPECT_EQ(files["150.json"], updated);
  EXPECT_EQ(files.size(), 151u);
  EXPECT_EQ(files["song.mid"], "MIDI");
}

TEST_F(LiveEditor, CleanupFailuresCanResumeWithoutRemovingInstalledPresets) {
  for (int failure = 1; failure <= 4; ++failure) {
    SetUp();
    LiveSetManifest manifest = {};
    manifest.oldCount = 2;
    manifest.newCount = 1;
    strcpy(manifest.names[0], "1.json");
    strcpy(manifest.names[1], "2.json");
    files["syn001.tmp"] = updated;
    failRemove = failure;
    EXPECT_FALSE(installLiveSet(manifest)) << failure;
    ASSERT_TRUE(recoverLiveSet()) << failure;
    EXPECT_EQ(files["1.json"], updated);
    EXPECT_FALSE(files.count("2.json"));
    EXPECT_EQ(files.size(), 2u);
    EXPECT_EQ(files["song.mid"], "MIDI");
  }
}

TEST_F(LiveEditor, StagedCorruptionIsRejectedBeforeOriginalsAreTouched) {
  uint32_t revision = 0;
  ASSERT_TRUE(liveTreeRevision(revision));
  ASSERT_TRUE(request(10, "{\"count\":1,\"revision\":" + std::to_string(revision) + "}"));
  ASSERT_TRUE(request(4, "{\"index\":0,\"size\":" + std::to_string(updated.size()) + "}"));
  ASSERT_TRUE(request(5, updated));
  ASSERT_TRUE(request(6));
  files["syn001.tmp"] = R"({"Name":"Corrupted stage"})";
  EXPECT_FALSE(request(11));
  EXPECT_EQ(files["1.json"], original);
  EXPECT_EQ(files["2.json"], original);
  EXPECT_FALSE(files.count(liveSetJournal));
  EXPECT_TRUE(request(9));
}

TEST_F(LiveEditor, RecoveryPreservesBackupsWhenAStagedPresetIsCorrupt) {
  LiveSetManifest manifest = {};
  manifest.oldCount = 2;
  manifest.newCount = 1;
  strcpy(manifest.names[0], "1.json");
  strcpy(manifest.names[1], "2.json");
  files["syn001.tmp"] = updated;
  ASSERT_TRUE(publishLiveSetManifest(manifest));
  ASSERT_TRUE(liveRename("1.json", "syb001.bak"));
  ASSERT_TRUE(liveRename("2.json", "syb002.bak"));
  files[liveSetReady] = "";
  files["syn001.tmp"] = "corrupted";
  EXPECT_FALSE(recoverLiveSet());
  EXPECT_FALSE(files.count("1.json"));
  EXPECT_EQ(files["syb001.bak"], original);
  EXPECT_EQ(files["syb002.bak"], original);
  EXPECT_TRUE(files.count(liveSetJournal));
}

TEST_F(LiveEditor, FullSetPreservesMultichunkUtf8JsonExactly) {
  const std::string text = std::string("{\"Name\":\"Caf\xc3\xa9\",\"Description\":\"") + std::string(500, 'a') + "\"}";
  uint32_t revision = 0;
  ASSERT_TRUE(liveTreeRevision(revision));
  ASSERT_TRUE(request(10, "{\"count\":1,\"revision\":" + std::to_string(revision) + "}"));
  ASSERT_TRUE(request(4, "{\"index\":0,\"size\":" + std::to_string(text.size()) + "}"));
  for (size_t offset = 0; offset < text.size(); offset += liveChunkBytes) {
    ASSERT_TRUE(request(5, text.substr(offset, liveChunkBytes), offset / liveChunkBytes));
  }
  ASSERT_TRUE(request(6)) << liveResponse;
  EXPECT_EQ(files["syn001.tmp"], text);
  EXPECT_EQ(files["1.json"], original);
  ASSERT_TRUE(request(11)) << liveResponse;
  EXPECT_EQ(files["1.json"], text);
}

} // namespace
