#pragma once

static constexpr const char *liveSetJournal = "synset.txn";
static constexpr const char *liveSetReady = "synset.go";
static constexpr const char *liveSetPending = "synset.tmp";

struct LiveSetManifest {
  uint32_t magic;
  uint32_t oldCount;
  uint32_t newCount;
  char names[maxPresetListSize][maxPresetNameLength];
  uint32_t revisions[maxPresetListSize];
  uint32_t checksum;
};

static bool liveSetFileHash(const char *name, uint32_t &hash) {
  FsFile file;
  if (!file.open(name, O_READ)) return false;
  const size_t size = file.fileSize();
  if (size == 0 || size > maxLivePresetBytes) { file.close(); return false; }
  hash = 2166136261UL;
  size_t remaining = size;
  while (remaining) {
    char buffer[128];
    const size_t expected = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    const int count = file.read(buffer, expected);
    if (count != static_cast<int>(expected)) { file.close(); return false; }
    for (int i = 0; i < count; ++i) hash = (hash ^ static_cast<uint8_t>(buffer[i])) * 16777619UL;
    remaining -= count;
  }
  file.close();
  return true;
}

static void liveSetPath(char *path, int index, bool backup) {
  snprintf(path, maxPresetNameLength, backup ? "syb%03d.bak" : "syn%03d.tmp", index + 1);
}

static uint32_t liveSetChecksum(const LiveSetManifest &manifest) {
  const auto *bytes = reinterpret_cast<const uint8_t *>(&manifest);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < offsetof(LiveSetManifest, checksum); ++i) hash = (hash ^ bytes[i]) * 16777619UL;
  return hash;
}

static bool readLiveSetManifest(LiveSetManifest &manifest) {
  FsFile file;
  if (!file.open(liveSetJournal, O_READ)) return false;
  const bool validSize = file.fileSize() == sizeof(manifest);
  const int count = file.read(reinterpret_cast<char *>(&manifest), sizeof(manifest));
  file.close();
  if (!validSize || count != sizeof(manifest) || manifest.magic != 0x53595331 ||
      manifest.oldCount > maxPresetListSize || manifest.newCount > maxPresetListSize ||
      manifest.checksum != liveSetChecksum(manifest)) return false;
  for (uint32_t i = 0; i < manifest.oldCount; ++i) {
    if (!memchr(manifest.names[i], 0, maxPresetNameLength) || !validLiveName(manifest.names[i])) return false;
    for (uint32_t j = 0; j < i; ++j) if (strcmp(manifest.names[i], manifest.names[j]) == 0) return false;
  }
  return true;
}

static bool cleanLiveSetStages() {
  for (int i = 0; i < maxPresetListSize; ++i) {
    char name[maxPresetNameLength];
    liveSetPath(name, i, false);
    if (!liveRemove(name)) return false;
  }
  return liveRemove(liveSetPending);
}

// Before the ready marker, roll back. After it, finish installing the complete new set.
static bool recoverLiveSet() {
  if (!SD.exists(liveSetJournal)) {
    for (int i = 0; i < maxPresetListSize; ++i) {
      char name[maxPresetNameLength];
      liveSetPath(name, i, true);
      if (SD.exists(name)) return false;
    }
    return cleanLiveSetStages() && liveRemove(liveSetReady);
  }
  LiveSetManifest manifest;
  if (!readLiveSetManifest(manifest)) return false;
  if (SD.exists(liveSetReady)) {
    for (uint32_t i = 0; i < manifest.newCount; ++i) {
      char stage[maxPresetNameLength], target[maxPresetNameLength];
      liveSetPath(stage, i, false);
      snprintf(target, sizeof(target), "%lu.json", static_cast<unsigned long>(i + 1));
      if (SD.exists(stage)) {
        uint32_t hash = 0;
        if (!liveSetFileHash(stage, hash) || hash != manifest.revisions[i]) return false;
        if (SD.exists(target) || !liveRename(stage, target)) return false;
      } else if (!SD.exists(target)) return false;
      uint32_t hash = 0;
      if (!liveSetFileHash(target, hash) || hash != manifest.revisions[i]) return false;
    }
    for (uint32_t i = 0; i < manifest.oldCount; ++i) {
      char backup[maxPresetNameLength];
      liveSetPath(backup, i, true);
      if (!liveRemove(backup)) return false;
    }
  } else {
    for (uint32_t i = 0; i < manifest.oldCount; ++i) {
      char backup[maxPresetNameLength];
      liveSetPath(backup, i, true);
      if (SD.exists(backup) &&
          (SD.exists(manifest.names[i]) || !liveRename(backup, manifest.names[i]))) return false;
      if (!SD.exists(manifest.names[i])) return false;
    }
  }
  return cleanLiveSetStages() && liveRemove(liveSetJournal) && liveRemove(liveSetReady);
}

static inline bool publishLiveSetManifest(LiveSetManifest &manifest) {
  for (uint32_t i = 0; i < manifest.newCount; ++i) {
    char name[maxPresetNameLength];
    liveSetPath(name, i, false);
    if (!liveSetFileHash(name, manifest.revisions[i])) return false;
  }
  manifest.magic = 0x53595331;
  manifest.checksum = liveSetChecksum(manifest);
  FsFile file;
  if (!file.open(liveSetPending, O_WRONLY | O_CREAT | O_TRUNC)) return false;
  const bool written = file.write(reinterpret_cast<const char *>(&manifest), sizeof(manifest)) == sizeof(manifest) && file.sync();
  file.close();
  return written && liveRename(liveSetPending, liveSetJournal);
}

static inline bool installLiveSet(LiveSetManifest &manifest) {
  if (!publishLiveSetManifest(manifest)) return false;
  for (uint32_t i = 0; i < manifest.oldCount; ++i) {
    char backup[maxPresetNameLength];
    liveSetPath(backup, i, true);
    if (!liveRename(manifest.names[i], backup)) return false;
  }
  FsFile marker;
  if (!marker.open(liveSetPending, O_WRONLY | O_CREAT | O_TRUNC)) return false;
  const bool synced = marker.sync();
  marker.close();
  if (!synced || !liveRename(liveSetPending, liveSetReady)) return false;
  return recoverLiveSet();
}
