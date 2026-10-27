#pragma once

#include "live_protocol.h"

static constexpr const char *liveStageName = "synapse.tmp";
static constexpr const char *liveBackupName = "synapse.bak";
static constexpr const char *liveJournalName = "synapse.txn";

static bool liveRename(const char *from, const char *to) {
  FsFile file;
  if (!file.open(from, O_RDWR)) return false;
  const bool renamed = file.rename(to);
  file.close();
  return renamed;
}

static bool liveRemove(const char *name) {
  return !SD.exists(name) || SD.remove(name);
}

static bool validLiveName(const char *name) {
  if (!name || !name[0] || strlen(name) >= maxPresetNameLength || !hasJsonExtension(name)) return false;
  return strchr(name, '/') == nullptr && strchr(name, '\\') == nullptr &&
    strchr(name, ':') == nullptr && strcmp(name, ".json") != 0;
}

static bool recoverLiveSave() {
  if (!SD.exists(liveJournalName)) return !SD.exists(liveBackupName) && liveRemove(liveStageName);
  FsFile journal;
  if (!journal.open(liveJournalName, O_READ)) return false;
  char target[maxPresetNameLength] = {};
  const int length = journal.read(target, sizeof(target) - 1);
  journal.close();
  if (length <= 0 || !validLiveName(target)) {
    if (SD.exists(liveBackupName)) return false;
    return liveRemove(liveStageName) && liveRemove(liveJournalName);
  }
  if (SD.exists(liveBackupName) && !SD.exists(target) && !liveRename(liveBackupName, target)) return false;
  return liveRemove(liveBackupName) && liveRemove(liveStageName) && liveRemove(liveJournalName);
}

#if defined(USB_MIDI) || defined(USB_MIDI_SERIAL) || defined(USB_MIDI4) || defined(USB_MIDI4_SERIAL) || defined(USB_MIDI16) || defined(USB_MIDI16_SERIAL)

static uint8_t liveRequest[livePacketBytes];
static int liveRequestLength = 0;
static char liveResponse[20000];
static size_t liveResponseLength = 0;
static size_t liveResponseOffset = 0;
static uint8_t liveResponseId = 0;
static uint8_t liveResponseCommand = 0;
static bool liveResponding = false;
static char liveUpload[maxLivePresetBytes + 1];
static int liveExpectedBytes = 0;
static int liveReceivedBytes = 0;
static int liveUploadSequence = 0;
static uint32_t liveUploadMillis = 0;
static uint32_t liveBaseRevision = 0;
static char liveTarget[maxPresetNameLength] = {};

static uint32_t liveRevision(const char *text, size_t length) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; ++i) hash = (hash ^ static_cast<uint8_t>(text[i])) * 16777619UL;
  return hash;
}

static void queueLiveResponse(uint8_t id, uint8_t command, const char *text, bool error = false) {
  liveResponseId = id;
  liveResponseCommand = error ? 0x7f : (command | 0x40);
  if (text != liveResponse) {
    snprintf(liveResponse, sizeof(liveResponse), "%s", text);
  }
  liveResponseLength = strlen(liveResponse);
  liveResponseOffset = 0;
  liveResponding = true;
}

static void liveSysExReceived(const uint8_t *data, uint16_t length, bool complete) {
  if (!complete || liveRequestLength || liveResponding || length < 2) return;
  if (data[0] != 0xf0 || data[length - 1] != 0xf7) return;
  data++;
  length -= 2;
  uint8_t decoded[liveChunkBytes];
  if (decodeLivePacket(data, length, decoded) < 0 || data[5] >= 0x40) return;
  memcpy(liveRequest, data, length);
  liveRequestLength = length;
}

static bool readLiveFile(const char *name, char *buffer, int &length) {
  FsFile file;
  if (!file.open(name, O_READ)) return false;
  if (file.fileSize() > maxLivePresetBytes) {
    file.close();
    return false;
  }
  const int expected = file.fileSize();
  length = file.read(buffer, maxLivePresetBytes);
  file.close();
  if (length < 0 || length != expected) return false;
  buffer[length] = '\0';
  return static_cast<int>(strlen(buffer)) == length;
}

static void serviceLiveRequest() {
  uint8_t payload[liveChunkBytes + 1] = {};
  const int length = decodeLivePacket(liveRequest, liveRequestLength, payload);
  const uint8_t id = liveRequest[4];
  const uint8_t command = liveRequest[5];
  const int sequence = (liveRequest[6] << 7) | liveRequest[7];
  liveRequestLength = 0;
  if (length < 0) return;
  payload[length] = 0;
  auto fail = [&](const char *text) { queueLiveResponse(id, command, text, true); };
  if (!liveRequest[8] || (command != 5 && sequence != 0)) { fail("Malformed request framing."); return; }
  if (!hasLoaded) { fail("Controller is starting; retry shortly."); return; }
  if (command == 1) {
    queueLiveResponse(id, command, "{\"product\":\"Synapse\",\"protocol\":1,\"maxBytes\":8192}");
    return;
  }
  if (command == 9) {
    liveExpectedBytes = 0;
    queueLiveResponse(id, command, "OK");
    return;
  }
  if ((playingMidiFile || stoppingMidiFile) && command != 8) { fail("Stop MIDI file playback before live editing."); return; }
  if (command == 2) {
    StaticJsonDocument<16384> listing;
    JsonArray presets = listing.createNestedArray("presets");
    for (int i = 0; i < presetCount; ++i) presets.add(presetList[i]);
    JsonArray midi = listing.createNestedArray("midiFiles");
    FsFile directory;
    FsFile file;
    if (!directory.open("/", O_READ)) { fail("Cannot open SD root."); return; }
    while (file.openNext(&directory, O_RDONLY)) {
      char name[64];
      file.getName(name, sizeof(name));
      const size_t size = strlen(name);
      if (!file.isDirectory() && size >= 4 &&
          (strcmp(name + size - 4, ".mid") == 0 || (size >= 5 && strcmp(name + size - 5, ".midi") == 0))) {
        if (midi.size() < 128) midi.add(name);
        else listing["midiFilesTruncated"] = true;
      }
      file.close();
    }
    directory.close();
    if (listing.overflowed() || measureJson(listing) >= sizeof(liveResponse)) { fail("SD listing is too large."); return; }
    serializeJson(listing, liveResponse, sizeof(liveResponse));
    queueLiveResponse(id, command, liveResponse);
    return;
  }
  if (command == 5) {
    if (!liveExpectedBytes || sequence != liveUploadSequence ||
        length == 0 || liveReceivedBytes + length > liveExpectedBytes) {
      fail("Unexpected upload chunk; abort and retry."); return;
    }
    memcpy(liveUpload + liveReceivedBytes, payload, length);
    liveReceivedBytes += length;
    liveUploadSequence++;
    liveUploadMillis = millis();
    queueLiveResponse(id, command, "OK");
    return;
  }
  if (command == 6) {
    if (!liveExpectedBytes || liveReceivedBytes != liveExpectedBytes) { fail("Incomplete upload."); return; }
    liveUpload[liveReceivedBytes] = 0;
    StaticJsonDocument<presetDocumentCapacity> document;
    const auto error = deserializeJson(document, liveUpload, liveReceivedBytes);
    if (error || strlen(liveUpload) != static_cast<size_t>(liveReceivedBytes) ||
        !validPerformancePreset(document.as<JsonVariantConst>())) { fail("Invalid or oversized preset JSON/actions."); return; }
    int currentLength = 0;
    if (!readLiveFile(liveTarget, liveResponse, currentLength) ||
        liveRevision(liveResponse, currentLength) != liveBaseRevision) { fail("Preset changed on SD; reload before saving."); return; }
    if (!recoverLiveSave()) { fail("SD recovery failed; check synapse.txn/bak before retrying."); return; }
    FsFile stage;
    if (!stage.open(liveStageName, O_WRONLY | O_CREAT | O_TRUNC)) { fail("Cannot stage preset on SD."); return; }
    const bool staged = stage.write(liveUpload, liveReceivedBytes) == static_cast<size_t>(liveReceivedBytes) && stage.sync();
    stage.close();
    if (!staged) { fail("SD write failed; original preset retained."); return; }
    FsFile journal;
    if (!journal.open(liveJournalName, O_WRONLY | O_CREAT | O_TRUNC)) { fail("Cannot create SD save journal."); return; }
    const size_t targetLength = strlen(liveTarget);
    const bool journaled = journal.write(liveTarget, targetLength) == targetLength && journal.sync();
    journal.close();
    if (!journaled) { fail("Cannot flush SD save journal; original retained."); return; }
    if (!liveRename(liveTarget, liveBackupName) || !liveRename(liveStageName, liveTarget)) {
      const bool recovered = recoverLiveSave();
      fail(recovered ? "SD replacement failed; reload to verify recovered preset." :
        "SD recovery failed; preserve synapse.txn/bak and check the SD card."); return;
    }
    if (!recoverLiveSave()) { fail("Preset written, but SD backup cleanup failed; reload before retrying."); return; }
    liveExpectedBytes = 0;
    prefetchedPresetIndex = -1;
    prefetchRequested = false;
    prefetchedPresetDoc.clear();
    // Saving is persistent but does not unexpectedly trigger OnLoad or change the rig.
    queueLiveResponse(id, command, "OK");
    return;
  }
  StaticJsonDocument<256> request;
  if (deserializeJson(request, payload, length)) { fail("Invalid request."); return; }
  if (command == 3 || command == 4) {
    const char *name = request["name"];
    bool known = false;
    if (validLiveName(name)) {
      for (int i = 0; i < presetCount; ++i) if (strcmp(presetList[i], name) == 0) known = true;
    }
    if (!known) { fail("Select an existing preset from this controller."); return; }
    int fileLength = 0;
    if (!readLiveFile(name, liveResponse, fileLength)) { fail("Cannot read preset (limit 8192 UTF-8 bytes)."); return; }
    if (command == 3) { queueLiveResponse(id, command, liveResponse); return; }
    if (liveExpectedBytes) { fail("Upload already active; abort or wait 5 seconds."); return; }
    if (!request["size"].is<int>() || request["size"].as<int>() <= 0 ||
        request["size"].as<int>() > maxLivePresetBytes || !request["revision"].is<uint32_t>() ||
        request["revision"].as<uint32_t>() != liveRevision(liveResponse, fileLength)) {
      fail("Invalid size or changed SD preset; reload before saving."); return;
    }
    snprintf(liveTarget, sizeof(liveTarget), "%s", name);
    liveBaseRevision = request["revision"];
    liveExpectedBytes = request["size"];
    liveReceivedBytes = liveUploadSequence = 0;
    liveUploadMillis = millis();
    queueLiveResponse(id, command, "OK");
    return;
  }
  if (command == 7) {
    const int index = request["index"] | -1;
    const char *name = request["name"] | "";
    if (!request["index"].is<int>() || index < 0 || index >= presetCount ||
        strcmp(presetList[index], name) != 0) { fail("Invalid preset selection; reconnect to refresh."); return; }
    currentPreset = index;
    changePreset();
    if (!activePresetValid) { fail("Preset could not be activated."); return; }
    queueLiveResponse(id, command, "OK");
    return;
  }
  if (command == 8) {
    const int number = request["switch"] | 0;
    const char *name = request["name"] | "";
    if (!activePresetValid || number < 1 || number > 3 ||
        strcmp(presetList[currentPreset], name) != 0) { fail("Preset changed; activate it again before testing."); return; }
    executeSwitchLogic(number);
    queueLiveResponse(id, command, "OK");
    return;
  }
  fail("Unknown Synapse command.");
}

static void serviceLiveEditor() {
  if (liveExpectedBytes && static_cast<uint32_t>(millis() - liveUploadMillis) >= 5000) liveExpectedBytes = 0;
  if (liveResponding) {
    uint8_t packet[livePacketBytes];
    const int count = min(static_cast<size_t>(liveChunkBytes), liveResponseLength - liveResponseOffset);
    const bool last = liveResponseOffset + count == liveResponseLength;
    const int length = encodeLivePacket(packet, liveResponseId, liveResponseCommand,
      liveResponseOffset / liveChunkBytes, last,
      reinterpret_cast<const uint8_t *>(liveResponse + liveResponseOffset), count);
    usbMIDI.sendSysEx(length, packet);
    usbMIDI.send_now();
    liveResponseOffset += count;
    if (last) liveResponding = false;
  }
  // Only process one request per loop, outside the USB receive callback.
  if (!liveResponding && liveRequestLength) serviceLiveRequest();
  usbMIDI.read();
}

static void beginLiveEditor() {
  usbMIDI.setHandleSysEx(liveSysExReceived);
}

#else
static void beginLiveEditor() {}
static void serviceLiveEditor() {}
#endif
