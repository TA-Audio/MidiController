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
static constexpr unsigned long presetNameScrollPeriodMs = 500;
static constexpr unsigned long eepromCommitDelayMs = 200;
static constexpr uint32_t midiFileActivityHoldMs = 100;

// ── Display ──────────────────────────────────────────────────────────────────

static constexpr int lcdColumnCount = 20;
static constexpr int uiTextBufferLength = 50;

// ── Presets ──────────────────────────────────────────────────────────────────

static constexpr int maxPresetListSize = 150;
static constexpr int maxPresetNameLength = 25;

// ── MIDI ─────────────────────────────────────────────────────────────────────

static constexpr int midiValueMin = 0;
static constexpr int midiValueMax = 127;

/// Return a positive configured tempo override, or 0 to use the MIDI file tempo.
inline int configuredMidiTempoOverride(bool tempoIsSet, int tempoBpm) {
  return tempoIsSet && tempoBpm > 0 ? tempoBpm : 0;
}

/// Return 0 when disabled, a valid MIDI Clock tempo, or -1 when enabled but invalid.
inline int configuredPresetMidiClockTempo(bool enabled, bool tempoIsSet, int tempoBpm) {
  if (!enabled) return 0;
  if (!tempoIsSet || tempoBpm <= 0 || tempoBpm > 65535) return -1;
  return tempoBpm;
}

/// Calculate the interval between MIDI Clock pulses at 24 pulses per quarter note.
inline uint32_t midiClockIntervalForTempo(uint16_t tempo) {
  return tempo == 0 ? 0 : 60000000UL / (static_cast<uint32_t>(tempo) * 24UL);
}

enum class MidiFileClockMode {
  Disabled,
  TransportOnly,
  ClockAndTransport
};

inline MidiFileClockMode midiFileClockMode(bool requested, bool clockEnabled,
                                         bool clockForMidiFile) {
  if (!requested) return MidiFileClockMode::Disabled;
  if (clockEnabled && !clockForMidiFile) return MidiFileClockMode::TransportOnly;
  return MidiFileClockMode::ClockAndTransport;
}

struct MidiFilePlaybackActions {
  MidiFileClockMode clockMode;
  bool sendStart;
  bool startClock;
};

inline MidiFilePlaybackActions midiFilePlaybackActions(bool sendFileClock,
                                                       bool clockEnabled,
                                                       bool clockForMidiFile,
                                                       uint16_t fileTempo) {
  const MidiFileClockMode clockMode = midiFileClockMode(
    sendFileClock, clockEnabled, clockForMidiFile);
  return {
    clockMode,
    clockMode != MidiFileClockMode::Disabled,
    clockMode == MidiFileClockMode::ClockAndTransport &&
      midiClockIntervalForTempo(fileTempo) != 0
  };
}

// ── EEPROM addresses ─────────────────────────────────────────────────────────

static constexpr int presetEepromAddress = 0;
static constexpr int controllerStateEepromAddress = presetEepromAddress + sizeof(int);

struct StoredControllerState {
  uint32_t magic;
  uint32_t version;
  uint32_t presetIndex;
  uint32_t presetSignature;
  uint32_t toggleMask;
  uint32_t checksum;
};

inline uint32_t controllerStateChecksum(const StoredControllerState &state) {
  uint32_t hash = 2166136261UL;
  const uint32_t words[] = {state.version, state.presetIndex, state.presetSignature, state.toggleMask};
  for (uint32_t word : words) {
    for (int byte = 0; byte < 4; ++byte) {
      hash = (hash ^ (word & 0xff)) * 16777619UL;
      word >>= 8;
    }
  }
  return hash;
}

inline StoredControllerState makeControllerState(int index, uint32_t signature, uint8_t mask) {
  StoredControllerState state = {0x53594e54UL, 1, static_cast<uint32_t>(index), signature, mask, 0};
  state.checksum = controllerStateChecksum(state);
  return state;
}

inline bool validControllerState(const StoredControllerState &state, int count) {
  return state.magic == 0x53594e54UL && state.version == 1 &&
    count > 0 && state.presetIndex < static_cast<uint32_t>(count) &&
    state.toggleMask < (1UL << switchCount) &&
    state.checksum == controllerStateChecksum(state);
}

template <typename Eeprom>
void writeControllerState(Eeprom &eeprom, const StoredControllerState &state) {
  StoredControllerState previous;
  eeprom.get(controllerStateEepromAddress, previous);
  if (memcmp(&state, &previous, sizeof(state)) == 0) return;
  StoredControllerState unpublished = state;
  unpublished.magic = 0;
  // Publish the marker last so interrupted writes are rejected.
  eeprom.put(controllerStateEepromAddress, unpublished);
  eeprom.put(controllerStateEepromAddress, state.magic);
  const int index = static_cast<int>(state.presetIndex);
  eeprom.put(presetEepromAddress, index);
}

inline uint8_t consumeBootToggleMask(StoredControllerState &state, int index,
                                    uint32_t signature, int count) {
  const uint8_t mask = validControllerState(state, count) &&
    state.presetIndex == static_cast<uint32_t>(index) && state.presetSignature == signature
      ? static_cast<uint8_t>(state.toggleMask) : 0;
  state.magic = 0;
  return mask;
}

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

struct MidiFileActivity {
  bool hasSent = false;
  uint32_t lastSentMillis = 0;

  void reset() { hasSent = false; }

  void recordSent(uint32_t now) {
    hasSent = true;
    lastSentMillis = now;
  }

  bool visible(bool playing, uint32_t now) const {
    return playing && hasSent &&
      static_cast<uint32_t>(now - lastSentMillis) < midiFileActivityHoldMs;
  }
};

inline int presetNameColumns(bool playingMidiFile) {
  return lcdColumnCount - (playingMidiFile ? 1 : 0);
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

/// Return the adjacent preset, wrapping at either end; -1 means no presets.
inline int adjacentPresetIndex(int currentPreset, int presetCount, int direction) {
  if (presetCount <= 0) return -1;
  if (currentPreset < 0 || currentPreset >= presetCount) return 0;
  if (direction > 0) return (currentPreset + 1) % presetCount;
  if (direction < 0) return (currentPreset + presetCount - 1) % presetCount;
  return currentPreset;
}

// ── EEPROM validation ────────────────────────────────────────────────────────

/// Validate a preset index read from EEPROM.  Returns 0 if out of range.
inline int validateStoredPreset(int value, int maxPresets) {
  if (value < 0 || value >= maxPresets) return 0;
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

  const int candidate = adjacentPresetIndex(currentPreset, presetCount, navigationDirection);

  if (candidate < 0 || candidate >= presetCount || candidate == currentPreset) {
    return -1;
  }

  if (candidate == alreadyPrefetchedIndex) {
    return -1;  // already prefetched
  }

  return candidate;
}

// ── Filename utilities ───────────────────────────────────────────────────────

/// Match JSON extensions even when FAT preserves an existing file's capitalization.
inline bool hasJsonExtension(const char *filename) {
  if (filename == nullptr) return false;
  const int nameLength = (int)strlen(filename);
  const int extLength = 5;  // ".json"
  if (nameLength < extLength) return false;
  const char *extension = filename + nameLength - extLength;
  return extension[0] == '.' &&
    (extension[1] == 'j' || extension[1] == 'J') &&
    (extension[2] == 's' || extension[2] == 'S') &&
    (extension[3] == 'o' || extension[3] == 'O') &&
    (extension[4] == 'n' || extension[4] == 'N');
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

// ── Debounce state machine ───────────────────────────────────────────────────

/// Debounce constants matching kimballa/button-debounce behaviour.
static constexpr unsigned long debounceIntervalMs = 25;

static constexpr uint8_t BTN_STATE_OPEN = 1;
static constexpr uint8_t BTN_STATE_PRESSED = 0;

/// A minimal pure-logic debounce state machine for unit testing.
/// Mirrors the algorithm in kimballa/button-debounce without Arduino dependencies.
struct DebounceState {
  uint8_t currentState;         // BTN_STATE_OPEN or BTN_STATE_PRESSED
  uint8_t priorPoll;            // last raw sample
  unsigned long readStartTime;  // when the current candidate reading began
  unsigned long pushInterval;   // ms to confirm a press
  unsigned long releaseInterval; // ms to confirm a release

  /// Initialize to open (unpressed) state.
  void init(unsigned long pushMs = debounceIntervalMs, unsigned long releaseMs = debounceIntervalMs) {
    currentState = BTN_STATE_OPEN;
    priorPoll = BTN_STATE_OPEN;
    readStartTime = 0;
    pushInterval = pushMs;
    releaseInterval = releaseMs;
  }

  /// Feed a new sample at the given time.  Returns true if state changed.
  bool update(uint8_t sample, unsigned long currentMs) {
    sample = (sample != 0) ? 1 : 0;  // collapse to 0/1

    if (sample != priorPoll) {
      readStartTime = currentMs;  // signal changed — reset timer
    }
    priorPoll = sample;

    unsigned long interval = (currentState == BTN_STATE_PRESSED) ? releaseInterval : pushInterval;

    if ((currentMs - readStartTime) > interval) {
      if (sample != currentState) {
        currentState = sample;
        return true;  // state changed
      }
    }
    return false;
  }
};

// ── Switch handler routing logic ─────────────────────────────────────────────

/// Determines the action to take when a debounced button event occurs.
/// This mirrors the firmware's switchHandler decision tree.
enum class SwitchAction {
  None,               // no action (not loaded, or invalid)
  ExecuteSwitch,      // fire switch logic for button 1/2/3
  NavigateNext,       // next preset button released
  NavigatePrev        // prev preset button released
};

/// Determine what action to take given a button event.
/// `btnId`: 1–5, `btnState`: BTN_STATE_PRESSED or BTN_STATE_OPEN
/// `hasLoaded`: whether initial load is complete
inline SwitchAction classifySwitchEvent(uint8_t btnId, uint8_t btnState,
                                        bool hasLoaded) {
  if (!hasLoaded) return SwitchAction::None;

  if (btnState == BTN_STATE_PRESSED) {
    if (btnId >= 1 && btnId <= 3) {
      return SwitchAction::ExecuteSwitch;
    }
    return SwitchAction::None;
  }

  if (btnState != BTN_STATE_OPEN) return SwitchAction::None;

  if (btnId == 4) {
    return SwitchAction::NavigateNext;
  }
  if (btnId == 5) {
    return SwitchAction::NavigatePrev;
  }

  return SwitchAction::None;
}
