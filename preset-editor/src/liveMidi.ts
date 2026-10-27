export const LIVE_CHUNK_BYTES = 96;
export const MAX_LIVE_PRESET_BYTES = 8192;
const HEADER_BYTES = 9;
const prefix = [0x7d, 0x53, 0x59, 1];

export function encodePacket(id: number, command: number, sequence: number, bytes: Uint8Array, last = true): number[] {
  if (!Number.isInteger(id) || id < 1 || id > 127 || !Number.isInteger(command) || command < 0 || command > 127 ||
      !Number.isInteger(sequence) || sequence < 0 || sequence > 16383 || bytes.length > LIVE_CHUNK_BYTES) {
    throw new Error('Invalid Synapse packet.');
  }
  return [0xf0, ...prefix, id, command, (sequence >> 7) & 127, sequence & 127, Number(last),
    ...Array.from(bytes).flatMap((b) => [b >> 4, b & 15]), 0xf7];
}

export function decodePacket(data: Uint8Array) {
  if (data.length < HEADER_BYTES + 2 || data[0] !== 0xf0 || data.at(-1) !== 0xf7 ||
      !prefix.every((b, i) => data[i + 1] === b)) return null;
  const body = data.subarray(1, -1);
  if (body[4] < 1 || body[4] > 127 || body[5] > 127 || body[6] > 127 ||
      body[7] > 127 || body[8] > 1 || (body.length - HEADER_BYTES) % 2 ||
      body.length > HEADER_BYTES + LIVE_CHUNK_BYTES * 2) throw new Error('Malformed Synapse packet.');
  const bytes = new Uint8Array((body.length - HEADER_BYTES) / 2);
  for (let i = 0; i < bytes.length; i++) {
    const high = body[HEADER_BYTES + i * 2];
    const low = body[HEADER_BYTES + i * 2 + 1];
    if (high > 15 || low > 15) throw new Error('Invalid SysEx payload.');
    bytes[i] = (high << 4) | low;
  }
  return { id: body[4], command: body[5], sequence: (body[6] << 7) | body[7], last: body[8] === 1, bytes };
}

export function presetRevision(bytes: Uint8Array): number {
  let hash = 2166136261;
  for (const byte of bytes) hash = Math.imul(hash ^ byte, 16777619) >>> 0;
  return hash;
}

export function presetSetRevision(files: { name: string; text: string }[]): number {
  let hash = 2166136261;
  const encoder = new TextEncoder();
  for (const file of files) {
    for (const text of [file.name, file.text]) {
      for (const byte of encoder.encode(text)) hash = Math.imul(hash ^ byte, 16777619) >>> 0;
      hash = Math.imul(hash, 16777619) >>> 0;
    }
  }
  return hash;
}

export interface LiveListing {
  presets: string[];
  midiFiles: string[];
  midiFilesTruncated?: boolean;
}

interface Pending {
  id: number;
  command: number;
  sequence: number;
  bytes: number[];
  resolve: (text: string) => void;
  reject: (error: Error) => void;
  timer: ReturnType<typeof setTimeout>;
  timeoutMs: number;
}

export class LiveMidiClient {
  supportsPresetSets = false;
  private input: MIDIInput;
  private output: MIDIOutput;
  private onDisconnect: () => void;
  private nextId = 0;
  private pending: Pending | null = null;
  private closed = false;
  private handleMessage = (event: MIDIMessageEvent) => {
    if (!event.data) return;
    try {
      const packet = decodePacket(event.data);
      const pending = this.pending;
      if (!packet || !pending || packet.id !== pending.id) return;
      if (packet.command !== (pending.command | 0x40) && packet.command !== 0x7f) return;
      if (packet.sequence !== pending.sequence++) throw new Error('Missing or out-of-order USB response.');
      clearTimeout(pending.timer);
      pending.timer = this.requestTimer(pending.command, pending.timeoutMs);
      pending.bytes.push(...packet.bytes);
      if (pending.bytes.length > 20000) throw new Error('USB response exceeds limit.');
      if (packet.last) {
        const text = new TextDecoder('utf-8', { fatal: true }).decode(new Uint8Array(pending.bytes));
        clearTimeout(pending.timer);
        this.pending = null;
        if (packet.command === 0x7f) pending.reject(new Error(text));
        else pending.resolve(text);
      }
    } catch (e) {
      this.fail(e instanceof Error ? e : new Error(String(e)));
    }
  };
  private handleState = () => {
    if (this.input.state === 'disconnected' || this.output.state === 'disconnected') {
      this.close();
      this.onDisconnect();
    }
  };

  constructor(input: MIDIInput, output: MIDIOutput, onDisconnect: () => void = () => {}) {
    this.input = input;
    this.output = output;
    this.onDisconnect = onDisconnect;
    input.addEventListener('midimessage', this.handleMessage);
    input.addEventListener('statechange', this.handleState);
    output.addEventListener('statechange', this.handleState);
  }

  private fail(error: Error) {
    if (!this.pending) return;
    clearTimeout(this.pending.timer);
    this.pending.reject(error);
    this.pending = null;
  }

  private requestTimer(command: number, timeoutMs: number) {
    return setTimeout(() => this.fail(new Error(
      `USB command ${command} timed out waiting for controller progress. Retry Reload controller set; if necessary, reconnect USB MIDI.`
    )), timeoutMs);
  }

  close() {
    this.closed = true;
    this.fail(new Error('USB MIDI connection closed.'));
    this.input.removeEventListener('midimessage', this.handleMessage);
    this.input.removeEventListener('statechange', this.handleState);
    this.output.removeEventListener('statechange', this.handleState);
  }

  async request(command: number, data: Uint8Array = new Uint8Array(), sequence = 0): Promise<string> {
    if (this.closed) throw new Error('Reconnect the USB MIDI controller.');
    if (this.pending) throw new Error('Another USB operation is still running.');
    const id = this.nextId = this.nextId % 127 + 1;
    const packet = encodePacket(id, command, sequence, data);
    return new Promise((resolve, reject) => {
      const timeoutMs = command === 11 ? 60000 : 8000;
      const timer = this.requestTimer(command, timeoutMs);
      this.pending = { id, command, sequence: 0, bytes: [], resolve, reject, timer, timeoutMs };
      try { this.output.send(packet); } catch (e) { this.fail(e instanceof Error ? e : new Error(String(e))); }
    });
  }

  private json(command: number, data: object) {
    return this.request(command, new TextEncoder().encode(JSON.stringify(data)));
  }

  async hello() {
    const hello = JSON.parse(await this.request(1));
    if (hello.product !== 'Synapse' || hello.protocol !== 1 || hello.maxBytes !== MAX_LIVE_PRESET_BYTES) {
      throw new Error('Unsupported controller firmware/protocol.');
    }
    this.supportsPresetSets = hello.presetSets === true;
  }

  async list(): Promise<LiveListing> {
    const result = JSON.parse(await this.request(2));
    if (!Array.isArray(result.presets) || !result.presets.every((n: unknown) => typeof n === 'string') ||
        !Array.isArray(result.midiFiles) || !result.midiFiles.every((n: unknown) => typeof n === 'string')) {
      throw new Error('Invalid controller file listing.');
    }
    return result;
  }

  async read(name: string) {
    const text = await this.json(3, { name });
    return { text, revision: presetRevision(new TextEncoder().encode(text)) };
  }

  private async treeRevision(): Promise<number> {
    const result = JSON.parse(await this.request(12));
    if (!Number.isInteger(result.revision) || result.revision < 0 || result.revision > 0xffffffff) {
      throw new Error('Invalid preset-set revision.');
    }
    return result.revision;
  }

  async readSet() {
    if (!this.supportsPresetSets) throw new Error('Update controller firmware for full USB preset management.');
    const revision = await this.treeRevision();
    const listing = await this.list();
    if (listing.presets.length > 150) throw new Error('Controller exceeds the 150-preset limit.');
    const files = [];
    for (const name of listing.presets) files.push({ name, text: (await this.read(name)).text });
    if (revision !== await this.treeRevision()) throw new Error('Preset set changed while loading; reload before editing.');
    return { files, revision, listing };
  }

  async saveSet(texts: string[], revision: number, sources: string[] = []) {
    if (!this.supportsPresetSets) throw new Error('Update controller firmware for full USB preset management.');
    if (texts.length > 150) throw new Error('At most 150 presets are supported.');
    const files = texts.map((text) => new TextEncoder().encode(text));
    if (files.some((bytes) => !bytes.length || bytes.length > MAX_LIVE_PRESET_BYTES)) {
      throw new Error('Each live preset must be 1–8192 UTF-8 bytes.');
    }
    try {
      await this.json(10, { count: files.length, revision });
      for (let index = 0; index < files.length; ++index) {
        const bytes = files[index];
        await this.json(4, { index, size: bytes.length, source: sources[index] ?? '' });
        for (let offset = 0; offset < bytes.length; offset += LIVE_CHUNK_BYTES) {
          await this.request(5, bytes.subarray(offset, offset + LIVE_CHUNK_BYTES), offset / LIVE_CHUNK_BYTES);
        }
        await this.request(6);
      }
      await this.request(11);
      return presetSetRevision(texts.map((text, index) => ({ name: `${index + 1}.json`, text })));
    } catch (error) {
      try { await this.request(9); }
      catch (abortError) { throw new Error(`${String(error)}; recovery request failed: ${String(abortError)}. Reconnect and reload the controller set.`); }
      throw new Error(`${String(error)}. Reload the controller set before retrying; an interrupted commit may have completed.`);
    }
  }

  async save(name: string, text: string, revision: number) {
    const bytes = new TextEncoder().encode(text);
    if (!bytes.length || bytes.length > MAX_LIVE_PRESET_BYTES) throw new Error('Live preset must be 1–8192 UTF-8 bytes.');
    await this.json(4, { name, size: bytes.length, revision });
    try {
      for (let offset = 0; offset < bytes.length; offset += LIVE_CHUNK_BYTES) {
        await this.request(5, bytes.subarray(offset, offset + LIVE_CHUNK_BYTES), offset / LIVE_CHUNK_BYTES);
      }
      await this.request(6);
      return presetRevision(bytes);
    } catch (error) {
      try { await this.request(9); }
      catch (abortError) { throw new Error(`${String(error)}; abort failed: ${String(abortError)}. Reconnect and reload before retrying.`); }
      throw error;
    }
  }

  activate(index: number, name: string) { return this.json(7, { index, name }); }
  trigger(number: number, name: string) { return this.json(8, { switch: number, name }); }
}
