import test from 'node:test';
import assert from 'node:assert/strict';
import { decodePacket, encodePacket, LIVE_CHUNK_BYTES, LiveMidiClient, presetRevision, presetSetRevision } from '../src/liveMidi.ts';
import { matchMidiPort, MIDI_SETTINGS_KEY, portIdentity, readMidiSettings } from '../src/midiSettings.ts';

test('MIDI settings round-trip only port identities and reject malformed stored data', () => {
  const port = { id: 'input-1', name: 'Teensy MIDI', manufacturer: 'PJRC', state: 'connected' };
  const settings = { input: portIdentity(port), output: null };
  assert.deepEqual(settings.input, { id: 'input-1', name: 'Teensy MIDI', manufacturer: 'PJRC' });
  assert.deepEqual(readMidiSettings({ getItem: (key) => { assert.equal(key, MIDI_SETTINGS_KEY); return JSON.stringify(settings); } }), settings);
  assert.deepEqual(readMidiSettings({ getItem: () => null }), { input: null, output: null });
  for (const text of ['bad JSON', 'null', '{}', '{"input":{"id":1},"output":null}']) {
    assert.throws(() => readMidiSettings({ getItem: () => text }));
  }
  assert.throws(() => readMidiSettings({ getItem: () => { throw new Error('Storage disabled'); } }), /Storage disabled/);
});

test('MIDI ports restore by ID then unique identity, without selecting disconnected or ambiguous devices', () => {
  const port = { id: 'old', name: 'Teensy MIDI', manufacturer: 'PJRC', state: 'connected' };
  const replacement = { ...port, id: 'new' };
  assert.equal(matchMidiPort([port, replacement], port), port);
  assert.equal(matchMidiPort([replacement], port), replacement);
  assert.equal(matchMidiPort([{ ...replacement, state: 'disconnected' }], port), undefined);
  assert.equal(matchMidiPort([replacement, { ...replacement, id: 'another' }], port), undefined);
  assert.equal(matchMidiPort([{ ...replacement, manufacturer: 'Unknown' }], port), undefined);
  assert.equal(matchMidiPort([replacement], null), replacement);
  assert.equal(matchMidiPort([{ ...replacement, name: 'Piano' }], null), undefined);
  assert.equal(matchMidiPort([port, replacement], null), undefined);
});

class MockPort extends EventTarget {
  state = 'connected';
  send: (bytes: number[]) => void = () => {};
}

function controller() {
  const input = new MockPort();
  const output = new MockPort();
  const commands: number[] = [];
  let uploaded: number[] = [];
  let current = '{"Name":"Original"}';
  let failChunk = false;
  let disconnected = false;
  output.send = (bytes) => {
    const packet = decodePacket(new Uint8Array(bytes))!;
    commands.push(packet.command);
    let response = 'OK';
    let error = false;
    const text = new TextDecoder().decode(packet.bytes);
    switch (packet.command) {
      case 1: response = '{"product":"Synapse","protocol":1,"maxBytes":8192}'; break;
      case 2: response = '{"presets":["1.json"],"midiFiles":["song.mid"]}'; break;
      case 3: response = current; break;
      case 4: {
        const request = JSON.parse(text);
        if (request.revision !== presetRevision(new TextEncoder().encode(current))) {
          error = true; response = 'Preset changed on SD';
        } else uploaded = [];
        break;
      }
      case 5:
        if (failChunk) { error = true; response = 'SD write failed'; }
        else uploaded.push(...packet.bytes);
        break;
      case 6: current = new TextDecoder().decode(new Uint8Array(uploaded)); break;
    }
    const encoded = new TextEncoder().encode(response);
    queueMicrotask(() => {
      for (let offset = 0; offset < encoded.length; offset += LIVE_CHUNK_BYTES) {
        const event = new Event('midimessage');
        Object.defineProperty(event, 'data', { value: new Uint8Array(encodePacket(packet.id, error ? 0x7f : packet.command | 0x40,
          offset / LIVE_CHUNK_BYTES, encoded.subarray(offset, offset + LIVE_CHUNK_BYTES), offset + LIVE_CHUNK_BYTES >= encoded.length)) });
        input.dispatchEvent(event);
      }
    });
  };
  const client = new LiveMidiClient(input as unknown as MIDIInput, output as unknown as MIDIOutput, () => { disconnected = true; });
  return { client, input, commands, get text() { return current; }, set failChunk(value: boolean) { failChunk = value; }, get disconnected() { return disconnected; } };
}

test('packet codec preserves all UTF-8 bytes and stays seven-bit safe', () => {
  assert.deepEqual(encodePacket(9, 5, 130, new Uint8Array([0, 0x7f, 0x80, 0xff])),
    [0xf0, 0x7d, 0x53, 0x59, 1, 9, 5, 1, 2, 1, 0, 0, 7, 15, 8, 0, 15, 15, 0xf7]);
  const bytes = new TextEncoder().encode('Lead 🎸 é 日本語');
  const encoded = encodePacket(127, 5, 1000, bytes);
  assert.ok(encoded.slice(1, -1).every((b) => b < 128));
  const decoded = decodePacket(new Uint8Array(encoded))!;
  assert.equal(decoded.sequence, 1000);
  assert.deepEqual(decoded.bytes, bytes);
  assert.equal(presetRevision(new TextEncoder().encode('hello')), 1335831723);
});

test('codec rejects oversized, malformed and non-Synapse messages', () => {
  assert.throws(() => encodePacket(1, 5, 0, new Uint8Array(97)));
  assert.throws(() => encodePacket(0, 5, 0, new Uint8Array()));
  const packet = encodePacket(1, 5, 0, new Uint8Array([255]));
  packet[10] = 16;
  assert.throws(() => decodePacket(new Uint8Array(packet)));
  assert.equal(decodePacket(new Uint8Array([0x90, 60, 127])), null);
});

test('connect/list/read/save/reload persists multi-chunk UTF-8 content', async () => {
  const mock = controller();
  await mock.client.hello();
  assert.deepEqual(await mock.client.list(), { presets: ['1.json'], midiFiles: ['song.mid'] });
  const original = await mock.client.read('1.json');
  const text = JSON.stringify({ Name: 'Lead 🎸', Description: 'long '.repeat(500) });
  const revision = await mock.client.save('1.json', text, original.revision);
  assert.equal(mock.text, text);
  assert.equal((await mock.client.read('1.json')).revision, revision);
  assert.ok(mock.commands.filter((c) => c === 5).length > 20);
  assert.ok(!mock.commands.includes(7), 'Saving must not activate a preset');
  mock.client.close();
});

test('failed uploads abort without committing or discarding original content', async () => {
  const mock = controller();
  const { revision } = await mock.client.read('1.json');
  mock.failChunk = true;
  await assert.rejects(mock.client.save('1.json', '{"Name":"Changed"}', revision), /SD write failed/);
  assert.ok(mock.commands.includes(9));
  assert.ok(!mock.commands.includes(6));
  assert.equal(mock.text, '{"Name":"Original"}');
  mock.client.close();
});

test('conflicts and oversized presets are rejected before writes', async () => {
  const mock = controller();
  await assert.rejects(mock.client.save('1.json', '{}', 0), /changed on SD/);
  assert.ok(!mock.commands.includes(5));
  await assert.rejects(mock.client.save('1.json', 'x'.repeat(8193), 0), /8192/);
  mock.client.close();
});

test('disconnect rejects pending operations and prevents further sends', async () => {
  const mock = controller();
  mock.input.state = 'disconnected';
  mock.input.dispatchEvent(new Event('statechange'));
  assert.ok(mock.disconnected);
  await assert.rejects(mock.client.read('1.json'), /Reconnect/);
});

test('activate and primary action testing are explicit separate commands', async () => {
  const mock = controller();
  await mock.client.activate(0, '1.json');
  await mock.client.trigger(2, '1.json');
  assert.deepEqual(mock.commands, [7, 8]);
  mock.client.close();
});

test('out-of-order responses reject the operation rather than accepting partial data', async () => {
  const input = new MockPort(), output = new MockPort();
  output.send = (bytes) => {
    const request = decodePacket(new Uint8Array(bytes))!;
    queueMicrotask(() => {
      const event = new Event('midimessage');
      Object.defineProperty(event, 'data', { value: new Uint8Array(encodePacket(request.id, 0x41, 1, new Uint8Array())) });
      input.dispatchEvent(event);
    });
  };
  const client = new LiveMidiClient(input as unknown as MIDIInput, output as unknown as MIDIOutput);
  await assert.rejects(client.hello(), /out-of-order/);
  client.close();
});

test('timeouts reject and clear the pending request', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const input = new MockPort(), output = new MockPort();
  const client = new LiveMidiClient(input as unknown as MIDIInput, output as unknown as MIDIOutput);
  const rejected = assert.rejects(client.hello(), /timed out/);
  t.mock.timers.tick(8000);
  await rejected;
  client.close();
});

test('ordered storage progress extends the idle timeout without completing the request', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const input = new MockPort(), output = new MockPort();
  let id = 0;
  output.send = (bytes) => { id = decodePacket(new Uint8Array(bytes))!.id; };
  const client = new LiveMidiClient(input as unknown as MIDIInput, output as unknown as MIDIOutput);
  let finished = false;
  const request = client.request(10).then((text) => { finished = true; return text; });
  const emit = (sequence: number, last: boolean, text = '') => {
    const event = new Event('midimessage');
    Object.defineProperty(event, 'data', { value: new Uint8Array(encodePacket(id, 10 | 64, sequence,
      new TextEncoder().encode(text), last)) });
    input.dispatchEvent(event);
  };
  for (let sequence = 0; sequence < 5; ++sequence) {
    t.mock.timers.tick(7000);
    emit(sequence, false);
    await Promise.resolve();
    assert.equal(finished, false);
  }
  emit(5, true, 'OK');
  assert.equal(await request, 'OK');
  client.close();
});

test('a stalled progress stream times out and leaves the connection usable', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const input = new MockPort(), output = new MockPort();
  let id = 0;
  output.send = (bytes) => { id = decodePacket(new Uint8Array(bytes))!.id; };
  const client = new LiveMidiClient(input as unknown as MIDIInput, output as unknown as MIDIOutput);
  const rejected = assert.rejects(client.request(10), /command 10 timed out/);
  const emit = (command: number, last: boolean) => {
    const event = new Event('midimessage');
    Object.defineProperty(event, 'data', { value: new Uint8Array(encodePacket(id, command | 64, 0,
      new Uint8Array(), last)) });
    input.dispatchEvent(event);
  };
  t.mock.timers.tick(7000);
  emit(10, false);
  t.mock.timers.tick(8000);
  await rejected;
  const recovery = client.request(9);
  emit(9, true);
  await recovery;
  client.close();
});

test('progress frames preserve multi-packet content and controller error responses', async () => {
  const input = new MockPort(), output = new MockPort();
  let shouldFail = false;
  output.send = (bytes) => {
    const packet = decodePacket(new Uint8Array(bytes))!;
    queueMicrotask(() => {
      const emit = (sequence: number, last: boolean, text: string, error = false) => {
        const event = new Event('midimessage');
        Object.defineProperty(event, 'data', { value: new Uint8Array(encodePacket(packet.id,
          error ? 127 : packet.command | 64, sequence, new TextEncoder().encode(text), last)) });
        input.dispatchEvent(event);
      };
      emit(0, false, '');
      if (shouldFail) emit(1, true, 'SD recovery failed; preserve journals', true);
      else {
        emit(1, false, '{"Name":"');
        emit(2, false, '');
        emit(3, true, 'Lead"}');
      }
    });
  };
  const client = new LiveMidiClient(input as unknown as MIDIInput, output as unknown as MIDIOutput);
  assert.equal((await client.read('1.json')).text, '{"Name":"Lead"}');
  shouldFail = true;
  await assert.rejects(client.request(9), /SD recovery failed/);
  client.close();
});

test('a timed-out set begin is aborted even when its acknowledgement was lost', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const input = new MockPort(), output = new MockPort();
  const commands: number[] = [];
  output.send = (bytes) => {
    const packet = decodePacket(new Uint8Array(bytes))!;
    commands.push(packet.command);
    if (packet.command !== 9) return;
    const event = new Event('midimessage');
    Object.defineProperty(event, 'data', { value: new Uint8Array(encodePacket(packet.id, 9 | 64, 0,
      new TextEncoder().encode('OK'))) });
    input.dispatchEvent(event);
  };
  const client = new LiveMidiClient(input as unknown as MIDIInput, output as unknown as MIDIOutput);
  client.supportsPresetSets = true;
  const rejected = assert.rejects(client.saveSet(['{}'], 123), /command 10 timed out.*Reload/);
  t.mock.timers.tick(8000);
  await rejected;
  assert.deepEqual(commands, [10, 9]);
  client.close();
});

test('unrelated MIDI and wrong response IDs are ignored; concurrent requests are rejected', async () => {
  const input = new MockPort(), output = new MockPort();
  const emit = (data: number[]) => {
    const event = new Event('midimessage');
    Object.defineProperty(event, 'data', { value: new Uint8Array(data) });
    input.dispatchEvent(event);
  };
  let packet: ReturnType<typeof decodePacket> = null;
  output.send = (bytes) => { packet = decodePacket(new Uint8Array(bytes)); };
  const client = new LiveMidiClient(input as unknown as MIDIInput, output as unknown as MIDIOutput);
  const read = client.read('1.json');
  await assert.rejects(client.list(), /still running/);
  emit([0x90, 60, 127]);
  emit(encodePacket(99, 0x43, 0, new Uint8Array()));
  emit(encodePacket(packet!.id, 0x43, 0, new TextEncoder().encode('{}')));
  assert.equal((await read).text, '{}');
  client.close();
});

function setController() {
  const input = new MockPort(), output = new MockPort();
  let files = [{ name: 'Lead.json', text: '{"Name":"Lead"}' }, { name: 'Rhythm.json', text: '{"Name":"Rhythm"}' }];
  let revision = 123;
  let staged: string[] | null = null;
  let upload: number[] = [];
  let failCommit = false;
  let mutateDuringRead = false;
  const commands: number[] = [];
  output.send = (data) => {
    const packet = decodePacket(new Uint8Array(data))!;
    commands.push(packet.command);
    const request = packet.bytes.length && packet.command !== 5 ? JSON.parse(new TextDecoder().decode(packet.bytes)) : {};
    let response = 'OK', error = false;
    switch (packet.command) {
      case 1: response = '{"product":"Synapse","protocol":1,"maxBytes":8192,"presetSets":true}'; break;
      case 2: response = JSON.stringify({ presets: files.map((file) => file.name), midiFiles: ['song.mid'] }); break;
      case 3:
        response = files.find((file) => file.name === request.name)!.text;
        if (mutateDuringRead) ++revision;
        break;
      case 12: response = JSON.stringify({ revision }); break;
      case 10:
        if (request.revision !== revision) { error = true; response = 'Preset set changed'; }
        else staged = [];
        break;
      case 4: upload = []; break;
      case 5: upload.push(...packet.bytes); break;
      case 6: staged!.push(new TextDecoder().decode(new Uint8Array(upload))); break;
      case 11:
        if (failCommit) { error = true; response = 'SD commit failed'; }
        else { files = staged!.map((text, index) => ({ name: `${index + 1}.json`, text })); revision = presetSetRevision(files); }
        break;
      case 9: staged = null; break;
    }
    const bytes = new TextEncoder().encode(response);
    queueMicrotask(() => {
      for (let offset = 0; offset < bytes.length; offset += LIVE_CHUNK_BYTES) {
        const event = new Event('midimessage');
        Object.defineProperty(event, 'data', { value: new Uint8Array(encodePacket(packet.id, error ? 127 : packet.command | 64,
          offset / LIVE_CHUNK_BYTES, bytes.subarray(offset, offset + LIVE_CHUNK_BYTES), offset + LIVE_CHUNK_BYTES >= bytes.length)) });
        input.dispatchEvent(event);
      }
    });
  };
  const client = new LiveMidiClient(input as unknown as MIDIInput, output as unknown as MIDIOutput);
  return { client, commands, get files() { return files; }, set failCommit(value: boolean) { failCommit = value; }, set mutateDuringRead(value: boolean) { mutateDuringRead = value; } };
}

test('full USB set loads consistently and saves reordered, duplicated and added presets numerically', async () => {
  assert.equal(presetSetRevision([{ name: '1.json', text: '{"Name":"Original"}' }, { name: '2.json', text: '{"Name":"Original"}' }]), 3620011878);
  const mock = setController();
  await mock.client.hello();
  const set = await mock.client.readSet();
  assert.deepEqual(set.files.map((file) => file.name), ['Lead.json', 'Rhythm.json']);
  const texts = [set.files[1].text, set.files[0].text, '{"Name":"New 🎸"}'];
  assert.equal(await mock.client.saveSet(texts, set.revision),
    presetSetRevision(texts.map((text, index) => ({ name: `${index + 1}.json`, text }))));
  assert.deepEqual(mock.files, texts.map((text, index) => ({ name: `${index + 1}.json`, text })));
  assert.ok(!mock.commands.includes(7));
  assert.deepEqual((await mock.client.readSet()).files, mock.files);
  mock.client.close();
});

test('full USB set supports deleting all presets and adding to an empty controller', async () => {
  const mock = setController();
  await mock.client.hello();
  const revision = await mock.client.saveSet([], 123);
  assert.deepEqual(mock.files, []);
  await mock.client.saveSet(['{"Name":"First"}'], revision);
  assert.equal(mock.files[0].name, '1.json');
  mock.client.close();
});

test('full USB set detects stale snapshots and aborts failed commits without losing edits', async () => {
  const mock = setController();
  await mock.client.hello();
  await assert.rejects(mock.client.saveSet([], 0), /changed/);
  mock.failCommit = true;
  await assert.rejects(mock.client.saveSet(['{}'], 123), /Reload the controller set/);
  assert.equal(mock.commands.at(-1), 9);
  assert.equal(mock.files[0].name, 'Lead.json');
  mock.client.close();
});

test('full USB loading rejects a changing set and saving enforces limits before beginning', async () => {
  const mock = setController();
  await mock.client.hello();
  mock.mutateDuringRead = true;
  await assert.rejects(mock.client.readSet(), /changed while loading/);
  await assert.rejects(mock.client.saveSet(Array(151).fill('{}'), 123), /150/);
  await assert.rejects(mock.client.saveSet(['x'.repeat(8193)], 123), /8192/);
  assert.ok(!mock.commands.includes(10));
  mock.client.close();
});

test('full USB management requires explicit firmware capability', async () => {
  const mock = controller();
  await mock.client.hello();
  await assert.rejects(mock.client.readSet(), /Update controller firmware/);
  await assert.rejects(mock.client.saveSet([], 0), /Update controller firmware/);
  mock.client.close();
});
