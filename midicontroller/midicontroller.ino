
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
const unsigned long switchDisplayPeriod = 1500;
bool resetPresetDisplay = false;
bool switchOneToggled = false;
bool switchTwoToggled = false;
bool switchThreeToggled = false;

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
      if (currentPreset + 1 >= numPrograms - 1)
      {
        return;
      }
      else
      {
        currentPreset++;
      }

      ChangePreset();
    }
    else if (btnId == 5)
    {
      currentPreset--;
      if (currentPreset < 0)
      {
        currentPreset = 0;
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

  const char *tempText = switchLogic["Name"].as<const char *>();
  char text[50];
  bool toggle = switchLogic["Toggle"].as<bool>();

  char *onText = " On!";
  char *offText = " Off!";

  if (toggle)
  {
    switch (switchNo)
    {
    case 1:
      if (!switchOneToggled)
      {
        strcpy(text, tempText);
        strcat(text, onText);
      }
      else
      {
        strcpy(text, tempText);
        strcat(text, offText);
      }
      break;
    case 2:
      if (!switchTwoToggled)
      {
        strcpy(text, tempText);
        strcat(text, onText);
      }
      else
      {
        strcpy(text, tempText);
        strcat(text, offText);
      }
      break;
    case 3:
      if (!switchThreeToggled)
      {
        strcpy(text, tempText);
        strcat(text, onText);
      }
      else
      {
        strcpy(text, tempText);
        strcat(text, offText);
      }
      break;
    }
  }
  else
  {
    strcpy(text, tempText);
  }

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
    startMillis = millis();
    resetPresetDisplay = true;
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

      switch (switchNo)
      {
      case 1:
        if (toggle && switchOneToggled)
        {
          ccValue = 0;
          switchOneToggled = false;
        }
        else if (toggle && !switchOneToggled)
        {
          ccValue = 127;
          switchOneToggled = true;
        }

        break;
      case 2:
        if (toggle && switchTwoToggled)
        {
          ccValue = 0;
          switchTwoToggled = false;
        }
        else if (toggle && !switchTwoToggled)
        {
          ccValue = 127;
          switchTwoToggled = true;
        }

        break;
      case 3:
        if (toggle && switchThreeToggled)
        {
          ccValue = 0;
          switchThreeToggled = false;
        }
        else if (toggle && !switchThreeToggled)
        {
          ccValue = 127;
          switchThreeToggled = true;
        }

        break;
      }

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
}

void ChangePreset()
{
  if (resetPresetDisplay)
  {
    resetPresetDisplay = false;
  }

  if (currentPreset < 0)
  {
    currentPreset = 0;
  }

  switchOneToggled = false;
  switchTwoToggled = false;
  switchThreeToggled = false;

  // Serial.println("Change preset called");
  // Serial.println(currentPreset);

  // if saved currentPreset value is greater than the number of presets, reset to 0
  if (currentPreset + 1 >= numPrograms)
  {
    currentPreset = 0;
    ChangePreset();
  }

  char *fileName = presetList[currentPreset + 1];

  // Serial.println(fileName);

  if (!configFile.open(fileName, O_READ))
  {
    sd.errorHalt("sd error");
  }

  DeserializationError error = deserializeJson(doc, configFile);

  preset = doc;

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

  SetPresetDisplayInfo();
  EEPROM.put(address, currentPreset);
}

void SetPresetDisplayInfo()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(preset["Name"].as<const char *>()); // print message at the second row

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

  char *sw1Indicator = new char[sw1Length + 1](); // Allocate memory and initialize to 0
  char *sw2Indicator = new char[sw2Length + 1](); // Allocate memory and initialize to 0
  char *sw3Indicator = new char[sw3Length + 1](); // Allocate memory and initialize to 0

  lcd.setCursor(0, 2);

  for (int i = 0; i < sw1Length; i++)
  {
    sw1Indicator[i] = (switchOneToggled) ? '*' : ' ';
  }

  for (int i = 0; i < sw2Length; i++)
  {
    sw2Indicator[i] = (switchTwoToggled) ? '*' : ' ';
  }

  for (int i = 0; i < sw3Length; i++)
  {
    sw3Indicator[i] = (switchThreeToggled) ? '*' : ' ';
  }

  lcd.print(sw1Indicator);
  for (int i = 0; i < padding1; i++)
  {
    lcd.print(" ");
  }

  lcd.print(sw2Indicator);

  for (int i = 0; i < padding3; i++)
  {
    lcd.print(" ");
  }
  lcd.print(sw3Indicator);

  lcd.setCursor(0, 3);
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

  // Free allocated memory for indicators
  delete[] sw1Indicator;
  delete[] sw2Indicator;
  delete[] sw3Indicator;
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

  currentPreset = EEPROM.get(address, readValue);

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
  lcd.print("v0.0.2");          // print message the fourth row
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

  if (resetPresetDisplay && currentMillis > (startMillis + switchDisplayPeriod))
  {
    resetPresetDisplay = false;
    SetPresetDisplayInfo();
  }
}
