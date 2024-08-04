#include <ArduinoJson.h>
#include <MIDI.h>
#include <debounce.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include <SD.h>

static constexpr int switch1 = 2;
static constexpr int switch2 = 3;
static constexpr int switch3 = 4;
static constexpr int nextPreset = 5;
static constexpr int prevPreset = 6;
const int SDCARD_CS_PIN = 10;
const int SDCARD_MOSI_PIN = 11;
const int SDCARD_MISO_PIN = 12;
const int SDCARD_SCK_PIN = 13;
bool hasLoaded = false;
bool switchOneToggled = false;
bool switchTwoToggled = false;
bool switchThreeToggled = false;
unsigned long startMillis;
unsigned long currentMillis;
const unsigned long period = 1000;
const unsigned long switchDisplayPeriod = 1500;
bool resetPresetDisplay = false;
LiquidCrystal_I2C lcd(0x27, 20, 4);  // I2C address 0x27, 20 column and 4 rows

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI1);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, MIDI2);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial3, MIDI3);

void ChangePreset() {
}

void SetPresetDisplayInfo() {
  lcd.clear();
  lcd.setCursor(0, 0);
}

static void switchHandler(uint8_t btnId, uint8_t btnState) {
}

void BootLCD() {

  lcd.setCursor(0, 0);           // move cursor the first row
  lcd.print("TA Audio");         // print message at the first row
  lcd.setCursor(0, 1);           // move cursor to the second row
  lcd.print("SYNAPSE");          // print message at the second row
  lcd.setCursor(0, 2);           // move cursor to the third row
  lcd.print("MIDI CONTROLLER");  // print message at the third row
  lcd.setCursor(0, 3);           // move cursor to the fourth row
  lcd.print("v0.1.0");           // print message the fourth row

  //Change to use millis to setup can continue whilst lcb boot seq is shown
  delay(4000);
}

static Button switch1Button(1, switchHandler);
static Button switch2Button(2, switchHandler);
static Button switch3Button(3, switchHandler);
static Button nextPresetButton(4, switchHandler);
static Button prevPresetButton(5, switchHandler);

void setup() {

  lcd.init();  // initialize the lcd
  lcd.backlight();
  BootLCD();

  // initialize the digital pin as an output.
  pinMode(13, OUTPUT);
  pinMode(switch1, INPUT);
  pinMode(switch2, INPUT);
  pinMode(switch3, INPUT);
  pinMode(nextPreset, INPUT);
  pinMode(prevPreset, INPUT);

  MIDI1.begin(MIDI_CHANNEL_OMNI);

  MIDI2.begin(MIDI_CHANNEL_OMNI);
  MIDI2.turnThruOff();
  MIDI3.begin(MIDI_CHANNEL_OMNI);
  MIDI3.turnThruOff();

  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);
  SPI.setMISO(SDCARD_MISO_PIN);

  // Initialize SD
  // if (!SD.begin(SDCARD_CS_PIN)) {
  //   // DEBUGS("\nSD init fail!");
  //   while (true)
  //     ;
  // }
}

static void pollButtons() {
  // update() will call buttonHandler() if PIN transitions to a new state and stays there
  // for multiple reads over 25+ ms.
  switch1Button.update(digitalRead(switch1));
  switch2Button.update(digitalRead(switch2));
  switch3Button.update(digitalRead(switch3));
  nextPresetButton.update(digitalRead(nextPreset));
  prevPresetButton.update(digitalRead(prevPreset));
}

void loop() {

  currentMillis = millis();

  digitalWrite(13, HIGH);  // set the LED on
  delay(1000);             // wait for a second
  digitalWrite(13, LOW);   // set the LED off
  delay(1000);             // wait for a second

  MIDI1.read();

  if (MIDI2.read()) {
    MIDI1.send(MIDI2.getType(),
               MIDI2.getData1(),
               MIDI2.getData2(),
               MIDI2.getChannel());
  }

  if (MIDI3.read()) {
    MIDI1.send(MIDI3.getType(),
               MIDI3.getData1(),
               MIDI3.getData2(),
               MIDI3.getChannel());
  }

  pollButtons();
  if (!hasLoaded && currentMillis - startMillis >= period) {
    hasLoaded = true;
    ChangePreset();
  }

  if (resetPresetDisplay && currentMillis > (startMillis + switchDisplayPeriod)) {
    resetPresetDisplay = false;
    SetPresetDisplayInfo();
  }
}
