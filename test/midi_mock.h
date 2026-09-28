#pragma once

// Minimal mock for MIDI output recording.
// Provides the same interface as the firmware's send functions,
// but records messages into a vector for test assertions.

#include <vector>
#include <cstring>

struct MidiMessage {
  enum Type { ProgramChange, ControlChange };
  Type type;
  int value1;   // PC program number, or CC number
  int value2;   // unused for PC (0), CC value for CC
  int channel;
  bool usb;
};

// Global log of all MIDI messages sent during a test
inline std::vector<MidiMessage> &midiLog() {
  static std::vector<MidiMessage> log;
  return log;
}

inline void clearMidiLog() {
  midiLog().clear();
}

// Mock send functions matching firmware signatures
inline void sendProgramChange(int pcValue, int channel, bool usbEvent) {
  const int safePc = clampMidi(pcValue);
  midiLog().push_back({MidiMessage::ProgramChange, safePc, 0, channel, usbEvent});
}

inline void sendControlChange(int ccNumber, int ccValue, int channel, bool usbEvent) {
  const int safeCcNumber = clampMidi(ccNumber);
  const int safeCcValue = clampMidi(ccValue);
  midiLog().push_back({MidiMessage::ControlChange, safeCcNumber, safeCcValue, channel, usbEvent});
}
