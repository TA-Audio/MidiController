#include <gtest/gtest.h>
#include <ArduinoJson.h>
#include "toggle_state.h"
#include "midi_mock.h"

struct MockEeprom {
  uint8_t bytes[64];
  std::vector<StoredControllerState> snapshots;

  MockEeprom() { memset(bytes, 0xff, sizeof(bytes)); }

  template <typename T>
  void get(int address, T &value) {
    memcpy(&value, bytes + address, sizeof(T));
  }

  template <typename T>
  void put(int address, const T &value) {
    memcpy(bytes + address, &value, sizeof(T));
    StoredControllerState snapshot;
    get(controllerStateEepromAddress, snapshot);
    snapshots.push_back(snapshot);
  }
};

TEST(StoredControllerState, WritesSnapshotBeforeMarkerAndLegacyPosition) {
  MockEeprom eeprom;
  const auto state = makeControllerState(1, 200, 5);
  writeControllerState(eeprom, state);
  ASSERT_EQ(eeprom.snapshots.size(), 3u);
  EXPECT_FALSE(validControllerState(eeprom.snapshots[0], 2));
  EXPECT_TRUE(validControllerState(eeprom.snapshots[1], 2));
  EXPECT_EQ(eeprom.snapshots[1].toggleMask, 5);
  int legacy;
  eeprom.get(presetEepromAddress, legacy);
  EXPECT_EQ(legacy, 1);
  writeControllerState(eeprom, state);
  EXPECT_EQ(eeprom.snapshots.size(), 3u);
  writeControllerState(eeprom, makeControllerState(0, 100, 0));
  StoredControllerState restored;
  eeprom.get(controllerStateEepromAddress, restored);
  EXPECT_TRUE(validControllerState(restored, 2));
  EXPECT_EQ(restored.presetIndex, 0);
  EXPECT_EQ(restored.toggleMask, 0);
}

TEST(StoredControllerState, ValidatesAllMasksAndRejectsInvalidRecords) {
  for (uint8_t mask = 0; mask < 8; ++mask) {
    auto state = makeControllerState(2, 1234, mask);
    EXPECT_TRUE(validControllerState(state, 3));
    EXPECT_FALSE(validControllerState(state, 2));
    EXPECT_FALSE(validControllerState(state, 0));
    EXPECT_EQ(consumeBootToggleMask(state, 2, 1234, 3), mask);
    EXPECT_EQ(consumeBootToggleMask(state, 2, 1234, 3), 0);
  }
  EXPECT_FALSE(validControllerState(StoredControllerState{}, 150));
  EXPECT_FALSE(validControllerState(makeControllerState(-1, 1, 0), 150));
  EXPECT_FALSE(validControllerState(makeControllerState(0, 1, 8), 150));
  auto valid = makeControllerState(0, 123, 5);
  for (int field = 0; field < 6; ++field) {
    auto corrupt = valid;
    switch (field) {
      case 0: corrupt.magic ^= 1; break;
      case 1: corrupt.version ^= 1; break;
      case 2: corrupt.presetIndex ^= 1; break;
      case 3: corrupt.presetSignature ^= 1; break;
      case 4: corrupt.toggleMask ^= 1; break;
      case 5: corrupt.checksum ^= 1; break;
    }
    EXPECT_FALSE(validControllerState(corrupt, 150));
  }
}

TEST(StoredControllerState, MismatchConsumesBootStateWithoutRestoringLater) {
  auto state = makeControllerState(1, 100, 7);
  EXPECT_EQ(consumeBootToggleMask(state, 0, 100, 2), 0);
  EXPECT_EQ(consumeBootToggleMask(state, 1, 100, 2), 0);
  state = makeControllerState(1, 100, 7);
  EXPECT_EQ(consumeBootToggleMask(state, 1, 101, 2), 0);
  EXPECT_EQ(consumeBootToggleMask(state, 1, 100, 2), 0);
}

TEST(StoredControllerState, LegacyAndInterruptedWritesStartWithoutToggles) {
  StoredControllerState uninitialized;
  memset(&uninitialized, 0xff, sizeof(uninitialized));
  EXPECT_FALSE(validControllerState(uninitialized, 3));
  EXPECT_EQ(validateStoredPreset(2, 3), 2);
  EXPECT_EQ(consumeBootToggleMask(uninitialized, 2, 123, 3), 0);
  auto interrupted = makeControllerState(2, 123, 7);
  interrupted.magic = 0;
  EXPECT_FALSE(validControllerState(interrupted, 3));
}

TEST(StoredControllerState, CurrentSnapshotCannotMixIndexAndToggles) {
  auto saved = makeControllerState(0, 100, 7);
  // A toggle immediately after navigation persists the new preset and mask together.
  saved = makeControllerState(1, 200, 1);
  EXPECT_EQ(saved.presetIndex, 1);
  EXPECT_EQ(consumeBootToggleMask(saved, 1, 200, 2), 1);
  saved = makeControllerState(0, 100, 0);
  EXPECT_EQ(consumeBootToggleMask(saved, 0, 100, 2), 0);
}

TEST(ToggleRestore, SignatureIncludesFilenameAndPresetConfiguration) {
  StaticJsonDocument<1024> preset;
  deserializeJson(preset, R"({"Name":"Drive","Switch1":{"Toggle":true,"CC":[{"CC":2,"Channel":1}]}})");
  const auto signature = presetStateSignature("1.json", preset.as<JsonVariantConst>());
  EXPECT_EQ(signature, presetStateSignature("1.json", preset.as<JsonVariantConst>()));
  EXPECT_NE(signature, presetStateSignature("2.json", preset.as<JsonVariantConst>()));
  preset["Switch1"]["CC"][0]["Channel"] = 2;
  EXPECT_NE(signature, presetStateSignature("1.json", preset.as<JsonVariantConst>()));
}

TEST(ToggleRestore, SendsOnAndOffAfterOnLoadWithoutReplayingPc) {
  clearMidiLog();
  StaticJsonDocument<2048> preset;
  deserializeJson(preset, R"({
    "OnLoad":{"CC":[{"CC":10,"Value":40,"Channel":1}]},
    "Switch1":{"Toggle":true,"PC":[{"PC":4,"Channel":1}],"CC":[{"CC":10,"Channel":1},{"CC":11,"Channel":2,"USB":true}]},
    "Switch2":{"Toggle":true,"CC":[{"CC":12,"Channel":3}]},
    "Switch3":{"Toggle":false,"CC":[{"CC":13,"Channel":4}]}
  })");
  bool states[switchCount] = {};
  sendControlChange(10, 40, 1, false);
  restoreToggleStates(preset.as<JsonVariantConst>(), 5, states, sendControlChange);
  ASSERT_EQ(midiLog().size(), 4u);
  EXPECT_EQ(midiLog()[0].value2, 40);
  EXPECT_EQ(midiLog()[1].value2, 127);
  EXPECT_EQ(midiLog()[2].value1, 11);
  EXPECT_EQ(midiLog()[2].channel, 2);
  EXPECT_TRUE(midiLog()[2].usb);
  EXPECT_EQ(midiLog()[3].value1, 12);
  EXPECT_EQ(midiLog()[3].value2, 0);
  EXPECT_TRUE(states[0]);
  EXPECT_FALSE(states[1]);
  EXPECT_FALSE(states[2]);
  EXPECT_EQ(toggleCcValue(states[0]), 0);
  EXPECT_EQ(toggleCcValue(states[1]), 127);
  for (const auto &message : midiLog()) EXPECT_EQ(message.type, MidiMessage::ControlChange);
}

TEST(ToggleRestore, ExcludesPlaybackAndNonCcSwitches) {
  clearMidiLog();
  StaticJsonDocument<1024> preset;
  deserializeJson(preset, R"({
    "FileInfo":{"FileName":"song.mid"},
    "Switch1":{"Toggle":true,"CC":[{"CC":1,"Channel":1}]},
    "Switch2":{"Toggle":true,"CC":null},
    "Switch3":{"Toggle":true,"CC":[{"CC":3,"Channel":1}]}
  })");
  bool states[switchCount] = {};
  restoreToggleStates(preset.as<JsonVariantConst>(), 7, states, sendControlChange);
  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_EQ(midiLog()[0].value1, 3);
  EXPECT_FALSE(states[0]);
  EXPECT_FALSE(states[1]);
  EXPECT_TRUE(states[2]);
}
