#include <ArduinoJson.h>
#include <MIDI.h>
#include <debounce.h>
#include <EEPROM.h>

// Allocate the JSON document
//
// Inside the brackets, 200 is the capacity of the memory pool in bytes.
// Don't forget to change this value to match your JSON document.
// Use arduinojson.org/v6/assistant to compute the capacity.
StaticJsonDocument<200> doc;
const int switch1 = 5;
const int switch2 = 6;
const int switch3 = 7;
const int switch4 = 8;
const int nextPreset = 9;
const int prevPreset = 10;
int currentPreset = 0;
int address = 0;
int readValue = 0;
JsonArray presets;

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

  JsonVariant preset = presets[currentPreset];
  JsonObject switchLogic;

  switch (switchNo) {
    case 1:
      switchLogic = preset["Switch1"];
      break;
    case 2:
      switchLogic = preset["Switch2"];
      break;
    case 3:
      switchLogic = preset["Switch3"];
      break;
    case 4:
      switchLogic = preset["Switch4"];
      break;
  }

  JsonArray ccArray = switchLogic["CC"];

  if (!ccArray.isNull()) {
    for (JsonVariant cc : ccArray) {
      int ccNumber = cc["CC"].as<int>();    // Extract integer value
      int ccValue = cc["Value"].as<int>();  // Extract integer value
      Serial.print("CC Number: ");
      Serial.print(ccNumber);
      Serial.print(", CC Value: ");
      Serial.println(ccValue);
    }
  }

  //PC logic goes here
}



void ChangePreset() {
  EEPROM.put(address, currentPreset);
  //update name on display etc
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


  //Create this using ChatGPT with the following prompt "turn this to a char [] for use with arduino, escape the ""

  const char json[] = "{\"Channel\": 11, \"Presets\": [ { \"Name\": \"Test1\", \"Switch1\": { \"CC\": [ { \"CC\": 1, \"Value\": 127 }, { \"CC\": 4, \"Value\": 127 } ], \"PC\": null }, \"Switch2\": { \"CC\": [ { \"CC\": 3, \"Value\": 127 }, { \"CC\": 5, \"Value\": 0 } ], \"PC\": null }, \"Switch3\": { \"CC\": null, \"PC\": 2 }, \"Switch4\": { \"CC\": null, \"PC\": 45 } }, { \"Name\": \"Test2\", \"Switch1\": { \"CC\": [ { \"CC\": 1, \"Value\": 127 }, { \"CC\": 4, \"Value\": 127 } ], \"PC\": null }, \"Switch2\": { \"CC\": [ { \"CC\": 3, \"Value\": 127 }, { \"CC\": 5, \"Value\": 0 } ], \"PC\": null }, \"Switch3\": { \"CC\": null, \"PC\": 2 }, \"Switch4\": { \"CC\": null, \"PC\": 45 } } ] }";


  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, json);
  presets = doc["Presets"];

  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  currentPreset = EEPROM.get(address, readValue);
  ChangePreset();
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
  pollButtons();
}
