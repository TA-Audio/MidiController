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

static constexpr int switch1Pin = 2;
static constexpr int switch2Pin = 3;
static constexpr int switch3Pin = 4;
static constexpr int nextPresetPin = 5;
static constexpr int prevPresetPin = 6;
static constexpr unsigned long initialLoadDelayMs = 1000;
static constexpr unsigned long switchDisplayPeriodMs = 1500;
static constexpr unsigned long longHoldToggleMs = 3000;
static constexpr int lcdColumnCount = 20;
static constexpr int maxPresetListSize = 150;
static constexpr int maxPresetNameLength = 25;
static constexpr int uiTextBufferLength = 50;
static constexpr int midiValueMin = 0;
static constexpr int midiValueMax = 127;
static constexpr unsigned long eepromCommitDelayMs = 200;

bool hasLoaded = false;
bool switchOneToggled = false;
bool switchTwoToggled = false;
bool switchThreeToggled = false;
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
int presetEepromAddress = 0;
int pcModeEepromAddress = 1000;
int presetListCount = 0;
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

int extractNumber(const char *filename);
bool loadPresetDocumentByIndex(int presetIndex, StaticJsonDocument<4096> &targetDoc);
bool applyPresetByIndex(int presetIndex);
void queueDirectionalPresetPrefetch();
void servicePresetPrefetch();
void changePreset();
void executeSwitchLogic(int switchNo);
void setPresetDisplayInfo();
void showError(const char *errorMessageLine1, const char *errorMessageLine2);

static inline bool hasElapsed(unsigned long start, unsigned long durationMs) {
  return (millis() - start) >= durationMs;
}

static inline int clampMidi(int value) {
  if (value < midiValueMin) {
    return midiValueMin;
  }
  if (value > midiValueMax) {
    return midiValueMax;
  }
  return value;
}

static inline bool isSwitchToggled(int switchNo) {
  switch (switchNo) {
    case 1:
      return switchOneToggled;
    case 2:
      return switchTwoToggled;
    default:
      return switchThreeToggled;
  }
}

static inline void setSwitchToggled(int switchNo, bool value) {
  switch (switchNo) {
    case 1:
      switchOneToggled = value;
      break;
    case 2:
      switchTwoToggled = value;
      break;
    default:
      switchThreeToggled = value;
      break;
  }
}

static inline void sendProgramChange(int pcValue, int channel, bool usbEvent) {
  const int safePc = clampMidi(pcValue);
  if (usbEvent) {
    usbMidiDevice.sendProgramChange(safePc, channel);
  } else {
    MIDI1.sendProgramChange(safePc, channel);
  }
}

static inline void sendControlChange(int ccNumber, int ccValue, int channel, bool usbEvent) {
  const int safeCcValue = clampMidi(ccValue);
  if (usbEvent) {
    usbMidiDevice.sendControlChange(ccNumber, safeCcValue, channel);
  } else {
    MIDI1.sendControlChange(ccNumber, safeCcValue, channel);
  }
}

static void setUiMessageTimeout() {
  switchDisplayStartMillis = millis();
  resetPresetDisplay = true;
}

static inline void requestPresetDisplayRefresh() {
  pendingDisplayRefresh = true;
}

static inline void queuePresetSave(int value) {
  pendingPresetValue = value;
  pendingPresetSave = true;
  eepromDirtyMillis = millis();
}

static inline void queuePcSave(int value) {
  pendingPcValue = value;
  pendingPcSave = true;
  eepromDirtyMillis = millis();
}

static void commitPendingEepromWrites() {
  const bool hasPendingWrite = pendingPresetSave || pendingPcSave;
  if (!hasPendingWrite || !hasElapsed(eepromDirtyMillis, eepromCommitDelayMs)) {
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
  if (presetCount <= 1) {
    prefetchRequested = false;
    prefetchTargetIndex = -1;
    prefetchedPresetIndex = -1;
    prefetchedPresetDoc.clear();
    return;
  }

  int candidateIndex = currentPreset + presetNavigationDirection;
  if (candidateIndex < 0 || candidateIndex >= presetCount) {
    candidateIndex = currentPreset - presetNavigationDirection;
  }

  if (candidateIndex < 0 || candidateIndex >= presetCount || candidateIndex == currentPreset) {
    prefetchRequested = false;
    prefetchTargetIndex = -1;
    return;
  }

  if (prefetchedPresetIndex == candidateIndex) {
    prefetchRequested = false;
    prefetchTargetIndex = -1;
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
  const int padding = (lcdColumnCount - safeLength) / 2;

  lcd.setCursor(0, row);
  for (int i = 0; i < padding; i++) {
    lcd.print(' ');
  }
  for (int i = 0; i < safeLength; i++) {
    lcd.print(text[i]);
  }
}

FLASHMEM static void showSwitchActionMessage(const char *text, const char *suffix) {
  char lineBuffer[uiTextBufferLength];
  if (suffix != nullptr && suffix[0] != '\0') {
    snprintf(lineBuffer, sizeof(lineBuffer), "%s%s", text != nullptr ? text : "", suffix);
  } else {
    snprintf(lineBuffer, sizeof(lineBuffer), "%s", text != nullptr ? text : "");
  }

  if (lineBuffer[0] == '\0') {
    return;
  }

  lcd.clear();
  displayCenteredLine(1, lineBuffer);
  setUiMessageTimeout();
}

FLASHMEM static int comparePresetNames(const void *lhs, const void *rhs) {
  const char *a = static_cast<const char *>(lhs);
  const char *b = static_cast<const char *>(rhs);

  const int numA = extractNumber(a);
  const int numB = extractNumber(b);

  if (numA < numB) {
    return -1;
  }
  if (numA > numB) {
    return 1;
  }
  return strncmp(a, b, maxPresetNameLength);
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
    lcd.print(activePreset["Name"].as<const char *>());  // print message at the second row

    JsonObject fileInfo = activePreset["FileInfo"];
    if (!fileInfo.isNull()) {

      if (playingMidiFile) {
        sw1 = "Stop";
      } else {
        sw1 = "Play";
      }

    } else {
      sw1 = activePreset["Switch1"]["Name"].as<const char *>();
    }
    sw2 = activePreset["Switch2"]["Name"].as<const char *>();
    sw3 = activePreset["Switch3"]["Name"].as<const char *>();
  }


  int sw1Length = (int)strlen(sw1);
  int sw2Length = (int)strlen(sw2);
  int sw3Length = (int)strlen(sw3);

  // Calculate the padding needed for each string
  int totalLength = sw1Length + sw2Length + sw3Length;
  int padding1 = (totalLength < lcdColumnCount) ? (lcdColumnCount - totalLength) / 2 : 0;
  int padding3 = (totalLength < lcdColumnCount) ? (lcdColumnCount - totalLength + 1) / 2 : 0;

  char sw1Indicator[maxPresetNameLength];
  char sw2Indicator[maxPresetNameLength];
  char sw3Indicator[maxPresetNameLength];

  const int sw1CopyLength = sw1Length < (maxPresetNameLength - 1) ? sw1Length : (maxPresetNameLength - 1);
  const int sw2CopyLength = sw2Length < (maxPresetNameLength - 1) ? sw2Length : (maxPresetNameLength - 1);
  const int sw3CopyLength = sw3Length < (maxPresetNameLength - 1) ? sw3Length : (maxPresetNameLength - 1);

  lcd.setCursor(0, 2);

  for (int i = 0; i < sw1CopyLength; i++) {
    sw1Indicator[i] = (switchOneToggled) ? '*' : ' ';
  }
  sw1Indicator[sw1CopyLength] = '\0';

  for (int i = 0; i < sw2CopyLength; i++) {
    sw2Indicator[i] = (switchTwoToggled) ? '*' : ' ';
  }
  sw2Indicator[sw2CopyLength] = '\0';

  for (int i = 0; i < sw3CopyLength; i++) {
    sw3Indicator[i] = (switchThreeToggled) ? '*' : ' ';
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

  if (btnState == BTN_PRESSED && (btnId == 4 || btnId == 5) && hasElapsed(longHoldStartMillis, longHoldToggleMs) && hasLoaded) {
    pcModeOn = !pcModeOn;

    sendProgramChange(pcModeProgram, 1, true);
    requestPresetDisplayRefresh();


    return;
  }

  if (btnState == BTN_PRESSED && hasLoaded) {
    if (btnId == 1 || btnId == 2 || btnId == 3) {
      executeSwitchLogic(btnId);
    } else if (btnId == 4) {
      presetNavigationDirection = 1;
      if (currentPreset >= (presetCount - 1)) {
        return;
      }

      currentPreset++;

      changePreset();
    } else if (btnId == 5) {
      presetNavigationDirection = -1;
      currentPreset--;
      if (currentPreset < 0) {
        currentPreset = 0;
      }

      changePreset();
    }
  }

  if (btnState == BTN_OPEN) {
    longHoldStartMillis = millis();
  }
}

FLASHMEM void stopMidiFile() {
  JsonArray stopCC = activePreset["FileInfo"]["StopCC"];

  if (!stopCC.isNull()) {
    for (JsonVariant ccEvent : stopCC) {
      int ccNumber = ccEvent["CC"];
      int ccValue = ccEvent["Value"];
      int ccChannel = ccEvent["Channel"];
      bool usbEvent = ccEvent["USB"];


      sendControlChange(ccNumber, ccValue, ccChannel, usbEvent);
    }
  }
}

FLASHMEM void toggleMidiFilePlayback(JsonObject fileInfo) {

  char text[uiTextBufferLength];
  const char *playingText = " Playing";
  const char *stoppingText = " Stopping";
  const char *midiFile = fileInfo["FileName"].as<const char *>();

  if (!playingMidiFile) {
    int err = midiFilePlayer.load(midiFile);
    if (err != MD_MIDIFile::E_OK) {
      showError("Midi File load Error ", "");
    } else {

      midiFilePlayer.setTempo(fileInfo["BPM"].as<int>());
      midiFileOutputChannel = fileInfo["Channel"].as<int>();
    }
    if (err == MD_MIDIFile::E_OK) {
      playingMidiFile = true;
    }
  } else {
    playingMidiFile = false;
    stoppingMidiFile = true;
  }

  snprintf(text, sizeof(text), "%s", midiFile != nullptr ? midiFile : "");
  int textLength = (int)strlen(text);

  if (textLength > 1) {
    lcd.clear();
    displayCenteredLine(1, text);
    lcd.setCursor(0, 2);
    if (playingMidiFile) {
      lcd.print(playingText);
    } else {
      lcd.print(stoppingText);
    }
    setUiMessageTimeout();
  }
}

void handlePcModeEvent(int switchNo) {

  // bool usbEvent = activePreset["PCMode"]["USB"].as<bool>();
  // int channel = activePreset["PCMode"]["Channel"].as<int>();


  if (switchNo == 2) {
    if (pcModeProgram >= 1) {
      pcModeProgram--;
    }

  } else {
    pcModeProgram++;
  }

  pcModeProgram = clampMidi(pcModeProgram);

  // if (usbEvent) {
  sendProgramChange(pcModeProgram, 1, true);
  // } else {
  //   MIDI1.sendProgramChange(pcModeProgram, channel);
  // }

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


  JsonArray switchPC = switchLogic["PC"];
  JsonObject fileInfo = activePreset["FileInfo"];


  if (!fileInfo.isNull() && switchNo == 1) {
    toggleMidiFilePlayback(fileInfo);
  } else {

    if (pcModeOn) {
      handlePcModeEvent(switchNo);

    } else {

      const char *tempText = switchLogic["Name"].as<const char *>();
      bool toggle = switchLogic["Toggle"].as<bool>();
      const bool wasToggled = isSwitchToggled(switchNo);
      const bool nextToggleStateForMessage = !wasToggled;

      const char *onText = " On!";
      const char *offText = " Off!";

      if (!switchPC.isNull()) {
        for (JsonVariant pcEvent : switchPC) {
          int pc = pcEvent["PC"];
          int channel = pcEvent["Channel"];
          bool usbEvent = pcEvent["USB"];

          sendProgramChange(pc - 1, channel, usbEvent);
        }
      }

      JsonArray ccArray = switchLogic["CC"];

      if (!ccArray.isNull()) {
        bool nextToggleState = false;
        bool useToggleValue = false;

        if (toggle) {
          nextToggleState = !isSwitchToggled(switchNo);
          useToggleValue = true;
        }

        for (JsonVariant cc : ccArray) {
          int ccNumber = cc["CC"];
          int ccValue = cc["Value"];

          if (useToggleValue) {
            ccValue = nextToggleState ? 127 : 0;
          }

          int ccChannel = cc["Channel"];
          bool usbEvent = cc["USB"];

          sendControlChange(ccNumber, ccValue, ccChannel, usbEvent);
        }

        if (useToggleValue) {
          setSwitchToggled(switchNo, nextToggleState);
        }
      }

      if (toggle) {
        showSwitchActionMessage(tempText, nextToggleStateForMessage ? onText : offText);
      } else {
        showSwitchActionMessage(tempText, "");
      }
    }
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

  switchOneToggled = false;
  switchTwoToggled = false;
  switchThreeToggled = false;

  // if saved currentPreset value is greater than the number of presets, reset to 0
  if (currentPreset >= presetCount) {
    currentPreset = 0;
  }

  if (!applyPresetByIndex(currentPreset)) {
    showError("Preset load error", "Check JSON / SD card");
    return;
  }

  activePreset = presetDoc;

  JsonArray onLoadPC = activePreset["OnLoad"]["PC"];
  JsonArray onLoadCC = activePreset["OnLoad"]["CC"];


  if (!onLoadPC.isNull()) {
    for (JsonVariant pcEvent : onLoadPC) {
      int pc = pcEvent["PC"];
      int channel = pcEvent["Channel"];
      bool usbEvent = pcEvent["USB"];

      sendProgramChange(pc - 1, channel, usbEvent);
    }
  }

  if (!onLoadCC.isNull()) {
    for (JsonVariant ccEvent : onLoadCC) {
      int ccNumber = ccEvent["CC"];
      int ccValue = ccEvent["Value"];
      int ccChannel = ccEvent["Channel"];
      bool usbEvent = ccEvent["USB"];

      sendControlChange(ccNumber, ccValue, ccChannel, usbEvent);
    }
  }

  setPresetDisplayInfo();
  queuePresetSave(currentPreset);
  queueDirectionalPresetPrefetch();
}

FLASHMEM void showBootScreen() {

  lcd.setCursor(0, 0);           // move cursor the first row
  lcd.print("TA Audio");         // print message at the first row
  lcd.setCursor(0, 1);           // move cursor to the second row
  lcd.print("SYNAPSE");          // print message at the second row
  lcd.setCursor(0, 2);           // move cursor to the third row
  lcd.print("MIDI CONTROLLER");  // print message at the third row
  lcd.setCursor(0, 3);           // move cursor to the fourth row
  lcd.print("v0.1.0");           // print message the fourth row

  // Change to use millis to setup can continue whilst lcb boot seq is shown
  delay(2000);
}

FLASHMEM void loadPresetList() {

  // Open root directory
  presetListCount = 0;
  rootDir.open("/");



  while (sdFile.openNext(&rootDir, O_RDONLY)) {

    int max_characters = maxPresetNameLength;
    char f_name[max_characters];  // the filename variable you want
    sdFile.getName(f_name, max_characters);

    // Check if the sdFile has a ".json" extension
    const char *extension = ".json";
    int nameLength = strlen(f_name);
    int extensionLength = strlen(extension);

    // Check if the sdFile ends with ".json"
    if (nameLength >= extensionLength && strcmp(f_name + nameLength - extensionLength, extension) == 0) {
      if (presetListCount >= maxPresetListSize) {
        sdFile.close();
        break;
      }
      // If it ends with ".json", copy the filename to presetList
      strncpy(presetList[presetListCount], f_name, maxPresetNameLength);
      presetList[presetListCount][maxPresetNameLength - 1] = '\0';
      presetListCount++;  // Increment the list length
    }

    sdFile.close();
  }

  if (rootDir.getError()) {
    showError("Error opening SD", "");
  }

  rootDir.close();

  // Sort presetList numerically
  if (presetListCount > 1) {
    qsort(presetList, presetListCount, sizeof(presetList[0]), comparePresetNames);
  }

  presetCount = presetListCount;
  prefetchedPresetIndex = -1;
  prefetchTargetIndex = -1;
  prefetchRequested = false;
  prefetchedPresetDoc.clear();
}

// Function to extract number from filename
FLASHMEM int extractNumber(const char *filename) {
  int num = 0;
  int i = 0;
  while (filename[i] != '\0' && filename[i] >= '0' && filename[i] <= '9') {
    num = num * 10 + (filename[i] - '0');
    i++;
  }
  return num;
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

  // initialize the digital pin as an output.
  pinMode(13, OUTPUT);
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

  // delay(500);

  EEPROM.get(presetEepromAddress, currentPreset);
  EEPROM.get(pcModeEepromAddress, pcModeProgram);

  if (pcModeProgram < 0 || pcModeProgram > 127) {
    pcModeProgram = 0;
    queuePcSave(pcModeProgram);
  }

  Serial.println(currentPreset);

  startMillis = millis();
}

static void pollButtons() {
  // update() will call buttonHandler() if PIN transitions to a new state and stays there
  // for multiple reads over 25+ ms.
  switch1Button.update(digitalRead(switch1Pin));
  switch2Button.update(digitalRead(switch2Pin));
  switch3Button.update(digitalRead(switch3Pin));
  nextPresetButton.update(digitalRead(nextPresetPin));
  prevPresetButton.update(digitalRead(prevPresetPin));
}

void loop() {

  currentMillis = millis();

  usbHost.Task();

  // usbMidiDevice.sendProgramChange(1, 1);
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
    if (stoppingMidiFile == true) {
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


