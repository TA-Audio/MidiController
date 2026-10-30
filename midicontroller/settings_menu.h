#pragma once

#include "settings.h"

struct SettingsChord {
  uint8_t down = 0;
  bool claimed = false;
  bool attempted = false;
  uint32_t startedAt = 0;

  bool update(uint8_t button, bool pressed, uint32_t now) {
    if (button != 4 && button != 5) return false;
    const uint8_t bit = 1 << (button - 4);
    if (pressed) down |= bit;
    else down &= ~bit;
    if (down == 3 && !claimed) {
      claimed = true;
      startedAt = now;
    }
    if (claimed && down != 3) attempted = true;
    const bool navigate = !pressed && !claimed;
    if (!down) {
      claimed = false;
      attempted = false;
    }
    return navigate;
  }

  bool ready(uint32_t now) {
    if (!claimed || down != 3 || attempted ||
        static_cast<uint32_t>(now - startedAt) < 5000) return false;
    attempted = true;
    return true;
  }
};

enum class SettingsItem : uint8_t {
  Din1, Din2, Mirror, Startup, FixedPreset, Restore, Backlight, Scroll,
  MessageTime, HoldTime, DoubleTime, Info, Reset, Count
};
enum class SettingsPage { Browse, Edit, Exit, Reset, Info, Routing, Error };
enum class SettingsResult { None, Save, Close };

struct SettingsMenu {
  bool active = false;
  SettingsItem item = SettingsItem::Din1;
  SettingsPage page = SettingsPage::Browse;
  DeviceSettings original, draft, beforeEdit;

  void open(const DeviceSettings &settings) {
    original = draft = settings;
    item = SettingsItem::Din1;
    page = SettingsPage::Browse;
    active = true;
  }

  bool dirty() const { return !sameSettings(original, draft); }
  bool routingChanged() const {
    return original.dinInput1 != draft.dinInput1 || original.dinInput2 != draft.dinInput2 ||
      original.usbMirror != draft.usbMirror;
  }

  const char *category() const {
    if (item <= SettingsItem::Mirror) return "MIDI routing";
    if (item <= SettingsItem::Restore) return "Startup";
    if (item <= SettingsItem::MessageTime) return "Display";
    if (item <= SettingsItem::DoubleTime) return "Footswitch timing";
    return "Device";
  }

  const char *label() const {
    static const char *const labels[] = {
      "DIN input 1 thru", "DIN input 2 thru", "Computer USB mirror",
      "Startup preset", "Fixed preset file", "Restore toggles",
      "LCD backlight", "Scroll preset names", "Message duration",
      "Long press", "Double tap", "Device information", "Reset settings"
    };
    return labels[static_cast<int>(item)];
  }

  uint32_t *value() {
    switch (item) {
      case SettingsItem::Din1: return &draft.dinInput1;
      case SettingsItem::Din2: return &draft.dinInput2;
      case SettingsItem::Mirror: return &draft.usbMirror;
      case SettingsItem::Startup: return &draft.fixedStartup;
      case SettingsItem::Restore: return &draft.restoreToggles;
      case SettingsItem::Backlight: return &draft.backlight;
      case SettingsItem::Scroll: return &draft.scrollNames;
      case SettingsItem::MessageTime: return &draft.messageMs;
      case SettingsItem::HoldTime: return &draft.holdMs;
      case SettingsItem::DoubleTime: return &draft.doubleMs;
      default: return nullptr;
    }
  }

  void formatValue(char *text, size_t size, bool usbAvailable) {
    if (item == SettingsItem::Mirror && !usbAvailable) snprintf(text, size, "Needs USB MIDI build");
    else if (item == SettingsItem::FixedPreset) snprintf(text, size, "%s", draft.fixedPreset[0] ? draft.fixedPreset : "(first on selection)");
    else if (item == SettingsItem::Startup) snprintf(text, size, "%s", draft.fixedStartup ? "Fixed preset" : "Last used");
    else if (item == SettingsItem::Info) snprintf(text, size, "2: View information");
    else if (item == SettingsItem::Reset) snprintf(text, size, "2: Restore defaults");
    else if (item >= SettingsItem::MessageTime) snprintf(text, size, "%lu ms", static_cast<unsigned long>(*value()));
    else snprintf(text, size, "%s", *value() ? "On" : "Off");
  }

  void step(int direction, const char names[][maxPresetNameLength], int count) {
    if (page == SettingsPage::Browse) {
      item = static_cast<SettingsItem>((static_cast<int>(item) + direction +
        static_cast<int>(SettingsItem::Count)) % static_cast<int>(SettingsItem::Count));
      return;
    }
    if (page != SettingsPage::Edit) return;
    if (item == SettingsItem::FixedPreset) {
      if (count <= 0) return;
      int index = -1;
      for (int i = 0; i < count; ++i) if (strcmp(draft.fixedPreset, names[i]) == 0) index = i;
      index = index < 0 ? 0 : (index + direction + count) % count;
      memset(draft.fixedPreset, 0, sizeof(draft.fixedPreset));
      strncpy(draft.fixedPreset, names[index], maxPresetNameLength - 1);
      return;
    }
    uint32_t *setting = value();
    if (!setting) return;
    uint32_t minimum = 0, maximum = 1, increment = 1;
    if (item == SettingsItem::MessageTime) { minimum = 500; maximum = 5000; increment = 250; }
    if (item == SettingsItem::HoldTime) { minimum = 400; maximum = 2000; increment = 100; }
    if (item == SettingsItem::DoubleTime) { minimum = 200; maximum = 600; increment = 50; }
    const int next = static_cast<int>(*setting) + direction * static_cast<int>(increment);
    if (next >= static_cast<int>(minimum) && next <= static_cast<int>(maximum)) *setting = next;
    if (item == SettingsItem::Startup && draft.fixedStartup && !draft.fixedPreset[0] && count > 0) {
      strncpy(draft.fixedPreset, names[0], maxPresetNameLength - 1);
    }
  }

  SettingsResult requestSave() {
    page = routingChanged() ? SettingsPage::Routing : SettingsPage::Browse;
    return routingChanged() ? SettingsResult::None : SettingsResult::Save;
  }

  SettingsResult button(uint8_t button, bool usbAvailable,
                        const char names[][maxPresetNameLength], int count) {
    if (!active) return SettingsResult::None;
    if (button == 4 || button == 5) {
      step(button == 4 ? 1 : -1, names, count);
      return SettingsResult::None;
    }
    if (page == SettingsPage::Error) {
      page = SettingsPage::Browse;
      return SettingsResult::None;
    }
    if (page == SettingsPage::Exit) {
      if (button == 1) return SettingsResult::Close;
      if (button == 2) page = SettingsPage::Browse;
      if (button == 3) return requestSave();
    } else if (page == SettingsPage::Routing) {
      if (button == 2) return SettingsResult::Save;
      if (button == 1 || button == 3) page = SettingsPage::Browse;
    } else if (page == SettingsPage::Reset) {
      if (button == 2) draft = DeviceSettings{};
      page = SettingsPage::Browse;
    } else if (page == SettingsPage::Edit) {
      if (button == 1) draft = beforeEdit;
      if (button == 1 || button == 2) page = SettingsPage::Browse;
      if (button == 3) return requestSave();
    } else if (page == SettingsPage::Info) {
      if (button == 3) return requestSave();
      page = SettingsPage::Browse;
    } else if (button == 1) {
      if (!dirty()) return SettingsResult::Close;
      page = SettingsPage::Exit;
    } else if (button == 3) {
      return requestSave();
    } else if (button == 2) {
      if (item == SettingsItem::Info) page = SettingsPage::Info;
      else if (item == SettingsItem::Reset) page = SettingsPage::Reset;
      else if (!(item == SettingsItem::Mirror && !usbAvailable) &&
               !(item == SettingsItem::FixedPreset && count == 0)) {
        beforeEdit = draft;
        page = SettingsPage::Edit;
        if (item == SettingsItem::FixedPreset && !draft.fixedPreset[0]) step(1, names, count);
      }
    }
    return SettingsResult::None;
  }
};
