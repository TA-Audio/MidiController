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

// ── EEPROM validation ────────────────────────────────────────────────────────

/// Validate a preset index read from EEPROM.  Returns 0 if out of range.
inline int validateStoredPreset(int value, int maxPresets) {
  if (value < 0 || value >= maxPresets) return 0;
  return value;
}

/// Validate a PC program value read from EEPROM.  Returns 0 if out of MIDI range.
inline int validateStoredPcProgram(int value) {
  if (value < midiValueMin || value > midiValueMax) return 0;
  return value;
}

// ── Toggle logic ─────────────────────────────────────────────────────────────

/// Determine the CC value to send for a toggle switch.
/// Returns 127 when turning on, 0 when turning off.
inline int toggleCcValue(bool wasToggled) {
  return wasToggled ? 0 : 127;
}

// ── Preset prefetch ──────────────────────────────────────────────────────────

/// Compute the next preset index to prefetch based on navigation direction.
/// Returns -1 if no valid prefetch candidate exists.
inline int computePrefetchCandidate(int currentPreset, int presetCount,
                                    int navigationDirection, int alreadyPrefetchedIndex) {
  if (presetCount <= 1) return -1;

  int candidate = currentPreset + navigationDirection;
  if (candidate < 0 || candidate >= presetCount) {
    candidate = currentPreset - navigationDirection;
  }

  if (candidate < 0 || candidate >= presetCount || candidate == currentPreset) {
    return -1;
  }

  if (candidate == alreadyPrefetchedIndex) {
    return -1;  // already prefetched
  }

  return candidate;
}

// ── Filename utilities ───────────────────────────────────────────────────────

/// Check whether a filename ends with ".json" (case-sensitive).
inline bool hasJsonExtension(const char *filename) {
  if (filename == nullptr) return false;
  const int nameLength = (int)strlen(filename);
  const int extLength = 5;  // ".json"
  if (nameLength < extLength) return false;
  return strcmp(filename + nameLength - extLength, ".json") == 0;
}

// ── PC offset ────────────────────────────────────────────────────────────────

/// Convert a 1-indexed program change value (from JSON) to the 0-indexed MIDI value.
inline int pcJsonToMidi(int jsonPcValue) {
  return clampMidi(jsonPcValue - 1);
}

// ── Switch message formatting ────────────────────────────────────────────────

/// Format a switch action message into `outBuffer`.
/// Returns true if the result is non-empty (should be displayed).
inline bool formatSwitchActionMessage(const char *text, const char *suffix,
                                      char *outBuffer, int bufSize) {
  if (bufSize <= 0) return false;
  const char *safeText = (text != nullptr) ? text : "";
  if (suffix != nullptr && suffix[0] != '\0') {
    snprintf(outBuffer, bufSize, "%s%s", safeText, suffix);
  } else {
    snprintf(outBuffer, bufSize, "%s", safeText);
  }
  return outBuffer[0] != '\0';
}

// ── Preset bounds clamping ───────────────────────────────────────────────────

/// Clamp a preset index to valid range.  If out of bounds, returns 0.
inline int clampPresetIndex(int index, int presetCount) {
  if (presetCount <= 0) return 0;
  if (index < 0 || index >= presetCount) return 0;
  return index;
}
