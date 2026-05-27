<#
.SYNOPSIS
    Installs all prerequisites for building the Synapse MIDI Controller firmware
    and optionally uploads it to a connected Teensy 4.1.

.DESCRIPTION
    This script:
      1. Downloads and installs arduino-cli (if not already on PATH).
      2. Installs the Teensy board package.
      3. Installs all required Arduino libraries.
      4. Patches the MD_MIDIFile library for Teensy/SdFat compatibility.
      5. Compiles the firmware.
      6. Optionally uploads to a connected Teensy 4.1.

.PARAMETER Upload
    If specified, uploads the compiled firmware to the Teensy after building.

.PARAMETER Port
    Serial port for upload (e.g. COM3). If omitted, arduino-cli will attempt
    auto-detection.

.EXAMPLE
    .\setup-local.ps1
    .\setup-local.ps1 -Upload
    .\setup-local.ps1 -Upload -Port COM5
#>
[CmdletBinding()]
param(
    [switch]$Upload,
    [string]$Port
)

$ErrorActionPreference = 'Stop'

$fqbn = 'teensy:avr:teensy41'
$sketchPath = Join-Path $PSScriptRoot 'midicontroller' 'midicontroller.ino'
$boardManagerUrl = 'https://www.pjrc.com/teensy/package_teensy_index.json'

# ── Helper ──────────────────────────────────────────────────────────────────────

function Write-Step {
    param([string]$Message)
    Write-Host "`n▶ $Message" -ForegroundColor Cyan
}

# ── 1. Ensure arduino-cli is installed ──────────────────────────────────────────

Write-Step 'Checking for arduino-cli...'

if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
    Write-Host '  arduino-cli not found. Installing via winget...'
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Write-Error 'winget is not available. Please install arduino-cli manually: https://arduino.github.io/arduino-cli/installation/'
        exit 1
    }
    winget install --id ArduinoSA.CLI --accept-source-agreements --accept-package-agreements
    # Refresh PATH for current session
    $env:PATH = [System.Environment]::GetEnvironmentVariable('PATH', 'Machine') + ';' +
                [System.Environment]::GetEnvironmentVariable('PATH', 'User')
    if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
        Write-Error 'arduino-cli installed but not found on PATH. Please restart your terminal and run this script again.'
        exit 1
    }
}

Write-Host "  Found: $(arduino-cli version)"

# ── 2. Configure arduino-cli ────────────────────────────────────────────────────

Write-Step 'Configuring arduino-cli...'
arduino-cli config init --overwrite 2>$null
$arduinoUserDir = Join-Path $env:TEMP 'ArduinoMidiController'
arduino-cli config set directories.user $arduinoUserDir
arduino-cli config set board_manager.additional_urls $boardManagerUrl
arduino-cli config set library.enable_unsafe_install true
Write-Host "  Arduino user directory set to $arduinoUserDir"
Write-Host '  Board manager URL and unsafe install configured.'

# ── 3. Install Teensy board package ─────────────────────────────────────────────

Write-Step 'Installing Teensy board package (this may take a few minutes)...'
arduino-cli core update-index
arduino-cli core install teensy:avr

# ── 4. Install libraries ───────────────────────────────────────────────────────

Write-Step 'Installing libraries...'

# Ensure the Arduino libraries directory exists
$libDir = Join-Path $arduinoUserDir 'libraries'
if (-not (Test-Path $libDir)) {
    New-Item -ItemType Directory -Path $libDir -Force | Out-Null
    Write-Host "  Created $libDir"
}

arduino-cli lib update-index
arduino-cli lib install 'ArduinoJson@6.21.5'
arduino-cli lib install 'MIDI Library'
arduino-cli lib install 'LiquidCrystal I2C'
arduino-cli lib install 'MD_MIDIFile'
arduino-cli lib install --git-url https://github.com/kimballa/button-debounce.git
Write-Host '  All libraries installed.'

# ── 5. Patch MD_MIDIFile for Teensy SdFat ───────────────────────────────────────

Write-Step 'Patching MD_MIDIFile for Teensy SdFat compatibility...'

$mdHeader = Join-Path $arduinoUserDir 'libraries' 'MD_MIDIFile' 'src' 'MD_MIDIFile.h'

if (Test-Path $mdHeader) {
    $content = Get-Content $mdHeader -Raw
    $patched = $content -replace 'typedef File SDDIR;', 'typedef SdFile SDDIR;' `
                        -replace 'typedef File SDFILE;', 'typedef SdFile SDFILE;'
    if ($content -ne $patched) {
        Set-Content $mdHeader -Value $patched -NoNewline
        Write-Host '  Patched typedef File → SdFile in MD_MIDIFile.h'
    } else {
        Write-Host '  MD_MIDIFile.h already patched or does not need patching.'
    }
} else {
    Write-Warning "Could not locate MD_MIDIFile.h at $mdHeader — you may need to patch it manually."
}

# ── 6. Compile ──────────────────────────────────────────────────────────────────

Write-Step 'Compiling firmware...'
$buildDir = Join-Path $env:TEMP 'ArduinoMidiController_build'
# The Teensy post-build hook (teensy_post_compile) fails on Windows CLI with
# "WaitForSingleObject: The handle is invalid." This is cosmetic — the actual
# compilation and hex generation succeed. We capture output, filter that noise,
# and verify success by checking the hex file was produced.
$ErrorActionPreference = 'Continue'
$compileOutput = arduino-cli compile --fqbn $fqbn --warnings all --build-path $buildDir $sketchPath 2>&1
$ErrorActionPreference = 'Stop'
$compileOutput | ForEach-Object {
    $line = if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.ToString() } else { $_ }
    if ($line -notmatch 'WaitForSingleObject') { Write-Host $line }
}
$hexFile = Join-Path $buildDir 'midicontroller.ino.hex'
if (-not (Test-Path $hexFile)) {
    Write-Error 'Compile failed — no hex file produced.'
    exit 1
}

Write-Host "`n✔ Build succeeded." -ForegroundColor Green

# ── 7. Upload (optional) ───────────────────────────────────────────────────────

if ($Upload) {
    Write-Step 'Uploading firmware to Teensy 4.1...'

    # Auto-detect Teensy COM port if not specified
    if (-not $Port) {
        Write-Host '  Detecting Teensy serial port...'
        $teensyPort = Get-CimInstance Win32_PnPEntity |
            Where-Object { $_.Name -match 'USB Serial.*\(COM\d+\)' -and $_.Manufacturer -match 'PJRC|Teensy' } |
            ForEach-Object { if ($_.Name -match '\((COM\d+)\)') { $Matches[1] } } |
            Select-Object -First 1

        if (-not $teensyPort) {
            # Fallback: use arduino-cli board list to find a Teensy
            $boardJson = arduino-cli board list --format json | ConvertFrom-Json
            $teensyBoard = $boardJson | Where-Object {
                $_.matching_boards | Where-Object { $_.fqbn -eq $fqbn }
            } | Select-Object -First 1
            if ($teensyBoard) {
                $teensyPort = $teensyBoard.port.address
            }
        }

        if ($teensyPort) {
            Write-Host "  Found Teensy on $teensyPort" -ForegroundColor Green
            $Port = $teensyPort
        } else {
            Write-Warning 'Could not auto-detect Teensy port. Letting arduino-cli attempt upload without -p flag.'
        }
    }

    $uploadArgs = @('upload', '--fqbn', $fqbn, '--input-dir', $buildDir)
    if ($Port) {
        $uploadArgs += @('-p', $Port)
    }
    $uploadArgs += $sketchPath
    & arduino-cli @uploadArgs
    Write-Host "`n✔ Upload complete." -ForegroundColor Green
} else {
    Write-Host "`nTo upload, run:  .\setup-local.ps1 -Upload" -ForegroundColor Yellow
}
