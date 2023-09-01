#include <ArduinoJson.h>
#include <MIDI.h>
#include <debounce.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include "SdFat.h"

// Allocate the JSON document
DynamicJsonDocument doc(2056);
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

  JsonArray presets = doc["Presets"];

  JsonVariant preset = presets[currentPreset];

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

  JsonVariant switchPC = switchLogic["PC"];

  if (!switchPC.isNull())
  {
    int pc = switchPC["PC"];
    int channel = switchPC["Channel"];
    Serial.print("PC: ");
    Serial.println(pc);
    Serial.print("Channel: ");
    Serial.println(channel);

    MIDI.sendProgramChange(pc, channel);
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

  EEPROM.put(address, currentPreset);
  Serial.println("Change preset called");
  Serial.println(currentPreset);

  JsonArray presets = doc["Presets"];

  JsonVariant preset = presets[currentPreset];

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

  JsonVariant onLoadPC = preset["OnLoad"]["PC"];
  JsonArray onLoadCC = preset["OnLoad"]["CC"];

  if (!onLoadPC.isNull())
  {
    int pc = onLoadPC["PC"];
    int channel = onLoadPC["Channel"];
    Serial.print("PC: ");
    Serial.println(pc);
    Serial.print("Channel: ");
    Serial.println(channel);

    MIDI.sendProgramChange(pc, channel);
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

  // serializeJsonPretty(doc, Serial);
  // Serial.println(doc.memoryUsage());

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
  // doc = DynamicJsonDocument(fileSize + 1);

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
  pollButtons();
}
