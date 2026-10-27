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
bool failSync = false;
uint32_t now = 0;
uint32_t millis() { return now; }
using std::min;

struct FakeSD {
  bool exists(const char *name) { return files.count(name) != 0; }
  bool remove(const char *name) { return files.erase(name) != 0; }
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
  bool openNext(FsFile *, int) { return false; }
  void getName(char *buffer, size_t count) { snprintf(buffer, count, "%s", name.c_str()); }
  bool isDirectory() { return name == "/"; }
};

struct FakeUsb {
  void sendSysEx(int, uint8_t *) {}
  void send_now() {}
  void read() {}
  void setHandleSysEx(void (*)(const uint8_t *, uint16_t, bool)) {}
} usbMIDI;

bool hasLoaded = true, playingMidiFile = false, stoppingMidiFile = false, activePresetValid = true;
int currentPreset = 0, presetCount = 2, prefetchedPresetIndex = -1;
bool prefetchRequested = false;
StaticJsonDocument<presetDocumentCapacity> prefetchedPresetDoc;
char presetList[maxPresetListSize][maxPresetNameLength] = {"1.json", "2.json"};
int activations = 0, triggers = 0;
void changePreset() { ++activations; }
void executeSwitchLogic(int) { ++triggers; }
bool hasJsonExtension(const char *name) {
  return strlen(name) >= 5 && strcmp(name + strlen(name) - 5, ".json") == 0;
}

#define USB_MIDI_SERIAL
#include "live_editor.h"

class LiveEditor : public ::testing::Test {
protected:
  const std::string original = R"({"Name":"Original"})";
  const std::string updated = R"({"Name":"Updated","Switch1":{"TapTempo":true}})";

  void SetUp() override {
    files = {{"1.json", original}, {"2.json", original}, {"song.mid", "MIDI"}};
    failRename = 0;
    failSync = false;
    now = 0;
    liveExpectedBytes = 0;
    liveRequestLength = 0;
    liveResponding = false;
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

} // namespace
