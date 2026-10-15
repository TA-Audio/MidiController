# Synapse MIDI Controller

A feature-rich MIDI foot controller built on the **Teensy 4.1** platform. Designed for live performance, it sends MIDI Program Change (PC) and Control Change (CC) messages over DIN-5 and USB MIDI, plays back MIDI files from an SD card, and displays the current state on a 20×4 LCD.

## Features

### Preset System
- Presets are stored as individual `.json` files on a micro-SD card.
- Up to **150 presets** are loaded at boot, sorted numerically by filename (e.g. `1.json`, `2.json`, …).
- Navigate presets with dedicated **Next / Previous** foot switches.
- Navigation happens on release, regardless of how long the switch is held.
- The active preset index is saved to EEPROM so the controller remembers its position across power cycles.
- Presets are prefetched in the direction of navigation for instant loading.
- Invalid preset JSON displays an error; use **Next / Previous** to skip the preset.
- Set top-level `BPM` and `SendMidiClock` fields to send preset MIDI Clock, which continues at the preset tempo during MIDI file playback.

### Three Configurable Foot Switches
Each preset defines up to three foot switch actions (`Switch1`, `Switch2`, `Switch3`):

| Capability | Description |
|---|---|
| **Program Change** | Send one or more PC messages on any MIDI channel, via DIN or USB. |
| **Control Change** | Send one or more CC messages on any MIDI channel, via DIN or USB. |
| **Toggle Mode** | A switch can be configured as a toggle — alternating between on (CC 127) and off (CC 0) with each press. The LCD shows `On!` / `Off!` indicators and a `*` marker on the display row. |

### On-Load Events
Each preset can define `OnLoad` PC and CC messages that are sent automatically when the preset is selected — useful for switching amp channels, enabling default effects, etc.

### On-Exit Events
Each preset can define `OnExit` CC messages that are sent when navigating away to a different preset — useful for disabling effects or resetting equipment. On Exit supports Control Change messages only.

### MIDI File Playback
- Assign a `.mid` file to a preset via the `FileInfo` key.
- Switch 1 becomes **Play / Stop** for that file.
- The selected MIDI file is prepared when its preset is loaded and stays ready between plays, reducing delay when Switch 1 starts playback.
- Optional BPM override and output channel. If `FileInfo.BPM` is omitted or null, playback uses the tempo embedded in the MIDI file.
- Set `FileInfo.SendMidiClock` to `true` to send MIDI Start and Stop over DIN MIDI OUT for file playback, plus 24-PPQN Clock pulses when no preset Clock is active. An active preset Clock takes precedence: its tempo and pulse schedule remain unchanged when the file starts, stops, or reaches EOF. File note playback still uses the file's own tempo or `FileInfo.BPM` override independently of the preset Clock.
- File Start/Stop messages are still sent when `FileInfo.SendMidiClock` is `true`, even with preset Clock active. A receiving device may stop its sequencer on MIDI Stop despite continuing to receive preset Clock pulses.
- Optional `StopCC` array to send CC messages when playback stops (e.g. silence a looper).

### MIDI I/O
| Port | Direction | Purpose |
|---|---|---|
| **Serial1 (DIN MIDI OUT)** | Output | Preset messages, MIDI file events, and optional MIDI clock. |
| **Serial2 (DIN MIDI IN 1)** | Input | Passes through to Serial1 (MIDI merge). |
| **Serial3 (DIN MIDI IN 2)** | Input | Passes through to Serial1 (MIDI merge). |
| **USB Host MIDI** | Output | PC and CC messages when `"USB": true` in the preset. |

### Display
- **20×4 I2C LCD** (address `0x27`).
- Row 0: Preset name.
- Preset names longer than 20 characters scroll left and right across row 0.
- Row 2: Toggle indicators (`*` per switch when toggled on).
- Row 3: Switch labels from the preset JSON.
- Temporary overlay messages (e.g. "DIST On!") display for 1.5 seconds then return to the preset view.

### Boot Screen
Displays "TA Audio / SYNAPSE / MIDI CONTROLLER / v0.2.0" for 2 seconds on power-up.

## Hardware

| Component | Detail |
|---|---|
| MCU | Teensy 4.1 |
| Display | 20×4 I2C LCD (PCF8574, address 0x27) |
| Storage | Micro-SD card (FAT32) |
| Foot switches | 5 momentary switches on pins 2–6 (active-low with internal/external pull-ups) |
| MIDI OUT | DIN-5, Serial1 |
| MIDI IN 1 | DIN-5, Serial2 |
| MIDI IN 2 | DIN-5, Serial3 |
| USB Host | USB MIDI device via USBHost_t36 |

### Pin Assignments

| Pin | Function |
|---|---|
| 2 | Switch 1 |
| 3 | Switch 2 |
| 4 | Switch 3 |
| 5 | Next Preset |
| 6 | Previous Preset |
| 13 | LED (onboard) |
| BUILTIN_SDCARD | SD card |

## Preset JSON Format

Each preset is a `.json` file on the SD card root. Filenames should be numbered (e.g. `1.json`, `2.json`) for sort order.

### Basic Preset

```json
{
  "Name": "My Preset",
  "OnLoad": {
    "PC": [{ "PC": 5, "Channel": 1, "USB": false }],
    "CC": [{ "CC": 1, "Value": 127, "Channel": 1 }]
  },
  "OnExit": {
    "CC": [{ "CC": 1, "Value": 0, "Channel": 1 }]
  },
  "Switch1": {
    "Name": "DRIVE",
    "Toggle": true,
    "CC": [{ "CC": 50, "Value": 127, "Channel": 1 }],
    "PC": null
  },
  "Switch2": {
    "Name": "DELAY",
    "Toggle": false,
    "CC": null,
    "PC": [{ "PC": 10, "Channel": 1, "USB": true }]
  },
  "Switch3": {
    "Name": "REVERB",
    "Toggle": true,
    "CC": [{ "CC": 51, "Value": 127, "Channel": 1 }],
    "PC": null
  }
}
```

### MIDI File Preset

When `FileInfo` is present, Switch 1 becomes a play/stop control for the specified MIDI file:

```json
{
  "Name": "Backing Track",
  "FileInfo": {
    "FileName": "song.mid",
    "BPM": 120,
    "Channel": 10,
    "SendMidiClock": true,
    "StopCC": [{ "CC": 4, "Value": 0, "Channel": 11 }]
  },
  "OnLoad": {
    "PC": [{ "PC": 98, "Channel": 11 }],
    "CC": null
  },
  "Switch1": { "Name": "", "Toggle": false, "CC": null, "PC": null },
  "Switch2": { "Name": "", "Toggle": false, "CC": null, "PC": null },
  "Switch3": { "Name": "", "Toggle": false, "CC": null, "PC": null }
}
```

### Field Reference

| Field | Type | Description |
|---|---|---|
| `Name` | string | Display name shown on the LCD. |
| `BPM` | int | Tempo for preset MIDI Clock (1–65535); required when `SendMidiClock` is `true`. |
| `SendMidiClock` | bool | If `true`, sends preset MIDI Start and 24-PPQN Clock over DIN MIDI OUT. Preset Clock continues at `BPM` during file playback and takes precedence over file Clock. Defaults to `false`. |
| `OnLoad.PC[]` | array | Program Change messages sent when the preset loads. |
| `OnLoad.CC[]` | array | Control Change messages sent when the preset loads. |
| `OnExit.CC[]` | array | Control Change messages sent when navigating away from the preset. |
| `Switch1/2/3.Name` | string | Label shown on the LCD for this switch. |
| `Switch1/2/3.Toggle` | bool | If `true`, the switch alternates between on/off states. |
| `Switch1/2/3.CC[]` | array | CC messages sent on press. In toggle mode, value is overridden to 127 (on) or 0 (off). |
| `Switch1/2/3.PC[]` | array | PC messages sent on press. `PC` value is 1-indexed (internally decremented by 1). |
| `FileInfo.FileName` | string | Name of the `.mid` file on the SD card. |
| `FileInfo.BPM` | int | Optional playback tempo override. If omitted or `null`, the tempo embedded in the MIDI file is used. |
| `FileInfo.Channel` | int | MIDI output channel for file playback. |
| `FileInfo.SendMidiClock` | bool | Optional. If `true`, sends MIDI Start/Stop for file playback and 24-PPQN Clock at the file playback tempo only when no preset Clock is active. Does not interrupt an active preset Clock. Defaults to `false`. |
| `FileInfo.StopCC[]` | array | CC messages sent when playback stops. |
| `*.USB` | bool | If `true`, the message is sent over USB Host MIDI instead of DIN. |
| `*.Channel` | int | MIDI channel (1–16). |

## Building

### Prerequisites

- [Arduino CLI](https://arduino.github.io/arduino-cli/) or the Arduino IDE with Teensy support.
- Teensy 4.1 board package from [PJRC](https://www.pjrc.com/teensy/td_download.html).

### Libraries

| Library | Source |
|---|---|
| ArduinoJson 6.21.5 | Arduino Library Manager |
| MIDI Library | Arduino Library Manager |
| LiquidCrystal I2C | Arduino Library Manager |
| MD_MIDIFile | Arduino Library Manager (requires Teensy SdFat patch) |
| button-debounce | [GitHub](https://github.com/kimballa/button-debounce) (git install) |

### Quick Start (Local)

Run the included setup script to install all tooling, libraries, compile, and optionally upload:

```powershell
.\setup-local.ps1            # install prerequisites and compile
.\setup-local.ps1 -Upload    # compile and upload to connected Teensy
```

See [setup-local.ps1](setup-local.ps1) for details.

### Manual Build

```bash
arduino-cli compile --fqbn teensy:avr:teensy41 midicontroller/midicontroller.ino
arduino-cli upload  --fqbn teensy:avr:teensy41 -p <PORT> midicontroller/midicontroller.ino
```

## Preset Editor (Web)

A browser-based preset editor lives in [`preset-editor/`](preset-editor) and is published to GitHub Pages at **https://ta-audio.github.io/MidiController/**.

- Click **Open preset folder** and choose the folder holding your preset `.json` files (e.g. the SD card root).
- Edit every preset field with the form (name, BPM / MIDI Clock, MIDI file playback, On Load and On Exit messages, and all three switches), or edit the raw JSON.
- An LCD preview and validation warnings show how the preset will look and flag out-of-range values.
- Add, duplicate, rename, or delete presets, then **Save to folder** (or `Ctrl+S`) to write the changes back.
- Direct folder saving requires a Chromium-based browser (Chrome / Edge). Other browsers can load presets and download the result as a zip.

Run it locally:

```bash
cd preset-editor
npm install
npm run dev
```

## CI

A GitHub Actions workflow (`.github/workflows/compile.yml`) compiles the firmware on every push and pull request. It automatically patches the MD_MIDIFile library for Teensy compatibility.

The `.github/workflows/pages.yml` workflow builds the preset editor and deploys it to GitHub Pages whenever `preset-editor/` changes on `main`. In the repository settings, set **Pages → Source** to **GitHub Actions**.

## License

See [LICENSE](LICENSE).
