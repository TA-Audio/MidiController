#include <ArduinoJson.h>
#include <MIDI.h>
#include <debounce.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>

// Allocate the JSON document
//
// Inside the brackets, 200 is the capacity of the memory pool in bytes.
// Don't forget to change this value to match your JSON document.
// Use arduinojson.org/v6/assistant to compute the capacity.
StaticJsonDocument<450> doc;
LiquidCrystal_I2C lcd(0x27, 20, 4);  // I2C address 0x27, 20 column and 4 rows
const int switch1 = 5;
const int switch2 = 6;
const int switch3 = 7;
const int switch4 = 8;
const int nextPreset = 9;
const int prevPreset = 10;
int currentPreset = 0;
int address = 0;
int readValue = 0;
//Create this using ChatGPT with the following prompt "turn this to a char [] for use with arduino, escape the ""

char json[] = "{\"Channel\": 11, \"Presets\": [ { \"Name\": \"Test1\", \"Switch1\": { \"CC\": [ { \"CC\": 1, \"Value\": 127 }, { \"CC\": 4, \"Value\": 127 } ], \"PC\": null }, \"Switch2\": { \"CC\": [ { \"CC\": 3, \"Value\": 127 }, { \"CC\": 5, \"Value\": 0 } ], \"PC\": null }, \"Switch3\": { \"CC\": null, \"PC\": 2 }, \"Switch4\": { \"CC\": null, \"PC\": 45 } }, { \"Name\": \"Test2\", \"Switch1\": { \"CC\": [ { \"CC\": 1, \"Value\": 127 }, { \"CC\": 4, \"Value\": 127 } ], \"PC\": null }, \"Switch2\": { \"CC\": [ { \"CC\": 3, \"Value\": 127 }, { \"CC\": 5, \"Value\": 0 } ], \"PC\": null }, \"Switch3\": { \"CC\": null, \"PC\": 2 }, \"Switch4\": { \"CC\": null, \"PC\": 45 } } ] }";


static void switch1Handler(uint8_t btnId, uint8_t btnState) {
  if (btnState == BTN_PRESSED) {
    ExecuteSwitchLogic(1);
  }
}

static void switch2Handler(uint8_t btnId, uint8_t btnState) {
  if (btnState == BTN_PRESSED) {
    ExecuteSwitchLogic(2);
  }
}

static void switch3Handler(uint8_t btnId, uint8_t btnState) {
  if (btnState == BTN_PRESSED) {
    ExecuteSwitchLogic(3);
  }
}

static void switch4Handler(uint8_t btnId, uint8_t btnState) {
  if (btnState == BTN_PRESSED) {
    ExecuteSwitchLogic(4);
  }
}

static void nextPresetHandler(uint8_t btnId, uint8_t btnState) {
  if (btnState == BTN_PRESSED) {
    currentPreset++;
    ChangePreset();
  }
}

static void prevPresetHanlder(uint8_t btnId, uint8_t btnState) {
  if (btnState == BTN_PRESSED) {
    currentPreset--;
    ChangePreset();
  }
}

void ExecuteSwitchLogic(int switchNo) {

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

  //PC logic goes here
}



void ChangePreset() {

  // EEPROM.put(address, currentPreset);
  Serial.println("Change preset called");
  Serial.println(currentPreset);

  //JsonVariant preset = presets[currentPreset];
  JsonArray presets = doc["Presets"];
  JsonVariant preset = presets[currentPreset];

  lcd.clear();
  lcd.setCursor(0, 0);                          // move cursor the first row
lcd.print("Preset: " + String(currentPreset)); // print message at the first row
  lcd.setCursor(0, 1);                          // move cursor to the second row
  lcd.print(preset["Name"].as<const char*>());  // print message at the second row
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
static Button switch4Button(4, switch4Handler);
static Button nextPresetButton(4, nextPresetHandler);
static Button prevPresetButton(4, prevPresetHanlder);

void setup() {

  pinMode(switch1, INPUT);
  pinMode(switch2, INPUT);
  pinMode(switch3, INPUT);
  pinMode(switch4, INPUT);
  pinMode(nextPreset, INPUT);
  pinMode(prevPreset, INPUT);


  // Initialize serial port
  Serial.begin(9600);
  while (!Serial) continue;

  DeserializationError error = deserializeJson(doc, json);

  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  lcd.init();  // initialize the lcd
  lcd.backlight();


  //currentPreset = EEPROM.get(address, readValue);
  BootLCD();

  ChangePreset();
}

void BootLCD() {

  lcd.setCursor(0, 0);           // move cursor the first row
  lcd.print("TA Audio");         // print message at the first row
  lcd.setCursor(0, 1);           // move cursor to the second row
  lcd.print("MIDI Controller");  // print message at the second row
  lcd.setCursor(0, 2);           // move cursor to the third row
  lcd.print("v0.0.1");           // print message at the third row
  delay(4000);
  // lcd.setCursor(0, 3);             // move cursor to the fourth row
  // lcd.print("www.diyables.io");    // print message the fourth row
}

static void pollButtons() {
  // update() will call buttonHandler() if PIN transitions to a new state and stays there
  // for multiple reads over 25+ ms.
  switch1Button.update(digitalRead(switch1));
  switch2Button.update(digitalRead(switch2));
  switch3Button.update(digitalRead(switch3));
  switch4Button.update(digitalRead(switch4));
  nextPresetButton.update(digitalRead(nextPreset));
  prevPresetButton.update(digitalRead(prevPreset));
}

void loop() {
  //pollButtons();
}
