#include <ArduinoJson.h>
#include <MIDI.h>
#include <debounce.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include "SdFat.h"

// Allocate the JSON document
DynamicJsonDocument doc(128);
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
char *json;

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

  // JsonVariant preset = presets[currentPreset];
  // JsonObject switchLogic;

  // switch (switchNo) {
  //   case 1:
  //     switchLogic = preset["Switch1"];
  //     break;
  //   case 2:
  //     switchLogic = preset["Switch2"];
  //     break;
  //   case 3:
  //     switchLogic = preset["Switch3"];
  //     break;
  //   case 4:
  //     switchLogic = preset["Switch4"];
  //     break;
  // }

  // JsonArray ccArray = switchLogic["CC"];

  // if (!ccArray.isNull()) {
  //   for (JsonVariant cc : ccArray) {
  //     int ccNumber = cc["CC"].as<int>();    // Extract integer value
  //     int ccValue = cc["Value"].as<int>();  // Extract integer value
  //     Serial.print("CC Number: ");
  //     Serial.print(ccNumber);
  //     Serial.print(", CC Value: ");
  //     Serial.println(ccValue);
  //   }
  // }

  // PC logic goes here
}

void ChangePreset()
{

  EEPROM.put(address, currentPreset);
  Serial.println("Change preset called");
  Serial.println(currentPreset);

  JsonArray presets = doc["Presets"];
  JsonVariant preset = presets[currentPreset];

  lcd.clear();
  lcd.setCursor(0, 0);                           // move cursor the first row
  lcd.print("Preset: " + String(currentPreset)); // print message at the first row
  lcd.setCursor(0, 1);                           // move cursor to the second row
  lcd.print(preset["Name"].as<const char *>());  // print message at the second row
  lcd.setCursor(0, 3);
  lcd.print("zzzzz  zzzzz  zzzzz");
  // create text with value sw1, pad to 5 characters with white space at front and back

  const char *sw1 = "sw1";
  // get lenth of sw1
  int sw1Length = strlen(sw1);
  if (sw1Length < 5)
  {
    // add whitespace at front and back of sw1 to make 5 len
  }

  // lcd.setCursor(0, 2);                    // move cursor to the third row
  // lcd.print("v0.0.1");                    // print message at the third row

  // for (JsonVariant preset : presets) {
  //   for (JsonVariant preset : presets) {
  //     Serial.println(preset["Name"].as<const char*>());
  //   }

  //   // lcd.clear();
  //   // lcd.setCursor(0, 0);                    // move cursor the first row
  //   // lcd.print("Preset: " + currentPreset);  // print message at the first row
  //   // lcd.setCursor(0, 1);                    // move cursor to the second row
  //   // lcd.print(presetName);           // print message at the second row
  //   // lcd.setCursor(0, 2);                    // move cursor to the third row
  //   // lcd.print("v0.0.1");                    // print message at the third row

  //   //update name on display etc
  // }
  // JsonArray presets = doc["Presets"];

  // for (JsonVariant preset : presets) {
  //   JsonObject switch1 = preset["Switch1"];
  //   JsonArray ccArray = switch1["CC"];
  //   Serial.println(preset["Name"].as<String>());
  //   Serial.println("Switch1 CC Objects:");

  //   for (JsonVariant cc : ccArray) {
  //     int ccNumber = cc["CC"];
  //     int ccValue = cc["Value"];
  //     Serial.print("CC Number: ");
  //     Serial.print(ccNumber);
  //     Serial.print(", CC Value: ");
  //     Serial.println(ccValue);
  //   }
  // }
}

static Button switch1Button(1, switch1Handler);
static Button switch2Button(2, switch2Handler);
static Button switch3Button(3, switch3Handler);
static Button nextPresetButton(4, nextPresetHandler);
static Button prevPresetButton(4, prevPresetHanlder);

void setup()
{

  pinMode(switch1, INPUT);
  pinMode(switch2, INPUT);
  pinMode(switch3, INPUT);
  pinMode(nextPreset, INPUT);
  pinMode(prevPreset, INPUT);

  // Initialize serial port
  Serial.begin(9600);
  while (!Serial)
    continue;

  if (!sd.begin(chipSelect, SPI_HALF_SPEED))
    sd.initErrorHalt();
  if (!configFile.open("exampleConfig.json", O_READ))
  {
    sd.errorHalt("sd error");
  }

  ReadConfigFile();

  DeserializationError error = deserializeJson(doc, json);

  // Test if parsing succeeds.
  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  lcd.init(); // initialize the lcd
  lcd.backlight();

  BootLCD();
  currentPreset = EEPROM.get(address, readValue);
  if (currentPreset == -1)
  {
    currentPreset = 0;
  }

  ChangePreset();
}

void ReadConfigFile()
{
  uint32_t fileSize = configFile.size();

  // Allocate memory dynamically based on file size
  json = new char[fileSize + 1];
  doc = DynamicJsonDocument(fileSize + 1);

  // Read the entire file into the allocated buffer
  uint32_t bytesRead = configFile.read((uint8_t *)json, fileSize);

  if (bytesRead > 0)
  {
    json[bytesRead] = '\0'; // Null-terminate the buffer
    // Serial.print(json);
  }
  else
  {
    // End of file or error
    configFile.close();
    delete[] json; // Free the dynamically allocated memory
    while (true)
      ; // Halt execution
  }
  // delete[] fileContents;
  configFile.close();
}

void BootLCD()
{

  lcd.setCursor(0, 0);          // move cursor the first row
  lcd.print("TA Audio");        // print message at the first row
  lcd.setCursor(0, 1);          // move cursor to the second row
  lcd.print("MIDI Controller"); // print message at the second row
  lcd.setCursor(0, 2);          // move cursor to the third row
  lcd.print("v0.0.1");          // print message at the third row
  delay(4000);
  // lcd.setCursor(0, 3);             // move cursor to the fourth row
  // lcd.print("www.diyables.io");    // print message the fourth row
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
  pollButtons();
}
