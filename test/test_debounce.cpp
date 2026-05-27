#include <gtest/gtest.h>

#ifndef ARDUINO
#define ARDUINO_MOCK
#endif

#include "logic.h"

// ═══════════════════════════════════════════════════════════════════════════════
// Debounce State Machine Tests
// ═══════════════════════════════════════════════════════════════════════════════

class DebounceTest : public ::testing::Test {
protected:
  DebounceState state;

  void SetUp() override {
    state.init();
  }
};

TEST_F(DebounceTest, InitialStateIsOpen) {
  EXPECT_EQ(state.currentState, BTN_STATE_OPEN);
}

TEST_F(DebounceTest, SingleLowSampleDoesNotTriggerPress) {
  // One sample at time 0 shouldn't trigger — debounce interval not elapsed
  bool changed = state.update(0, 0);
  EXPECT_FALSE(changed);
  EXPECT_EQ(state.currentState, BTN_STATE_OPEN);
}

TEST_F(DebounceTest, StableLowSignalTriggersPress) {
  // Signal goes low at t=0, stays low past debounce interval
  state.update(0, 0);
  bool changed = state.update(0, 26);  // 26ms > 25ms threshold
  EXPECT_TRUE(changed);
  EXPECT_EQ(state.currentState, BTN_STATE_PRESSED);
}

TEST_F(DebounceTest, SignalAtExactThresholdDoesNotTrigger) {
  // Must be GREATER than interval, not equal
  state.update(0, 0);
  bool changed = state.update(0, 25);
  EXPECT_FALSE(changed);
  EXPECT_EQ(state.currentState, BTN_STATE_OPEN);
}

TEST_F(DebounceTest, BouncingResetsTimer) {
  // Signal goes low at t=0
  state.update(0, 0);
  // Bounces back high at t=10 — resets timer
  state.update(1, 10);
  // Goes low again at t=20 — resets timer again
  state.update(0, 20);
  // At t=40 (only 20ms since last reset) — should NOT trigger
  bool changed = state.update(0, 40);
  EXPECT_FALSE(changed);
  EXPECT_EQ(state.currentState, BTN_STATE_OPEN);
  // At t=46 (26ms since last reset at t=20) — NOW triggers
  changed = state.update(0, 46);
  EXPECT_TRUE(changed);
  EXPECT_EQ(state.currentState, BTN_STATE_PRESSED);
}

TEST_F(DebounceTest, ReleaseAfterPress) {
  // Press first
  state.update(0, 0);
  state.update(0, 26);
  ASSERT_EQ(state.currentState, BTN_STATE_PRESSED);

  // Now release — signal goes high
  state.update(1, 50);
  bool changed = state.update(1, 76);  // 26ms after release began
  EXPECT_TRUE(changed);
  EXPECT_EQ(state.currentState, BTN_STATE_OPEN);
}

TEST_F(DebounceTest, RapidBouncingNeverTriggers) {
  // Simulate rapid bouncing that never settles
  for (unsigned long t = 0; t < 200; t += 10) {
    uint8_t sample = (t / 10) % 2;  // alternates every 10ms
    state.update(sample, t);
  }
  // Should still be in initial state
  EXPECT_EQ(state.currentState, BTN_STATE_OPEN);
}

TEST_F(DebounceTest, NoChangeWhenSignalMatchesCurrentState) {
  // Signal is high (matches BTN_STATE_OPEN) — no transition
  bool changed = state.update(1, 0);
  EXPECT_FALSE(changed);
  changed = state.update(1, 100);
  EXPECT_FALSE(changed);
  EXPECT_EQ(state.currentState, BTN_STATE_OPEN);
}

TEST_F(DebounceTest, CustomPushInterval) {
  state.init(50, 25);  // 50ms push debounce, 25ms release

  state.update(0, 0);
  // At 26ms — would trigger with default but not with 50ms
  bool changed = state.update(0, 26);
  EXPECT_FALSE(changed);
  EXPECT_EQ(state.currentState, BTN_STATE_OPEN);

  // At 51ms — triggers
  changed = state.update(0, 51);
  EXPECT_TRUE(changed);
  EXPECT_EQ(state.currentState, BTN_STATE_PRESSED);
}

TEST_F(DebounceTest, CustomReleaseInterval) {
  state.init(25, 50);  // 25ms push, 50ms release debounce

  // Press normally
  state.update(0, 0);
  state.update(0, 26);
  ASSERT_EQ(state.currentState, BTN_STATE_PRESSED);

  // Release — needs 50ms
  state.update(1, 100);
  bool changed = state.update(1, 126);  // only 26ms — not enough
  EXPECT_FALSE(changed);
  EXPECT_EQ(state.currentState, BTN_STATE_PRESSED);

  changed = state.update(1, 151);  // 51ms — triggers
  EXPECT_TRUE(changed);
  EXPECT_EQ(state.currentState, BTN_STATE_OPEN);
}

TEST_F(DebounceTest, TimerOverflowHandledCorrectly) {
  // Simulate unsigned long overflow
  unsigned long nearMax = (unsigned long)(-50);  // 50ms before overflow

  state.update(0, nearMax);
  // After overflow: nearMax + 26 wraps around
  unsigned long afterOverflow = nearMax + 26;
  bool changed = state.update(0, afterOverflow);
  EXPECT_TRUE(changed);
  EXPECT_EQ(state.currentState, BTN_STATE_PRESSED);
}

TEST_F(DebounceTest, MultipleTransitions) {
  // Full press-release-press cycle
  state.update(0, 0);
  state.update(0, 26);
  EXPECT_EQ(state.currentState, BTN_STATE_PRESSED);

  state.update(1, 100);
  state.update(1, 126);
  EXPECT_EQ(state.currentState, BTN_STATE_OPEN);

  state.update(0, 200);
  state.update(0, 226);
  EXPECT_EQ(state.currentState, BTN_STATE_PRESSED);
}

TEST_F(DebounceTest, UpdateReturnsFalseWhenNoChange) {
  // Already pressed, signal stays low
  state.update(0, 0);
  state.update(0, 26);  // pressed
  bool changed = state.update(0, 100);
  EXPECT_FALSE(changed);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Switch Handler Routing Logic Tests
// ═══════════════════════════════════════════════════════════════════════════════

class SwitchHandlerTest : public ::testing::Test {};

// --- BTN_PRESSED on switch buttons (1-3) ---

TEST_F(SwitchHandlerTest, PressButton1WhenLoaded) {
  auto action = classifySwitchEvent(1, BTN_STATE_PRESSED, true, false);
  EXPECT_EQ(action, SwitchAction::ExecuteSwitch);
}

TEST_F(SwitchHandlerTest, PressButton2WhenLoaded) {
  auto action = classifySwitchEvent(2, BTN_STATE_PRESSED, true, false);
  EXPECT_EQ(action, SwitchAction::ExecuteSwitch);
}

TEST_F(SwitchHandlerTest, PressButton3WhenLoaded) {
  auto action = classifySwitchEvent(3, BTN_STATE_PRESSED, true, false);
  EXPECT_EQ(action, SwitchAction::ExecuteSwitch);
}

TEST_F(SwitchHandlerTest, PressButton1NotLoaded) {
  auto action = classifySwitchEvent(1, BTN_STATE_PRESSED, false, false);
  EXPECT_EQ(action, SwitchAction::None);
}

TEST_F(SwitchHandlerTest, PressButton2NotLoaded) {
  auto action = classifySwitchEvent(2, BTN_STATE_PRESSED, false, false);
  EXPECT_EQ(action, SwitchAction::None);
}

TEST_F(SwitchHandlerTest, PressButton3NotLoaded) {
  auto action = classifySwitchEvent(3, BTN_STATE_PRESSED, false, false);
  EXPECT_EQ(action, SwitchAction::None);
}

// --- BTN_PRESSED on nav buttons (4-5) ---

TEST_F(SwitchHandlerTest, PressButton4RecordsHoldStart) {
  auto action = classifySwitchEvent(4, BTN_STATE_PRESSED, true, false);
  EXPECT_EQ(action, SwitchAction::RecordHoldStart);
}

TEST_F(SwitchHandlerTest, PressButton5RecordsHoldStart) {
  auto action = classifySwitchEvent(5, BTN_STATE_PRESSED, true, false);
  EXPECT_EQ(action, SwitchAction::RecordHoldStart);
}

TEST_F(SwitchHandlerTest, PressNavButtonRecordsEvenWhenNotLoaded) {
  // The firmware records longHoldStartMillis regardless of hasLoaded
  auto action = classifySwitchEvent(4, BTN_STATE_PRESSED, false, false);
  EXPECT_EQ(action, SwitchAction::RecordHoldStart);
}

// --- BTN_OPEN (release) on nav buttons — short press ---

TEST_F(SwitchHandlerTest, ReleaseButton4ShortPressNavigatesNext) {
  auto action = classifySwitchEvent(4, BTN_STATE_OPEN, true, false);
  EXPECT_EQ(action, SwitchAction::NavigateNext);
}

TEST_F(SwitchHandlerTest, ReleaseButton5ShortPressNavigatesPrev) {
  auto action = classifySwitchEvent(5, BTN_STATE_OPEN, true, false);
  EXPECT_EQ(action, SwitchAction::NavigatePrev);
}

// --- BTN_OPEN (release) on nav buttons — long hold ---

TEST_F(SwitchHandlerTest, ReleaseButton4LongHoldTogglesPcMode) {
  auto action = classifySwitchEvent(4, BTN_STATE_OPEN, true, true);
  EXPECT_EQ(action, SwitchAction::TogglePcMode);
}

TEST_F(SwitchHandlerTest, ReleaseButton5LongHoldTogglesPcMode) {
  auto action = classifySwitchEvent(5, BTN_STATE_OPEN, true, true);
  EXPECT_EQ(action, SwitchAction::TogglePcMode);
}

// --- BTN_OPEN before loaded ---

TEST_F(SwitchHandlerTest, ReleaseButton4NotLoadedNoAction) {
  auto action = classifySwitchEvent(4, BTN_STATE_OPEN, false, false);
  EXPECT_EQ(action, SwitchAction::None);
}

TEST_F(SwitchHandlerTest, ReleaseButton5NotLoadedNoAction) {
  auto action = classifySwitchEvent(5, BTN_STATE_OPEN, false, false);
  EXPECT_EQ(action, SwitchAction::None);
}

TEST_F(SwitchHandlerTest, ReleaseButton4NotLoadedLongHoldNoAction) {
  auto action = classifySwitchEvent(4, BTN_STATE_OPEN, false, true);
  EXPECT_EQ(action, SwitchAction::None);
}

// --- Release on switch buttons (1-3) does nothing ---

TEST_F(SwitchHandlerTest, ReleaseButton1NoAction) {
  auto action = classifySwitchEvent(1, BTN_STATE_OPEN, true, false);
  EXPECT_EQ(action, SwitchAction::None);
}

TEST_F(SwitchHandlerTest, ReleaseButton2NoAction) {
  auto action = classifySwitchEvent(2, BTN_STATE_OPEN, true, false);
  EXPECT_EQ(action, SwitchAction::None);
}

TEST_F(SwitchHandlerTest, ReleaseButton3NoAction) {
  auto action = classifySwitchEvent(3, BTN_STATE_OPEN, true, false);
  EXPECT_EQ(action, SwitchAction::None);
}

// --- Invalid button IDs ---

TEST_F(SwitchHandlerTest, PressButton0NoAction) {
  auto action = classifySwitchEvent(0, BTN_STATE_PRESSED, true, false);
  EXPECT_EQ(action, SwitchAction::None);
}

TEST_F(SwitchHandlerTest, PressButton6NoAction) {
  auto action = classifySwitchEvent(6, BTN_STATE_PRESSED, true, false);
  EXPECT_EQ(action, SwitchAction::None);
}

TEST_F(SwitchHandlerTest, ReleaseButton0NoAction) {
  auto action = classifySwitchEvent(0, BTN_STATE_OPEN, true, false);
  EXPECT_EQ(action, SwitchAction::None);
}

TEST_F(SwitchHandlerTest, ReleaseButton6NoAction) {
  auto action = classifySwitchEvent(6, BTN_STATE_OPEN, true, false);
  EXPECT_EQ(action, SwitchAction::None);
}
