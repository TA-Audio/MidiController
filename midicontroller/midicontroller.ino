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

bool hasLoaded = false;
bool switchToggled[switchCount] = {};
unsigned long startMillis;
unsigned long currentMillis;
unsigned long switchDisplayStartMillis = 0;
bool resetPresetDisplay = false;
LiquidCrystal_I2C lcd(0x27, 20, 4);  // I2C address 0x27, 20 columns and 4 rows
JsonVariant activePreset;
int currentPreset = 0;
int presetCount = 0;
DMAMEM char presetList[maxPresetListSize][maxPresetNameLength];
DMAMEM StaticJsonDocument<4096> presetDoc;
FsFile rootDir;
FsFile sdFile;
MD_MIDIFile midiFilePlayer;
int midiFileOutputChannel;
bool playingMidiFile = false;
bool stoppingMidiFile = false;
int pcModeProgram = 0;
bool pcModeOn = false;
unsigned long longHoldStartMillis;
bool pendingDisplayRefresh = false;
bool pendingPresetSave = false;
bool pendingPcSave = false;
int pendingPresetValue = 0;
int pendingPcValue = 0;
unsigned long eepromDirtyMillis = 0;
int presetNavigationDirection = 1;
int prefetchedPresetIndex = -1;
int prefetchTargetIndex = -1;
bool prefetchRequested = false;
DMAMEM StaticJsonDocument<4096> prefetchedPresetDoc;

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI1);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, MIDI2);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial3, MIDI3);

USBHost usbHost;
MIDIDevice usbMidiDevice(usbHost);

bool loadPresetDocumentByIndex(int presetIndex, StaticJsonDocument<4096> &targetDoc);
bool applyPresetByIndex(int presetIndex);
void queueDirectionalPresetPrefetch();
void servicePresetPrefetch();
void changePreset();
void executeSwitchLogic(int switchNo);
void setPresetDisplayInfo();
void showError(const char *errorMessageLine1, const char *errorMessageLine2);

static inline void sendProgramChange(int pcValue, int channel, bool usbEvent) {
  const int safePc = clampMidi(pcValue);
  if (usbEvent) {
    usbMidiDevice.sendProgramChange(safePc, channel);
  } else {
    MIDI1.sendProgramChange(safePc, channel);
  }
}

static inline void sendControlChange(int ccNumber, int ccValue, int channel, bool usbEvent) {
  const int safeCcNumber = clampMidi(ccNumber);
  const int safeCcValue = clampMidi(ccValue);
  if (usbEvent) {
    usbMidiDevice.sendControlChange(safeCcNumber, safeCcValue, channel);
  } else {
    MIDI1.sendControlChange(safeCcNumber, safeCcValue, channel);
  }
}

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

static void setUiMessageTimeout() {
  switchDisplayStartMillis = currentMillis;
  resetPresetDisplay = true;
}

static inline void requestPresetDisplayRefresh() {
  pendingDisplayRefresh = true;
}

static inline void queuePresetSave(int value) {
  pendingPresetValue = value;
  pendingPresetSave = true;
  eepromDirtyMillis = currentMillis;
}

static inline void queuePcSave(int value) {
  pendingPcValue = value;
  pendingPcSave = true;
  eepromDirtyMillis = currentMillis;
}

static void commitPendingEepromWrites() {
  const bool hasPendingWrite = pendingPresetSave || pendingPcSave;
  if (!hasPendingWrite || !hasElapsed(currentMillis, eepromDirtyMillis, eepromCommitDelayMs)) {
    return;
  }

  if (pendingPresetSave) {
    EEPROM.put(presetEepromAddress, pendingPresetValue);
    pendingPresetSave = false;
  }

  if (pendingPcSave) {
    EEPROM.put(pcModeEepromAddress, pendingPcValue);
    pendingPcSave = false;
  }
}

static void servicePresetDisplayRefresh() {
  if (!pendingDisplayRefresh || resetPresetDisplay) {
    return;
  }

  pendingDisplayRefresh = false;
  setPresetDisplayInfo();
}

static void serviceMidiPassthrough() {
  while (MIDI2.read()) {
    MIDI1.send(MIDI2.getType(),
               MIDI2.getData1(),
               MIDI2.getData2(),
               MIDI2.getChannel());
  }

  while (MIDI3.read()) {
    MIDI1.send(MIDI3.getType(),
               MIDI3.getData1(),
               MIDI3.getData2(),
               MIDI3.getChannel());
  }
}

FLASHMEM bool loadPresetDocumentByIndex(int presetIndex, StaticJsonDocument<4096> &targetDoc) {
  if (presetIndex < 0 || presetIndex >= presetCount) {
    return false;
  }

  const char *fileName = presetList[presetIndex];
  if (!sdFile.open(fileName, O_READ)) {
    return false;
  }

  targetDoc.clear();
  DeserializationError error = deserializeJson(targetDoc, sdFile);
  sdFile.close();

  return !error;
}

bool applyPresetByIndex(int presetIndex) {
  if (prefetchedPresetIndex == presetIndex) {
    presetDoc.clear();
    presetDoc.set(prefetchedPresetDoc.as<JsonVariantConst>());
    return true;
  }

  return loadPresetDocumentByIndex(presetIndex, presetDoc);
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

  lcd.clear();
  displayCenteredLine(1, lineBuffer);
  setUiMessageTimeout();
}

FLASHMEM void showError(const char *errorMessageLine1, const char *errorMessageLine2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(errorMessageLine1);
  lcd.setCursor(0, 1);
  lcd.print(errorMessageLine2);
}

void setPresetDisplayInfo() {
  lcd.clear();
  lcd.setCursor(0, 0);
  const char *sw1 = "";
  const char *sw2 = "";
  const char *sw3 = "";

  if (pcModeOn) {
    lcd.print("Prog Change Mode");
    sw1 = "";
    sw2 = "Down";
    sw3 = "Up";
    lcd.setCursor(0, 1);
    lcd.print("Current PC: ");
    lcd.print(pcModeProgram);
  } else {
    lcd.print(activePreset["Name"] | "");

    JsonObject fileInfo = activePreset["FileInfo"];
    if (!fileInfo.isNull()) {

      if (playingMidiFile) {
        sw1 = "Stop";
      } else {
        sw1 = "Play";
      }

    } else {
      sw1 = activePreset["Switch1"]["Name"] | "";
    }
    sw2 = activePreset["Switch2"]["Name"] | "";
    sw3 = activePreset["Switch3"]["Name"] | "";
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

static void switchHandler(uint8_t btnId, uint8_t btnState) {
  if (btnState == BTN_PRESSED) {
    longHoldStartMillis = currentMillis;
    if (hasLoaded && btnId <= 3) {
      executeSwitchLogic(btnId);
    }
    return;
  }

  // BTN_OPEN — handle nav buttons on release to support long-hold detection
  if (!hasLoaded) {
    return;
  }

  if ((btnId == 4 || btnId == 5) && hasElapsed(currentMillis, longHoldStartMillis, longHoldToggleMs)) {
    pcModeOn = !pcModeOn;
    sendProgramChange(pcModeProgram, 1, true);
    requestPresetDisplayRefresh();
    return;
  }

  if (btnId == 4) {
    presetNavigationDirection = 1;
    if (!canNavigateNext(currentPreset, presetCount)) {
      return;
    }
    currentPreset++;
    changePreset();
  } else if (btnId == 5) {
    presetNavigationDirection = -1;
    if (!canNavigatePrev(currentPreset)) {
      return;
    }
    currentPreset--;
    changePreset();
  }
}

FLASHMEM void stopMidiFile() {
  sendCcArray(activePreset["FileInfo"]["StopCC"]);
}

FLASHMEM void toggleMidiFilePlayback(JsonObject fileInfo) {
  const char *midiFile = fileInfo["FileName"].as<const char *>();

  if (!playingMidiFile) {
    int err = midiFilePlayer.load(midiFile);
    if (err != MD_MIDIFile::E_OK) {
      showError("Midi File load Error ", "");
    } else {
      midiFilePlayer.setTempo(fileInfo["BPM"].as<int>());
      midiFileOutputChannel = fileInfo["Channel"].as<int>();
      playingMidiFile = true;
    }
  } else {
    playingMidiFile = false;
    stoppingMidiFile = true;
  }

  char text[uiTextBufferLength];
  snprintf(text, sizeof(text), "%s", midiFile != nullptr ? midiFile : "");

  if (strlen(text) > 1) {
    lcd.clear();
    displayCenteredLine(1, text);
    lcd.setCursor(0, 2);
    lcd.print(playingMidiFile ? " Playing" : " Stopping");
    setUiMessageTimeout();
  }
}

void handlePcModeEvent(int switchNo) {
  pcModeProgram = adjustPcProgram(pcModeProgram, switchNo == 2);
  sendProgramChange(pcModeProgram, 1, true);
  queuePcSave(pcModeProgram);
  requestPresetDisplayRefresh();
}

void executeSwitchLogic(int switchNo) {
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

  if (pcModeOn) {
    handlePcModeEvent(switchNo);
    return;
  }

  const char *switchName = switchLogic["Name"].as<const char *>();
  bool toggle = switchLogic["Toggle"].as<bool>();
  const bool wasToggled = switchToggled[switchNo - 1];

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

      sendControlChange(ccNumber, ccValue, ccChannel, usbEvent);
    }

    if (useToggleValue) {
      switchToggled[switchNo - 1] = nextToggleState;
    }
  }

  if (toggle) {
    showSwitchActionMessage(switchName, !wasToggled ? " On!" : " Off!");
  } else {
    showSwitchActionMessage(switchName, "");
  }
}

FLASHMEM void changePreset() {
  if (pcModeOn) {
    return;
  }

  if (resetPresetDisplay) {
    resetPresetDisplay = false;
  }

  if (presetCount <= 0) {
    showError("No presets found", "Add .json files to SD");
    return;
  }

  if (currentPreset < 0) {
    currentPreset = 0;
  }

  if (playingMidiFile) {
    playingMidiFile = false;
    stoppingMidiFile = true;
  }

  memset(switchToggled, 0, sizeof(switchToggled));

  // if saved currentPreset value is greater than the number of presets, reset to 0
  currentPreset = clampPresetIndex(currentPreset, presetCount);

  if (!applyPresetByIndex(currentPreset)) {
    showError("Preset load error", "Check JSON / SD card");
    return;
  }

  activePreset = presetDoc;

  sendPcArray(activePreset["OnLoad"]["PC"]);
  sendCcArray(activePreset["OnLoad"]["CC"]);

  setPresetDisplayInfo();
  queuePresetSave(currentPreset);
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
  lcd.print("SYNAPSE");
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

  if ((pev->data[0] >= 0x80) && (pev->data[0] <= 0xe0)) {
    Serial1.write(pev->data[0] | (midiFileOutputChannel - 1));
    Serial1.write(&pev->data[1], pev->size - 1);
  }
}

static Button switch1Button(1, switchHandler);
static Button switch2Button(2, switchHandler);
static Button switch3Button(3, switchHandler);
static Button nextPresetButton(4, switchHandler);
static Button prevPresetButton(5, switchHandler);

FLASHMEM void setup() {

  lcd.init();  // initialize the lcd
  lcd.backlight();
  showBootScreen();

  // Wait 1.5 seconds before turning on USB Host.  If connected USB devices
  // use too much power, Teensy at least completes USB enumeration, which
  // makes isolating the power issue easier.
  delay(1500);
  usbHost.begin();

  pinMode(switch1Pin, INPUT);
  pinMode(switch2Pin, INPUT);
  pinMode(switch3Pin, INPUT);
  pinMode(nextPresetPin, INPUT);
  pinMode(prevPresetPin, INPUT);

  MIDI1.begin(MIDI_CHANNEL_OMNI);

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

  loadPresetList();

  EEPROM.get(presetEepromAddress, currentPreset);
  EEPROM.get(pcModeEepromAddress, pcModeProgram);

  currentPreset = validateStoredPreset(currentPreset, maxPresetListSize);

  const int validatedPc = validateStoredPcProgram(pcModeProgram);
  if (validatedPc != pcModeProgram) {
    pcModeProgram = validatedPc;
    queuePcSave(pcModeProgram);
  }

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

  MIDI1.read();
  serviceMidiPassthrough();

  pollButtons();
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
      midiFilePlayer.close();
      stopMidiFile();
      stoppingMidiFile = false;
      playingMidiFile = false;
      requestPresetDisplayRefresh();
    }
  }

  servicePresetDisplayRefresh();
  servicePresetPrefetch();
  commitPendingEepromWrites();
}


