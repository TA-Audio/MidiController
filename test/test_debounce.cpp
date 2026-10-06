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

TEST_F(SwitchHandlerTest, SwitchButtonsExecuteOnPressOnly) {
  for (uint8_t btnId : {1, 2, 3}) {
    EXPECT_EQ(classifySwitchEvent(btnId, BTN_STATE_PRESSED, true), SwitchAction::ExecuteSwitch);
    EXPECT_EQ(classifySwitchEvent(btnId, BTN_STATE_OPEN, true), SwitchAction::None);
  }
}

TEST_F(SwitchHandlerTest, NavigationButtonsNavigateOnReleaseOnly) {
  for (uint8_t btnId : {4, 5}) {
    EXPECT_EQ(classifySwitchEvent(btnId, BTN_STATE_PRESSED, true), SwitchAction::None);
    EXPECT_EQ(classifySwitchEvent(btnId, BTN_STATE_OPEN, true),
              btnId == 4 ? SwitchAction::NavigateNext : SwitchAction::NavigatePrev);
  }
}

TEST_F(SwitchHandlerTest, AllButtonsIgnoredBeforeLoaded) {
  for (uint8_t btnId : {1, 2, 3, 4, 5}) {
    EXPECT_EQ(classifySwitchEvent(btnId, BTN_STATE_PRESSED, false), SwitchAction::None);
    EXPECT_EQ(classifySwitchEvent(btnId, BTN_STATE_OPEN, false), SwitchAction::None);
  }
}

TEST_F(SwitchHandlerTest, InvalidButtonsAndStatesIgnored) {
  for (uint8_t btnId : {0, 6, 255}) {
    EXPECT_EQ(classifySwitchEvent(btnId, BTN_STATE_PRESSED, true), SwitchAction::None);
    EXPECT_EQ(classifySwitchEvent(btnId, BTN_STATE_OPEN, true), SwitchAction::None);
  }
  for (uint8_t btnId : {1, 2, 3, 4, 5}) {
    EXPECT_EQ(classifySwitchEvent(btnId, 2, true), SwitchAction::None);
  }
}

TEST_F(SwitchHandlerTest, ShortAndLongNavigationHoldsBothNavigateOnce) {
  for (uint8_t btnId : {4, 5}) {
    for (unsigned long duration : {100UL, 3000UL, 10000UL}) {
      DebounceState button;
      button.init();
      button.update(BTN_STATE_PRESSED, 10000);
      ASSERT_TRUE(button.update(BTN_STATE_PRESSED, 10026));
      EXPECT_EQ(classifySwitchEvent(btnId, button.currentState, true), SwitchAction::None);
      EXPECT_FALSE(button.update(BTN_STATE_PRESSED, 10026 + duration));
      button.update(BTN_STATE_OPEN, 10026 + duration);
      ASSERT_TRUE(button.update(BTN_STATE_OPEN, 10052 + duration));
      EXPECT_EQ(classifySwitchEvent(btnId, button.currentState, true),
                btnId == 4 ? SwitchAction::NavigateNext : SwitchAction::NavigatePrev);
      EXPECT_FALSE(button.update(BTN_STATE_OPEN, 10078 + duration));
    }
  }
}
