#pragma once

#include "logic.h"

static constexpr uint32_t longPressMs = 600;
static constexpr uint32_t doubleTapMs = 300;
static constexpr int maxSequenceSteps = 16;
static constexpr int presetDocumentCapacity = 8192;
static constexpr int maxLivePresetBytes = 8192;

enum GestureEvent : uint8_t {
  GestureNone = 0, GestureTap = 1, GestureRelease = 2,
  GestureHold = 4, GestureDouble = 8, GesturePress = 16
};

struct SwitchGesture {
  bool down = false;
  bool held = false;
  bool pending = false;
  bool second = false;
  uint32_t pressedAt = 0;
  uint32_t releasedAt = 0;

  constexpr uint8_t update(bool pressed, uint32_t now, bool longEnabled, bool doubleEnabled,
                           uint32_t holdMs = longPressMs, uint32_t doubleMs = doubleTapMs) {
    uint8_t events = GestureNone;
    if (down && !held && longEnabled &&
        static_cast<uint32_t>(now - pressedAt) >= holdMs) {
      held = true;
      events |= GestureHold;
    }
    if (pending && static_cast<uint32_t>(now - releasedAt) >= doubleMs) {
      pending = false;
      events |= GestureTap;
    }
    if (pressed != down) {
      down = pressed;
      if (pressed) {
        second = pending && doubleEnabled;
        pending = false;
        pressedAt = now;
        held = false;
        events |= GesturePress;
      } else {
        events |= GestureRelease;
        if (!held) {
          if (second) events |= GestureDouble;
          else if (doubleEnabled) {
            pending = true;
            releasedAt = now;
          } else events |= GestureTap;
        }
        second = false;
      }
    }
    return events;
  }
};

struct TapTempo {
  bool started = false;
  uint32_t lastTap = 0;
  uint32_t intervals[3] = {};
  uint8_t count = 0;
  uint8_t cursor = 0;

  constexpr int tap(uint32_t now) {
    const uint32_t interval = now - lastTap;
    if (!started || interval > 2000) {
      started = true;
      lastTap = now;
      count = cursor = 0;
      return 0;
    }
    if (interval < 200) return 0;
    lastTap = now;
    intervals[cursor] = interval;
    cursor = (cursor + 1) % 3;
    if (count < 3) ++count;
    uint32_t total = 0;
    for (uint8_t i = 0; i < count; ++i) total += intervals[i];
    return static_cast<int>((60000UL * count + total / 2) / total);
  }
};

constexpr int nextSequenceStep(int current, int count) {
  return count > 0 && count <= maxSequenceSteps ? (current + 1) % count : 0;
}
