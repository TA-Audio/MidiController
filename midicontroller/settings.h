#pragma once

#include <cstdio>
#include "logic.h"

static constexpr const char *firmwareVersion = "0.2.0";

struct DeviceSettings {
  uint32_t dinInput1 = 1;
  uint32_t dinInput2 = 1;
  uint32_t usbMirror = 1;
  uint32_t fixedStartup = 0;
  uint32_t restoreToggles = 1;
  uint32_t backlight = 1;
  uint32_t scrollNames = 1;
  uint32_t messageMs = 1500;
  uint32_t holdMs = 600;
  uint32_t doubleMs = 300;
  char fixedPreset[28] = {};
};

inline bool sameSettings(const DeviceSettings &a, const DeviceSettings &b) {
  return memcmp(&a, &b, sizeof(a)) == 0;
}

inline bool validSettings(const DeviceSettings &settings) {
  const char *end = static_cast<const char *>(memchr(settings.fixedPreset, 0, sizeof(settings.fixedPreset)));
  if (!end || end - settings.fixedPreset >= maxPresetNameLength) return false;
  if (settings.fixedPreset[0] &&
      (!hasJsonExtension(settings.fixedPreset) || strchr(settings.fixedPreset, '/') ||
       strchr(settings.fixedPreset, '\\') || strchr(settings.fixedPreset, ':'))) return false;
  return settings.dinInput1 <= 1 && settings.dinInput2 <= 1 && settings.usbMirror <= 1 &&
    settings.fixedStartup <= 1 && settings.restoreToggles <= 1 && settings.backlight <= 1 &&
    settings.scrollNames <= 1 &&
    settings.messageMs >= 500 && settings.messageMs <= 5000 && settings.messageMs % 250 == 0 &&
    settings.holdMs >= 400 && settings.holdMs <= 2000 && settings.holdMs % 100 == 0 &&
    settings.doubleMs >= 200 && settings.doubleMs <= 600 && settings.doubleMs % 50 == 0;
}

inline int startupPresetIndex(const DeviceSettings &settings,
                              const char names[][maxPresetNameLength], int count,
                              int lastIndex, bool &missing) {
  missing = false;
  if (!settings.fixedStartup) return clampPresetIndex(lastIndex, count);
  for (int i = 0; i < count; ++i) {
    if (strcmp(settings.fixedPreset, names[i]) == 0) return i;
  }
  missing = count > 0;
  return 0;
}

struct StoredSettings {
  uint32_t magic = 0;
  uint32_t version = 1;
  uint32_t sequence = 0;
  DeviceSettings settings;
  uint32_t checksum = 0;
};

static_assert(sizeof(DeviceSettings) == 68, "Settings persistence must not include padding");
static_assert(sizeof(StoredSettings) == 84, "Settings record layout changed");
static constexpr uint32_t settingsMagic = 0x53594e53UL;
static constexpr int settingsEepromAddress = controllerStateEepromAddress + sizeof(StoredControllerState);

inline uint32_t settingsChecksum(const StoredSettings &record) {
  uint32_t hash = 2166136261UL;
  const auto *bytes = reinterpret_cast<const uint8_t *>(&record) + sizeof(record.magic);
  const size_t length = sizeof(record.version) + sizeof(record.sequence) + sizeof(record.settings);
  for (size_t i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * 16777619UL;
  return hash;
}

inline bool validStoredSettings(const StoredSettings &record) {
  return record.magic == settingsMagic && record.version == 1 &&
    validSettings(record.settings) && record.checksum == settingsChecksum(record);
}

inline int latestSettingsSlot(const StoredSettings (&records)[2]) {
  const bool first = validStoredSettings(records[0]);
  const bool second = validStoredSettings(records[1]);
  if (!first) return second ? 1 : -1;
  if (!second) return 0;
  return static_cast<int32_t>(records[1].sequence - records[0].sequence) > 0 ? 1 : 0;
}

inline bool emptySettingsRecord(const StoredSettings &record) {
  const auto *bytes = reinterpret_cast<const uint8_t *>(&record);
  bool zero = true, erased = true;
  for (size_t i = 0; i < sizeof(record); ++i) {
    zero = zero && bytes[i] == 0;
    erased = erased && bytes[i] == 0xff;
  }
  return zero || erased;
}

enum class SettingsLoad { Saved, Defaults, Invalid };

template <typename Eeprom>
SettingsLoad loadDeviceSettings(Eeprom &eeprom, DeviceSettings &settings) {
  StoredSettings records[2];
  for (int i = 0; i < 2; ++i) eeprom.get(settingsEepromAddress + i * sizeof(StoredSettings), records[i]);
  const int slot = latestSettingsSlot(records);
  settings = slot < 0 ? DeviceSettings{} : records[slot].settings;
  if (slot >= 0) return SettingsLoad::Saved;
  return emptySettingsRecord(records[0]) && emptySettingsRecord(records[1])
    ? SettingsLoad::Defaults : SettingsLoad::Invalid;
}

template <typename Eeprom>
void writeSettingsBytes(Eeprom &eeprom, int address, const void *value, size_t length,
                        void (*service)()) {
  const auto *bytes = static_cast<const uint8_t *>(value);
  for (size_t i = 0; i < length; ++i) {
    eeprom.put(address + static_cast<int>(i), bytes[i]);
    if (service) service();
  }
}

template <typename Eeprom>
bool saveDeviceSettings(Eeprom &eeprom, const DeviceSettings &settings,
                        void (*service)() = nullptr) {
  if (!validSettings(settings)) return false;
  StoredSettings records[2];
  for (int i = 0; i < 2; ++i) eeprom.get(settingsEepromAddress + i * sizeof(StoredSettings), records[i]);
  const int slot = latestSettingsSlot(records);
  if (slot >= 0 && sameSettings(records[slot].settings, settings)) return true;
  const int target = slot == 0 ? 1 : 0;
  const int address = settingsEepromAddress + target * sizeof(StoredSettings);
  StoredSettings next;
  next.sequence = slot < 0 ? 0 : records[slot].sequence + 1;
  next.settings = settings;
  next.checksum = settingsChecksum(next);
  // Invalidate before replacing a reused slot, then publish only verified data.
  const uint32_t unpublished = 0;
  writeSettingsBytes(eeprom, address, &unpublished, sizeof(unpublished), service);
  writeSettingsBytes(eeprom, address, &next, sizeof(next), service);
  StoredSettings verified;
  eeprom.get(address, verified);
  if (memcmp(&next, &verified, sizeof(next)) != 0) return false;
  writeSettingsBytes(eeprom, address, &settingsMagic, sizeof(settingsMagic), service);
  eeprom.get(address, verified);
  return validStoredSettings(verified) && sameSettings(verified.settings, settings) &&
    verified.sequence == next.sequence;
}
