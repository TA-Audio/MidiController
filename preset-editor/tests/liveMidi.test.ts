import test from 'node:test';
import assert from 'node:assert/strict';
import { decodePacket, encodePacket, LIVE_CHUNK_BYTES, LiveMidiClient, presetRevision } from '../src/liveMidi.ts';

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
