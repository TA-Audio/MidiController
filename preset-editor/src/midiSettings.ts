export const MIDI_SETTINGS_KEY = 'synapse.midiPorts';

export interface PortIdentity {
  id: string;
  name: string | null;
  manufacturer: string | null;
}

export interface MidiSettings {
  input: PortIdentity | null;
  output: PortIdentity | null;
}

export function portIdentity(port: PortIdentity): PortIdentity {
  return { id: port.id, name: port.name, manufacturer: port.manufacturer };
}

export function readMidiSettings(storage: Pick<Storage, 'getItem'>): MidiSettings {
  const text = storage.getItem(MIDI_SETTINGS_KEY);
  if (!text) return { input: null, output: null };
  const value = JSON.parse(text);
  const valid = (port: unknown): port is PortIdentity => {
    if (!port || typeof port !== 'object') return false;
    const p = port as Record<string, unknown>;
    return typeof p.id === 'string' && (p.name === null || typeof p.name === 'string') &&
      (p.manufacturer === null || typeof p.manufacturer === 'string');
  };
  if (!value || typeof value !== 'object' ||
      (value.input !== null && !valid(value.input)) || (value.output !== null && !valid(value.output))) {
    throw new Error('Invalid saved USB MIDI settings. Select the controller ports again.');
  }
  return value;
}

export function matchMidiPort<T extends PortIdentity & { state: string }>(ports: T[], saved: PortIdentity | null): T | undefined {
  const available = ports.filter((port) => port.state === 'connected');
  if (saved) {
    const exact = available.find((port) => port.id === saved.id);
    if (exact) return exact;
    const matches = available.filter((port) => port.name === saved.name && port.manufacturer === saved.manufacturer);
    return matches.length === 1 ? matches[0] : undefined;
  }
  const controllers = available.filter((port) => /synapse|teensy/i.test(port.name ?? ''));
  return controllers.length === 1 ? controllers[0] : undefined;
}
