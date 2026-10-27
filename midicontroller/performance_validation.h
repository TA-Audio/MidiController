#pragma once

#include <ArduinoJson.h>
#include "performance.h"

inline bool validActionMessages(JsonVariantConst action) {
  if (!action.is<JsonObjectConst>()) return false;
  const char *types[] = {"PC", "CC"};
  for (const char *type : types) {
    JsonVariantConst list = action[type];
    if (!list.isNull() && !list.is<JsonArrayConst>()) return false;
    for (JsonVariantConst message : list.as<JsonArrayConst>()) {
      if (!message.is<JsonObjectConst>() || !message["Channel"].is<int>() ||
          message["Channel"].as<int>() < 1 || message["Channel"].as<int>() > 16 ||
          (message["USB"].as<bool>() && message["USBDevice"].as<bool>())) return false;
      const bool pc = strcmp(type, "PC") == 0;
      const int value = message[type].as<int>();
      if (!message[type].is<int>() || value < (pc ? 1 : 0) || value > (pc ? 128 : 127)) return false;
      if (!pc && (!message["Value"].is<int>() || message["Value"].as<int>() < 0 ||
                  message["Value"].as<int>() > 127)) return false;
    }
  }
  return true;
}

inline bool validPerformancePreset(JsonVariantConst preset) {
  if (!preset.is<JsonObjectConst>()) return false;
  const char *keys[] = {"Switch1", "Switch2", "Switch3"};
  const char *gestures[] = {"Press", "Release", "LongPress", "DoubleTap"};
  for (const char *key : keys) {
    JsonVariantConst sw = preset[key];
    const bool explicitStates = sw.containsKey("On") || sw.containsKey("Off");
    const bool sequence = sw.containsKey("Sequence");
    const bool tap = sw["TapTempo"].as<bool>();
    if (sw.containsKey("TapTempo") && !sw["TapTempo"].is<bool>()) return false;
    if (static_cast<int>(sw["Toggle"].as<bool>()) + static_cast<int>(sequence) +
        static_cast<int>(tap) > 1) return false;
    if (explicitStates && (!sw["Toggle"].as<bool>() ||
        !validActionMessages(sw["On"]) || !validActionMessages(sw["Off"]))) return false;
    if (sequence) {
      JsonVariantConst steps = sw["Sequence"];
      if (!steps.is<JsonArrayConst>() || steps.size() == 0 || steps.size() > maxSequenceSteps) return false;
      for (JsonVariantConst step : steps.as<JsonArrayConst>()) if (!validActionMessages(step)) return false;
    }
    if (sw.containsKey("Gestures")) {
      if (!sw["Gestures"].is<JsonObjectConst>()) return false;
      for (const char *gesture : gestures) {
        if (sw["Gestures"].containsKey(gesture) && !validActionMessages(sw["Gestures"][gesture])) return false;
      }
    }
  }
  return true;
}
