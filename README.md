# Synapse MIDI Controller

A feature-rich MIDI foot controller built on the **Teensy 4.1** platform. Designed for live performance, it sends MIDI Program Change (PC) and Control Change (CC) messages over DIN-5 and USB MIDI, plays back MIDI files from an SD card, and displays the current state on a 20×4 LCD.

## Features

### Preset System
- Presets are stored as individual `.json` files on a micro-SD card.
- Up to **150 presets** are loaded at boot, sorted numerically by filename (e.g. `1.json`, `2.json`, …).
- Navigate presets with dedicated **Next / Previous** foot switches. Previous from the first preset wraps to the last, and Next from the last wraps to the first.
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

The current preset's toggle states are saved to EEPROM immediately when a toggle changes. On power-up, the controller restores those states **after** the normal OnLoad messages. Legacy toggles send their matching CC 127/0 messages without replaying switch PC messages. Explicit On/Off toggles replay the configured PC and CC list for their saved state, including custom values and USB destinations. MIDI file playback is never resumed. Changing presets resets all toggles; returning to a preset does not restore its previous toggles.

Saved toggle states are used only if the preset filename and configuration still match. Older firmware's saved preset position remains compatible, with toggles initially Off. Immediate saving removes the deliberate save delay, but power loss during an EEPROM write can still prevent restoration.

### Performance Actions

These features are opt-in: existing presets still send their primary action immediately on press.

- **Explicit On/Off**: set `Toggle: true` and supply `On` and `Off` action objects. Each state has its own label, PC list and CC list; CC values are used exactly as configured. These replace the switch's legacy PC/CC lists.
- **Sequences**: supply a `Sequence` array of 1-16 action objects. Each primary activation sends one step, then advances and wraps. The LCD shows the last step's label after activation. Changing presets resets the sequence to step 1; position is not persisted.
- **Tap tempo**: set `TapTempo: true`. Two taps establish tempo; subsequent taps average the last three intervals (30-300 BPM). Taps closer than 200 ms are ignored; a gap longer than two seconds starts a new measurement. Tempo is temporary and resets when changing presets. Tapping updates the active DIN MIDI Clock without restarting transport, or starts preset Clock if none is running. When file Clock owns the tempo, tapping also changes file playback tempo; when preset Clock owns it, file note tempo remains independent.
- **Gestures**: optional `Gestures.Press`, `Release`, `LongPress`, and `DoubleTap` action objects. Press/Release run on their respective edges, useful for momentary effects. With gestures enabled, the primary action runs on release, unless replaced by a 600 ms long press or a double tap. Enabling DoubleTap delays a single primary activation by 300 ms after release. Release still runs after a long/double action and when navigating away while held. Leave primary lists empty for a momentary-only switch.

Toggle, Sequence and TapTempo are mutually exclusive primary modes; gestures can accompany any mode. Switch 1 remains Play/Stop when a MIDI file is assigned, ignoring its performance actions. Action labels fall back to the switch label when empty.

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
- While a file is playing, a `*` in the LCD's top-right corner lights for at least 100 ms when transmitted file MIDI events are displayed. Events remain pending until the LCD update, so slow SD reads or display work cannot consume the pulse before it appears. It stays lit during dense activity and clears during gaps or when playback stops. MIDI Clock, transport, preset messages, and passthrough traffic do not trigger it.

### MIDI I/O
| Port | Direction | Purpose |
|---|---|---|
| **Serial1 (DIN MIDI OUT / RX thru)** | Output + input | Preset messages, MIDI file events, and optional MIDI clock; received MIDI passes through to Serial1 output. |
| **Serial2 (DIN MIDI IN 1)** | Input | Passes through to Serial1 (MIDI merge). |
| **Serial3 (DIN MIDI IN 2)** | Input | Passes through to Serial1 (MIDI merge). |
| **USB Host MIDI** | Output | PC and CC messages when `"USB": true` in the preset. |
| **USB Computer MIDI** | Bidirectional | PC/CC to a computer/DAW when `"USBDevice": true`; SysEx for live editing. Requires a USB MIDI firmware build. |

Use only one explicit USB destination per message. Neither flag means DIN output, automatically mirrored to the computer USB MIDI port whenever it is enumerated by a host. This mirror includes preset/footswitch PC/CC, MIDI-file channel events, clock/transport, and incoming DIN passthrough (including SysEx). DIN output continues when USB is disconnected; USB mirroring requires a USB MIDI firmware build. `"USB": true` remains USB Host-only, and `"USBDevice": true` remains computer-only without a second mirrored copy. USB Computer does not merge incoming DAW notes or implement an incoming remote-control mapping; use your DAW's MIDI mapping for outgoing PC/CC. Avoid routing the mirrored output back into a physical MIDI input, which can create a MIDI feedback loop.

### Display
- **20×4 I2C LCD** (address `0x27`).
- Row 0: Preset name.
- Preset names longer than 20 characters scroll left and right across row 0. During MIDI file playback, the name uses 19 columns so the activity indicator stays visible, including during temporary playback/switch messages.
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
| `Switch1/2/3.On`, `.Off` | object | Explicit toggle state actions: optional `Name`, `PC[]`, `CC[]`. Both required with `Toggle: true`. |
| `Switch1/2/3.Sequence[]` | array | 1-16 actions, one per primary activation, wrapping to the first. |
| `Switch1/2/3.TapTempo` | bool | Make the primary action a tap-tempo control. |
| `Switch1/2/3.Gestures` | object | Optional `Press`, `Release`, `LongPress`, `DoubleTap` actions with `Name`, `PC[]`, `CC[]`. |
| `FileInfo.FileName` | string | Name of the `.mid` file on the SD card. |
| `FileInfo.BPM` | int | Optional playback tempo override. If omitted or `null`, the tempo embedded in the MIDI file is used. |
| `FileInfo.Channel` | int | MIDI output channel for file playback. |
| `FileInfo.SendMidiClock` | bool | Optional. If `true`, sends MIDI Start/Stop for file playback and 24-PPQN Clock at the file playback tempo only when no preset Clock is active. Does not interrupt an active preset Clock. Defaults to `false`. |
| `FileInfo.StopCC[]` | array | CC messages sent when playback stops. |
| `*.USB` | bool | If `true`, the message is sent over USB Host MIDI instead of DIN. |
| `*.USBDevice` | bool | If `true`, sends to the computer over device USB MIDI. Cannot be combined with `USB: true`. |
| `*.Channel` | int | MIDI channel (1–16). |

For example, this switch sends a computer CC while held, switches to a different DIN program on a long press, and has no primary messages:

```json
{
  "Name": "Momentary and hold",
  "Switch1": {
    "Name": "HOLD",
    "Toggle": false,
    "Gestures": {
      "Press": { "CC": [{ "CC": 64, "Value": 127, "Channel": 1, "USBDevice": true }] },
      "Release": { "CC": [{ "CC": 64, "Value": 0, "Channel": 1, "USBDevice": true }] },
      "LongPress": { "Name": "LEAD", "PC": [{ "PC": 9, "Channel": 2 }] }
    }
  }
}
```

Preset documents now have an 8192-byte ArduinoJson memory pool. Live transfers additionally limit serialized JSON to 8192 UTF-8 bytes; heavily nested presets can exhaust the JSON pool before that byte limit and are rejected rather than truncated.

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
arduino-cli compile --fqbn teensy:avr:teensy41:usb=serialmidi midicontroller/midicontroller.ino
arduino-cli upload  --fqbn teensy:avr:teensy41:usb=serialmidi -p <PORT> midicontroller/midicontroller.ino
```

The setup script defaults to **Serial + MIDI** (`usb=serialmidi`). In the Arduino IDE, choose **Tools > USB Type > Serial + MIDI**. Serial-only builds retain DIN/USB Host features but cannot use live editing or USB Computer messages; attempting to send a USB Computer message displays an error.

## Preset Editor (Web)

A browser-based preset editor lives in [`preset-editor/`](preset-editor) and is published to GitHub Pages at **https://ta-audio.github.io/MidiController/**.

- Click **Open preset folder** and choose the folder holding your preset `.json` files (e.g. the SD card root).
- Edit every preset field with the form (name, BPM / MIDI Clock, MIDI file playback, On Load and On Exit messages, and all three switches), or edit the raw JSON.
- An LCD preview and validation warnings show how the preset will look and flag out-of-range values.
- Add, duplicate, change display names, or delete presets, then **Save to folder** (or `Ctrl+S`) to write the changes back.
- Drag presets in the sidebar to change their order, or select a preset and use **Move up / Move down** (also works with keyboard and touch). Clear the filter before reordering.
- Every save numbers preset files consecutively as `1.json`, `2.json`, `3.json`, etc., in sidebar order, regardless of their original filenames. Display names are preserved. New and duplicate presets append to the list; deleting a preset closes numbering gaps.
- Direct saves write the numbered presets before removing obsolete preset files. MIDI files are left untouched. A failed save can be partial; keep the editor open and retry saving before reloading.
- Direct folder saving requires a Chromium-based browser (Chrome / Edge). Other browsers can load presets and download the result as a zip.
- ZIP downloads use the same numbered filenames. Before extracting, remove the obsolete preset filenames listed by the editor so old presets are not loaded alongside the new numbered ones; keep your MIDI files.
- On case-insensitive SD cards, an existing filename's capitalization may be preserved; the firmware accepts `.json` extensions in any letter case.

Run it locally:

```bash
cd preset-editor
npm install
npm run dev
```

Run the editor's ordering, folder/ZIP, performance schema and live MIDI transport tests with `npm test` (Node.js 22.6 or newer), and check the production build with `npm run build`.

### Live USB Editing

Connect the Teensy's **device USB port** to your computer with Serial + MIDI firmware, then expand **Live USB editor and DAW connection**. In Chrome/Edge over HTTPS or localhost, enable MIDI access (including SysEx), choose the matching Synapse input/output ports, and Connect. Stop MIDI file playback before loading or saving.

The website remembers the input/output ports in this browser and automatically connects when the remembered controller appears. It first matches saved port IDs, then a unique name/manufacturer if the browser changes the IDs. With no saved selection, a single Teensy/Synapse input/output pair can be detected automatically; ambiguous devices require manual selection. Once SysEx permission is granted, future visits restore MIDI access without prompting. The first visit, revoked permission, or a browser without permission-query support still requires **Enable USB MIDI access**. Unplugging/replugging reconnects without discarding unsaved edits. **Disconnect** pauses automatic connection for the current page; the automatic-connection checkbox can re-enable it. Device preferences are local to the browser/site, not stored on the controller.

Connecting loads the complete preset set, including presets with invalid JSON so they can be repaired or deleted. Use **New preset**, **Duplicate preset**, **Delete preset**, drag-and-drop, or **Move up/down** to manage up to 150 presets without removing the SD card. Reordering is disabled while filtering. Changes stay local until **Save all to controller**, which writes `1.json`, `2.json`, etc. in list order and removes obsolete preset files while preserving preset display names and all MIDI files. Deleting all presets requires an additional confirmation; an empty controller can be populated over USB too. Invalid presets must be fixed or removed before saving. Keep the controller connected until saving finishes.

Long SD operations send USB progress packets so an active save does not time out just because the card is slow. After a failed or interrupted save, **Reload controller set** retries SD recovery before reading the files; a successfully recovered controller does not need a reset. Reloading asks before discarding unsaved edits. A timeout identifies the USB command that stalled; an interrupted commit may already have completed, so reload before saving again. Update both the firmware and website to use progress-aware timeouts.

Save errors also identify the upload/staging/commit phase, preset filename, and upload byte offset when applicable. Serial1, Serial2, and Serial3 inputs pass through to Serial1 output and are mirrored to USB when enumerated. Library soft-thru is disabled so each received message is forwarded only once. Passthrough is processed in bounded batches so continuous MIDI traffic cannot monopolize the main loop.

Save does not activate a preset or send OnLoad/OnExit messages. The running preset, its toggle/sequence states and MIDI Clock remain in memory even if its file was edited or deleted. If the active file survives reordering, navigation follows its new position; otherwise the old position is clamped to the new list. Saved toggle restoration is invalidated until a saved preset is explicitly loaded, avoiding applying the old rig's state to a different file. **Activate saved preset** loads the selected saved preset and sends OnExit/OnLoad normally. Physical Next/Previous also load from the refreshed list. **Test switch 1/2/3** sends the selected saved preset's primary action; use physical switches for gestures. Tests require explicit activation after a set save, and firmware rejects tests if physical navigation changed the preset. Test Switch 1 can stop a file it started.

Unsaved edits remain visible after disconnection. Reconnecting clears abandoned uploads but preserves local changes. Whole-set revisions are checked before/after loading and before starting/committing saves, rejecting stale content, additions, deletions and filename changes. After an interrupted or failed save, editing is gated until **Reload controller set** establishes the actual disk contents; discarding local edits requires confirmation. Presets exceeding the 8192 UTF-8 byte limit or SD sets outside the firmware's filename/count limits must be corrected using folder editing first.

Full-set saves validate and stage every preset before touching existing files, verify staged bytes against their upload hashes, then sync a checksummed manifest, move originals to backups, publish an installation marker, and install numbered files. Boot recovery in both serial-only and MIDI builds rolls back a partially backed-up set, or finishes installing the staged set after the marker was published. Backups are removed only after every replacement is installed and its hash verified. A recovery failure preserves the journal/backups and blocks further SD-dependent switch operations until restart/recovery; boot halts with an error for an unrecoverable journal. Keep an SD backup: FAT/SD hardware cannot guarantee atomic writes across power loss.

Reserved save filenames are `syn001.tmp` through `syn150.tmp`, `syb001.bak` through `syb150.bak`, `synset.tmp`, `synset.txn`, and `synset.go`. Do not use these for other files. The older single-file protocol remains supported, reserving `synapse.tmp`, `synapse.bak`, and `synapse.txn`.

The version-1 protocol uses noncommercial SysEx ID `7D`, magic `53 59`, and nibble-encoded payloads (96 raw bytes/packet, preserving UTF-8). The body is `7D 53 59 01 ID CMD SEQ_H SEQ_L LAST PAYLOAD`; Web MIDI adds `F0`/`F7`. Commands are hello (1), list (2), read (3), begin upload (4), data (5), finish upload (6), activate (7), test (8), abort (9), begin set (10), commit set (11), and set revision (12); responses set command bit `40`, errors use `7F`. Hello advertises `presetSets: true`; full management requires firmware with this capability. Begin set takes `{count, revision}`; during a set upload, begin upload takes `{index, size, source}` instead of single-file `{name, size, revision}`, where `source` is the original filename or empty for a new/duplicated preset. Upload every numbered slot in sequence and finish each with command 6, then commit with command 11. Abort discards staging before installation or recovers an already journaled transaction. Requests are acknowledged individually, with sequenced responses/uploads, bounded buffers, 5-second chunk and 30-second set inactivity expiry. Set revisions use FNV-1a over each sorted original filename, a zero byte, exact file bytes, and another zero byte. Only one editor should be connected at a time; this protocol is not an authenticated control channel.

During long SD operations, empty response packets with `LAST=0` report progress at storage checkpoints (at most once per 250 ms). They share the request ID/response command and consume response sequence numbers just like payload packets. Concatenate payloads until the final packet; progress alone is not success. Each valid, in-order response restarts the browser's inactivity timeout (8 seconds, or 60 seconds for commit); a stalled stream still fails.

## CI

A GitHub Actions workflow (`.github/workflows/compile.yml`) compiles serial-only and Serial + MIDI firmware variants on pushes to main/master and pull requests, and runs native logic/performance/protocol tests. It automatically patches the MD_MIDIFile library for Teensy compatibility.

The `.github/workflows/pages.yml` workflow builds the preset editor and deploys it to GitHub Pages whenever `preset-editor/` changes on `main`. In the repository settings, set **Pages → Source** to **GitHub Actions**.

## License

See [LICENSE](LICENSE).
