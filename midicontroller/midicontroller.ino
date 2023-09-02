#include <ArduinoJson.h>
#include <MIDI.h>
#include <debounce.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include "SdFat.h"

// Allocate the JSON document
DynamicJsonDocument doc(1024);
LiquidCrystal_I2C lcd(0x27, 20, 4); // I2C address 0x27, 20 column and 4 rows
const int switch1 = 5;
const int switch2 = 6;
const int switch3 = 7;
const int nextPreset = 9;
const int prevPreset = 10;
const int chipSelect = 53;
int currentPreset = 0;
int address = 0;
int readValue = 0;
SdFat sd;
SdFile configFile;
JsonVariant preset;
FsFile dir;
FsFile file;
const int maxListSize = 150;                   // Maximum number of words
const int maxStringLength = 25;                // Maximum length of each word
char presetList[maxListSize][maxStringLength]; // Array to store strings
int currentIndex = 0;                          // Current index in the array

MIDI_CREATE_DEFAULT_INSTANCE();

static void switch1Handler(uint8_t btnId, uint8_t btnState)
{
  if (btnState == BTN_PRESSED)
  {
    ExecuteSwitchLogic(1);
  }
}

static void switch2Handler(uint8_t btnId, uint8_t btnState)
{
  if (btnState == BTN_PRESSED)
  {
    ExecuteSwitchLogic(2);
  }
}

static void switch3Handler(uint8_t btnId, uint8_t btnState)
{
  if (btnState == BTN_PRESSED)
  {
    ExecuteSwitchLogic(3);
  }
}

static void nextPresetHandler(uint8_t btnId, uint8_t btnState)
{
  if (btnState == BTN_PRESSED)
  {
    currentPreset++;
    ChangePreset();
  }
}

static void prevPresetHanlder(uint8_t btnId, uint8_t btnState)
{
  if (btnState == BTN_PRESSED)
  {
    currentPreset--;
    ChangePreset();
  }
}

void ExecuteSwitchLogic(int switchNo)
{

  JsonObject switchLogic;

  switch (switchNo)
  {
  case 1:
    switchLogic = preset["Switch1"];
    break;
  case 2:
    switchLogic = preset["Switch2"];
    break;
  case 3:
    switchLogic = preset["Switch3"];
    break;
  }

  JsonArray switchPC = switchLogic["PC"];

  if (!switchPC.isNull())
  {
    for (JsonVariant pcEvent : switchPC)
    {
      int pc = pcEvent["PC"];
      int channel = pcEvent["Channel"];
      Serial.print("PC: ");
      Serial.println(pc);
      Serial.print("Channel: ");
      Serial.println(channel);

      MIDI.sendProgramChange(pc, channel);
    }
  }

  JsonArray ccArray = switchLogic["CC"];

  if (!ccArray.isNull())
  {
    for (JsonVariant cc : ccArray)
    {
      int ccNumber = cc["CC"];
      int ccValue = cc["Value"];
      int ccChannel = cc["Channel"];
      Serial.print("CC Number: ");
      Serial.println(ccNumber);
      Serial.print("CC Value: ");
      Serial.println(ccValue);
      Serial.print("CC Channel: ");
      Serial.println(ccChannel);

      MIDI.sendControlChange(ccValue, ccNumber, ccChannel);
    }
  }
}

void ChangePreset()
{

  // if end of list, go back to start

  if (currentPreset >= currentIndex)
  {
    currentPreset = 0;
  }

  EEPROM.put(address, currentPreset);
  Serial.println("Change preset called");
  Serial.println(currentPreset);

  char *fileName = presetList[currentPreset + 1];

  Serial.println(fileName);

  if (!configFile.open(fileName, O_READ))
  {
    sd.errorHalt("sd error");
  }

  DeserializationError error = deserializeJson(doc, configFile);

  // Test if parsing succeeds.
  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  preset = doc;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(preset["Name"].as<const char *>()); // print message at the second row
  lcd.setCursor(0, 3);
  const char *sw1 = preset["Switch1"]["Name"].as<const char *>();
  const char *sw2 = preset["Switch2"]["Name"].as<const char *>();
  const char *sw3 = preset["Switch3"]["Name"].as<const char *>();
  int sw1Length = strlen(sw1);
  int sw2Length = strlen(sw2);
  int sw3Length = strlen(sw3);

  // Calculate the padding needed for each string
  int totalLength = sw1Length + sw2Length + sw3Length;
  int padding1 = (totalLength < 20) ? (20 - totalLength) / 2 : 0;
  int padding3 = (totalLength < 20) ? (20 - totalLength + 1) / 2 : 0;

  lcd.print(sw1);
  for (int i = 0; i < padding1; i++)
  {
    lcd.print(" ");
  }

  lcd.print(sw2);

  for (int i = 0; i < padding3; i++)
  {
    lcd.print(" ");
  }
  lcd.print(sw3);

  JsonArray onLoadPC = preset["OnLoad"]["PC"];
  JsonArray onLoadCC = preset["OnLoad"]["CC"];

  if (!onLoadPC.isNull())
  {
    for (JsonVariant pcEvent : onLoadPC)
    {
      int pc = pcEvent["PC"];
      int channel = pcEvent["Channel"];
      Serial.print("PC: ");
      Serial.println(pc);
      Serial.print("Channel: ");
      Serial.println(channel);

      MIDI.sendProgramChange(pc, channel);
    }
  }

  if (!onLoadCC.isNull())
  {
    for (JsonVariant ccEvent : onLoadCC)
    {
      int ccNumber = ccEvent["CC"];
      int ccValue = ccEvent["Value"];
      int ccChannel = ccEvent["Channel"];
      Serial.print("CC Number: ");
      Serial.println(ccNumber);
      Serial.print("CC Value: ");
      Serial.println(ccValue);
      Serial.print("CC Channel: ");
      Serial.println(ccChannel);

      MIDI.sendControlChange(ccValue, ccNumber, ccChannel);
    }
  }
}

static Button switch1Button(1, switch1Handler);
static Button switch2Button(2, switch2Handler);
static Button switch3Button(3, switch3Handler);
static Button nextPresetButton(4, nextPresetHandler);
static Button prevPresetButton(4, prevPresetHanlder);

void setup()
{

  lcd.init(); // initialize the lcd
  lcd.backlight();
  BootLCD();

  pinMode(switch1, INPUT);
  pinMode(switch2, INPUT);
  pinMode(switch3, INPUT);
  pinMode(nextPreset, INPUT);
  pinMode(prevPreset, INPUT);

  MIDI.begin();
  MIDI.turnThruOff();

  // Initialize serial port
  Serial.begin(9600);
  while (!Serial)
    continue;

  lcd.clear();
  lcd.print("LOADING PRESETS...");

  if (!sd.begin(chipSelect, SPI_HALF_SPEED))
    sd.initErrorHalt();

  GetPresets();

  delay(500);

  // Serial.println(doc.memoryUsage());

  // currentPreset = EEPROM.get(address, readValue);
  // if (currentPreset == -1)
  // {
  //   currentPreset = 0;
  // }

  currentPreset = 0;

  ChangePreset();
}

void GetPresets()
{

  // Open root directory
  dir.open("/");

  // create an array for each file name
  // filenames = new String[dir.count()];

  while (file.openNext(&dir, O_RDONLY))
  {

    int max_characters = 25;     // guess the needed characters
    char f_name[max_characters]; // the filename variable you want
    file.getName(f_name, max_characters);
    // Serial.println(f_name);

    strncpy(presetList[currentIndex], f_name, maxStringLength);
    currentIndex++; // Increment the list length

    file.close();
  }
  if (dir.getError())
  {
    Serial.println("openNext failed");
  }
  else
  {
    Serial.println("Done!");
  }
}

void BootLCD()
{

  lcd.setCursor(0, 0);          // move cursor the first row
  lcd.print("TA Audio");        // print message at the first row
  lcd.setCursor(0, 1);          // move cursor to the second row
  lcd.print("SYNAPSE");         // print message at the second row
  lcd.setCursor(0, 2);          // move cursor to the third row
  lcd.print("MIDI CONTROLLER"); // print message at the third row
  lcd.setCursor(0, 3);          // move cursor to the fourth row
  lcd.print("v0.0.1");          // print message the fourth row
  delay(4000);
}

static void pollButtons()
{
  // update() will call buttonHandler() if PIN transitions to a new state and stays there
  // for multiple reads over 25+ ms.
  switch1Button.update(digitalRead(switch1));
  switch2Button.update(digitalRead(switch2));
  switch3Button.update(digitalRead(switch3));
  nextPresetButton.update(digitalRead(nextPreset));
  prevPresetButton.update(digitalRead(prevPreset));
}

void loop()
{
  // pollButtons();
  currentPreset++;
  ChangePreset();
  delay(10000);
}
