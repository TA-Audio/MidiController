#pragma once

#include <ArduinoJson.h>
#include "logic.h"

struct PresetSignatureWriter {
  uint32_t hash = 2166136261UL;

  size_t write(uint8_t byte) {
    hash = (hash ^ byte) * 16777619UL;
    return 1;
  }

  size_t write(const uint8_t *bytes, size_t length) {
    for (size_t i = 0; i < length; ++i) write(bytes[i]);
    return length;
  }
};

inline uint32_t presetStateSignature(const char *fileName, JsonVariantConst preset) {
  PresetSignatureWriter writer;
  for (const char *p = fileName; *p; ++p) writer.write(static_cast<uint8_t>(*p));
  writer.write(0);
  serializeJson(preset, writer);
  return writer.hash;
}

inline bool isToggleCcSwitch(JsonVariantConst preset, int index) {
  if (index == 0 && !preset["FileInfo"].isNull()) return false;
  const char *keys[] = {"Switch1", "Switch2", "Switch3"};
  JsonVariantConst sw = preset[keys[index]];
  return sw["Toggle"].as<bool>() && (sw["CC"].is<JsonArrayConst>() ||
    sw["On"].is<JsonObjectConst>() || sw["Off"].is<JsonObjectConst>());
}

template <typename SendCc, typename SendPc>
void restoreToggleStates(JsonVariantConst preset, uint8_t savedMask, bool *states,
                         SendCc sendCc, SendPc sendPc) {
  const char *keys[] = {"Switch1", "Switch2", "Switch3"};
  for (int i = 0; i < switchCount; ++i) {
    states[i] = isToggleCcSwitch(preset, i) && (savedMask & (1 << i)) != 0;
    if (!isToggleCcSwitch(preset, i)) continue;
    JsonVariantConst sw = preset[keys[i]];
    if (sw["On"].is<JsonObjectConst>() || sw["Off"].is<JsonObjectConst>()) {
      JsonVariantConst action = sw[states[i] ? "On" : "Off"];
      for (JsonVariantConst pc : action["PC"].as<JsonArrayConst>()) {
        sendPc(pcJsonToMidi(pc["PC"].as<int>()), pc["Channel"].as<int>(),
               pc["USB"].as<bool>(), pc["USBDevice"].as<bool>());
      }
      for (JsonVariantConst cc : action["CC"].as<JsonArrayConst>()) {
        sendCc(cc["CC"].as<int>(), cc["Value"].as<int>(), cc["Channel"].as<int>(),
               cc["USB"].as<bool>(), cc["USBDevice"].as<bool>());
      }
    } else {
      for (JsonVariantConst cc : sw["CC"].as<JsonArrayConst>()) {
        sendCc(cc["CC"].as<int>(), states[i] ? 127 : 0,
               cc["Channel"].as<int>(), cc["USB"].as<bool>(), cc["USBDevice"].as<bool>());
      }
    }
  }
}

template <typename SendCc>
void restoreToggleStates(JsonVariantConst preset, uint8_t savedMask, bool *states,
                         SendCc sendCc) {
  const char *keys[] = {"Switch1", "Switch2", "Switch3"};
  for (int i = 0; i < switchCount; ++i) {
    states[i] = isToggleCcSwitch(preset, i) && (savedMask & (1 << i)) != 0;
    if (!isToggleCcSwitch(preset, i)) continue;
    for (JsonVariantConst cc : preset[keys[i]]["CC"].as<JsonArrayConst>()) {
      sendCc(cc["CC"].as<int>(), states[i] ? 127 : 0,
             cc["Channel"].as<int>(), cc["USB"].as<bool>());
    }
  }
}
