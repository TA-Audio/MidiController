#pragma once

// When building for Arduino/Teensy, Arduino.h provides standard types.
// For native builds (unit tests), use standard C++ headers.
#ifndef ARDUINO
#include <cstdint>
#include <cstdlib>
#include <cstring>
#endif

// ── Pin assignments ──────────────────────────────────────────────────────────

static constexpr int switch1Pin = 2;
static constexpr int switch2Pin = 3;
static constexpr int switch3Pin = 4;
static constexpr int switchCount = 3;
static constexpr int nextPresetPin = 5;
static constexpr int prevPresetPin = 6;

// ── Timing ───────────────────────────────────────────────────────────────────

static constexpr unsigned long initialLoadDelayMs = 1000;
static constexpr unsigned long switchDisplayPeriodMs = 1500;
static constexpr unsigned long longHoldToggleMs = 3000;
static constexpr unsigned long eepromCommitDelayMs = 200;

// ── Display ──────────────────────────────────────────────────────────────────

static constexpr int lcdColumnCount = 20;
static constexpr int uiTextBufferLength = 50;

// ── Presets ──────────────────────────────────────────────────────────────────

static constexpr int maxPresetListSize = 150;
static constexpr int maxPresetNameLength = 25;

// ── MIDI ─────────────────────────────────────────────────────────────────────

static constexpr int midiValueMin = 0;
static constexpr int midiValueMax = 127;

// ── EEPROM addresses ─────────────────────────────────────────────────────────

static constexpr int presetEepromAddress = 0;
static constexpr int pcModeEepromAddress = 1000;

// ── Pure logic functions ─────────────────────────────────────────────────────

/// Clamp a MIDI value to the valid range (0–127).
inline int clampMidi(int value) {
  if (value < midiValueMin) return midiValueMin;
  if (value > midiValueMax) return midiValueMax;
  return value;
}

/// Check whether `durationMs` has elapsed since `start`, using `currentMs` as
/// the reference time.  Handles unsigned wrap-around correctly.
inline bool hasElapsed(unsigned long currentMs, unsigned long start, unsigned long durationMs) {
  return (currentMs - start) >= durationMs;
}

/// Extract the leading decimal number from a filename (e.g. "12.json" -> 12).
/// Returns 0 for filenames that don't start with a digit.
inline int extractNumber(const char *filename) {
  int num = 0;
  int i = 0;
  while (filename[i] != '\0' && filename[i] >= '0' && filename[i] <= '9') {
    num = num * 10 + (filename[i] - '0');
    i++;
  }
  return num;
}

/// qsort comparator – sorts preset filenames by leading number, falling back
/// to lexicographic order when numbers are equal.
inline int comparePresetNames(const void *lhs, const void *rhs) {
  const char *a = static_cast<const char *>(lhs);
  const char *b = static_cast<const char *>(rhs);
  const int numA = extractNumber(a);
  const int numB = extractNumber(b);
  if (numA < numB) return -1;
  if (numA > numB) return 1;
  return strncmp(a, b, maxPresetNameLength);
}

/// Calculate left-padding to centre `textLength` characters on the LCD row.
inline int calculateCenterPadding(int textLength) {
  const int safeLength = (textLength > lcdColumnCount) ? lcdColumnCount : textLength;
  return (lcdColumnCount - safeLength) / 2;
}

/// Calculate padding between three switch labels so they span the LCD evenly.
/// `leftPad` goes between sw1 and sw2; `rightPad` between sw2 and sw3.
inline void calculateSwitchPadding(int sw1Len, int sw2Len, int sw3Len,
                                   int &leftPad, int &rightPad) {
  const int totalLength = sw1Len + sw2Len + sw3Len;
  leftPad  = (totalLength < lcdColumnCount) ? (lcdColumnCount - totalLength) / 2 : 0;
  rightPad = (totalLength < lcdColumnCount) ? (lcdColumnCount - totalLength + 1) / 2 : 0;
}

/// Return true when the preset index can be incremented.
inline bool canNavigateNext(int currentPreset, int presetCount) {
  return currentPreset < (presetCount - 1);
}

/// Return true when the preset index can be decremented.
inline bool canNavigatePrev(int currentPreset) {
  return currentPreset > 0;
}

/// Increment or decrement a PC program number, clamping to 0–127.
inline int adjustPcProgram(int currentProgram, bool decrement) {
  if (decrement) {
    if (currentProgram >= 1) currentProgram--;
  } else {
    currentProgram++;
  }
  return clampMidi(currentProgram);
}
