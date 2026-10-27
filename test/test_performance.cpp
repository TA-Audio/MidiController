#include <gtest/gtest.h>
#include "performance.h"
#include "live_protocol.h"
#include "performance_validation.h"
#include "toggle_state.h"
#include "midi_mock.h"

constexpr bool timingAssertions() {
  SwitchGesture single;
  if (single.update(true, 10, true, true) != GesturePress) return false;
  if (single.update(false, 20, true, true) != GestureRelease) return false;
  if (single.update(false, 319, true, true) != GestureNone) return false;
  if (single.update(false, 320, true, true) != GestureTap) return false;
  SwitchGesture held;
  held.update(true, UINT32_MAX - 100, true, true);
  if (held.update(false, 499, true, true) != (GestureHold | GestureRelease)) return false;
  SwitchGesture twice;
  twice.update(true, 10, true, true);
  twice.update(false, 20, true, true);
  twice.update(true, 200, true, true);
  if (twice.update(false, 210, true, true) != (GestureDouble | GestureRelease)) return false;
  if (twice.update(false, 900, true, true) != GestureNone) return false;
  TapTempo tempo;
  if (tempo.tap(100) != 0 || tempo.tap(150) != 0 || tempo.tap(600) != 120) return false;
  if (tempo.tap(1200) != 109 || tempo.tap(1600) != 120 || tempo.tap(2000) != 129) return false;
  if (tempo.tap(4001) != 0 || tempo.tap(6001) != 30) return false;
  return nextSequenceStep(15, 16) == 0 && nextSequenceStep(0, 0) == 0 &&
    nextSequenceStep(0, 17) == 0;
}

static_assert(timingAssertions(), "Performance timing and sequence invariants");

constexpr bool protocolAssertions() {
  const uint8_t payload[] = {0, 0x7f, 0x80, 0xff};
  const uint8_t golden[] = {0x7d, 0x53, 0x59, 1, 9, 5, 1, 2, 1, 0, 0, 7, 15, 8, 0, 15, 15};
  uint8_t packet[livePacketBytes] = {};
  uint8_t decoded[liveChunkBytes] = {};
  const int length = encodeLivePacket(packet, 9, 5, 130, true, payload, 4);
  if (length != sizeof(golden)) return false;
  for (int i = 0; i < length; ++i) if (packet[i] != golden[i]) return false;
  if (decodeLivePacket(packet, length, decoded) != 4) return false;
  for (int i = 0; i < 4; ++i) if (payload[i] != decoded[i]) return false;
  packet[liveHeaderBytes] = 16;
  if (decodeLivePacket(packet, length, decoded) != -1) return false;
  return encodeLivePacket(packet, 0, 5, 0, true, payload, 4) == -1 &&
    encodeLivePacket(packet, 1, 5, 16384, true, payload, 4) == -1 &&
    encodeLivePacket(packet, 1, 5, 0, true, payload, 97) == -1;
}

static_assert(protocolAssertions(), "Seven-bit USB codec golden vector and bounds");

TEST(SwitchGestures, TapAndMomentaryEdges) {
  SwitchGesture state;
  EXPECT_EQ(state.update(true, 10, true, false), GesturePress);
  EXPECT_EQ(state.update(true, 20, true, false), GestureNone);
  EXPECT_EQ(state.update(false, 30, true, false), GestureTap | GestureRelease);
  EXPECT_EQ(state.update(false, 1000, true, false), GestureNone);
}

TEST(SwitchGestures, HoldSuppressesTapAndStillReleases) {
  SwitchGesture state;
  state.update(true, 10, true, true);
  EXPECT_EQ(state.update(true, 609, true, true), GestureNone);
  EXPECT_EQ(state.update(true, 610, true, true), GestureHold);
  EXPECT_EQ(state.update(true, 611, true, true), GestureNone);
  EXPECT_EQ(state.update(false, 700, true, true), GestureRelease);
  EXPECT_EQ(state.update(false, 1200, true, true), GestureNone);
}

TEST(SwitchGestures, LateReleaseStillDetectsHold) {
  SwitchGesture state;
  state.update(true, 10, true, false);
  EXPECT_EQ(state.update(false, 1000, true, false), GestureHold | GestureRelease);
}

TEST(SwitchGestures, DoubleTapCancelsPendingPrimary) {
  SwitchGesture state;
  state.update(true, 10, true, true);
  EXPECT_EQ(state.update(false, 20, true, true), GestureRelease);
  EXPECT_EQ(state.update(false, 319, true, true), GestureNone);
  EXPECT_EQ(state.update(true, 319, true, true), GesturePress);
  EXPECT_EQ(state.update(false, 330, true, true), GestureDouble | GestureRelease);
  EXPECT_EQ(state.update(false, 900, true, true), GestureNone);
}

TEST(SwitchGestures, SingleWaitsExactlyDoubleWindow) {
  SwitchGesture state;
  state.update(true, 10, false, true);
  state.update(false, 20, false, true);
  EXPECT_EQ(state.update(false, 319, false, true), GestureNone);
  EXPECT_EQ(state.update(false, 320, false, true), GestureTap);
  EXPECT_EQ(state.update(false, 321, false, true), GestureNone);
}

TEST(SwitchGestures, TimersWrapAndPresetResetCancelsPending) {
  SwitchGesture state;
  state.update(true, UINT32_MAX - 100, true, false);
  EXPECT_EQ(state.update(true, 499, true, false), GestureHold);
  state = SwitchGesture{};
  EXPECT_EQ(state.update(false, 1000, true, true), GestureNone);
}

TEST(TapTempo, FirstTapPrimesThenAveragesLastThreeIntervals) {
  TapTempo tempo;
  EXPECT_EQ(tempo.tap(0), 0);
  EXPECT_EQ(tempo.tap(500), 120);
  EXPECT_EQ(tempo.tap(1100), 109);
  EXPECT_EQ(tempo.tap(1500), 120);
  EXPECT_EQ(tempo.tap(1900), 129);
}

TEST(TapTempo, RejectsBounceAndResetsAfterTwoSeconds) {
  TapTempo tempo;
  EXPECT_EQ(tempo.tap(100), 0);
  EXPECT_EQ(tempo.tap(150), 0);
  EXPECT_EQ(tempo.tap(300), 300);
  EXPECT_EQ(tempo.tap(2300), 55);
  EXPECT_EQ(tempo.tap(4301), 0);
  EXPECT_EQ(tempo.tap(6301), 30);
}

TEST(TapTempo, HandlesMillisWrap) {
  TapTempo tempo;
  tempo.tap(UINT32_MAX - 249);
  EXPECT_EQ(tempo.tap(250), 120);
}

TEST(SwitchSequence, WrapsAndRejectsUnsupportedLengths) {
  EXPECT_EQ(nextSequenceStep(0, 3), 1);
  EXPECT_EQ(nextSequenceStep(1, 3), 2);
  EXPECT_EQ(nextSequenceStep(2, 3), 0);
  EXPECT_EQ(nextSequenceStep(0, 1), 0);
  EXPECT_EQ(nextSequenceStep(0, 0), 0);
  EXPECT_EQ(nextSequenceStep(0, 17), 0);
}

TEST(LiveProtocol, RoundTripsEveryByteThroughSevenBitSafePackets) {
  uint8_t packet[livePacketBytes];
  uint8_t payload[liveChunkBytes], decoded[liveChunkBytes];
  for (int first = 0; first < 256; first += liveChunkBytes) {
    for (int i = 0; i < liveChunkBytes; ++i) payload[i] = first + i;
    const int length = encodeLivePacket(packet, 127, 0x43, 1234, true, payload, liveChunkBytes);
    EXPECT_EQ(length, livePacketBytes);
    for (int i = 0; i < length; ++i) EXPECT_LT(packet[i], 128);
    ASSERT_EQ(decodeLivePacket(packet, length, decoded), liveChunkBytes);
    EXPECT_EQ(memcmp(payload, decoded, liveChunkBytes), 0);
  }
}

TEST(LiveProtocol, RejectsMalformedPacketsAndOversizeChunks) {
  uint8_t packet[livePacketBytes], decoded[liveChunkBytes];
  const uint8_t payload[] = {255};
  const int length = encodeLivePacket(packet, 1, 5, 0, true, payload, 1);
  EXPECT_EQ(decodeLivePacket(packet, length - 1, decoded), -1);
  packet[liveHeaderBytes] = 16;
  EXPECT_EQ(decodeLivePacket(packet, length, decoded), -1);
  EXPECT_EQ(encodeLivePacket(packet, 1, 5, 0, true, payload, 97), -1);
  EXPECT_EQ(encodeLivePacket(packet, 1, 5, 16384, true, payload, 1), -1);
}

TEST(PerformanceValidation, LegacyPresetsRemainValidAndInvalidModesAreRejected) {
  StaticJsonDocument<2048> doc;
  deserializeJson(doc, R"({"Name":"Legacy","Switch1":{"Toggle":true,"CC":null}})");
  EXPECT_TRUE(validPerformancePreset(doc.as<JsonVariantConst>()));
  doc["Switch1"]["TapTempo"] = true;
  EXPECT_FALSE(validPerformancePreset(doc.as<JsonVariantConst>()));
  doc["Switch1"]["Toggle"] = false;
  EXPECT_TRUE(validPerformancePreset(doc.as<JsonVariantConst>()));
  doc["Switch1"]["Gestures"]["LongPress"]["CC"][0]["Channel"] = 17;
  EXPECT_FALSE(validPerformancePreset(doc.as<JsonVariantConst>()));
}

TEST(ExplicitToggle, RestoresExactPcAndCcStatesWithDeviceRouting) {
  clearMidiLog();
  StaticJsonDocument<2048> doc;
  deserializeJson(doc, R"({
    "Switch1":{"Toggle":true,
      "On":{"Name":"Lead","PC":[{"PC":9,"Channel":2,"USBDevice":true}],"CC":[{"CC":7,"Value":100,"Channel":2}]},
      "Off":{"Name":"Clean","PC":[{"PC":2,"Channel":2}],"CC":[{"CC":7,"Value":30,"Channel":2}]}}
  })");
  EXPECT_TRUE(validPerformancePreset(doc.as<JsonVariantConst>()));
  bool states[switchCount] = {};
  bool devicePc = false;
  restoreToggleStates(doc.as<JsonVariantConst>(), 1, states,
    [](int cc, int value, int channel, bool host, bool) { sendControlChange(cc, value, channel, host); },
    [&](int pc, int channel, bool host, bool device) { devicePc = device; sendProgramChange(pc, channel, host); });
  ASSERT_EQ(midiLog().size(), 2u);
  EXPECT_TRUE(states[0]);
  EXPECT_TRUE(devicePc);
  EXPECT_EQ(midiLog()[0].value1, 8);
  EXPECT_EQ(midiLog()[1].value2, 100);
  clearMidiLog();
  restoreToggleStates(doc.as<JsonVariantConst>(), 0, states,
    [](int cc, int value, int channel, bool host, bool) { sendControlChange(cc, value, channel, host); },
    [](int pc, int channel, bool host, bool) { sendProgramChange(pc, channel, host); });
  ASSERT_EQ(midiLog().size(), 2u);
  EXPECT_FALSE(states[0]);
  EXPECT_EQ(midiLog()[0].value1, 1);
  EXPECT_EQ(midiLog()[1].value2, 30);
}

TEST(ExplicitToggle, LegacyToggleRestoresComputerDestinationWithoutReplayingPc) {
  StaticJsonDocument<512> doc;
  deserializeJson(doc, R"({"Switch1":{"Toggle":true,"PC":[{"PC":9,"Channel":1}],
    "CC":[{"CC":7,"Value":30,"Channel":2,"USBDevice":true}]}})");
  bool states[switchCount] = {};
  int calls = 0;
  restoreToggleStates(doc.as<JsonVariantConst>(), 1, states,
    [&](int cc, int value, int channel, bool host, bool device) {
      ++calls;
      EXPECT_EQ(cc, 7);
      EXPECT_EQ(value, 127);
      EXPECT_EQ(channel, 2);
      EXPECT_FALSE(host);
      EXPECT_TRUE(device);
    },
    [](int, int, bool, bool) { ADD_FAILURE() << "Legacy toggle must not replay PC"; });
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(states[0]);
}
