#pragma once

static char settingsDisplay[4][lcdColumnCount];
static bool settingsDisplayValid = false;

static void invalidateSettingsDisplay() {
  settingsDisplayValid = false;
}

static void settingsLine(char (&frame)[4][lcdColumnCount + 1], int row, const char *text) {
  const int length = static_cast<int>(strlen(text));
  const int padding = calculateCenterPadding(length);
  for (int i = 0; i < length && i < lcdColumnCount; ++i) frame[row][padding + i] = text[i];
}

static void drawSettingsFrame(const char (&frame)[4][lcdColumnCount + 1]) {
  midiFileActivityDisplayAllowed = false;
  for (int row = 0; row < 4; ++row) {
    bool positioned = false;
    for (int column = 0; column < lcdColumnCount; ++column) {
      if (settingsDisplayValid && settingsDisplay[row][column] == frame[row][column]) {
        positioned = false;
        continue;
      }
      if (!positioned) lcd.setCursor(column, row);
      lcd.print(frame[row][column]);
      settingsDisplay[row][column] = frame[row][column];
      positioned = true;
      serviceMidiClock();
    }
    usbHost.Task();
    serviceMidiPassthrough();
    serviceMidiClock();
  }
  settingsDisplayValid = true;
}

static bool computerMidiAvailable() {
#if defined(USB_MIDI) || defined(USB_MIDI_SERIAL) || defined(USB_MIDI4) || defined(USB_MIDI4_SERIAL) || defined(USB_MIDI16) || defined(USB_MIDI16_SERIAL)
  return true;
#else
  return false;
#endif
}

static const char *computerMidiStatus() {
#if defined(USB_MIDI) || defined(USB_MIDI_SERIAL) || defined(USB_MIDI4) || defined(USB_MIDI4_SERIAL) || defined(USB_MIDI16) || defined(USB_MIDI16_SERIAL)
  return usb_configuration ? "USB MIDI: connected" : "USB MIDI: unplugged";
#else
  return "USB: no MIDI build";
#endif
}

static void drawSettingsMenu() {
  char frame[4][lcdColumnCount + 1];
  memset(frame, ' ', sizeof(frame));
  char text[50];
  if (settingsMenu.page == SettingsPage::Exit) {
    settingsLine(frame, 0, "Unsaved settings");
    settingsLine(frame, 1, "Save or discard?");
    settingsLine(frame, 3, "1:Drop 2:Stay 3:Save");
  } else if (settingsMenu.page == SettingsPage::Routing) {
    settingsLine(frame, 0, "MIDI routing change");
    settingsLine(frame, 1, "Stop incoming notes");
    settingsLine(frame, 2, "before saving!");
    settingsLine(frame, 3, "1:Back 2:Save 3:Back");
  } else if (settingsMenu.page == SettingsPage::Reset) {
    settingsLine(frame, 0, "Reset settings?");
    settingsLine(frame, 1, "Presets kept intact");
    settingsLine(frame, 2, "Defaults staged only");
    settingsLine(frame, 3, "1:No 2:Reset 3:No");
  } else if (settingsMenu.page == SettingsPage::Error) {
    settingsLine(frame, 0, "Settings save failed");
    settingsLine(frame, 1, "EEPROM verify error");
    settingsLine(frame, 2, "Draft kept; retry");
    settingsLine(frame, 3, "Press 1/2/3: Back");
  } else if (settingsMenu.page == SettingsPage::Info) {
    snprintf(text, sizeof(text), "Synapse v%s", firmwareVersion);
    settingsLine(frame, 0, text);
    snprintf(text, sizeof(text), "%d presets on SD", presetCount);
    settingsLine(frame, 1, text);
    settingsLine(frame, 2, computerMidiStatus());
    settingsLine(frame, 3, "1:Back 2:Back 3:Save");
  } else {
    snprintf(text, sizeof(text), "%s%s", settingsMenu.category(), settingsMenu.dirty() ? " *" : "");
    settingsLine(frame, 0, text);
    settingsLine(frame, 1, settingsMenu.label());
    settingsMenu.formatValue(text, sizeof(text), computerMidiAvailable());
    // Filenames may be 24 characters: alternate the visible end while editing.
    if (strlen(text) > lcdColumnCount && (currentMillis / 1500) % 2) {
      settingsLine(frame, 2, text + strlen(text) - lcdColumnCount);
    } else {
      settingsLine(frame, 2, text);
    }
    settingsLine(frame, 3, settingsMenu.page == SettingsPage::Edit
      ? "1:Undo 2:OK 3:Save" : "1:Back 2:Edit 3:Save");
  }
  drawSettingsFrame(frame);
}

static void closeSettingsMenu() {
  settingsMenu.active = false;
  settingsChord.attempted = settingsChord.down != 0;
  settingsDisplayValid = false;
  suppressedButtons |= buttonsDown;
  if (deviceSettings.backlight) lcd.backlight();
  else lcd.noBacklight();
  resetPresetDisplay = false;
  requestPresetDisplayRefresh();
}

static void serviceSettingsSave() {
  usbHost.Task();
  serviceMidiPassthrough();
  serviceMidiClock();
}

static void handleSettingsButton(uint8_t button) {
  const SettingsResult result = settingsMenu.button(button, computerMidiAvailable(), presetList, presetCount);
  if (result == SettingsResult::Save) {
    if (!saveDeviceSettings(EEPROM, settingsMenu.draft, serviceSettingsSave)) {
      settingsMenu.page = SettingsPage::Error;
    } else {
      deviceSettings = settingsMenu.draft;
      closeSettingsMenu();
      return;
    }
  } else if (result == SettingsResult::Close) {
    closeSettingsMenu();
    return;
  }
  drawSettingsMenu();
}

static void serviceSettingsMenu() {
  static int lastCountdown = -1;
  static uint32_t lastMenuRefresh = 0;
  const uint32_t now = millis();
  if (settingsMenu.active) {
    if (static_cast<uint32_t>(now - lastMenuRefresh) >= 500) {
      lastMenuRefresh = now;
      drawSettingsMenu();
    }
    return;
  }
  if (!settingsChord.claimed) {
    lastCountdown = -1;
    settingsDisplayValid = false;
    return;
  }
  if (settingsChord.ready(now)) {
    if (!hasLoaded || liveStorageFault || liveEditorBusy() || playingMidiFile || stoppingMidiFile) {
      char frame[4][lcdColumnCount + 1];
      memset(frame, ' ', sizeof(frame));
      settingsLine(frame, 0, "Settings unavailable");
      settingsLine(frame, 1, (playingMidiFile || stoppingMidiFile)
        ? "Stop MIDI file first" : "Wait for editor/boot");
      drawSettingsFrame(frame);
      return;
    }
    suspendPerformanceSwitches();
    suppressedButtons |= buttonsDown;
    resetPresetDisplay = false;
    settingsMenu.open(deviceSettings);
    drawSettingsMenu();
    return;
  }
  if (settingsChord.attempted || settingsChord.down != 3) return;
  const int remaining = 5 - static_cast<uint32_t>(now - settingsChord.startedAt) / 1000;
  if (remaining != lastCountdown) {
    lastCountdown = remaining;
    char frame[4][lcdColumnCount + 1];
    memset(frame, ' ', sizeof(frame));
    settingsLine(frame, 0, "Hold Prev + Next");
    char text[24];
    snprintf(text, sizeof(text), "Settings in %d s", remaining);
    settingsLine(frame, 1, text);
    settingsLine(frame, 3, "Release to cancel");
    drawSettingsFrame(frame);
  }
}
