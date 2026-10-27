#pragma once

static void mirrorDinToUsb(uint8_t type, uint8_t data1, uint8_t data2, uint8_t channel) {
#if defined(USB_MIDI) || defined(USB_MIDI_SERIAL) || defined(USB_MIDI4) || defined(USB_MIDI4_SERIAL) || defined(USB_MIDI16) || defined(USB_MIDI16_SERIAL)
  if (usb_configuration) usbMIDI.send(type, data1, data2, channel, 0);
#endif
}

static void sendDinMidi(midi::MidiType type, uint8_t data1 = 0, uint8_t data2 = 0, uint8_t channel = 0) {
  if (type == midi::TimeCodeQuarterFrame || type == midi::SongPosition ||
      type == midi::SongSelect || type == midi::TuneRequest) {
    MIDI1.sendCommon(type, type == midi::SongPosition ? data1 | (static_cast<unsigned>(data2) << 7) : data1);
  } else {
    MIDI1.send(type, data1, data2, channel);
  }
  mirrorDinToUsb(type, data1, data2, channel);
}

template <typename Serial, typename Event>
static size_t sendMidiFileEvent(Serial &serial, const Event &event, uint8_t channel) {
  if (event.data[0] < 0x80 || event.data[0] > 0xe0 || event.size < 2 || event.size > 3) return 0;
  size_t sent = serial.write(event.data[0] | (channel - 1));
  sent += serial.write(&event.data[1], event.size - 1);
  if (sent) mirrorDinToUsb(event.data[0], event.data[1], event.size > 2 ? event.data[2] : 0, channel);
  return sent;
}

template <typename Input>
static void forwardMidiInput(Input &input) {
  // Keep continuous input traffic from starving USB editing and switch polling.
  for (int count = 0; count < 32 && input.read(); ++count) {
    if (input.getType() == midi::SystemExclusive) {
      MIDI1.sendSysEx(input.getSysExArrayLength(), input.getSysExArray(), true);
#if defined(USB_MIDI) || defined(USB_MIDI_SERIAL) || defined(USB_MIDI4) || defined(USB_MIDI4_SERIAL) || defined(USB_MIDI16) || defined(USB_MIDI16_SERIAL)
      if (usb_configuration) usbMIDI.sendSysEx(input.getSysExArrayLength(), input.getSysExArray(), true);
#endif
    } else {
      sendDinMidi(input.getType(), input.getData1(), input.getData2(), input.getChannel());
    }
  }
}
