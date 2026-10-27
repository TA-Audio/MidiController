#pragma once

#include "performance.h"

static constexpr int liveChunkBytes = 96;
static constexpr int liveHeaderBytes = 9;
static constexpr int livePacketBytes = liveHeaderBytes + liveChunkBytes * 2;

constexpr int decodeLivePacket(const uint8_t *packet, int length, uint8_t *payload) {
  if (length < liveHeaderBytes || length > livePacketBytes ||
      packet[0] != 0x7d || packet[1] != 'S' || packet[2] != 'Y' ||
      packet[3] != 1 || packet[4] == 0 || packet[4] > 127 ||
      packet[5] > 127 || packet[6] > 127 || packet[7] > 127 ||
      packet[8] > 1 || (length - liveHeaderBytes) % 2 != 0) return -1;
  const int count = (length - liveHeaderBytes) / 2;
  for (int i = 0; i < count; ++i) {
    const uint8_t high = packet[liveHeaderBytes + i * 2];
    const uint8_t low = packet[liveHeaderBytes + i * 2 + 1];
    if (high > 15 || low > 15) return -1;
    payload[i] = (high << 4) | low;
  }
  return count;
}

constexpr int encodeLivePacket(uint8_t *packet, uint8_t id, uint8_t command,
                            int sequence, bool last, const uint8_t *payload, int count) {
  if (id == 0 || id > 127 || command > 127 || count < 0 || count > liveChunkBytes ||
      sequence < 0 || sequence > 16383) return -1;
  const uint8_t header[] = {0x7d, 'S', 'Y', 1, id, command,
    static_cast<uint8_t>((sequence >> 7) & 127),
    static_cast<uint8_t>(sequence & 127), static_cast<uint8_t>(last)};
  for (int i = 0; i < liveHeaderBytes; ++i) packet[i] = header[i];
  for (int i = 0; i < count; ++i) {
    packet[liveHeaderBytes + i * 2] = payload[i] >> 4;
    packet[liveHeaderBytes + i * 2 + 1] = payload[i] & 15;
  }
  return liveHeaderBytes + count * 2;
}
