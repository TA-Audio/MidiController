
#include <ArduinoJson.h>
#include <MIDI.h>
#include <debounce.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include "SdFat.h"
#include <debounce.h>

// Allocate the JSON document
DynamicJsonDocument doc(1024);
LiquidCrystal_I2C lcd(0x27, 20, 4); // I2C address 0x27, 20 column and 4 rows
static constexpr int switch1 = 5;
static constexpr int switch2 = 6;
static constexpr int switch3 = 7;
static constexpr int nextPreset = 9;
static constexpr int prevPreset = 10;
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
unsigned long debounceDelay = 50;              // debounce time in milliseconds
unsigned long lastDebounceTimeInc = 0;         // variable to store the last button press time
unsigned long lastDebounceTimeDec = 0;         // variable to store the last button press time
int numPrograms = 0;
bool hasLoaded = false;
unsigned long startMillis;
unsigned long currentMillis;
const unsigned long period = 1000;

MIDI_CREATE_DEFAULT_INSTANCE();

static void switchHandler(uint8_t btnId, uint8_t btnState)
{
  if (btnState == BTN_PRESSED && hasLoaded == true)
  {
    // Serial.println("Switch pressed");

    if (btnId == 1 || btnId == 2 || btnId == 3)
    {
      ExecuteSwitchLogic(btnId);
    }
    else if (btnId == 4)
    {
      currentPreset++;
      if (currentPreset >= numPrograms)
      {
        currentPreset = 0; // loop back to the start
      }

      ChangePreset();
    }
    else if (btnId == 5)
    {
      currentPreset--;
      if (currentPreset < 0)
      {
        // currentPreset = numPrograms - 1; // loop back to the end
        currentPreset = 0; // loop back to the end
      }

      ChangePreset();
    }
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

  const char *text = switchLogic["Name"].as<const char *>();

  int textLength = strlen(text);

  if (textLength > 1)
  {
    lcd.clear();
    lcd.setCursor(0, 1);
    int padding = (20 - textLength) / 2;

    // Print leading spaces for centering
    for (int i = 0; i < padding; i++)
    {
      lcd.print(" ");
    }

    // Print the text
    lcd.print(text);
  }

  if (!switchPC.isNull())
  {
    for (JsonVariant pcEvent : switchPC)
    {
      int pc = pcEvent["PC"];
      int channel = pcEvent["Channel"];
      // Serial.print("PC: ");
      // Serial.println(pc);
      // Serial.print("Channel: ");
      // Serial.println(channel);

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
      // Serial.print("CC Number: ");
      // Serial.println(ccNumber);
      // Serial.print("CC Value: ");
      // Serial.println(ccValue);
      // Serial.print("CC Channel: ");
      // Serial.println(ccChannel);

      MIDI.sendControlChange(ccNumber, ccValue, ccChannel);
    }
  }

  delay(1000);
  SetPresetDisplayInfo();
  
}

void ChangePreset()
{

  if (currentPreset < 0)
  {
    currentPreset = 0;
  }

  // EEPROM.put(address, currentPreset);
  // Serial.println("Change preset called");
  // Serial.println(currentPreset);

  char *fileName = presetList[currentPreset + 1];

  // Serial.println(fileName);

  if (!configFile.open(fileName, O_READ))
  {
    sd.errorHalt("sd error");
  }

  DeserializationError error = deserializeJson(doc, configFile);

  // Test if parsing succeeds.
  if (error)
  {
    // Serial.print(F("deserializeJson() failed: "));
    // Serial.println(error.f_str());
    return;
  }

  preset = doc;

  SetPresetDisplayInfo();

  JsonArray onLoadPC = preset["OnLoad"]["PC"];
  JsonArray onLoadCC = preset["OnLoad"]["CC"];

  if (!onLoadPC.isNull())
  {
    for (JsonVariant pcEvent : onLoadPC)
    {
      int pc = pcEvent["PC"];
      int channel = pcEvent["Channel"];
      // Serial.print("PC: ");
      // Serial.println(pc);
      // Serial.print("Channel: ");
      // Serial.println(channel);

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
      // Serial.print("CC Number: ");
      // Serial.println(ccNumber);
      // Serial.print("CC Value: ");
      // Serial.println(ccValue);
      // Serial.print("CC Channel: ");
      // Serial.println(ccChannel);

      MIDI.sendControlChange(ccNumber, ccValue, ccChannel);
    }
  }
}

void SetPresetDisplayInfo()
{
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
}

static Button switch1Button(1, switchHandler);
static Button switch2Button(2, switchHandler);
static Button switch3Button(3, switchHandler);
static Button nextPresetButton(4, switchHandler);
static Button prevPresetButton(5, switchHandler);

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

  // Initialize serial port
  Serial.begin(9600);
  while (!Serial)
    continue;

  MIDI.begin(MIDI_CHANNEL_OMNI);

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
  startMillis = millis();
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
    // Serial.println("openNext failed");
  }
  else
  {
    // Serial.println("Done!");
  }

  numPrograms = currentIndex;
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
  currentMillis = millis();

  pollButtons();
  delay(25);
  if (!hasLoaded && currentMillis - startMillis >= period)
  {
    hasLoaded = true;
    ChangePreset();
  }
}
