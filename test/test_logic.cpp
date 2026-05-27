#include <gtest/gtest.h>
#include "logic.h"

// ═══════════════════════════════════════════════════════════════════════════════
// ClampMidi
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ClampMidi, BelowMinimum) {
  EXPECT_EQ(clampMidi(-1), 0);
}

TEST(ClampMidi, AtMinimum) {
  EXPECT_EQ(clampMidi(0), 0);
}

TEST(ClampMidi, InRange) {
  EXPECT_EQ(clampMidi(64), 64);
}

TEST(ClampMidi, AtMaximum) {
  EXPECT_EQ(clampMidi(127), 127);
}

TEST(ClampMidi, AboveMaximum) {
  EXPECT_EQ(clampMidi(128), 127);
}

TEST(ClampMidi, LargeNegative) {
  EXPECT_EQ(clampMidi(-1000), 0);
}

TEST(ClampMidi, LargePositive) {
  EXPECT_EQ(clampMidi(1000), 127);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ExtractNumber
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ExtractNumber, SingleDigit) {
  EXPECT_EQ(extractNumber("1.json"), 1);
}

TEST(ExtractNumber, MultiDigit) {
  EXPECT_EQ(extractNumber("100.json"), 100);
}

TEST(ExtractNumber, NonNumericFilename) {
  EXPECT_EQ(extractNumber("abc.json"), 0);
}

TEST(ExtractNumber, MixedNumericPrefix) {
  EXPECT_EQ(extractNumber("12abc.json"), 12);
}

TEST(ExtractNumber, EmptyString) {
  EXPECT_EQ(extractNumber(""), 0);
}

TEST(ExtractNumber, JustDigits) {
  EXPECT_EQ(extractNumber("42"), 42);
}

TEST(ExtractNumber, LeadingZeros) {
  EXPECT_EQ(extractNumber("007.json"), 7);
}

TEST(ExtractNumber, LargeNumber) {
  EXPECT_EQ(extractNumber("999.json"), 999);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ComparePresetNames
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ComparePresetNames, FirstSmaller) {
  char a[maxPresetNameLength] = "1.json";
  char b[maxPresetNameLength] = "2.json";
  EXPECT_LT(comparePresetNames(a, b), 0);
}

TEST(ComparePresetNames, FirstLarger) {
  char a[maxPresetNameLength] = "10.json";
  char b[maxPresetNameLength] = "2.json";
  EXPECT_GT(comparePresetNames(a, b), 0);
}

TEST(ComparePresetNames, Equal) {
  char a[maxPresetNameLength] = "5.json";
  char b[maxPresetNameLength] = "5.json";
  EXPECT_EQ(comparePresetNames(a, b), 0);
}

TEST(ComparePresetNames, NumericSortNotLexicographic) {
  // Lexicographic: "10" < "2", but numeric: 10 > 2
  char a[maxPresetNameLength] = "10.json";
  char b[maxPresetNameLength] = "9.json";
  EXPECT_GT(comparePresetNames(a, b), 0);
}

TEST(ComparePresetNames, NonNumericFallsBackToLexicographic) {
  char a[maxPresetNameLength] = "abc.json";
  char b[maxPresetNameLength] = "def.json";
  // Both extract 0, so falls back to strncmp
  EXPECT_LT(comparePresetNames(a, b), 0);
}

TEST(ComparePresetNames, NumericVsNonNumeric) {
  char a[maxPresetNameLength] = "1.json";
  char b[maxPresetNameLength] = "abc.json";
  // a extracts 1, b extracts 0 → a > b
  EXPECT_GT(comparePresetNames(a, b), 0);
}

TEST(ComparePresetNames, SortFullList) {
  char names[][maxPresetNameLength] = {
    "10.json", "2.json", "1.json", "20.json", "3.json", "100.json"
  };
  qsort(names, 6, sizeof(names[0]), comparePresetNames);
  EXPECT_STREQ(names[0], "1.json");
  EXPECT_STREQ(names[1], "2.json");
  EXPECT_STREQ(names[2], "3.json");
  EXPECT_STREQ(names[3], "10.json");
  EXPECT_STREQ(names[4], "20.json");
  EXPECT_STREQ(names[5], "100.json");
}

// ═══════════════════════════════════════════════════════════════════════════════
// HasElapsed
// ═══════════════════════════════════════════════════════════════════════════════

TEST(HasElapsed, NotYetElapsed) {
  EXPECT_FALSE(hasElapsed(100, 50, 100));
}

TEST(HasElapsed, ExactlyElapsed) {
  EXPECT_TRUE(hasElapsed(150, 50, 100));
}

TEST(HasElapsed, PastElapsed) {
  EXPECT_TRUE(hasElapsed(200, 50, 100));
}

TEST(HasElapsed, ZeroDuration) {
  EXPECT_TRUE(hasElapsed(100, 100, 0));
}

TEST(HasElapsed, UnsignedOverflowElapsed) {
  // Simulates millis() wrap-around: start near max, current past zero
  // On Arduino/Teensy unsigned long is 32-bit.  On 64-bit Linux it's 64-bit.
  // Use values that work correctly under both widths.
  const unsigned long start = (unsigned long)(-50);  // near max
  const unsigned long current = start + 100;         // 100ms after start (wraps on 32-bit)
  EXPECT_TRUE(hasElapsed(current, start, 100));
}

TEST(HasElapsed, UnsignedOverflowNotYetElapsed) {
  const unsigned long start = (unsigned long)(-50);
  const unsigned long current = start + 30;          // only 30ms after start
  EXPECT_FALSE(hasElapsed(current, start, 100));
}

TEST(HasElapsed, LongHoldThreshold) {
  // Real-world: long hold of 3000ms
  EXPECT_FALSE(hasElapsed(2999, 0, longHoldToggleMs));
  EXPECT_TRUE(hasElapsed(3000, 0, longHoldToggleMs));
  EXPECT_TRUE(hasElapsed(5000, 0, longHoldToggleMs));
}

TEST(HasElapsed, EepromCommitDelay) {
  EXPECT_FALSE(hasElapsed(199, 0, eepromCommitDelayMs));
  EXPECT_TRUE(hasElapsed(200, 0, eepromCommitDelayMs));
}

// ═══════════════════════════════════════════════════════════════════════════════
// CalculateCenterPadding
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CalculateCenterPadding, ShortText) {
  // "PLAY" = 4 chars → (20-4)/2 = 8
  EXPECT_EQ(calculateCenterPadding(4), 8);
}

TEST(CalculateCenterPadding, EmptyText) {
  EXPECT_EQ(calculateCenterPadding(0), 10);
}

TEST(CalculateCenterPadding, FullWidth) {
  EXPECT_EQ(calculateCenterPadding(20), 0);
}

TEST(CalculateCenterPadding, ExceedsWidth) {
  // Text longer than LCD is clamped to lcdColumnCount before padding
  EXPECT_EQ(calculateCenterPadding(30), 0);
}

TEST(CalculateCenterPadding, OddLength) {
  // 5 chars → (20-5)/2 = 7 (integer division floors)
  EXPECT_EQ(calculateCenterPadding(5), 7);
}

TEST(CalculateCenterPadding, SingleChar) {
  EXPECT_EQ(calculateCenterPadding(1), 9);
}

// ═══════════════════════════════════════════════════════════════════════════════
// CalculateSwitchPadding
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CalculateSwitchPadding, EvenDistribution) {
  int left, right;
  calculateSwitchPadding(4, 4, 4, left, right);
  // total=12, remaining=8 → left=4, right=4
  EXPECT_EQ(left, 4);
  EXPECT_EQ(right, 4);
}

TEST(CalculateSwitchPadding, OddRemainder) {
  int left, right;
  calculateSwitchPadding(4, 4, 3, left, right);
  // total=11, remaining=9 → left=4, right=5
  EXPECT_EQ(left, 4);
  EXPECT_EQ(right, 5);
}

TEST(CalculateSwitchPadding, OverflowsWidth) {
  int left, right;
  calculateSwitchPadding(10, 10, 10, left, right);
  // total=30 >= 20 → both 0
  EXPECT_EQ(left, 0);
  EXPECT_EQ(right, 0);
}

TEST(CalculateSwitchPadding, EmptyLabels) {
  int left, right;
  calculateSwitchPadding(0, 0, 0, left, right);
  // total=0, remaining=20 → left=10, right=10
  EXPECT_EQ(left, 10);
  EXPECT_EQ(right, 10);
}

TEST(CalculateSwitchPadding, TotalFillsExactly) {
  int left, right;
  calculateSwitchPadding(8, 6, 6, left, right);
  // total=20, remaining=0 → both 0
  EXPECT_EQ(left, 0);
  EXPECT_EQ(right, 0);
}

TEST(CalculateSwitchPadding, TypicalPresetLayout) {
  // e.g. "DIST" (4) + "DELAY" (5) + "REV" (3) = 12 → remaining=8
  int left, right;
  calculateSwitchPadding(4, 5, 3, left, right);
  EXPECT_EQ(left, 4);
  EXPECT_EQ(right, 4);
  // Verify total fills LCD: 4 + 4 + 5 + 4 + 3 = 20
  EXPECT_EQ(4 + left + 5 + right + 3, lcdColumnCount);
}

// ═══════════════════════════════════════════════════════════════════════════════
// CanNavigateNext
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CanNavigateNext, CanAdvance) {
  EXPECT_TRUE(canNavigateNext(0, 10));
}

TEST(CanNavigateNext, AtLastPreset) {
  EXPECT_FALSE(canNavigateNext(9, 10));
}

TEST(CanNavigateNext, OnlyOnePreset) {
  EXPECT_FALSE(canNavigateNext(0, 1));
}

TEST(CanNavigateNext, EmptyList) {
  // presetCount=0, currentPreset=0 → 0 < -1 is false
  EXPECT_FALSE(canNavigateNext(0, 0));
}

TEST(CanNavigateNext, MiddleOfList) {
  EXPECT_TRUE(canNavigateNext(5, 150));
}

// ═══════════════════════════════════════════════════════════════════════════════
// CanNavigatePrev
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CanNavigatePrev, CanGoBack) {
  EXPECT_TRUE(canNavigatePrev(5));
}

TEST(CanNavigatePrev, AtFirstPreset) {
  EXPECT_FALSE(canNavigatePrev(0));
}

TEST(CanNavigatePrev, SecondPreset) {
  EXPECT_TRUE(canNavigatePrev(1));
}

// ═══════════════════════════════════════════════════════════════════════════════
// AdjustPcProgram
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AdjustPcProgram, IncrementFromZero) {
  EXPECT_EQ(adjustPcProgram(0, false), 1);
}

TEST(AdjustPcProgram, IncrementMidRange) {
  EXPECT_EQ(adjustPcProgram(63, false), 64);
}

TEST(AdjustPcProgram, IncrementAtMax) {
  // 127 + 1 = 128 → clamped to 127
  EXPECT_EQ(adjustPcProgram(127, false), 127);
}

TEST(AdjustPcProgram, IncrementNearMax) {
  EXPECT_EQ(adjustPcProgram(126, false), 127);
}

TEST(AdjustPcProgram, DecrementFromMax) {
  EXPECT_EQ(adjustPcProgram(127, true), 126);
}

TEST(AdjustPcProgram, DecrementMidRange) {
  EXPECT_EQ(adjustPcProgram(64, true), 63);
}

TEST(AdjustPcProgram, DecrementAtZero) {
  // Already at 0, can't go lower
  EXPECT_EQ(adjustPcProgram(0, true), 0);
}

TEST(AdjustPcProgram, DecrementFromOne) {
  EXPECT_EQ(adjustPcProgram(1, true), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Constants validation
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Constants, MidiRange) {
  EXPECT_EQ(midiValueMin, 0);
  EXPECT_EQ(midiValueMax, 127);
}

TEST(Constants, LcdWidth) {
  EXPECT_EQ(lcdColumnCount, 20);
}

TEST(Constants, SwitchCount) {
  EXPECT_EQ(switchCount, 3);
}

TEST(Constants, PresetLimits) {
  EXPECT_EQ(maxPresetListSize, 150);
  EXPECT_EQ(maxPresetNameLength, 25);
}

TEST(Constants, TimingValues) {
  EXPECT_EQ(initialLoadDelayMs, 1000UL);
  EXPECT_EQ(switchDisplayPeriodMs, 1500UL);
  EXPECT_EQ(longHoldToggleMs, 3000UL);
  EXPECT_EQ(eepromCommitDelayMs, 200UL);
}

TEST(Constants, EepromAddresses) {
  EXPECT_EQ(presetEepromAddress, 0);
  EXPECT_EQ(pcModeEepromAddress, 1000);
  // Ensure addresses don't overlap (preset stores an int = 4 bytes)
  EXPECT_GT(pcModeEepromAddress, presetEepromAddress + (int)sizeof(int));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration: Toggle state management
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ToggleState, ArrayInitializedToFalse) {
  bool toggles[switchCount] = {};
  for (int i = 0; i < switchCount; i++) {
    EXPECT_FALSE(toggles[i]);
  }
}

TEST(ToggleState, IndependentPerSwitch) {
  bool toggles[switchCount] = {};
  toggles[0] = true;
  EXPECT_TRUE(toggles[0]);
  EXPECT_FALSE(toggles[1]);
  EXPECT_FALSE(toggles[2]);
}

TEST(ToggleState, ToggleOnOff) {
  bool toggles[switchCount] = {};
  // Toggle on
  toggles[1] = !toggles[1];
  EXPECT_TRUE(toggles[1]);
  // Toggle off
  toggles[1] = !toggles[1];
  EXPECT_FALSE(toggles[1]);
}

TEST(ToggleState, ResetAll) {
  bool toggles[switchCount] = {true, true, true};
  memset(toggles, 0, sizeof(toggles));
  for (int i = 0; i < switchCount; i++) {
    EXPECT_FALSE(toggles[i]);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ValidateStoredPreset (EEPROM validation — new on branch)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ValidateStoredPreset, ValidIndex) {
  EXPECT_EQ(validateStoredPreset(5, 150), 5);
}

TEST(ValidateStoredPreset, ZeroIndex) {
  EXPECT_EQ(validateStoredPreset(0, 150), 0);
}

TEST(ValidateStoredPreset, AtMax) {
  // Index 149 is the last valid with maxPresets=150
  EXPECT_EQ(validateStoredPreset(149, 150), 149);
}

TEST(ValidateStoredPreset, NegativeReturnsZero) {
  EXPECT_EQ(validateStoredPreset(-1, 150), 0);
}

TEST(ValidateStoredPreset, LargeNegativeReturnsZero) {
  EXPECT_EQ(validateStoredPreset(-9999, 150), 0);
}

TEST(ValidateStoredPreset, EqualToMaxReturnsZero) {
  EXPECT_EQ(validateStoredPreset(150, 150), 0);
}

TEST(ValidateStoredPreset, AboveMaxReturnsZero) {
  EXPECT_EQ(validateStoredPreset(200, 150), 0);
}

TEST(ValidateStoredPreset, CorruptedLargeValueReturnsZero) {
  // Simulates uninitialized EEPROM (0xFFFF... = large negative or positive)
  EXPECT_EQ(validateStoredPreset(0x7FFFFFFF, 150), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ValidateStoredPcProgram (EEPROM validation — new on branch)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ValidateStoredPcProgram, ValidZero) {
  EXPECT_EQ(validateStoredPcProgram(0), 0);
}

TEST(ValidateStoredPcProgram, ValidMidRange) {
  EXPECT_EQ(validateStoredPcProgram(64), 64);
}

TEST(ValidateStoredPcProgram, ValidMax) {
  EXPECT_EQ(validateStoredPcProgram(127), 127);
}

TEST(ValidateStoredPcProgram, NegativeReturnsZero) {
  EXPECT_EQ(validateStoredPcProgram(-1), 0);
}

TEST(ValidateStoredPcProgram, AboveMaxReturnsZero) {
  EXPECT_EQ(validateStoredPcProgram(128), 0);
}

TEST(ValidateStoredPcProgram, LargeCorruptedValue) {
  EXPECT_EQ(validateStoredPcProgram(65535), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ToggleCcValue (refactored toggle logic — new on branch)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ToggleCcValue, TurningOnSends127) {
  // wasToggled=false means switch is currently off, so we're turning ON
  EXPECT_EQ(toggleCcValue(false), 127);
}

TEST(ToggleCcValue, TurningOffSends0) {
  // wasToggled=true means switch is currently on, so we're turning OFF
  EXPECT_EQ(toggleCcValue(true), 0);
}

TEST(ToggleCcValue, ConsecutiveToggles) {
  bool state = false;
  // First press: off→on
  int val1 = toggleCcValue(state);
  EXPECT_EQ(val1, 127);
  state = !state;  // now on

  // Second press: on→off
  int val2 = toggleCcValue(state);
  EXPECT_EQ(val2, 0);
  state = !state;  // now off

  // Third press: off→on again
  int val3 = toggleCcValue(state);
  EXPECT_EQ(val3, 127);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ComputePrefetchCandidate (directional prefetch — new on branch)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ComputePrefetchCandidate, ForwardFromStart) {
  // current=0, count=10, direction=+1, nothing prefetched
  EXPECT_EQ(computePrefetchCandidate(0, 10, 1, -1), 1);
}

TEST(ComputePrefetchCandidate, ForwardFromMiddle) {
  EXPECT_EQ(computePrefetchCandidate(5, 10, 1, -1), 6);
}

TEST(ComputePrefetchCandidate, BackwardFromEnd) {
  EXPECT_EQ(computePrefetchCandidate(9, 10, -1, -1), 8);
}

TEST(ComputePrefetchCandidate, BackwardFromMiddle) {
  EXPECT_EQ(computePrefetchCandidate(5, 10, -1, -1), 4);
}

TEST(ComputePrefetchCandidate, ForwardAtEndFallsBackToReverse) {
  // current=9 (last), direction=+1 → candidate=10 OOB → tries 9-1=8
  EXPECT_EQ(computePrefetchCandidate(9, 10, 1, -1), 8);
}

TEST(ComputePrefetchCandidate, BackwardAtStartFallsForward) {
  // current=0, direction=-1 → candidate=-1 OOB → tries 0+1=1
  EXPECT_EQ(computePrefetchCandidate(0, 10, -1, -1), 1);
}

TEST(ComputePrefetchCandidate, SinglePresetReturnsNegative) {
  EXPECT_EQ(computePrefetchCandidate(0, 1, 1, -1), -1);
}

TEST(ComputePrefetchCandidate, EmptyListReturnsNegative) {
  EXPECT_EQ(computePrefetchCandidate(0, 0, 1, -1), -1);
}

TEST(ComputePrefetchCandidate, AlreadyPrefetchedReturnsNegative) {
  // Candidate would be 6, but it's already prefetched
  EXPECT_EQ(computePrefetchCandidate(5, 10, 1, 6), -1);
}

TEST(ComputePrefetchCandidate, DifferentPrefetchStillReturnsCandidate) {
  // Candidate is 6, but index 3 is prefetched (different) → valid
  EXPECT_EQ(computePrefetchCandidate(5, 10, 1, 3), 6);
}

TEST(ComputePrefetchCandidate, TwoPresetsForward) {
  // current=0, count=2, direction=+1 → candidate=1
  EXPECT_EQ(computePrefetchCandidate(0, 2, 1, -1), 1);
}

TEST(ComputePrefetchCandidate, TwoPresetsBackward) {
  // current=1, count=2, direction=-1 → candidate=0
  EXPECT_EQ(computePrefetchCandidate(1, 2, -1, -1), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// HasJsonExtension (file filtering — new on branch)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(HasJsonExtension, ValidJsonFile) {
  EXPECT_TRUE(hasJsonExtension("1.json"));
}

TEST(HasJsonExtension, MultiDigitJson) {
  EXPECT_TRUE(hasJsonExtension("100.json"));
}

TEST(HasJsonExtension, TextFile) {
  EXPECT_FALSE(hasJsonExtension("readme.txt"));
}

TEST(HasJsonExtension, NoExtension) {
  EXPECT_FALSE(hasJsonExtension("noext"));
}

TEST(HasJsonExtension, DotOnly) {
  EXPECT_TRUE(hasJsonExtension(".json"));  // ".json" is length 5, ends with ".json" → true
}

TEST(HasJsonExtension, EmptyString) {
  EXPECT_FALSE(hasJsonExtension(""));
}

TEST(HasJsonExtension, NullPointer) {
  EXPECT_FALSE(hasJsonExtension(nullptr));
}

TEST(HasJsonExtension, SimilarExtensionNotJson) {
  EXPECT_FALSE(hasJsonExtension("file.jsonl"));
}

TEST(HasJsonExtension, UpperCaseNotMatched) {
  // Case-sensitive: .JSON doesn't match .json
  EXPECT_FALSE(hasJsonExtension("file.JSON"));
}

TEST(HasJsonExtension, HiddenJsonFile) {
  EXPECT_TRUE(hasJsonExtension(".hidden.json"));
}

TEST(HasJsonExtension, ShortFilename) {
  EXPECT_FALSE(hasJsonExtension("a.js"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// PcJsonToMidi (PC offset conversion — new on branch)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(PcJsonToMidi, OneBecomes0) {
  // JSON PC=1 → MIDI PC=0
  EXPECT_EQ(pcJsonToMidi(1), 0);
}

TEST(PcJsonToMidi, TwoBecomes1) {
  EXPECT_EQ(pcJsonToMidi(2), 1);
}

TEST(PcJsonToMidi, MaxValidPC128Becomes127) {
  EXPECT_EQ(pcJsonToMidi(128), 127);
}

TEST(PcJsonToMidi, ZeroClampedToZero) {
  // JSON PC=0 → 0-1=-1 → clamped to 0
  EXPECT_EQ(pcJsonToMidi(0), 0);
}

TEST(PcJsonToMidi, NegativeClampedToZero) {
  EXPECT_EQ(pcJsonToMidi(-5), 0);
}

TEST(PcJsonToMidi, LargeValueClamped) {
  // JSON PC=200 → 199 → clamped to 127
  EXPECT_EQ(pcJsonToMidi(200), 127);
}

TEST(PcJsonToMidi, Pc64Becomes63) {
  EXPECT_EQ(pcJsonToMidi(64), 63);
}

// ═══════════════════════════════════════════════════════════════════════════════
// FormatSwitchActionMessage (UI message formatting — new on branch)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(FormatSwitchActionMessage, TextWithSuffix) {
  char buf[50];
  EXPECT_TRUE(formatSwitchActionMessage("Dist", " On!", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "Dist On!");
}

TEST(FormatSwitchActionMessage, TextWithOffSuffix) {
  char buf[50];
  EXPECT_TRUE(formatSwitchActionMessage("Reverb", " Off!", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "Reverb Off!");
}

TEST(FormatSwitchActionMessage, TextWithEmptySuffix) {
  char buf[50];
  EXPECT_TRUE(formatSwitchActionMessage("Delay", "", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "Delay");
}

TEST(FormatSwitchActionMessage, TextWithNullSuffix) {
  char buf[50];
  EXPECT_TRUE(formatSwitchActionMessage("Boost", nullptr, buf, sizeof(buf)));
  EXPECT_STREQ(buf, "Boost");
}

TEST(FormatSwitchActionMessage, NullTextWithSuffix) {
  char buf[50];
  EXPECT_TRUE(formatSwitchActionMessage(nullptr, " On!", buf, sizeof(buf)));
  EXPECT_STREQ(buf, " On!");
}

TEST(FormatSwitchActionMessage, NullTextNullSuffix) {
  char buf[50];
  EXPECT_FALSE(formatSwitchActionMessage(nullptr, nullptr, buf, sizeof(buf)));
}

TEST(FormatSwitchActionMessage, NullTextEmptySuffix) {
  char buf[50];
  EXPECT_FALSE(formatSwitchActionMessage(nullptr, "", buf, sizeof(buf)));
}

TEST(FormatSwitchActionMessage, EmptyTextEmptySuffix) {
  char buf[50];
  EXPECT_FALSE(formatSwitchActionMessage("", "", buf, sizeof(buf)));
}

TEST(FormatSwitchActionMessage, EmptyTextWithSuffix) {
  char buf[50];
  EXPECT_TRUE(formatSwitchActionMessage("", " On!", buf, sizeof(buf)));
  EXPECT_STREQ(buf, " On!");
}

TEST(FormatSwitchActionMessage, TruncationHandled) {
  char buf[10];
  EXPECT_TRUE(formatSwitchActionMessage("LongSwitchName", " On!", buf, sizeof(buf)));
  EXPECT_EQ(strlen(buf), 9u);  // truncated to bufSize-1
}

TEST(FormatSwitchActionMessage, ZeroBufferReturnsFalse) {
  char buf[1] = {0};
  EXPECT_FALSE(formatSwitchActionMessage("X", " On!", buf, 0));
}

// ═══════════════════════════════════════════════════════════════════════════════
// ClampPresetIndex (preset bounds — new on branch)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ClampPresetIndex, ValidIndex) {
  EXPECT_EQ(clampPresetIndex(5, 10), 5);
}

TEST(ClampPresetIndex, ZeroIndex) {
  EXPECT_EQ(clampPresetIndex(0, 10), 0);
}

TEST(ClampPresetIndex, LastValid) {
  EXPECT_EQ(clampPresetIndex(9, 10), 9);
}

TEST(ClampPresetIndex, EqualToCountResetsToZero) {
  EXPECT_EQ(clampPresetIndex(10, 10), 0);
}

TEST(ClampPresetIndex, AboveCountResetsToZero) {
  EXPECT_EQ(clampPresetIndex(100, 10), 0);
}

TEST(ClampPresetIndex, NegativeResetsToZero) {
  EXPECT_EQ(clampPresetIndex(-1, 10), 0);
}

TEST(ClampPresetIndex, ZeroPresetCountAlwaysReturnsZero) {
  EXPECT_EQ(clampPresetIndex(0, 0), 0);
  EXPECT_EQ(clampPresetIndex(5, 0), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration: Full switch toggle cycle with CC values
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SwitchToggleCycle, FullOnOffCycleForThreeSwitches) {
  // Simulates the refactored executeSwitchLogic toggle behavior
  bool switchToggled[switchCount] = {};

  for (int sw = 0; sw < switchCount; sw++) {
    // Press 1: turn on
    bool wasToggled = switchToggled[sw];
    int ccVal = toggleCcValue(wasToggled);
    EXPECT_EQ(ccVal, 127);
    switchToggled[sw] = !wasToggled;
    EXPECT_TRUE(switchToggled[sw]);

    // Press 2: turn off
    wasToggled = switchToggled[sw];
    ccVal = toggleCcValue(wasToggled);
    EXPECT_EQ(ccVal, 0);
    switchToggled[sw] = !wasToggled;
    EXPECT_FALSE(switchToggled[sw]);
  }
}

TEST(SwitchToggleCycle, ResetAllAfterPresetChange) {
  bool switchToggled[switchCount] = {true, true, true};
  // Simulates memset(switchToggled, 0, ...) done in changePreset()
  memset(switchToggled, 0, sizeof(switchToggled));
  for (int i = 0; i < switchCount; i++) {
    EXPECT_FALSE(switchToggled[i]);
    EXPECT_EQ(toggleCcValue(switchToggled[i]), 127);  // next press would be ON
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration: Navigation + prefetch scenario
// ═══════════════════════════════════════════════════════════════════════════════

TEST(NavigationPrefetch, ForwardThroughList) {
  const int presetCount = 5;
  int currentPreset = 0;
  int direction = 1;
  int prefetchedIndex = -1;

  // Navigate forward through all presets
  for (int i = 0; i < presetCount - 1; i++) {
    EXPECT_TRUE(canNavigateNext(currentPreset, presetCount));
    currentPreset++;
    int candidate = computePrefetchCandidate(currentPreset, presetCount, direction, prefetchedIndex);
    if (candidate >= 0) {
      prefetchedIndex = candidate;
    }
  }
  // At the end
  EXPECT_EQ(currentPreset, 4);
  EXPECT_FALSE(canNavigateNext(currentPreset, presetCount));
}

TEST(NavigationPrefetch, BackwardThroughList) {
  const int presetCount = 5;
  int currentPreset = 4;
  int direction = -1;
  int prefetchedIndex = -1;

  for (int i = 0; i < presetCount - 1; i++) {
    EXPECT_TRUE(canNavigatePrev(currentPreset));
    currentPreset--;
    int candidate = computePrefetchCandidate(currentPreset, presetCount, direction, prefetchedIndex);
    if (candidate >= 0) {
      prefetchedIndex = candidate;
    }
  }
  EXPECT_EQ(currentPreset, 0);
  EXPECT_FALSE(canNavigatePrev(currentPreset));
}

TEST(NavigationPrefetch, DirectionReversal) {
  // Going forward, then back — prefetch should adapt
  int currentPreset = 5;
  int presetCount = 10;
  int prefetchedIndex = -1;

  // Forward
  int candidate = computePrefetchCandidate(currentPreset, presetCount, 1, prefetchedIndex);
  EXPECT_EQ(candidate, 6);
  prefetchedIndex = candidate;

  // Reverse direction
  candidate = computePrefetchCandidate(currentPreset, presetCount, -1, prefetchedIndex);
  EXPECT_EQ(candidate, 4);
}
