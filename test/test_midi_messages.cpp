#include <gtest/gtest.h>
#include <ArduinoJson.h>
#include "logic.h"
#include "midi_mock.h"

namespace outputTests {
namespace midi {
enum MidiType : uint8_t {
  NoteOff = 0x80, NoteOn = 0x90, ControlChange = 0xb0, ProgramChange = 0xc0,
  AfterTouchChannel = 0xd0, PitchBend = 0xe0, SystemExclusive = 0xf0,
  TimeCodeQuarterFrame = 0xf1, SongPosition = 0xf2, SongSelect = 0xf3,
  TuneRequest = 0xf6, Clock = 0xf8, Start = 0xfa, Stop = 0xfc
};
}
struct Message {
  uint8_t type, data1, data2, channel;
};
struct Wire {
  std::vector<Message> messages;
  std::vector<uint8_t> sysex;
  void send(uint8_t type, uint8_t data1, uint8_t data2, uint8_t channel, uint8_t = 0) {
    messages.push_back({type, data1, data2, channel});
  }
  void sendCommon(uint8_t type, unsigned data) { send(type, data & 127, data >> 7, 0); }
  void sendSysEx(unsigned length, const uint8_t *bytes, bool) { sysex.assign(bytes, bytes + length); }
} MIDI1, usbMIDI;
uint8_t usb_configuration = 0;
#define USB_MIDI_SERIAL
#include "midi_output.h"
#undef USB_MIDI_SERIAL

TEST(MidiOutput, DinAlwaysSendsAndUsbOnlyMirrorsWhenEnumerated) {
  MIDI1 = {};
  usbMIDI = {};
  usb_configuration = 0;
  sendDinMidi(midi::ControlChange, 64, 127, 3);
  EXPECT_EQ(MIDI1.messages.size(), 1u);
  EXPECT_TRUE(usbMIDI.messages.empty());
  usb_configuration = 1;
  for (auto type : {midi::NoteOn, midi::NoteOff, midi::ProgramChange, midi::AfterTouchChannel,
                    midi::PitchBend, midi::ControlChange}) {
    sendDinMidi(type, 12, 34, 16);
    const auto &din = MIDI1.messages.back();
    const auto &usb = usbMIDI.messages.back();
    EXPECT_EQ(din.type, usb.type);
    EXPECT_EQ(din.data1, usb.data1);
    EXPECT_EQ(din.data2, usb.data2);
    EXPECT_EQ(din.channel, usb.channel);
  }
  EXPECT_EQ(MIDI1.messages.size(), 7u);
  EXPECT_EQ(usbMIDI.messages.size(), 6u);
}

TEST(MidiOutput, ClockTransportAndSystemCommonReachBothOutputsExactlyOnce) {
  MIDI1 = {};
  usbMIDI = {};
  usb_configuration = 1;
  for (auto type : {midi::Clock, midi::Start, midi::Stop, midi::TimeCodeQuarterFrame,
                    midi::SongPosition, midi::SongSelect, midi::TuneRequest}) {
    sendDinMidi(type, 42, type == midi::SongPosition ? 7 : 0);
    EXPECT_EQ(MIDI1.messages.back().type, type);
    EXPECT_EQ(usbMIDI.messages.back().type, type);
    EXPECT_EQ(MIDI1.messages.back().data1, usbMIDI.messages.back().data1);
    EXPECT_EQ(MIDI1.messages.back().data2, usbMIDI.messages.back().data2);
  }
  EXPECT_EQ(MIDI1.messages.size(), 7u);
  EXPECT_EQ(usbMIDI.messages.size(), 7u);
}

TEST(MidiOutput, PassthroughPreservesChannelMessagesAndFramedSysex) {
  struct Input {
    int remaining = 1;
    midi::MidiType type = midi::NoteOn;
    uint8_t bytes[5] = {0xf0, 0x7d, 1, 2, 0xf7};
    bool read() { return remaining-- > 0; }
    midi::MidiType getType() { return type; }
    uint8_t getData1() { return 60; }
    uint8_t getData2() { return 100; }
    uint8_t getChannel() { return 2; }
    unsigned getSysExArrayLength() { return sizeof(bytes); }
    const uint8_t *getSysExArray() { return bytes; }
  } input;
  MIDI1 = {};
  usbMIDI = {};
  usb_configuration = 1;
  forwardMidiInput(input);
  ASSERT_EQ(MIDI1.messages.size(), 1u);
  ASSERT_EQ(usbMIDI.messages.size(), 1u);
  EXPECT_EQ(usbMIDI.messages[0].channel, 2);
  EXPECT_EQ(usbMIDI.messages[0].data1, 60);
  input.remaining = 1;
  input.type = midi::SystemExclusive;
  forwardMidiInput(input);
  EXPECT_EQ(MIDI1.sysex, (std::vector<uint8_t>{0xf0, 0x7d, 1, 2, 0xf7}));
  EXPECT_EQ(MIDI1.sysex, usbMIDI.sysex);
  usbMIDI.sysex.clear();
  usb_configuration = 0;
  input.remaining = 1;
  forwardMidiInput(input);
  EXPECT_TRUE(usbMIDI.sysex.empty());
}

TEST(MidiOutput, FileEventsPreserveDinBytesAndMirrorConfiguredChannelAndMessageSize) {
  struct Serial {
    std::vector<uint8_t> bytes;
    size_t write(uint8_t byte) { bytes.push_back(byte); return 1; }
    size_t write(const uint8_t *data, size_t count) {
      bytes.insert(bytes.end(), data, data + count);
      return count;
    }
  } serial;
  struct Event { uint8_t data[3]; uint8_t size; };
  usbMIDI = {};
  usb_configuration = 1;
  EXPECT_EQ(sendMidiFileEvent(serial, Event{{0x90, 60, 127}, 3}, 16), 3u);
  EXPECT_EQ(serial.bytes, (std::vector<uint8_t>{0x9f, 60, 127}));
  ASSERT_EQ(usbMIDI.messages.size(), 1u);
  EXPECT_EQ(usbMIDI.messages.back().channel, 16);
  EXPECT_EQ(usbMIDI.messages.back().data2, 127);
  EXPECT_EQ(sendMidiFileEvent(serial, Event{{0xc0, 5, 99}, 2}, 2), 2u);
  EXPECT_EQ(usbMIDI.messages.back().data2, 0);
  EXPECT_EQ(usbMIDI.messages.back().type, 0xc0);
  EXPECT_EQ(serial.bytes, (std::vector<uint8_t>{0x9f, 60, 127, 0xc1, 5}));
  EXPECT_EQ(sendMidiFileEvent(serial, Event{{0xf0, 5, 0}, 2}, 2), 0u);
  EXPECT_EQ(usbMIDI.messages.size(), 2u);
  usb_configuration = 0;
  EXPECT_EQ(sendMidiFileEvent(serial, Event{{0x80, 60, 0}, 3}, 2), 3u);
  EXPECT_EQ(usbMIDI.messages.size(), 2u);
}

TEST(MidiOutput, ContinuousPassthroughReturnsToTheLoopAfterABoundedBatch) {
  struct Input {
    int reads = 0;
    bool read() { ++reads; return true; }
    midi::MidiType getType() { return midi::NoteOn; }
    uint8_t getData1() { return 60; }
    uint8_t getData2() { return 100; }
    uint8_t getChannel() { return 1; }
    unsigned getSysExArrayLength() { return 0; }
    const uint8_t *getSysExArray() { return nullptr; }
  } input;
  MIDI1 = {};
  usbMIDI = {};
  usb_configuration = 1;
  forwardMidiInput(input);
  EXPECT_EQ(input.reads, 32);
  EXPECT_EQ(MIDI1.messages.size(), 32u);
  EXPECT_EQ(usbMIDI.messages.size(), 32u);
}

TEST(MidiOutput, MirrorDisabledKeepsDinEventsClockAndFileEventsWithoutUsbCopies) {
  MIDI1 = {};
  usbMIDI = {};
  usb_configuration = 1;
  for (auto type : {midi::NoteOn, midi::NoteOff, midi::ControlChange, midi::ProgramChange,
                    midi::Clock, midi::Start, midi::Stop, midi::SongPosition}) {
    sendDinMidi(type, 12, 34, 3, false);
  }
  EXPECT_EQ(MIDI1.messages.size(), 8u);
  EXPECT_TRUE(usbMIDI.messages.empty());
  struct Serial {
    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t *, size_t count) { return count; }
  } serial;
  struct Event { uint8_t data[3]; uint8_t size; };
  EXPECT_EQ(sendMidiFileEvent(serial, Event{{0x90, 60, 127}, 3}, 1, false), 3u);
  EXPECT_TRUE(usbMIDI.messages.empty());
  usbMIDI.send(midi::ControlChange, 64, 127, 2);
  EXPECT_EQ(usbMIDI.messages.size(), 1u);
}

TEST(MidiOutput, DisabledInputsDrainWithoutForwardingAndSysexObeysMirrorPreference) {
  struct Input {
    int remaining = 1;
    midi::MidiType type = midi::NoteOn;
    uint8_t bytes[3] = {0xf0, 0x7d, 0xf7};
    bool read() { return remaining-- > 0; }
    midi::MidiType getType() { return type; }
    uint8_t getData1() { return 60; }
    uint8_t getData2() { return 100; }
    uint8_t getChannel() { return 2; }
    unsigned getSysExArrayLength() { return sizeof(bytes); }
    const uint8_t *getSysExArray() { return bytes; }
  } first, second;
  MIDI1 = {};
  usbMIDI = {};
  usb_configuration = 1;
  forwardMidiInput(first, false, true);
  EXPECT_LE(first.remaining, 0);
  EXPECT_TRUE(MIDI1.messages.empty());
  forwardMidiInput(second, true, false);
  EXPECT_EQ(MIDI1.messages.size(), 1u);
  EXPECT_TRUE(usbMIDI.messages.empty());
  first.remaining = 1;
  first.type = midi::SystemExclusive;
  forwardMidiInput(first, false, true);
  EXPECT_TRUE(MIDI1.sysex.empty());
  first.remaining = 1;
  forwardMidiInput(first, true, false);
  EXPECT_EQ(MIDI1.sysex, (std::vector<uint8_t>{0xf0, 0x7d, 0xf7}));
  EXPECT_TRUE(usbMIDI.sysex.empty());
  first.remaining = 1;
  forwardMidiInput(first, true, true);
  EXPECT_EQ(MIDI1.sysex, usbMIDI.sysex);
}
} // namespace outputTests

// ═══════════════════════════════════════════════════════════════════════════════
// Helpers — replicate firmware logic using mock sends
// ═══════════════════════════════════════════════════════════════════════════════

static void sendPcArray(JsonArray pcArray) {
  if (pcArray.isNull()) return;
  for (JsonVariant pcEvent : pcArray) {
    sendProgramChange(pcJsonToMidi(pcEvent["PC"].as<int>()), pcEvent["Channel"], pcEvent["USB"]);
  }
}

static void sendCcArray(JsonArray ccArray) {
  if (ccArray.isNull()) return;
  for (JsonVariant ccEvent : ccArray) {
    sendControlChange(ccEvent["CC"], ccEvent["Value"], ccEvent["Channel"], ccEvent["USB"]);
  }
}

/// Simulates executeSwitchLogic: processes a switch press and logs MIDI messages.
/// Returns the new toggle state for the switch.
static bool executeSwitchPress(JsonObject switchLogic, bool wasToggled) {
  sendPcArray(switchLogic["PC"]);

  JsonArray ccArray = switchLogic["CC"];
  if (!ccArray.isNull()) {
    bool toggle = switchLogic["Toggle"].as<bool>();
    bool nextToggleState = false;
    bool useToggleValue = false;

    if (toggle) {
      nextToggleState = !wasToggled;
      useToggleValue = true;
    }

    for (JsonVariant cc : ccArray) {
      int ccNumber = cc["CC"];
      int ccValue = cc["Value"];

      if (useToggleValue) {
        ccValue = toggleCcValue(wasToggled);
      }

      int ccChannel = cc["Channel"];
      bool usbEvent = cc["USB"];
      sendControlChange(ccNumber, ccValue, ccChannel, usbEvent);
    }

    if (useToggleValue) {
      return nextToggleState;
    }
  }

  return wasToggled;
}

/// Simulates changePreset OnLoad: sends PC and CC arrays from preset OnLoad.
static void processPresetOnLoad(JsonObject preset) {
  sendPcArray(preset["OnLoad"]["PC"]);
  sendCcArray(preset["OnLoad"]["CC"]);
}

/// Simulates leaving a preset: sends only the preset's OnExit CC messages.
static void processPresetOnExit(JsonObject preset) {
  sendCcArray(preset["OnExit"]["CC"]);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Test fixture
// ═══════════════════════════════════════════════════════════════════════════════

class MidiMessageTest : public ::testing::Test {
protected:
  void SetUp() override {
    clearMidiLog();
  }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Preset OnLoad — MIDI messages sent when navigating to a new preset
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(MidiMessageTest, PresetOnLoad_SendsPcAndCc) {
  // Matches configExample.json: OnLoad has PC=2 ch11 and CC=1 val=127 ch11
  StaticJsonDocument<1024> doc;
  deserializeJson(doc, R"({
    "Name": "Preset1",
    "OnLoad": {
      "PC": [{"PC": 2, "Channel": 11}],
      "CC": [{"CC": 1, "Value": 127, "Channel": 11}]
    }
  })");

  processPresetOnLoad(doc.as<JsonObject>());

  ASSERT_EQ(midiLog().size(), 2u);

  // PC: JSON PC=2 → MIDI PC=1 (pcJsonToMidi applies -1), channel 11, serial (USB defaults false)
  EXPECT_EQ(midiLog()[0].type, MidiMessage::ProgramChange);
  EXPECT_EQ(midiLog()[0].value1, 1);  // 2-1=1
  EXPECT_EQ(midiLog()[0].channel, 11);
  EXPECT_FALSE(midiLog()[0].usb);

  // CC: CC#1, Value 127, channel 11, serial
  EXPECT_EQ(midiLog()[1].type, MidiMessage::ControlChange);
  EXPECT_EQ(midiLog()[1].value1, 1);
  EXPECT_EQ(midiLog()[1].value2, 127);
  EXPECT_EQ(midiLog()[1].channel, 11);
  EXPECT_FALSE(midiLog()[1].usb);
}

TEST_F(MidiMessageTest, PresetOnLoad_UsbFlag) {
  StaticJsonDocument<512> doc;
  deserializeJson(doc, R"({
    "OnLoad": {
      "PC": [{"PC": 5, "Channel": 1, "USB": true}],
      "CC": null
    }
  })");

  processPresetOnLoad(doc.as<JsonObject>());

  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_EQ(midiLog()[0].type, MidiMessage::ProgramChange);
  EXPECT_EQ(midiLog()[0].value1, 4);  // 5-1=4
  EXPECT_EQ(midiLog()[0].channel, 1);
  EXPECT_TRUE(midiLog()[0].usb);
}

TEST_F(MidiMessageTest, PresetOnLoad_NullOnLoad_NoMessages) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, R"({"Name": "Empty", "OnLoad": null})");

  processPresetOnLoad(doc.as<JsonObject>());

  EXPECT_EQ(midiLog().size(), 0u);
}

TEST_F(MidiMessageTest, PresetOnLoad_MultiplePcEvents) {
  StaticJsonDocument<512> doc;
  deserializeJson(doc, R"({
    "OnLoad": {
      "PC": [
        {"PC": 10, "Channel": 1, "USB": false},
        {"PC": 20, "Channel": 2, "USB": true}
      ],
      "CC": null
    }
  })");

  processPresetOnLoad(doc.as<JsonObject>());

  ASSERT_EQ(midiLog().size(), 2u);
  EXPECT_EQ(midiLog()[0].value1, 9);   // 10-1
  EXPECT_EQ(midiLog()[0].channel, 1);
  EXPECT_FALSE(midiLog()[0].usb);
  EXPECT_EQ(midiLog()[1].value1, 19);  // 20-1
  EXPECT_EQ(midiLog()[1].channel, 2);
  EXPECT_TRUE(midiLog()[1].usb);
}

TEST_F(MidiMessageTest, PresetOnLoad_MidiFilePreset) {
  // configExampleWithFile.json: OnLoad PC=98 ch11
  StaticJsonDocument<512> doc;
  deserializeJson(doc, R"({
    "Name": "Unsustainable",
    "OnLoad": {"PC": [{"Channel": 11, "PC": 98}], "CC": null}
  })");

  processPresetOnLoad(doc.as<JsonObject>());

  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_EQ(midiLog()[0].value1, 97);  // 98-1
  EXPECT_EQ(midiLog()[0].channel, 11);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Switch button press — non-toggle switch sends CC values as-is
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(MidiMessageTest, SwitchPress_NonToggle_SendsCcAsIs) {
  // configExample.json Switch2: Toggle=false, CC=[{CC:3, Value:127, Ch:11}, {CC:5, Value:0, Ch:11}]
  StaticJsonDocument<512> doc;
  deserializeJson(doc, R"({
    "Name": "QECUN",
    "Toggle": false,
    "CC": [
      {"CC": 3, "Value": 127, "Channel": 11},
      {"CC": 5, "Value": 0, "Channel": 11}
    ],
    "PC": null
  })");

  executeSwitchPress(doc.as<JsonObject>(), false);

  ASSERT_EQ(midiLog().size(), 2u);
  // CC#3 = 127
  EXPECT_EQ(midiLog()[0].type, MidiMessage::ControlChange);
  EXPECT_EQ(midiLog()[0].value1, 3);
  EXPECT_EQ(midiLog()[0].value2, 127);
  EXPECT_EQ(midiLog()[0].channel, 11);
  // CC#5 = 0
  EXPECT_EQ(midiLog()[1].type, MidiMessage::ControlChange);
  EXPECT_EQ(midiLog()[1].value1, 5);
  EXPECT_EQ(midiLog()[1].value2, 0);
  EXPECT_EQ(midiLog()[1].channel, 11);
}

TEST_F(MidiMessageTest, SwitchPress_NonToggle_SendsPc) {
  // configExample.json Switch3: PC=[{PC:2, Channel:11, USB:true}]
  StaticJsonDocument<512> doc;
  deserializeJson(doc, R"({
    "Name": "GLAKX",
    "Toggle": false,
    "CC": null,
    "PC": [{"PC": 2, "Channel": 11, "USB": true}]
  })");

  executeSwitchPress(doc.as<JsonObject>(), false);

  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_EQ(midiLog()[0].type, MidiMessage::ProgramChange);
  EXPECT_EQ(midiLog()[0].value1, 1);  // 2-1=1
  EXPECT_EQ(midiLog()[0].channel, 11);
  EXPECT_TRUE(midiLog()[0].usb);
}

TEST_F(MidiMessageTest, SwitchPress_NonToggle_NoPcNoCc_NoMessages) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, R"({
    "Name": "Empty",
    "Toggle": false,
    "CC": null,
    "PC": null
  })");

  executeSwitchPress(doc.as<JsonObject>(), false);

  EXPECT_EQ(midiLog().size(), 0u);
}

TEST_F(MidiMessageTest, SwitchPress_NonToggle_PcAndCc) {
  StaticJsonDocument<512> doc;
  deserializeJson(doc, R"({
    "Name": "Combo",
    "Toggle": false,
    "PC": [{"PC": 5, "Channel": 1, "USB": false}],
    "CC": [{"CC": 10, "Value": 100, "Channel": 2, "USB": true}]
  })");

  executeSwitchPress(doc.as<JsonObject>(), false);

  ASSERT_EQ(midiLog().size(), 2u);
  // PC sent first
  EXPECT_EQ(midiLog()[0].type, MidiMessage::ProgramChange);
  EXPECT_EQ(midiLog()[0].value1, 4);  // 5-1
  EXPECT_EQ(midiLog()[0].channel, 1);
  EXPECT_FALSE(midiLog()[0].usb);
  // Then CC
  EXPECT_EQ(midiLog()[1].type, MidiMessage::ControlChange);
  EXPECT_EQ(midiLog()[1].value1, 10);
  EXPECT_EQ(midiLog()[1].value2, 100);
  EXPECT_EQ(midiLog()[1].channel, 2);
  EXPECT_TRUE(midiLog()[1].usb);
}

TEST_F(MidiMessageTest, PresetOnExit_SendsCcMessagesOnly) {
  StaticJsonDocument<512> doc;
  deserializeJson(doc, R"({
    "OnExit": {
      "PC": [{"PC": 5, "Channel": 1}],
      "CC": [
        {"CC": 12, "Value": 0, "Channel": 2},
        {"CC": 13, "Value": 127, "Channel": 3, "USB": true}
      ]
    }
  })");

  processPresetOnExit(doc.as<JsonObject>());

  ASSERT_EQ(midiLog().size(), 2u);
  EXPECT_EQ(midiLog()[0].type, MidiMessage::ControlChange);
  EXPECT_EQ(midiLog()[0].value1, 12);
  EXPECT_EQ(midiLog()[0].value2, 0);
  EXPECT_EQ(midiLog()[0].channel, 2);
  EXPECT_FALSE(midiLog()[0].usb);
  EXPECT_EQ(midiLog()[1].type, MidiMessage::ControlChange);
  EXPECT_EQ(midiLog()[1].value1, 13);
  EXPECT_EQ(midiLog()[1].value2, 127);
  EXPECT_EQ(midiLog()[1].channel, 3);
  EXPECT_TRUE(midiLog()[1].usb);
}

TEST_F(MidiMessageTest, PresetOnExit_MissingOrNullOnExit_NoMessages) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, R"({"Name": "Empty", "OnExit": null})");

  processPresetOnExit(doc.as<JsonObject>());

  EXPECT_EQ(midiLog().size(), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Switch button press — toggle switch overrides CC value
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(MidiMessageTest, SwitchPress_Toggle_FirstPress_Sends127) {
  // configExample.json Switch1: Toggle=true, CC=[{CC:1,Val:127,Ch:11}, {CC:4,Val:127,Ch:11}]
  StaticJsonDocument<512> doc;
  deserializeJson(doc, R"({
    "Name": "JAXDF",
    "Toggle": true,
    "CC": [
      {"CC": 1, "Value": 127, "Channel": 11},
      {"CC": 4, "Value": 127, "Channel": 11}
    ],
    "PC": null
  })");

  // wasToggled=false → turning ON → CC value = 127
  bool newState = executeSwitchPress(doc.as<JsonObject>(), false);

  EXPECT_TRUE(newState);  // Toggle is now ON
  ASSERT_EQ(midiLog().size(), 2u);
  // Both CCs get value 127 (toggle ON overrides JSON value)
  EXPECT_EQ(midiLog()[0].value1, 1);
  EXPECT_EQ(midiLog()[0].value2, 127);
  EXPECT_EQ(midiLog()[0].channel, 11);
  EXPECT_EQ(midiLog()[1].value1, 4);
  EXPECT_EQ(midiLog()[1].value2, 127);
  EXPECT_EQ(midiLog()[1].channel, 11);
}

TEST_F(MidiMessageTest, SwitchPress_Toggle_SecondPress_Sends0) {
  StaticJsonDocument<512> doc;
  deserializeJson(doc, R"({
    "Name": "JAXDF",
    "Toggle": true,
    "CC": [
      {"CC": 1, "Value": 127, "Channel": 11},
      {"CC": 4, "Value": 127, "Channel": 11}
    ],
    "PC": null
  })");

  // wasToggled=true → turning OFF → CC value = 0
  bool newState = executeSwitchPress(doc.as<JsonObject>(), true);

  EXPECT_FALSE(newState);  // Toggle is now OFF
  ASSERT_EQ(midiLog().size(), 2u);
  EXPECT_EQ(midiLog()[0].value2, 0);
  EXPECT_EQ(midiLog()[1].value2, 0);
}

TEST_F(MidiMessageTest, SwitchPress_Toggle_FullCycle) {
  StaticJsonDocument<512> doc;
  deserializeJson(doc, R"({
    "Name": "Effect",
    "Toggle": true,
    "CC": [{"CC": 50, "Value": 64, "Channel": 5, "USB": true}],
    "PC": null
  })");

  // Press 1: OFF → ON (sends 127)
  bool state = executeSwitchPress(doc.as<JsonObject>(), false);
  EXPECT_TRUE(state);
  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_EQ(midiLog()[0].value2, 127);
  EXPECT_TRUE(midiLog()[0].usb);

  clearMidiLog();

  // Press 2: ON → OFF (sends 0)
  state = executeSwitchPress(doc.as<JsonObject>(), state);
  EXPECT_FALSE(state);
  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_EQ(midiLog()[0].value2, 0);

  clearMidiLog();

  // Press 3: OFF → ON again (sends 127)
  state = executeSwitchPress(doc.as<JsonObject>(), state);
  EXPECT_TRUE(state);
  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_EQ(midiLog()[0].value2, 127);
}

TEST_F(MidiMessageTest, SwitchPress_Toggle_WithPc_BothSent) {
  // Toggle switch that also sends a PC
  StaticJsonDocument<512> doc;
  deserializeJson(doc, R"({
    "Name": "Combo",
    "Toggle": true,
    "PC": [{"PC": 3, "Channel": 1, "USB": false}],
    "CC": [{"CC": 20, "Value": 64, "Channel": 1}]
  })");

  bool state = executeSwitchPress(doc.as<JsonObject>(), false);
  EXPECT_TRUE(state);
  ASSERT_EQ(midiLog().size(), 2u);
  // PC sent first (always, regardless of toggle)
  EXPECT_EQ(midiLog()[0].type, MidiMessage::ProgramChange);
  EXPECT_EQ(midiLog()[0].value1, 2);  // 3-1
  // CC with toggle value
  EXPECT_EQ(midiLog()[1].type, MidiMessage::ControlChange);
  EXPECT_EQ(midiLog()[1].value2, 127);  // toggle ON
}

// ═══════════════════════════════════════════════════════════════════════════════
// Value clamping — out-of-range values in JSON are safely clamped
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(MidiMessageTest, Clamping_PcAbove127) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, R"({"OnLoad": {"PC": [{"PC": 200, "Channel": 1}], "CC": null}})");

  processPresetOnLoad(doc.as<JsonObject>());

  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_EQ(midiLog()[0].value1, 127);  // 200-1=199 → clamped to 127
}

TEST_F(MidiMessageTest, Clamping_PcZero) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, R"({"OnLoad": {"PC": [{"PC": 0, "Channel": 1}], "CC": null}})");

  processPresetOnLoad(doc.as<JsonObject>());

  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_EQ(midiLog()[0].value1, 0);  // 0-1=-1 → clamped to 0
}

TEST_F(MidiMessageTest, Clamping_CcNumberAbove127) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, R"({"OnLoad": {"PC": null, "CC": [{"CC": 200, "Value": 50, "Channel": 1}]}})");

  processPresetOnLoad(doc.as<JsonObject>());

  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_EQ(midiLog()[0].value1, 127);  // CC# clamped
  EXPECT_EQ(midiLog()[0].value2, 50);
}

TEST_F(MidiMessageTest, Clamping_CcValueAbove127) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, R"({"OnLoad": {"PC": null, "CC": [{"CC": 10, "Value": 255, "Channel": 1}]}})");

  processPresetOnLoad(doc.as<JsonObject>());

  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_EQ(midiLog()[0].value1, 10);
  EXPECT_EQ(midiLog()[0].value2, 127);  // value clamped
}

TEST_F(MidiMessageTest, Clamping_NegativeCcValue) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, R"({"OnLoad": {"PC": null, "CC": [{"CC": 1, "Value": -5, "Channel": 1}]}})");

  processPresetOnLoad(doc.as<JsonObject>());

  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_EQ(midiLog()[0].value2, 0);  // clamped to 0
}

// ═══════════════════════════════════════════════════════════════════════════════
// Full preset scenario — simulates complete preset navigation + switch presses
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(MidiMessageTest, FullScenario_LoadPresetThenPressSwitch) {
  // Load a preset → get OnLoad messages
  StaticJsonDocument<2048> doc;
  deserializeJson(doc, R"({
    "Name": "Preset1",
    "OnLoad": {
      "PC": [{"PC": 2, "Channel": 11}],
      "CC": [{"CC": 1, "Value": 127, "Channel": 11}]
    },
    "Switch1": {
      "Name": "Drive",
      "Toggle": true,
      "CC": [{"CC": 1, "Value": 127, "Channel": 11}, {"CC": 4, "Value": 127, "Channel": 11}],
      "PC": null
    },
    "Switch3": {
      "Name": "Solo",
      "Toggle": false,
      "CC": null,
      "PC": [{"PC": 2, "Channel": 11, "USB": true}]
    }
  })");

  // Step 1: Navigate to preset → OnLoad fires
  processPresetOnLoad(doc.as<JsonObject>());
  ASSERT_EQ(midiLog().size(), 2u);
  EXPECT_EQ(midiLog()[0].type, MidiMessage::ProgramChange);
  EXPECT_EQ(midiLog()[0].value1, 1);  // PC 2→1
  EXPECT_EQ(midiLog()[1].type, MidiMessage::ControlChange);
  EXPECT_EQ(midiLog()[1].value1, 1);
  EXPECT_EQ(midiLog()[1].value2, 127);

  clearMidiLog();

  // Step 2: Press Switch1 (toggle ON)
  bool sw1State = executeSwitchPress(doc["Switch1"], false);
  EXPECT_TRUE(sw1State);
  ASSERT_EQ(midiLog().size(), 2u);
  EXPECT_EQ(midiLog()[0].value1, 1);   // CC#1 = 127
  EXPECT_EQ(midiLog()[0].value2, 127);
  EXPECT_EQ(midiLog()[1].value1, 4);   // CC#4 = 127
  EXPECT_EQ(midiLog()[1].value2, 127);

  clearMidiLog();

  // Step 3: Press Switch1 again (toggle OFF)
  sw1State = executeSwitchPress(doc["Switch1"], sw1State);
  EXPECT_FALSE(sw1State);
  ASSERT_EQ(midiLog().size(), 2u);
  EXPECT_EQ(midiLog()[0].value2, 0);  // CC#1 = 0
  EXPECT_EQ(midiLog()[1].value2, 0);  // CC#4 = 0

  clearMidiLog();

  // Step 4: Press Switch3 (non-toggle, sends PC via USB)
  executeSwitchPress(doc["Switch3"], false);
  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_EQ(midiLog()[0].type, MidiMessage::ProgramChange);
  EXPECT_EQ(midiLog()[0].value1, 1);  // PC 2→1
  EXPECT_TRUE(midiLog()[0].usb);
}

TEST_F(MidiMessageTest, FullScenario_NavigateBetweenPresets) {
  // Simulates pressing next preset button: old preset → new preset OnLoad
  StaticJsonDocument<512> doc1;
  deserializeJson(doc1, R"({
    "OnLoad": {"PC": [{"PC": 10, "Channel": 1}], "CC": null}
  })");

  StaticJsonDocument<512> doc2;
  deserializeJson(doc2, R"({
    "OnLoad": {"PC": [{"PC": 20, "Channel": 2, "USB": true}], "CC": [{"CC": 80, "Value": 100, "Channel": 2}]}
  })");

  // Load first preset
  processPresetOnLoad(doc1.as<JsonObject>());
  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_EQ(midiLog()[0].value1, 9);  // 10-1

  clearMidiLog();

  // Navigate to second preset
  processPresetOnLoad(doc2.as<JsonObject>());
  ASSERT_EQ(midiLog().size(), 2u);
  EXPECT_EQ(midiLog()[0].type, MidiMessage::ProgramChange);
  EXPECT_EQ(midiLog()[0].value1, 19);  // 20-1
  EXPECT_EQ(midiLog()[0].channel, 2);
  EXPECT_TRUE(midiLog()[0].usb);
  EXPECT_EQ(midiLog()[1].type, MidiMessage::ControlChange);
  EXPECT_EQ(midiLog()[1].value1, 80);
  EXPECT_EQ(midiLog()[1].value2, 100);
}

TEST_F(MidiMessageTest, FullScenario_TogglesResetOnPresetChange) {
  // Verify that toggle states don't carry over between presets
  StaticJsonDocument<512> doc;
  deserializeJson(doc, R"({
    "Name": "Toggle",
    "Toggle": true,
    "CC": [{"CC": 50, "Value": 0, "Channel": 1}],
    "PC": null
  })");

  // First press: toggle ON
  bool sw1 = executeSwitchPress(doc.as<JsonObject>(), false);
  EXPECT_TRUE(sw1);
  EXPECT_EQ(midiLog()[0].value2, 127);

  clearMidiLog();

  // Simulate preset change → toggles reset
  bool togglesAfterPresetChange[switchCount] = {};

  // Now press the same switch on the "new" preset (toggle starts at false again)
  bool sw1AfterChange = executeSwitchPress(doc.as<JsonObject>(), togglesAfterPresetChange[0]);
  EXPECT_TRUE(sw1AfterChange);  // starts fresh → ON
  EXPECT_EQ(midiLog()[0].value2, 127);  // sends 127 again (reset worked)
}

// ═══════════════════════════════════════════════════════════════════════════════
// Edge cases — USB routing
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(MidiMessageTest, UsbRouting_DefaultsToFalse) {
  // When JSON doesn't specify "USB" field, it should default to false
  StaticJsonDocument<256> doc;
  deserializeJson(doc, R"({"OnLoad": {"PC": [{"PC": 1, "Channel": 1}], "CC": null}})");

  processPresetOnLoad(doc.as<JsonObject>());

  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_FALSE(midiLog()[0].usb);
}

TEST_F(MidiMessageTest, UsbRouting_ExplicitTrue) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, R"({"OnLoad": {"PC": [{"PC": 1, "Channel": 1, "USB": true}], "CC": null}})");

  processPresetOnLoad(doc.as<JsonObject>());

  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_TRUE(midiLog()[0].usb);
}

TEST_F(MidiMessageTest, UsbRouting_ExplicitFalse) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, R"({"OnLoad": {"PC": [{"PC": 1, "Channel": 1, "USB": false}], "CC": null}})");

  processPresetOnLoad(doc.as<JsonObject>());

  ASSERT_EQ(midiLog().size(), 1u);
  EXPECT_FALSE(midiLog()[0].usb);
}

TEST_F(MidiMessageTest, UsbRouting_MixedInSamePreset) {
  StaticJsonDocument<1024> doc;
  deserializeJson(doc, R"({
    "OnLoad": {
      "PC": [
        {"PC": 1, "Channel": 1, "USB": true},
        {"PC": 2, "Channel": 2, "USB": false}
      ],
      "CC": [
        {"CC": 1, "Value": 127, "Channel": 1, "USB": true},
        {"CC": 2, "Value": 64, "Channel": 2, "USB": false}
      ]
    }
  })");

  processPresetOnLoad(doc.as<JsonObject>());

  ASSERT_EQ(midiLog().size(), 4u);
  EXPECT_TRUE(midiLog()[0].usb);   // PC USB
  EXPECT_FALSE(midiLog()[1].usb);  // PC serial
  EXPECT_TRUE(midiLog()[2].usb);   // CC USB
  EXPECT_FALSE(midiLog()[3].usb);  // CC serial
}

// ═══════════════════════════════════════════════════════════════════════════════
// Channel routing — messages go to correct MIDI channel
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(MidiMessageTest, ChannelRouting_DifferentChannelsPerEvent) {
  StaticJsonDocument<1024> doc;
  deserializeJson(doc, R"({
    "Name": "MultiChannel",
    "Toggle": false,
    "PC": [{"PC": 1, "Channel": 3, "USB": false}],
    "CC": [
      {"CC": 1, "Value": 127, "Channel": 5},
      {"CC": 2, "Value": 64, "Channel": 10}
    ]
  })");

  executeSwitchPress(doc.as<JsonObject>(), false);

  ASSERT_EQ(midiLog().size(), 3u);
  EXPECT_EQ(midiLog()[0].channel, 3);   // PC to ch3
  EXPECT_EQ(midiLog()[1].channel, 5);   // CC to ch5
  EXPECT_EQ(midiLog()[2].channel, 10);  // CC to ch10
}

// ═══════════════════════════════════════════════════════════════════════════════
// PC offset verification — JSON PC values are 1-indexed, MIDI sends 0-indexed
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(MidiMessageTest, PcOffset_Pc1SendsZero) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, R"({"OnLoad": {"PC": [{"PC": 1, "Channel": 1}], "CC": null}})");
  processPresetOnLoad(doc.as<JsonObject>());
  EXPECT_EQ(midiLog()[0].value1, 0);
}

TEST_F(MidiMessageTest, PcOffset_Pc128Sends127) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, R"({"OnLoad": {"PC": [{"PC": 128, "Channel": 1}], "CC": null}})");
  processPresetOnLoad(doc.as<JsonObject>());
  EXPECT_EQ(midiLog()[0].value1, 127);
}

TEST_F(MidiMessageTest, PcOffset_Pc64Sends63) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, R"({"OnLoad": {"PC": [{"PC": 64, "Channel": 1}], "CC": null}})");
  processPresetOnLoad(doc.as<JsonObject>());
  EXPECT_EQ(midiLog()[0].value1, 63);
}
