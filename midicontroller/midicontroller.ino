#include <ArduinoJson.h>
#include <MIDI.h>
#include <debounce.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include <SD.h>
#include "USBHost_t36.h"
#include <MD_MIDIFile.h>
#include <stdlib.h>
#include <string.h>
#include "logic.h"
#include "toggle_state.h"
#include "performance.h"
#include "performance_validation.h"

bool hasLoaded = false;
bool switchToggled[switchCount] = {};
unsigned long startMillis;
unsigned long currentMillis;
unsigned long switchDisplayStartMillis = 0;
bool resetPresetDisplay = false;
LiquidCrystal_I2C lcd(0x27, 20, 4);  // I2C address 0x27, 20 columns and 4 rows
JsonVariant activePreset;
bool activePresetValid = false;
bool livePresetDetached = false;
bool livePresetHasSlot = true;
bool liveStorageFault = false;
int currentPreset = 0;
int presetCount = 0;
DMAMEM char presetList[maxPresetListSize][maxPresetNameLength];
DMAMEM StaticJsonDocument<presetDocumentCapacity> presetDoc;
SwitchGesture switchGestures[switchCount];
int sequenceSteps[switchCount] = {};
bool sequenceStarted[switchCount] = {};
TapTempo tapTempo;
FsFile rootDir;
FsFile sdFile;
MD_MIDIFile midiFilePlayer;
int midiFileOutputChannel;
bool playingMidiFile = false;
MidiFileActivity midiFileActivity;
bool midiFileActivityDisplayed = false;
bool midiFileActivityDisplayAllowed = false;
bool midiFileActivityWasPlaying = false;
bool stoppingMidiFile = false;
bool midiFileLoaded = false;
MidiFileClockMode activeMidiFileClockMode = MidiFileClockMode::Disabled;
bool midiClockEnabled = false;
bool midiClockForMidiFile = false;
uint16_t midiClockTempo = 0;
uint32_t midiClockIntervalUs = 0;
uint32_t midiClockNextPulseUs = 0;
int presetNameScrollOffset = 0;
int presetNameScrollDirection = 1;
unsigned long presetNameScrollStartMillis = 0;
bool pendingDisplayRefresh = false;
bool pendingPresetSave = false;
StoredControllerState bootControllerState = {};
uint32_t activePresetSignature = 0;
unsigned long eepromDirtyMillis = 0;
int presetNavigationDirection = 1;
int prefetchedPresetIndex = -1;
int prefetchTargetIndex = -1;
bool prefetchRequested = false;
DMAMEM StaticJsonDocument<presetDocumentCapacity> prefetchedPresetDoc;

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI1);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, MIDI2);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial3, MIDI3);

USBHost usbHost;
MIDIDevice usbMidiDevice(usbHost);

bool loadPresetDocumentByIndex(int presetIndex, StaticJsonDocument<presetDocumentCapacity> &targetDoc);
bool applyPresetByIndex(int presetIndex);
void queueDirectionalPresetPrefetch();
void servicePresetPrefetch();
void changePreset();
void executeSwitchLogic(int switchNo);
void setPresetDisplayInfo();
void showError(const char *errorMessageLine1, const char *errorMessageLine2);
static void setUiMessageTimeout();
static void serviceSwitchGesture(int index, bool pressed);
static void executeTapTempo();
static void executePerformanceAction(JsonObject action);
static const char *switchDisplayName(int index);

static void clearDisplay() {
  lcd.clear();
  midiFileActivityDisplayed = false;
  midiFileActivityDisplayAllowed = false;
}

#include "midi_output.h"

static inline void sendProgramChange(int pcValue, int channel, bool usbEvent, bool deviceEvent) {
  const int safePc = clampMidi(pcValue);
#if defined(USB_MIDI) || defined(USB_MIDI_SERIAL) || defined(USB_MIDI4) || defined(USB_MIDI4_SERIAL) || defined(USB_MIDI16) || defined(USB_MIDI16_SERIAL)
  if (deviceEvent) {
    usbMIDI.sendProgramChange(safePc, channel);
    return;
  }
#else
  if (deviceEvent) {
    showError("USB MIDI disabled", "Build serialmidi");
    setUiMessageTimeout();
    return;
  }
#endif
  if (usbEvent) {
    usbMidiDevice.sendProgramChange(safePc, channel);
  } else {
    sendDinMidi(midi::ProgramChange, safePc, 0, channel);
  }
}

static inline void sendControlChange(int ccNumber, int ccValue, int channel, bool usbEvent, bool deviceEvent) {
  const int safeCcNumber = clampMidi(ccNumber);
  const int safeCcValue = clampMidi(ccValue);
#if defined(USB_MIDI) || defined(USB_MIDI_SERIAL) || defined(USB_MIDI4) || defined(USB_MIDI4_SERIAL) || defined(USB_MIDI16) || defined(USB_MIDI16_SERIAL)
  if (deviceEvent) {
    usbMIDI.sendControlChange(safeCcNumber, safeCcValue, channel);
    return;
  }
#else
  if (deviceEvent) {
    showError("USB MIDI disabled", "Build serialmidi");
    setUiMessageTimeout();
    return;
  }
#endif
  if (usbEvent) {
    usbMidiDevice.sendControlChange(safeCcNumber, safeCcValue, channel);
  } else {
    sendDinMidi(midi::ControlChange, safeCcNumber, safeCcValue, channel);
  }
}

static void sendPcArray(JsonArray pcArray) {
  if (pcArray.isNull()) return;
  for (JsonVariant pcEvent : pcArray) {
    sendProgramChange(pcJsonToMidi(pcEvent["PC"].as<int>()), pcEvent["Channel"], pcEvent["USB"], pcEvent["USBDevice"]);
  }
}

static void sendCcArray(JsonArray ccArray) {
  if (ccArray.isNull()) return;
  for (JsonVariant ccEvent : ccArray) {
    sendControlChange(ccEvent["CC"], ccEvent["Value"], ccEvent["Channel"], ccEvent["USB"], ccEvent["USBDevice"]);
  }
}

static void setUiMessageTimeout() {
  switchDisplayStartMillis = currentMillis;
  resetPresetDisplay = true;
}

static inline void requestPresetDisplayRefresh() {
  pendingDisplayRefresh = true;
}

static inline void queuePresetSave() {
  pendingPresetSave = true;
  eepromDirtyMillis = currentMillis;
}

static void saveControllerStateNow() {
  uint8_t mask = 0;
  for (int i = 0; i < switchCount; ++i) {
    if (switchToggled[i]) mask |= 1 << i;
  }
  StoredControllerState state = makeControllerState(
    currentPreset, activePresetValid ? activePresetSignature : 0, mask);
  writeControllerState(EEPROM, state);
  pendingPresetSave = false;
}

static void commitPendingEepromWrites() {
  if (!pendingPresetSave || !hasElapsed(currentMillis, eepromDirtyMillis, eepromCommitDelayMs)) {
    return;
  }

  saveControllerStateNow();
}

static void servicePresetDisplayRefresh() {
  if (!pendingDisplayRefresh || resetPresetDisplay) {
    return;
  }

  pendingDisplayRefresh = false;
  setPresetDisplayInfo();
}

static void serviceMidiPassthrough() {
  forwardMidiInput(MIDI2);
  forwardMidiInput(MIDI3);
}

FLASHMEM bool loadPresetDocumentByIndex(int presetIndex, StaticJsonDocument<presetDocumentCapacity> &targetDoc) {
  if (presetIndex < 0 || presetIndex >= presetCount) {
    return false;
  }

  const char *fileName = presetList[presetIndex];
  if (!sdFile.open(fileName, O_READ)) {
    targetDoc.clear();
    return false;
  }

  targetDoc.clear();
  DeserializationError error = deserializeJson(targetDoc, sdFile);
  sdFile.close();

  if (error || !targetDoc.is<JsonObject>() || !validPerformancePreset(targetDoc.as<JsonVariantConst>())) {
    targetDoc.clear();
    return false;
  }

  return true;
}

bool applyPresetByIndex(int presetIndex) {
  if (prefetchedPresetIndex == presetIndex) {
    presetDoc.clear();
    presetDoc.set(prefetchedPresetDoc.as<JsonVariantConst>());
    prefetchedPresetDoc.clear();
    prefetchedPresetIndex = -1;
    return true;
  }

  prefetchedPresetIndex = -1;
  if (!loadPresetDocumentByIndex(presetIndex, prefetchedPresetDoc)) {
    return false;
  }

  presetDoc.clear();
  presetDoc.set(prefetchedPresetDoc.as<JsonVariantConst>());
  prefetchedPresetDoc.clear();
  return true;
}

void queueDirectionalPresetPrefetch() {
  int candidateIndex = computePrefetchCandidate(currentPreset, presetCount,
                                                  presetNavigationDirection, prefetchedPresetIndex);
  if (candidateIndex < 0) {
    prefetchRequested = false;
    prefetchTargetIndex = -1;
    if (presetCount <= 1) {
      prefetchedPresetIndex = -1;
      prefetchedPresetDoc.clear();
    }
    return;
  }

  prefetchTargetIndex = candidateIndex;
  prefetchRequested = true;
}

void servicePresetPrefetch() {
  if (!prefetchRequested || prefetchTargetIndex < 0 || prefetchTargetIndex >= presetCount) {
    return;
  }

  if (loadPresetDocumentByIndex(prefetchTargetIndex, prefetchedPresetDoc)) {
    prefetchedPresetIndex = prefetchTargetIndex;
  } else {
    prefetchedPresetIndex = -1;
  }

  prefetchRequested = false;
  prefetchTargetIndex = -1;
}

FLASHMEM static void displayCenteredLine(int row, const char *text) {
  if (text == nullptr) {
    return;
  }

  const int textLength = (int)strlen(text);
  const int safeLength = textLength > lcdColumnCount ? lcdColumnCount : textLength;
  const int padding = calculateCenterPadding(textLength);

  lcd.setCursor(0, row);
  for (int i = 0; i < padding; i++) {
    lcd.print(' ');
  }
  for (int i = 0; i < safeLength; i++) {
    lcd.print(text[i]);
  }
  for (int i = padding + safeLength; i < lcdColumnCount; i++) {
    lcd.print(' ');
  }
}

FLASHMEM static void showSwitchActionMessage(const char *text, const char *suffix) {
  char lineBuffer[uiTextBufferLength];
  if (!formatSwitchActionMessage(text, suffix, lineBuffer, sizeof(lineBuffer))) {
    return;
  }

  clearDisplay();
  midiFileActivityDisplayAllowed = true;
  displayCenteredLine(1, lineBuffer);
  setUiMessageTimeout();
}

FLASHMEM void showError(const char *errorMessageLine1, const char *errorMessageLine2) {
  clearDisplay();
  lcd.setCursor(0, 0);
  lcd.print(errorMessageLine1);
  lcd.setCursor(0, 1);
  lcd.print(errorMessageLine2);
}

void setPresetDisplayInfo() {
  clearDisplay();
  lcd.setCursor(0, 0);
  const char *sw1 = "";
  const char *sw2 = "";
  const char *sw3 = "";

  if (!activePresetValid) {
    displayCenteredLine(0, "Preset load error");
    displayCenteredLine(1, "Next/Prev to skip");
    return;
  } else {
    midiFileActivityDisplayAllowed = true;
    presetNameScrollOffset = 0;
    presetNameScrollDirection = 1;
    presetNameScrollStartMillis = currentMillis;
    displayPresetName();

    JsonObject fileInfo = activePreset["FileInfo"];
    if (!fileInfo.isNull()) {

      if (playingMidiFile) {
        sw1 = "Stop";
      } else {
        sw1 = "Play";
      }

    } else {
      sw1 = switchDisplayName(0);
    }
    sw2 = switchDisplayName(1);
    sw3 = switchDisplayName(2);
  }

  int sw1Length = (int)strlen(sw1);
  int sw2Length = (int)strlen(sw2);
  int sw3Length = (int)strlen(sw3);

  int padding1, padding3;
  calculateSwitchPadding(sw1Length, sw2Length, sw3Length, padding1, padding3);

  char sw1Indicator[maxPresetNameLength];
  char sw2Indicator[maxPresetNameLength];
  char sw3Indicator[maxPresetNameLength];

  const int sw1CopyLength = sw1Length < (maxPresetNameLength - 1) ? sw1Length : (maxPresetNameLength - 1);
  const int sw2CopyLength = sw2Length < (maxPresetNameLength - 1) ? sw2Length : (maxPresetNameLength - 1);
  const int sw3CopyLength = sw3Length < (maxPresetNameLength - 1) ? sw3Length : (maxPresetNameLength - 1);

  lcd.setCursor(0, 2);

  for (int i = 0; i < sw1CopyLength; i++) {
    sw1Indicator[i] = switchToggled[0] ? '*' : ' ';
  }
  sw1Indicator[sw1CopyLength] = '\0';

  for (int i = 0; i < sw2CopyLength; i++) {
    sw2Indicator[i] = switchToggled[1] ? '*' : ' ';
  }
  sw2Indicator[sw2CopyLength] = '\0';

  for (int i = 0; i < sw3CopyLength; i++) {
    sw3Indicator[i] = switchToggled[2] ? '*' : ' ';
  }
  sw3Indicator[sw3CopyLength] = '\0';

  lcd.print(sw1Indicator);
  for (int i = 0; i < padding1; i++) {
    lcd.print(" ");
  }

  lcd.print(sw2Indicator);

  for (int i = 0; i < padding3; i++) {
    lcd.print(" ");
  }
  lcd.print(sw3Indicator);

  lcd.setCursor(0, 3);
  lcd.print(sw1);
  for (int i = 0; i < padding1; i++) {
    lcd.print(" ");
  }

  lcd.print(sw2);

  for (int i = 0; i < padding3; i++) {
    lcd.print(" ");
  }
  lcd.print(sw3);
}

static void displayPresetName() {
  const char *name = activePreset["Name"] | "";
  const int nameLength = static_cast<int>(strlen(name));
  const int columns = presetNameColumns(playingMidiFile);
  const int maxOffset = nameLength > columns ? nameLength - columns : 0;
  if (presetNameScrollOffset > maxOffset) {
    presetNameScrollOffset = maxOffset;
  }

  lcd.setCursor(0, 0);
  int column = 0;
  for (; column < columns && presetNameScrollOffset + column < nameLength; ++column) {
    lcd.print(name[presetNameScrollOffset + column]);
  }
  for (; column < columns; ++column) {
    lcd.print(' ');
  }
  if (playingMidiFile) {
    drawMidiFileActivity(lcd, midiFileActivity, true, midiFileActivityDisplayed, millis(), true);
  } else {
    midiFileActivityDisplayed = false;
  }
}

static void servicePresetNameScroll() {
  if (!activePresetValid || resetPresetDisplay ||
      !hasElapsed(currentMillis, presetNameScrollStartMillis, presetNameScrollPeriodMs)) {
    return;
  }

  const char *name = activePreset["Name"] | "";
  const int maxOffset = static_cast<int>(strlen(name)) - presetNameColumns(playingMidiFile);
  if (maxOffset <= 0) {
    return;
  }

  presetNameScrollOffset += presetNameScrollDirection;
  if (presetNameScrollOffset >= maxOffset) {
    presetNameScrollOffset = maxOffset;
    presetNameScrollDirection = -1;
  } else if (presetNameScrollOffset <= 0) {
    presetNameScrollOffset = 0;
    presetNameScrollDirection = 1;
  }
  presetNameScrollStartMillis = currentMillis;
  displayPresetName();
}

static void serviceMidiFileActivityIndicator() {
  const bool playbackChanged = midiFileActivityWasPlaying != playingMidiFile;
  midiFileActivityWasPlaying = playingMidiFile;
  if (!playingMidiFile) {
    midiFileActivity.reset();
  }
  if (!midiFileActivityDisplayAllowed) return;

  if (playbackChanged && !resetPresetDisplay) {
    displayPresetName();
  }
  drawMidiFileActivity(lcd, midiFileActivity, playingMidiFile, midiFileActivityDisplayed, millis());
}

static void stopMidiClock() {
  if (midiClockEnabled) {
    sendDinMidi(midi::Stop);
  }
  midiClockEnabled = false;
  midiClockForMidiFile = false;
  midiClockTempo = 0;
  midiClockIntervalUs = 0;
  midiClockNextPulseUs = 0;
}

static void startMidiClock(uint16_t tempo, bool forMidiFile, bool sendStart = true) {
  const uint32_t interval = midiClockIntervalForTempo(tempo);
  if (interval == 0) {
    return;
  }

  midiClockEnabled = true;
  midiClockForMidiFile = forMidiFile;
  midiClockTempo = tempo;
  midiClockIntervalUs = interval;
  if (sendStart) {
    sendDinMidi(midi::Start);
  }
  sendDinMidi(midi::Clock);
  midiClockNextPulseUs = micros() + midiClockIntervalUs;
}

static bool startPresetMidiClock() {
  if (!activePresetValid) {
    return true;
  }

  if (activePreset.containsKey("SendMidiClock") &&
    !activePreset["SendMidiClock"].is<bool>()) {
    showError("Preset Clock Error", "Set SendMidiClock true/false");
    setUiMessageTimeout();
    return false;
  }

  if (!activePreset["SendMidiClock"].as<bool>()) {
    return true;
  }

  const int tempo = configuredPresetMidiClockTempo(
    true, activePreset["BPM"].is<int>(), activePreset["BPM"].as<int>());
  if (tempo < 0) {
    showError("Preset Clock Error", "Set BPM to 1..65535");
    setUiMessageTimeout();
    return false;
  }

  startMidiClock(static_cast<uint16_t>(tempo), false);
  return midiClockEnabled;
}

static void executeTapTempo() {
  const int bpm = tapTempo.tap(static_cast<uint32_t>(millis()));
  if (bpm == 0) {
    showSwitchActionMessage("Tap tempo", "");
    return;
  }
  if (midiClockForMidiFile) {
    midiFilePlayer.setTempo(bpm);
    midiClockTempo = bpm;
    midiClockIntervalUs = midiClockIntervalForTempo(bpm);
    midiClockNextPulseUs = micros() + midiClockIntervalUs;
  } else if (midiClockEnabled) {
    midiClockTempo = bpm;
    midiClockIntervalUs = midiClockIntervalForTempo(bpm);
    midiClockNextPulseUs = micros() + midiClockIntervalUs;
  } else {
    startMidiClock(bpm, false);
  }
  char text[24];
  snprintf(text, sizeof(text), "Tempo %d BPM", bpm);
  showSwitchActionMessage(text, "");
}

static void executePerformanceAction(JsonObject action) {
  if (action.isNull()) return;
  sendPcArray(action["PC"]);
  sendCcArray(action["CC"]);
}

static const char *actionDisplayName(JsonVariantConst action, const char *fallback) {
  const char *label = action["Name"] | "";
  return label[0] ? label : fallback;
}

static void serviceSwitchGesture(int index, bool pressed) {
  const char *keys[] = {"Switch1", "Switch2", "Switch3"};
  JsonObject sw = activePreset[keys[index]];
  JsonObject gestures = sw["Gestures"];
  const uint8_t events = switchGestures[index].update(
    pressed, static_cast<uint32_t>(millis()),
    gestures["LongPress"].is<JsonObject>(), gestures["DoubleTap"].is<JsonObject>());
  if (events & GesturePress) executePerformanceAction(gestures["Press"]);
  if (events & GestureHold) {
    executePerformanceAction(gestures["LongPress"]);
    showSwitchActionMessage(actionDisplayName(gestures["LongPress"], sw["Name"] | ""), "");
  }
  if (events & GestureDouble) {
    executePerformanceAction(gestures["DoubleTap"]);
    showSwitchActionMessage(actionDisplayName(gestures["DoubleTap"], sw["Name"] | ""), "");
  }
  if (events & GestureTap) executeSwitchLogic(index + 1);
  if (events & GestureRelease) executePerformanceAction(gestures["Release"]);
}

static const char *switchDisplayName(int index) {
  const char *keys[] = {"Switch1", "Switch2", "Switch3"};
  JsonObject sw = activePreset[keys[index]];
  if (sw["Toggle"].as<bool>()) {
    return actionDisplayName(sw[switchToggled[index] ? "On" : "Off"], sw["Name"] | "");
  }
  JsonArray sequence = sw["Sequence"];
  if (!sequence.isNull() && sequence.size() > 0 && sequenceStarted[index]) {
    const int previous = (sequenceSteps[index] + sequence.size() - 1) % sequence.size();
    return actionDisplayName(sequence[previous], sw["Name"] | "");
  }
  return sw["Name"] | "";
}

static void switchHandler(uint8_t btnId, uint8_t btnState) {
  if (hasLoaded && activePresetValid && btnId >= 1 && btnId <= 3 &&
      !(btnId == 1 && !activePreset["FileInfo"].isNull())) {
    const char *keys[] = {"Switch1", "Switch2", "Switch3"};
    JsonObject sw = activePreset[keys[btnId - 1]];
    if (sw["Gestures"].is<JsonObject>()) {
      serviceSwitchGesture(btnId - 1, btnState == BTN_STATE_PRESSED);
      return;
    }
  }
  const SwitchAction action = classifySwitchEvent(btnId, btnState, hasLoaded);

  if (action == SwitchAction::ExecuteSwitch) {
    executeSwitchLogic(btnId);
    return;
  }

  if (action == SwitchAction::NavigateNext) {
    presetNavigationDirection = 1;
  } else if (action == SwitchAction::NavigatePrev) {
    presetNavigationDirection = -1;
  } else {
    return;
  }
  const int target = adjacentPresetIndex(currentPreset, presetCount, presetNavigationDirection);
  if (target < 0 || target == currentPreset) return;
  currentPreset = target;
  changePreset();
}

FLASHMEM void stopMidiFile(bool changingPreset) {
  midiFileActivity.reset();
  const bool stopClock = midiClockForMidiFile || changingPreset;
  const bool clockSendsStop = stopClock && midiClockEnabled;
  if (stopClock) {
    stopMidiClock();
  }
  if (activeMidiFileClockMode != MidiFileClockMode::Disabled && !clockSendsStop) {
    sendDinMidi(midi::Stop);
  }
  activeMidiFileClockMode = MidiFileClockMode::Disabled;
  sendCcArray(activePreset["FileInfo"]["StopCC"]);
}

static void serviceMidiClock() {
  if (!midiClockEnabled || (midiClockForMidiFile && !playingMidiFile)) {
    return;
  }

  const uint16_t tempo = midiClockForMidiFile ? midiFilePlayer.getTempo() : midiClockTempo;
  if (midiClockForMidiFile && tempo != midiClockTempo) {
    midiClockTempo = tempo;
    midiClockIntervalUs = midiClockIntervalForTempo(tempo);
    midiClockNextPulseUs = micros() + midiClockIntervalUs;
  }
  if (midiClockIntervalUs == 0) {
    return;
  }

  const uint32_t now = micros();
  if (static_cast<int32_t>(now - midiClockNextPulseUs) < 0) {
    return;
  }

  sendDinMidi(midi::Clock);
  midiClockNextPulseUs += midiClockIntervalUs;
  if (static_cast<int32_t>(now - midiClockNextPulseUs) >= static_cast<int32_t>(midiClockIntervalUs)) {
    midiClockNextPulseUs = now + midiClockIntervalUs;
  }
}

static bool prepareMidiFilePlayback(JsonObject fileInfo) {
  const char *midiFile = fileInfo["FileName"].as<const char *>();
  if (midiFile == nullptr || midiFile[0] == '\0') {
    return false;
  }

  midiFilePlayer.close();
  midiFileLoaded = false;

  if (midiFilePlayer.load(midiFile) != MD_MIDIFile::E_OK) {
    midiFilePlayer.close();
    return false;
  }

  midiFileLoaded = true;
  return true;
}

FLASHMEM void toggleMidiFilePlayback(JsonObject fileInfo) {
  const char *midiFile = fileInfo["FileName"].as<const char *>();

  if (!playingMidiFile) {
    if (!midiFileLoaded && !prepareMidiFilePlayback(fileInfo)) {
      showError("Midi File load Error ", "");
      return;
    }

    midiFilePlayer.setTempo(120);
    const int tempoOverride = configuredMidiTempoOverride(
      fileInfo["BPM"].is<int>(), fileInfo["BPM"].as<int>());
    if (tempoOverride > 0) {
      midiFilePlayer.setTempo(tempoOverride);
    }
    midiFilePlayer.restart();
    midiFileOutputChannel = fileInfo["Channel"].as<int>();
    stoppingMidiFile = false;
    midiFileActivity.reset();
    playingMidiFile = true;
    const bool sendFileClock = fileInfo["SendMidiClock"].is<bool>() &&
      fileInfo["SendMidiClock"].as<bool>();
    const MidiFilePlaybackActions actions = midiFilePlaybackActions(
      sendFileClock, midiClockEnabled, midiClockForMidiFile,
      midiFilePlayer.getTempo());
    activeMidiFileClockMode = actions.clockMode;
    if (actions.sendStart) {
      sendDinMidi(midi::Start);
    }
    if (actions.startClock) {
      startMidiClock(midiFilePlayer.getTempo(), true, false);
    }
  } else {
    playingMidiFile = false;
    stoppingMidiFile = true;
  }

  char text[uiTextBufferLength];
  snprintf(text, sizeof(text), "%s", midiFile != nullptr ? midiFile : "");

  if (strlen(text) > 1) {
    clearDisplay();
    midiFileActivityDisplayAllowed = true;
    displayCenteredLine(1, text);
    lcd.setCursor(0, 2);
    lcd.print(playingMidiFile ? " Playing" : " Stopping");
    setUiMessageTimeout();
  }
}

void executeSwitchLogic(int switchNo) {
  if (!activePresetValid) {
    return;
  }

  JsonObject switchLogic;

  switch (switchNo) {
    case 1:
      switchLogic = activePreset["Switch1"];
      break;
    case 2:
      switchLogic = activePreset["Switch2"];
      break;
    case 3:
      switchLogic = activePreset["Switch3"];
      break;
  }


  // MIDI file playback only triggers on switch 1 — skip FileInfo lookup for 2/3
  if (switchNo == 1) {
    JsonObject fileInfo = activePreset["FileInfo"];
    if (!fileInfo.isNull()) {
      toggleMidiFilePlayback(fileInfo);
      return;
    }
  }

  const char *switchName = switchLogic["Name"].as<const char *>();
  bool toggle = switchLogic["Toggle"].as<bool>();
  const bool wasToggled = switchToggled[switchNo - 1];

  if (switchLogic["TapTempo"].as<bool>()) {
    executeTapTempo();
    return;
  }
  JsonArray sequence = switchLogic["Sequence"];
  if (!sequence.isNull() && sequence.size() > 0) {
    if (sequence.size() > maxSequenceSteps) {
      showError("Sequence too long", "Maximum 16 steps");
      setUiMessageTimeout();
      return;
    }
    const int step = sequenceSteps[switchNo - 1];
    JsonObject action = sequence[step];
    executePerformanceAction(action);
    sequenceSteps[switchNo - 1] = nextSequenceStep(step, sequence.size());
    sequenceStarted[switchNo - 1] = true;
    const char *label = actionDisplayName(action, switchName);
    showSwitchActionMessage(label, "");
    return;
  }
  if (toggle && (switchLogic["On"].is<JsonObject>() || switchLogic["Off"].is<JsonObject>())) {
    JsonObject state = switchLogic[wasToggled ? "Off" : "On"];
    executePerformanceAction(state);
    switchToggled[switchNo - 1] = !wasToggled;
    saveControllerStateNow();
    const char *label = actionDisplayName(state, switchName);
    showSwitchActionMessage(label, wasToggled ? " Off!" : " On!");
    return;
  }

  sendPcArray(switchLogic["PC"]);

  JsonArray ccArray = switchLogic["CC"];
  if (!ccArray.isNull()) {
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

      sendControlChange(ccNumber, ccValue, ccChannel, usbEvent, cc["USBDevice"]);
    }

    if (useToggleValue) {
      switchToggled[switchNo - 1] = nextToggleState;
      saveControllerStateNow();
    }
  }

  if (toggle) {
    showSwitchActionMessage(switchName, !wasToggled ? " On!" : " Off!");
  } else {
    showSwitchActionMessage(switchName, "");
  }
}

FLASHMEM void changePreset() {
  livePresetDetached = false;
  livePresetHasSlot = true;
  if (resetPresetDisplay) {
    resetPresetDisplay = false;
  }

  if (presetCount <= 0) {
    bootControllerState.magic = 0;
    showError("No presets found", "Add .json files to SD");
    return;
  }

  if (currentPreset < 0) {
    currentPreset = 0;
  }

  if (activePresetValid) {
    const char *keys[] = {"Switch1", "Switch2", "Switch3"};
    for (int i = 0; i < switchCount; ++i) {
      if (switchGestures[i].down) executePerformanceAction(activePreset[keys[i]]["Gestures"]["Release"]);
    }
    sendCcArray(activePreset["OnExit"]["CC"]);
  }

  const bool stopActiveMidiFile = playingMidiFile || stoppingMidiFile;
  if (stopActiveMidiFile && activePresetValid) {
    stopMidiFile(true);
  } else {
    stopMidiClock();
  }
  midiFilePlayer.close();
  midiFileLoaded = false;
  playingMidiFile = false;
  midiFileActivity.reset();
  stoppingMidiFile = false;

  memset(switchToggled, 0, sizeof(switchToggled));
  memset(sequenceSteps, 0, sizeof(sequenceSteps));
  memset(sequenceStarted, 0, sizeof(sequenceStarted));
  for (auto &gesture : switchGestures) gesture = SwitchGesture{};
  tapTempo = TapTempo{};

  // if saved currentPreset value is greater than the number of presets, reset to 0
  currentPreset = clampPresetIndex(currentPreset, presetCount);

  if (!applyPresetByIndex(currentPreset)) {
    activePresetValid = false;
    bootControllerState.magic = 0;
    queuePresetSave();
    showError("Preset load error", "Next/Prev to skip");
    setUiMessageTimeout();
    return;
  }

  activePreset = presetDoc;
  activePresetValid = true;
  activePresetSignature = presetStateSignature(presetList[currentPreset], activePreset);
  const bool restoreBootState = validControllerState(bootControllerState, presetCount) &&
    bootControllerState.presetIndex == static_cast<uint32_t>(currentPreset) &&
    bootControllerState.presetSignature == activePresetSignature;
  const uint8_t bootMask = consumeBootToggleMask(
    bootControllerState, currentPreset, activePresetSignature, presetCount);

  JsonObject fileInfo = activePreset["FileInfo"];
  if (!fileInfo.isNull()) {
    prepareMidiFilePlayback(fileInfo);
  }

  sendPcArray(activePreset["OnLoad"]["PC"]);
  sendCcArray(activePreset["OnLoad"]["CC"]);

  if (restoreBootState) {
    restoreToggleStates(activePreset, bootMask, switchToggled,
      [](int cc, int value, int channel, bool host, bool device) {
        sendControlChange(cc, value, channel, host, device);
      },
      [](int pc, int channel, bool host, bool device) {
        sendProgramChange(pc, channel, host, device);
      });
  }

  setPresetDisplayInfo();
  startPresetMidiClock();
  queuePresetSave();
  queueDirectionalPresetPrefetch();
}

FLASHMEM void showBootScreen() {
  // Custom character: solid block for loading bar
  byte fullBlock[8] = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
  byte emptyBlock[8] = {0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F, 0x00};
  lcd.createChar(0, fullBlock);
  lcd.createChar(1, emptyBlock);

  lcd.setCursor(0, 0);
  lcd.print("TA Audio");
  lcd.setCursor(0, 1);
  lcd.print("SYNAPSE v0.2.0");
  lcd.setCursor(0, 2);
  lcd.print("MIDI CONTROLLER");

  // Draw empty loading bar frame on row 3
  for (int i = 0; i < lcdColumnCount; i++) {
    lcd.setCursor(i, 3);
    lcd.write((uint8_t)1);
  }

  // Animate the loading bar filling left to right
  for (int i = 0; i < lcdColumnCount; i++) {
    lcd.setCursor(i, 3);
    lcd.write((uint8_t)0);
    delay(80);
  }
  delay(400);
}

FLASHMEM void loadPresetList() {
  presetCount = 0;
  rootDir.open("/");

  while (sdFile.openNext(&rootDir, O_RDONLY)) {
    char fileName[maxPresetNameLength];
    sdFile.getName(fileName, maxPresetNameLength);

    if (hasJsonExtension(fileName)) {
      if (presetCount >= maxPresetListSize) {
        sdFile.close();
        break;
      }
      strncpy(presetList[presetCount], fileName, maxPresetNameLength);
      presetList[presetCount][maxPresetNameLength - 1] = '\0';
      presetCount++;
    }

    sdFile.close();
  }

  if (rootDir.getError()) {
    showError("Error opening SD", "");
  }

  rootDir.close();

  // Sort presetList numerically
  if (presetCount > 1) {
    qsort(presetList, presetCount, sizeof(presetList[0]), comparePresetNames);
  }

  prefetchedPresetIndex = -1;
  prefetchTargetIndex = -1;
  prefetchRequested = false;
  prefetchedPresetDoc.clear();
}

void midiFileCallback(midi_event *pev) {
  if (sendMidiFileEvent(Serial1, *pev, midiFileOutputChannel) && playingMidiFile) {
    midiFileActivity.recordSent(millis());
  }
}

static Button switch1Button(1, switchHandler);
static Button switch2Button(2, switchHandler);
static Button switch3Button(3, switchHandler);
static Button nextPresetButton(4, switchHandler);
static Button prevPresetButton(5, switchHandler);

#include "live_editor.h"

FLASHMEM void setup() {

  lcd.init();  // initialize the lcd
  lcd.backlight();
  showBootScreen();

  // Wait 1.5 seconds before turning on USB Host.  If connected USB devices
  // use too much power, Teensy at least completes USB enumeration, which
  // makes isolating the power issue easier.
  delay(1500);
  usbHost.begin();

  pinMode(switch1Pin, INPUT_PULLUP);
  pinMode(switch2Pin, INPUT_PULLUP);
  pinMode(switch3Pin, INPUT_PULLUP);
  pinMode(nextPresetPin, INPUT_PULLUP);
  pinMode(prevPresetPin, INPUT_PULLUP);

  MIDI1.begin(MIDI_CHANNEL_OMNI);
  MIDI1.turnThruOff();

  MIDI2.begin(MIDI_CHANNEL_OMNI);
  MIDI2.turnThruOff();
  MIDI3.begin(MIDI_CHANNEL_OMNI);
  MIDI3.turnThruOff();

  if (!SD.begin(BUILTIN_SDCARD)) {
    showError("SD Card Error", "Is the card inserted and fat32?");
    while (true)
      ;
  }

  midiFilePlayer.begin(&(SdFat &)SD);
  midiFilePlayer.setMidiHandler(midiFileCallback);

  if (!recoverLiveSave()) {
    showError("SD recovery failed", "Check synapse.txn/bak");
    while (true) {}
  }
  if (!recoverLiveSet()) {
    showError("SD recovery failed", "Check synset.txn/bak");
    while (true) {}
  }
  loadPresetList();
  beginLiveEditor();

  EEPROM.get(presetEepromAddress, currentPreset);
  EEPROM.get(controllerStateEepromAddress, bootControllerState);
  currentPreset = validControllerState(bootControllerState, presetCount)
    ? static_cast<int>(bootControllerState.presetIndex)
    : validateStoredPreset(currentPreset, presetCount);

  startMillis = millis();
}

static void pollButtons() {
  // digitalReadFast compiles to a single register read vs digitalRead's pin lookup table.
  switch1Button.update(digitalReadFast(switch1Pin));
  switch2Button.update(digitalReadFast(switch2Pin));
  switch3Button.update(digitalReadFast(switch3Pin));
  nextPresetButton.update(digitalReadFast(nextPresetPin));
  prevPresetButton.update(digitalReadFast(prevPresetPin));
}

void loop() {

  currentMillis = millis();

  usbHost.Task();

  serviceMidiPassthrough();

  if (!liveStorageFault) pollButtons();
  if (!liveStorageFault && hasLoaded && activePresetValid) {
    const char *keys[] = {"Switch1", "Switch2", "Switch3"};
    for (int i = 0; i < switchCount; ++i) {
      if (i == 0 && !activePreset["FileInfo"].isNull()) continue;
      if (activePreset[keys[i]]["Gestures"].is<JsonObject>()) {
        serviceSwitchGesture(i, switchGestures[i].down);
      }
    }
  }
  if (!hasLoaded && (currentMillis - startMillis >= initialLoadDelayMs)) {
    hasLoaded = true;
    changePreset();
  }

  if (resetPresetDisplay && (currentMillis - switchDisplayStartMillis >= switchDisplayPeriodMs)) {
    resetPresetDisplay = false;
    requestPresetDisplayRefresh();
  }


  if (playingMidiFile) {
    if (!midiFilePlayer.isEOF()) {
      midiFilePlayer.getNextEvent();
    } else {
      playingMidiFile = false;
      stoppingMidiFile = true;
    }
  } else {
    if (stoppingMidiFile) {
      stopMidiFile(false);
      stoppingMidiFile = false;
      playingMidiFile = false;
      if (!midiClockEnabled) {
        startPresetMidiClock();
      }
      requestPresetDisplayRefresh();
    }
  }

  serviceMidiClock();
  serviceLiveEditor();
  servicePresetDisplayRefresh();
  servicePresetNameScroll();
  serviceMidiFileActivityIndicator();
  if (!liveStorageFault) servicePresetPrefetch();
  commitPendingEepromWrites();
}
