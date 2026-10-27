import assert from 'node:assert/strict';
import test from 'node:test';
import { contentOf, loadEntries, moveEntry, obsoleteFileNames } from '../src/entries.ts';
import { createPresetZip, readDirectory, saveNumberedFiles } from '../src/folder.ts';
import JSZip from 'jszip';

function folderMock(initial: Record<string, string>, caseInsensitive = false) {
  const contents = new Map(Object.entries(initial));
  const operations: string[] = [];
  let failWrite: string | null = null;
  let failDelete: string | null = null;
  let permission: PermissionState = 'granted';
  const keyOf = (name: string) => caseInsensitive ? [...contents.keys()].find((key) => key.toLowerCase() === name.toLowerCase()) ?? name : name;
  const handle = {
    name: 'presets',
    queryPermission: async () => permission,
    requestPermission: async () => permission,
    async *values() {
      for (const name of contents.keys()) yield await handle.getFileHandle(name);
    },
    async getFileHandle(name: string, options?: { create?: boolean }) {
      const key = keyOf(name);
      if (!contents.has(key)) {
        if (!options?.create) throw new DOMException('Missing file', 'NotFoundError');
        contents.set(key, '');
      }
      return {
        kind: 'file',
        name: key,
        getFile: async () => new File([contents.get(key)!], key),
        isSameEntry: async (other: FileSystemFileHandle) => key === other.name,
        createWritable: async () => {
          let pending = '';
          return {
            write: async (text: string) => {
              operations.push(`write:${name}`);
              if (name === failWrite) throw new Error(`write failed: ${name}`);
              pending = text;
            },
            close: async () => { contents.set(key, pending); },
          };
        },
      } as FileSystemFileHandle;
    },
    async removeEntry(name: string) {
      operations.push(`delete:${name}`);
      if (name === failDelete) throw new Error(`delete failed: ${name}`);
      if (!contents.delete(keyOf(name))) throw new DOMException('Missing file', 'NotFoundError');
    },
  };
  return {
    handle: handle as FileSystemDirectoryHandle, contents, operations,
    failWriting: (name: string | null) => { failWrite = name; },
    failDeleting: (name: string | null) => { failDelete = name; },
    denyPermission: () => { permission = 'denied'; },
  };
}

test('folder round-trip saves exact numerical order, swaps safely, and preserves names and MIDI', async () => {
  const folder = folderMock({
    '1.json': '{"Name":"First"}',
    '10.json': '{"Name":"Last"}',
    'song.json': '{"Name":"Named"}',
    'song.mid': 'MIDI bytes',
    'notes.txt': 'Do not change',
  });
  const source = await readDirectory(folder.handle);
  const loaded = loadEntries(source.presetFiles);
  const reordered = moveEntry(loaded, loaded[2].id, 0);
  await saveNumberedFiles(folder.handle, reordered.map((e) => ({ name: e.fileName, text: contentOf(e) })), obsoleteFileNames(reordered, []));
  assert.deepEqual([...folder.contents.keys()].filter((n) => n.endsWith('.json')).sort(), ['1.json', '2.json', '3.json']);
  const reloaded = loadEntries((await readDirectory(folder.handle)).presetFiles);
  assert.deepEqual(reloaded.map((e) => e.preset?.Name), ['Named', 'First', 'Last']);
  assert.equal(folder.contents.get('song.mid'), 'MIDI bytes');
  assert.equal(folder.contents.get('notes.txt'), 'Do not change');
  assert.ok(folder.operations.indexOf('delete:10.json') > folder.operations.indexOf('write:3.json'));
});

test('numbered filename swaps write both destinations without deleting either', async () => {
  const folder = folderMock({ '1.json': 'A', '2.json': 'B' });
  await saveNumberedFiles(folder.handle, [{ name: '1.json', text: 'B' }, { name: '2.json', text: 'A' }], ['1.json', '2.json']);
  assert.equal(folder.contents.get('1.json'), 'B');
  assert.equal(folder.contents.get('2.json'), 'A');
  assert.ok(!folder.operations.some((op) => op.startsWith('delete:')));
});

test('failed writes surface errors, do not clean old filenames, and can be retried', async () => {
  const folder = folderMock({ 'old.json': 'A', 'older.json': 'B' });
  const files = [{ name: '1.json', text: 'A' }, { name: '2.json', text: 'B' }];
  folder.failWriting('2.json');
  await assert.rejects(saveNumberedFiles(folder.handle, files, ['old.json', 'older.json']), /write failed/);
  assert.ok(folder.contents.has('old.json'));
  assert.ok(folder.contents.has('older.json'));
  assert.ok(!folder.operations.some((op) => op.startsWith('delete:')));
  folder.failWriting(null);
  await saveNumberedFiles(folder.handle, files, ['old.json', 'older.json']);
  assert.deepEqual([...folder.contents], files.map((f) => [f.name, f.text]));
});

test('failed deletion surfaces errors and repeated cleanup tolerates missing files', async () => {
  const folder = folderMock({ 'old.json': 'A', 'older.json': 'B' });
  const files = [{ name: '1.json', text: 'A' }];
  folder.failDeleting('older.json');
  await assert.rejects(saveNumberedFiles(folder.handle, files, ['old.json', 'older.json']), /delete failed/);
  folder.failDeleting(null);
  await saveNumberedFiles(folder.handle, files, ['old.json', 'older.json']);
  assert.deepEqual([...folder.contents], [['1.json', 'A']]);
});

test('case-only obsolete names do not delete destinations on FAT, but are removed on case-sensitive storage', async () => {
  for (const insensitive of [true, false]) {
    const folder = folderMock({ '1.JSON': 'old' }, insensitive);
    await saveNumberedFiles(folder.handle, [{ name: '1.json', text: 'new' }], ['1.JSON']);
    assert.equal(folder.contents.size, 1);
    assert.equal([...folder.contents.values()][0], 'new');
  }
});

test('denied write permission surfaces before any mutation', async () => {
  const folder = folderMock({ 'old.json': 'A' });
  folder.denyPermission();
  await assert.rejects(saveNumberedFiles(folder.handle, [{ name: '1.json', text: 'B' }], ['old.json']), /denied/);
  assert.deepEqual([...folder.contents], [['old.json', 'A']]);
  assert.deepEqual(folder.operations, []);
});

test('deleting all presets removes only known preset files', async () => {
  const folder = folderMock({ '1.json': 'A', 'song.mid': 'MIDI' });
  await saveNumberedFiles(folder.handle, [], ['1.json']);
  assert.deepEqual([...folder.contents], [['song.mid', 'MIDI']]);
});

test('ZIP contains only sequential preset filenames and preserves names and invalid JSON', async () => {
  const entries = loadEntries([{ name: 'song.json', text: '{"Name":"Song"}' }, { name: 'bad.json', text: '{invalid' }]);
  const files = entries.map((e) => ({ name: e.fileName, text: contentOf(e) }));
  const zip = await JSZip.loadAsync(await createPresetZip(files).generateAsync({ type: 'uint8array' }));
  assert.deepEqual(Object.keys(zip.files), ['1.json', '2.json']);
  assert.equal(await zip.file('1.json')!.async('string'), '{invalid');
  assert.equal(JSON.parse(await zip.file('2.json')!.async('string')).Name, 'Song');
  assert.deepEqual(obsoleteFileNames(entries, []), ['bad.json', 'song.json']);
});
