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


LiquidCrystal_I2C lcd(0x27, 20, 4);  // I2C address 0x27, 20 column and 4 rows



MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI1);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, MIDI2);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial3, MIDI3);

static void switchHandler(uint8_t btnId, uint8_t btnState) {
}

static Button switch1Button(1, switchHandler);
static Button switch2Button(2, switchHandler);
static Button switch3Button(3, switchHandler);
static Button nextPresetButton(4, switchHandler);
static Button prevPresetButton(5, switchHandler);

void setup() {

  lcd.init();  // initialize the lcd
  lcd.backlight();

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
  if (!SD.begin(SDCARD_CS_PIN)) {
    // DEBUGS("\nSD init fail!");
    while (true)
      ;
  }
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

  pollButtons();


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
}
