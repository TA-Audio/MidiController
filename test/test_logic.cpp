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
