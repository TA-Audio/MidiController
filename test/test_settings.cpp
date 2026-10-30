#include <gtest/gtest.h>
#include <array>
#include <climits>
#include <cstring>
#include <string>
#include "settings_menu.h"
#include "performance.h"

namespace {
struct SettingsEeprom {
  std::array<uint8_t, 512> bytes;
  int writes = 0;
  int remaining = INT_MAX;

  SettingsEeprom() { bytes.fill(0xff); }
  template <typename T> void get(int address, T &value) {
    ASSERT_LE(address + sizeof(T), bytes.size());
    memcpy(&value, bytes.data() + address, sizeof(T));
  }
  template <typename T> void put(int address, const T &value) {
    ASSERT_LE(address + sizeof(T), bytes.size());
    const auto *source = reinterpret_cast<const uint8_t *>(&value);
    for (size_t i = 0; i < sizeof(T); ++i) {
      if (remaining-- <= 0) return;
      bytes[address + i] = source[i];
      ++writes;
    }
  }
};

const char names[][maxPresetNameLength] = {"1.json", "20.json", "1234567890123456789.json"};

TEST(SettingsStorage, DefaultsPreserveExistingBehavior) {
  DeviceSettings settings;
  EXPECT_TRUE(validSettings(settings));
  EXPECT_EQ(settings.dinInput1, 1u);
  EXPECT_EQ(settings.dinInput2, 1u);
  EXPECT_EQ(settings.usbMirror, 1u);
  EXPECT_EQ(settings.fixedStartup, 0u);
  EXPECT_EQ(settings.restoreToggles, 1u);
  EXPECT_EQ(settings.backlight, 1u);
  EXPECT_EQ(settings.scrollNames, 1u);
  EXPECT_EQ(settings.messageMs, switchDisplayPeriodMs);
  EXPECT_EQ(settings.holdMs, longPressMs);
  EXPECT_EQ(settings.doubleMs, doubleTapMs);
}

TEST(SettingsStorage, ErasedAndZeroStorageAreFirstBootNotCorruption) {
  SettingsEeprom eeprom;
  DeviceSettings settings;
  settings.messageMs = 2500;
  EXPECT_EQ(loadDeviceSettings(eeprom, settings), SettingsLoad::Defaults);
  EXPECT_EQ(settings.messageMs, 1500u);
  eeprom.bytes.fill(0);
  EXPECT_EQ(loadDeviceSettings(eeprom, settings), SettingsLoad::Defaults);
}

TEST(SettingsStorage, SavedSettingsRoundTripAndUnchangedSavesDoNotWrite) {
  SettingsEeprom eeprom;
  DeviceSettings settings;
  settings.usbMirror = 0;
  settings.doubleMs = 450;
  strcpy(settings.fixedPreset, "20.json");
  ASSERT_TRUE(saveDeviceSettings(eeprom, settings));
  const int writes = eeprom.writes;
  EXPECT_TRUE(saveDeviceSettings(eeprom, settings));
  EXPECT_EQ(eeprom.writes, writes);
  DeviceSettings loaded;
  EXPECT_EQ(loadDeviceSettings(eeprom, loaded), SettingsLoad::Saved);
  EXPECT_TRUE(sameSettings(settings, loaded));
  for (int i = 0; i < settingsEepromAddress; ++i) EXPECT_EQ(eeprom.bytes[i], 0xff);
}

TEST(SettingsStorage, InvalidValuesAreRejectedWithoutWrites) {
  DeviceSettings settings;
  for (uint32_t DeviceSettings::*field : {&DeviceSettings::dinInput1, &DeviceSettings::dinInput2,
       &DeviceSettings::usbMirror, &DeviceSettings::fixedStartup, &DeviceSettings::restoreToggles,
       &DeviceSettings::backlight, &DeviceSettings::scrollNames}) {
    settings = {};
    settings.*field = 2;
    EXPECT_FALSE(validSettings(settings));
  }
  for (uint32_t value : {0u, 499u, 501u, 5250u, UINT32_MAX}) {
    settings = {};
    settings.messageMs = value;
    EXPECT_FALSE(validSettings(settings));
  }
  settings = {};
  settings.holdMs = 399;
  EXPECT_FALSE(validSettings(settings));
  settings.holdMs = 2100;
  EXPECT_FALSE(validSettings(settings));
  settings = {};
  settings.doubleMs = 175;
  EXPECT_FALSE(validSettings(settings));
  settings.doubleMs = 650;
  SettingsEeprom eeprom;
  EXPECT_FALSE(saveDeviceSettings(eeprom, settings));
  EXPECT_EQ(eeprom.writes, 0);
  for (const char *name : {"../1.json", "sub\\1.json", "a:1.json", "file.mid"}) {
    settings = {};
    strcpy(settings.fixedPreset, name);
    EXPECT_FALSE(validSettings(settings));
  }
  memset(settings.fixedPreset, 'a', sizeof(settings.fixedPreset));
  EXPECT_FALSE(validSettings(settings));
}

TEST(SettingsStorage, SavingAndResettingSettingsPreservesLegacyPresetAndToggleState) {
  SettingsEeprom eeprom;
  const auto controller = makeControllerState(2, 12345, 5);
  writeControllerState(eeprom, controller);
  DeviceSettings settings;
  settings.scrollNames = 0;
  ASSERT_TRUE(saveDeviceSettings(eeprom, settings));
  ASSERT_TRUE(saveDeviceSettings(eeprom, DeviceSettings{}));
  StoredControllerState loaded;
  eeprom.get(controllerStateEepromAddress, loaded);
  EXPECT_TRUE(validControllerState(loaded, 3));
  EXPECT_EQ(loaded.presetIndex, 2u);
  EXPECT_EQ(loaded.toggleMask, 5u);
  EXPECT_EQ(loaded.presetSignature, 12345u);
  int index = -1;
  eeprom.get(presetEepromAddress, index);
  EXPECT_EQ(index, 2);
}

TEST(SettingsStorage, CorruptionAndUnknownVersionsUseVisibleInvalidStatus) {
  SettingsEeprom eeprom;
  DeviceSettings settings;
  ASSERT_TRUE(saveDeviceSettings(eeprom, settings));
  eeprom.bytes[settingsEepromAddress + sizeof(uint32_t)] = 2;
  EXPECT_EQ(loadDeviceSettings(eeprom, settings), SettingsLoad::Invalid);
  EXPECT_TRUE(sameSettings(settings, DeviceSettings{}));
  ASSERT_TRUE(saveDeviceSettings(eeprom, settings));
  eeprom.bytes[settingsEepromAddress + 20] ^= 0x10;
  EXPECT_EQ(loadDeviceSettings(eeprom, settings), SettingsLoad::Invalid);
}

TEST(SettingsStorage, EveryInterruptedWritePreservesThePreviousPublishedRecord) {
  SettingsEeprom original;
  DeviceSettings first, second, third;
  second.scrollNames = 0;
  third.backlight = 0;
  ASSERT_TRUE(saveDeviceSettings(original, first));
  for (int budget = 0; budget < 100; ++budget) {
    auto interrupted = original;
    interrupted.remaining = budget;
    const bool saved = saveDeviceSettings(interrupted, second);
    DeviceSettings loaded;
    EXPECT_EQ(loadDeviceSettings(interrupted, loaded), SettingsLoad::Saved);
    EXPECT_TRUE(sameSettings(loaded, first) || sameSettings(loaded, second));
    if (saved) EXPECT_TRUE(sameSettings(loaded, second));
  }
  ASSERT_TRUE(saveDeviceSettings(original, second));
  for (int budget = 0; budget < 100; ++budget) {
    auto interrupted = original;
    interrupted.remaining = budget;
    const bool saved = saveDeviceSettings(interrupted, third);
    DeviceSettings loaded;
    EXPECT_EQ(loadDeviceSettings(interrupted, loaded), SettingsLoad::Saved);
    EXPECT_TRUE(sameSettings(loaded, second) || sameSettings(loaded, third));
    if (saved) EXPECT_TRUE(sameSettings(loaded, third));
  }
}

TEST(SettingsStorage, ReadbackFailureDoesNotPublishOrReportSuccess) {
  SettingsEeprom eeprom;
  DeviceSettings settings;
  ASSERT_TRUE(saveDeviceSettings(eeprom, settings));
  settings.dinInput2 = 0;
  eeprom.remaining = 20;
  EXPECT_FALSE(saveDeviceSettings(eeprom, settings));
  DeviceSettings loaded;
  EXPECT_EQ(loadDeviceSettings(eeprom, loaded), SettingsLoad::Saved);
  EXPECT_EQ(loaded.dinInput2, 1u);
}

TEST(SettingsStorage, FirstSaveInterruptionNeverRestoresPartialSettings) {
  for (int budget = 0; budget < 100; ++budget) {
    SettingsEeprom eeprom;
    eeprom.remaining = budget;
    DeviceSettings changed;
    changed.messageMs = 3000;
    const bool saved = saveDeviceSettings(eeprom, changed);
    DeviceSettings loaded;
    const auto status = loadDeviceSettings(eeprom, loaded);
    if (saved) {
      EXPECT_EQ(status, SettingsLoad::Saved);
      EXPECT_TRUE(sameSettings(loaded, changed));
    } else {
      EXPECT_NE(status, SettingsLoad::Saved);
      EXPECT_TRUE(sameSettings(loaded, DeviceSettings{}));
    }
  }
}

TEST(SettingsStorage, SlotSequenceWraparoundSelectsNewerAndIgnoresInvalidSlots) {
  StoredSettings records[2];
  for (auto &record : records) record.magic = settingsMagic;
  records[0].sequence = UINT32_MAX;
  records[1].sequence = 0;
  for (auto &record : records) record.checksum = settingsChecksum(record);
  EXPECT_EQ(latestSettingsSlot(records), 1);
  records[1].settings.backlight = 5;
  EXPECT_EQ(latestSettingsSlot(records), 0);
  records[0].magic = 0;
  EXPECT_EQ(latestSettingsSlot(records), -1);
}

TEST(SettingsStartup, LastFixedMissingAndReorderedFilesUseAuthoritativeList) {
  DeviceSettings settings;
  bool missing = true;
  EXPECT_EQ(startupPresetIndex(settings, names, 3, 1, missing), 1);
  EXPECT_FALSE(missing);
  settings.fixedStartup = 1;
  strcpy(settings.fixedPreset, "20.json");
  EXPECT_EQ(startupPresetIndex(settings, names, 3, 0, missing), 1);
  const char reordered[][maxPresetNameLength] = {"20.json", "1.json"};
  EXPECT_EQ(startupPresetIndex(settings, reordered, 2, 1, missing), 0);
  EXPECT_FALSE(missing);
  strcpy(settings.fixedPreset, "missing.json");
  EXPECT_EQ(startupPresetIndex(settings, names, 3, 2, missing), 0);
  EXPECT_TRUE(missing);
  EXPECT_EQ(startupPresetIndex(settings, names, 0, 2, missing), 0);
  EXPECT_FALSE(missing);
}

TEST(SettingsChord, SinglesNavigateOnlyOnReleaseWithoutHoldDelay) {
  SettingsChord chord;
  EXPECT_FALSE(chord.update(4, true, 0));
  EXPECT_FALSE(chord.ready(5001));
  EXPECT_TRUE(chord.update(4, false, 6000));
  EXPECT_FALSE(chord.update(5, true, 6001));
  EXPECT_TRUE(chord.update(5, false, 6002));
  EXPECT_FALSE(chord.update(1, false, 6003));
}

TEST(SettingsChord, EntryRequiresExactlyFiveContinuousSecondsAfterSecondPress) {
  SettingsChord chord;
  chord.update(4, true, 100);
  chord.update(5, true, 300);
  EXPECT_TRUE(chord.claimed);
  EXPECT_FALSE(chord.ready(5299));
  EXPECT_TRUE(chord.ready(5300));
  EXPECT_FALSE(chord.ready(10000));
  EXPECT_FALSE(chord.update(4, false, 10001));
  EXPECT_FALSE(chord.update(5, false, 10002));
  EXPECT_FALSE(chord.claimed);
}

TEST(SettingsChord, BothReleaseOrdersConsumeShortChordsAndRequireFullReleaseToRetry) {
  for (uint8_t first : {4, 5}) {
    SettingsChord chord;
    const uint8_t other = first == 4 ? 5 : 4;
    chord.update(first, true, 0);
    chord.update(other, true, 1);
    EXPECT_FALSE(chord.update(first, false, 1000));
    EXPECT_FALSE(chord.ready(6000));
    chord.update(first, true, 6100);
    EXPECT_FALSE(chord.ready(12000));
    EXPECT_FALSE(chord.update(first, false, 12001));
    EXPECT_FALSE(chord.update(other, false, 12002));
    chord.update(first, true, 13000);
    chord.update(other, true, 13001);
    EXPECT_TRUE(chord.ready(18001));
  }
}

TEST(SettingsChord, FiveSecondHoldHandlesTimerWraparound) {
  SettingsChord chord;
  chord.update(4, true, UINT32_MAX - 1000);
  chord.update(5, true, UINT32_MAX - 999);
  EXPECT_FALSE(chord.ready(3999));
  EXPECT_TRUE(chord.ready(4000));
}

TEST(SettingsChord, DebouncedBounceCannotStartAnEarlyHold) {
  DebounceState next, previous;
  next.init();
  previous.init();
  SettingsChord chord;
  auto poll = [&](uint32_t now, uint8_t sample) {
    if (next.update(sample, now)) chord.update(4, next.currentState == BTN_STATE_PRESSED, now);
    if (previous.update(sample, now)) chord.update(5, previous.currentState == BTN_STATE_PRESSED, now);
  };
  poll(0, 0);
  poll(10, 1);
  poll(20, 0);
  poll(45, 0);
  EXPECT_FALSE(chord.claimed);
  poll(46, 0);
  EXPECT_TRUE(chord.claimed);
  EXPECT_FALSE(chord.ready(5045));
  EXPECT_TRUE(chord.ready(5046));
}

class SettingsMenuTest : public ::testing::Test {
protected:
  SettingsMenu menu;
  void SetUp() override { menu.open(DeviceSettings{}); }
  SettingsResult button(uint8_t id, bool usb = true, int count = 3) {
    return menu.button(id, usb, names, count);
  }
};

TEST_F(SettingsMenuTest, BrowsingWrapsAndTextFitsDisplay) {
  button(5);
  EXPECT_EQ(menu.item, SettingsItem::Reset);
  button(4);
  EXPECT_EQ(menu.item, SettingsItem::Din1);
  for (int i = 0; i < static_cast<int>(SettingsItem::Count); ++i) {
    EXPECT_LE(strlen(menu.label()), 20u);
    EXPECT_LE(strlen(menu.category()) + 2, 20u);
    char text[50];
    menu.formatValue(text, sizeof(text), true);
    EXPECT_LE(strlen(text), 24u);
    button(4);
  }
  EXPECT_FALSE(menu.dirty());
}

TEST_F(SettingsMenuTest, EditCancelRestoresEntryValueAndConfirmOnlyStagesChanges) {
  button(2);
  EXPECT_EQ(menu.page, SettingsPage::Edit);
  button(5);
  EXPECT_EQ(menu.draft.dinInput1, 0u);
  EXPECT_EQ(menu.original.dinInput1, 1u);
  button(1);
  EXPECT_EQ(menu.draft.dinInput1, 1u);
  EXPECT_FALSE(menu.dirty());
  button(2);
  button(5);
  button(2);
  EXPECT_TRUE(menu.dirty());
  EXPECT_EQ(menu.page, SettingsPage::Browse);
}

TEST_F(SettingsMenuTest, TimingChoicesClampAndRetainValidDefaults) {
  for (SettingsItem item : {SettingsItem::MessageTime, SettingsItem::HoldTime, SettingsItem::DoubleTime}) {
    menu.item = item;
    button(2);
    for (int i = 0; i < 100; ++i) button(5);
    EXPECT_TRUE(validSettings(menu.draft));
    const uint32_t minimum = *menu.value();
    button(5);
    EXPECT_EQ(*menu.value(), minimum);
    for (int i = 0; i < 100; ++i) button(4);
    EXPECT_TRUE(validSettings(menu.draft));
    const uint32_t maximum = *menu.value();
    button(4);
    EXPECT_EQ(*menu.value(), maximum);
    button(2);
  }
}

TEST_F(SettingsMenuTest, FixedPresetSelectsExistingFilenamesAndCancelUndoesSelection) {
  menu.item = SettingsItem::FixedPreset;
  button(2);
  EXPECT_STREQ(menu.draft.fixedPreset, "1.json");
  button(4);
  EXPECT_STREQ(menu.draft.fixedPreset, "20.json");
  button(5);
  button(5);
  EXPECT_STREQ(menu.draft.fixedPreset, names[2]);
  EXPECT_TRUE(validSettings(menu.draft));
  button(1);
  EXPECT_STREQ(menu.draft.fixedPreset, "");
  menu.item = SettingsItem::Startup;
  button(2);
  button(4);
  EXPECT_EQ(menu.draft.fixedStartup, 1u);
  EXPECT_STREQ(menu.draft.fixedPreset, names[0]);
}

TEST_F(SettingsMenuTest, MissingUsbBuildAndEmptyListCannotEditUnavailableItems) {
  menu.item = SettingsItem::Mirror;
  button(2, false);
  EXPECT_EQ(menu.page, SettingsPage::Browse);
  char text[50];
  menu.formatValue(text, sizeof(text), false);
  EXPECT_STREQ(text, "Needs USB MIDI build");
  menu.item = SettingsItem::FixedPreset;
  button(2, true, 0);
  EXPECT_EQ(menu.page, SettingsPage::Browse);
  EXPECT_FALSE(menu.dirty());
}

TEST_F(SettingsMenuTest, CleanBackClosesAndDirtyBackOffersDiscardCancelOrSave) {
  EXPECT_EQ(button(1), SettingsResult::Close);
  menu.draft.backlight = 0;
  EXPECT_EQ(button(1), SettingsResult::None);
  EXPECT_EQ(menu.page, SettingsPage::Exit);
  EXPECT_EQ(button(2), SettingsResult::None);
  EXPECT_EQ(menu.page, SettingsPage::Browse);
  button(1);
  EXPECT_EQ(button(1), SettingsResult::Close);
  button(1);
  EXPECT_EQ(button(3), SettingsResult::Save);
}

TEST_F(SettingsMenuTest, RoutingChangesRequireAnAdditionalStopNotesConfirmation) {
  menu.draft.usbMirror = 0;
  EXPECT_EQ(button(3), SettingsResult::None);
  EXPECT_EQ(menu.page, SettingsPage::Routing);
  EXPECT_EQ(button(1), SettingsResult::None);
  EXPECT_TRUE(menu.dirty());
  EXPECT_EQ(button(3), SettingsResult::None);
  EXPECT_EQ(button(2), SettingsResult::Save);
}

TEST_F(SettingsMenuTest, ResetRequiresConfirmationAndOnlyStagesDefaults) {
  menu.draft.backlight = 0;
  menu.item = SettingsItem::Reset;
  button(2);
  EXPECT_EQ(menu.page, SettingsPage::Reset);
  button(1);
  EXPECT_EQ(menu.draft.backlight, 0u);
  button(2);
  button(2);
  EXPECT_TRUE(sameSettings(menu.draft, DeviceSettings{}));
  EXPECT_EQ(menu.page, SettingsPage::Browse);
  EXPECT_TRUE(sameSettings(menu.original, DeviceSettings{}));
}

TEST_F(SettingsMenuTest, SaveFromEditUsesCurrentDraftAndFailureKeepsItForRetry) {
  menu.item = SettingsItem::HoldTime;
  button(2);
  button(4);
  EXPECT_EQ(button(3), SettingsResult::Save);
  menu.page = SettingsPage::Error;
  button(2);
  EXPECT_EQ(menu.page, SettingsPage::Browse);
  EXPECT_EQ(menu.draft.holdMs, 700u);
  EXPECT_EQ(button(3), SettingsResult::Save);
}

TEST(SettingsGestureTiming, ConfiguredThresholdsReplaceOnlyPerformanceDefaults) {
  SwitchGesture gesture;
  EXPECT_EQ(gesture.update(true, 0, true, false, 1200, 500), GesturePress);
  EXPECT_EQ(gesture.update(true, 1199, true, false, 1200, 500), GestureNone);
  EXPECT_EQ(gesture.update(true, 1200, true, false, 1200, 500), GestureHold);
  EXPECT_EQ(gesture.update(false, 1300, true, false, 1200, 500), GestureRelease);
  gesture = {};
  gesture.update(true, 0, false, true, 1200, 500);
  gesture.update(false, 10, false, true, 1200, 500);
  EXPECT_EQ(gesture.update(false, 509, false, true, 1200, 500), GestureNone);
  EXPECT_EQ(gesture.update(false, 510, false, true, 1200, 500), GestureTap);
  gesture = {};
  gesture.update(true, 0, false, true, 1200, 500);
  gesture.update(false, 10, false, true, 1200, 500);
  EXPECT_EQ(gesture.update(true, 400, false, true, 1200, 500), GesturePress);
  EXPECT_EQ(gesture.update(false, 410, false, true, 1200, 500), GestureRelease | GestureDouble);
}
} // namespace

namespace settingsUiTests {
struct Display {
  char cells[4][20] = {};
  int row = 0, column = 0, writes = 0;
  bool lit = false;
  void setCursor(int x, int y) { column = x; row = y; }
  void print(char value) {
    ASSERT_LT(row, 4);
    ASSERT_LT(column, 20);
    cells[row][column++] = value;
    ++writes;
  }
  void backlight() { lit = true; }
  void noBacklight() { lit = false; }
} lcd;
struct Host { int services = 0; void Task() { ++services; } } usbHost;
int clockServices = 0, inputServices = 0, releases = 0;
void serviceMidiClock() { ++clockServices; }
void serviceMidiPassthrough() { ++inputServices; }
void suspendPerformanceSwitches() { ++releases; }
bool pendingRefresh = false;
void requestPresetDisplayRefresh() { pendingRefresh = true; }
DeviceSettings deviceSettings;
SettingsMenu settingsMenu;
SettingsChord settingsChord;
SettingsEeprom EEPROM;
char presetList[maxPresetListSize][maxPresetNameLength] = {"1.json", "20.json"};
int presetCount = 2;
uint32_t currentMillis = 0;
uint32_t millis() { return currentMillis; }
uint8_t buttonsDown = 0, suppressedButtons = 0;
bool midiFileActivityDisplayAllowed = false, resetPresetDisplay = false;
bool hasLoaded = true, liveStorageFault = false, playingMidiFile = false, stoppingMidiFile = false;
bool editorBusy = false;
bool liveEditorBusy() { return editorBusy; }

#include "settings_ui.h"

class SettingsUi : public ::testing::Test {
protected:
  void SetUp() override {
    deviceSettings = {};
    settingsMenu = {};
    settingsChord = {};
    EEPROM = {};
    lcd = {};
    usbHost = {};
    buttonsDown = suppressedButtons = 0;
    currentMillis = 0;
    playingMidiFile = stoppingMidiFile = editorBusy = liveStorageFault = false;
    hasLoaded = true;
    pendingRefresh = false;
    settingsDisplayValid = false;
    clockServices = inputServices = releases = 0;
    serviceSettingsMenu();
  }
  void hold() {
    settingsChord.update(4, true, 0);
    settingsChord.update(5, true, 0);
    buttonsDown = 24;
    currentMillis = 5000;
    serviceSettingsMenu();
  }
  std::string line(int row) { return std::string(lcd.cells[row], 20); }
};

TEST_F(SettingsUi, OpensAtThresholdAndConsumesHeldEntryButtons) {
  hold();
  EXPECT_TRUE(settingsMenu.active);
  EXPECT_EQ(suppressedButtons & 24, 24);
  EXPECT_EQ(releases, 1);
  EXPECT_NE(line(0).find("MIDI routing"), std::string::npos);
  EXPECT_GT(clockServices, 0);
  EXPECT_GT(inputServices, 0);
  EXPECT_GT(usbHost.services, 0);
}

TEST_F(SettingsUi, PlaybackStopProcessingBootAndEditorTransfersRefuseEntry) {
  for (int reason = 0; reason < 5; ++reason) {
    settingsChord = {};
    settingsMenu = {};
    playingMidiFile = reason == 0;
    stoppingMidiFile = reason == 1;
    hasLoaded = reason != 2;
    editorBusy = reason == 3;
    liveStorageFault = reason == 4;
    hold();
    EXPECT_FALSE(settingsMenu.active);
    EXPECT_NE(line(0).find("unavailable"), std::string::npos);
    EXPECT_EQ(playingMidiFile, reason == 0);
    EXPECT_EQ(stoppingMidiFile, reason == 1);
  }
}

TEST_F(SettingsUi, DisplayUpdatesOnlyChangedCellsAndSupportsEveryMenuPage) {
  settingsMenu.open(deviceSettings);
  for (auto page : {SettingsPage::Browse, SettingsPage::Edit, SettingsPage::Exit,
                    SettingsPage::Reset, SettingsPage::Info, SettingsPage::Routing, SettingsPage::Error}) {
    settingsMenu.page = page;
    drawSettingsMenu();
    const int writes = lcd.writes;
    drawSettingsMenu();
    EXPECT_EQ(lcd.writes, writes);
    EXPECT_FALSE(midiFileActivityDisplayAllowed);
  }
  EXPECT_FALSE(computerMidiAvailable());
  EXPECT_STREQ(computerMidiStatus(), "USB: no MIDI build");
  const int writes = lcd.writes;
  invalidateSettingsDisplay();
  drawSettingsMenu();
  EXPECT_EQ(lcd.writes, writes + 80);
}

TEST_F(SettingsUi, LongFixedFilenamesShowBothEndsWithoutOverflowingLcdRows) {
  settingsMenu.open(deviceSettings);
  settingsMenu.item = SettingsItem::FixedPreset;
  strcpy(settingsMenu.draft.fixedPreset, names[2]);
  drawSettingsMenu();
  EXPECT_EQ(line(2), std::string(names[2], 20));
  currentMillis = 1500;
  drawSettingsMenu();
  EXPECT_EQ(line(2), std::string(names[2] + strlen(names[2]) - 20));
}

TEST_F(SettingsUi, FailedSaveRetainsDraftAndSuccessAppliesWithoutPresetReload) {
  settingsMenu.open(deviceSettings);
  settingsMenu.draft.messageMs = 2500;
  EEPROM.remaining = 0;
  handleSettingsButton(3);
  EXPECT_TRUE(settingsMenu.active);
  EXPECT_EQ(settingsMenu.page, SettingsPage::Error);
  EXPECT_EQ(settingsMenu.draft.messageMs, 2500u);
  EXPECT_EQ(deviceSettings.messageMs, 1500u);
  EEPROM.remaining = INT_MAX;
  handleSettingsButton(1);
  buttonsDown = 1;
  handleSettingsButton(3);
  EXPECT_FALSE(settingsMenu.active);
  EXPECT_EQ(deviceSettings.messageMs, 2500u);
  EXPECT_EQ(suppressedButtons, 1);
  EXPECT_TRUE(pendingRefresh);
  DeviceSettings persisted;
  EXPECT_EQ(loadDeviceSettings(EEPROM, persisted), SettingsLoad::Saved);
  EXPECT_EQ(persisted.messageMs, 2500u);
  EXPECT_GT(clockServices, 0);
  EXPECT_GT(inputServices, 0);
}

TEST_F(SettingsUi, DiscardPreservesSavedSettingsAndBacklightPreference) {
  deviceSettings.backlight = 0;
  settingsMenu.open(deviceSettings);
  settingsMenu.draft.backlight = 1;
  handleSettingsButton(1);
  EXPECT_EQ(settingsMenu.page, SettingsPage::Exit);
  handleSettingsButton(1);
  EXPECT_FALSE(settingsMenu.active);
  EXPECT_EQ(deviceSettings.backlight, 0u);
  EXPECT_FALSE(lcd.lit);
  EXPECT_EQ(EEPROM.writes, 0);
}

TEST_F(SettingsUi, ClosingWhileNavigationIsHeldCannotReopenUntilReleased) {
  settingsMenu.open(deviceSettings);
  settingsChord.update(4, true, 0);
  settingsChord.update(5, true, 0);
  buttonsDown = 24;
  currentMillis = 6000;
  closeSettingsMenu();
  serviceSettingsMenu();
  EXPECT_FALSE(settingsMenu.active);
  EXPECT_FALSE(settingsChord.ready(currentMillis));
  EXPECT_EQ(suppressedButtons, 24);
  EXPECT_FALSE(settingsChord.update(4, false, 6001));
  EXPECT_FALSE(settingsChord.update(5, false, 6002));
  settingsChord.update(4, true, 7000);
  settingsChord.update(5, true, 7000);
  EXPECT_TRUE(settingsChord.ready(12000));
}

TEST_F(SettingsUi, ClosingWithReleasedKeysAllowsTheNextNormalEntryAttempt) {
  settingsMenu.open(deviceSettings);
  closeSettingsMenu();
  hold();
  EXPECT_TRUE(settingsMenu.active);
}
} // namespace settingsUiTests
