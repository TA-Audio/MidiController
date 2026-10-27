import assert from 'node:assert/strict';
import test from 'node:test';
import { contentOf, entryFromFile, isDirty, loadEntries, moveEntry, numberEntries, obsoleteFileNames } from '../src/entries.ts';
import { newPreset, serializePreset } from '../src/preset.ts';

const file = (name: string, displayName: string) => ({ name, text: JSON.stringify({ Name: displayName }) });

test('loading sorts existing filenames then numbers every preset without changing display names', () => {
  const entries = loadEntries([file('12.json', 'Twelve'), file('2.json', 'Two'), file('99 song.json', 'Song')]);
  assert.deepEqual(entries.map((e) => e.fileName), ['1.json', '2.json', '3.json']);
  assert.deepEqual(entries.map((e) => e.preset?.Name), ['Two', 'Twelve', 'Song']);
  assert.deepEqual(entries.map((e) => e.diskName), ['2.json', '12.json', '99 song.json']);
  assert.ok(entries.every(isDirty));
});

test('numeric lists remain numeric beyond 9; already numbered files are initially clean', () => {
  const entries = loadEntries(Array.from({ length: 15 }, (_, i) => file(`${15 - i}.json`, `Song ${15 - i}`)));
  assert.deepEqual(entries.map((e) => e.fileName), Array.from({ length: 15 }, (_, i) => `${i + 1}.json`));
  assert.ok(entries.every((e) => !isDirty(e)));
});

test('moving in both directions preserves IDs, content, disk baselines, and unsaved order', () => {
  const entries = loadEntries([file('1.json', 'A'), file('2.json', 'B'), file('3.json', 'C')]);
  const selected = entries[0].id;
  const moved = moveEntry(entries, selected, 2);
  assert.deepEqual(moved.map((e) => e.preset?.Name), ['B', 'C', 'A']);
  assert.equal(moved[2].id, selected);
  assert.equal(moved[2].diskName, '1.json');
  assert.equal(contentOf(moved[2]), contentOf(entries[0]));
  assert.ok(moved.every(isDirty));
  const restored = moveEntry(moved, selected, 0);
  assert.deepEqual(restored.map((e) => e.id), entries.map((e) => e.id));
  assert.ok(restored.every((e) => !isDirty(e)));
  assert.equal(moveEntry(entries, 'unknown', 0), entries);
  assert.equal(moveEntry(entries, selected, -1), entries);
  assert.equal(moveEntry(entries, selected, 3), entries);
});

test('deleting closes gaps; adding and duplicating append with generated numbers', () => {
  const entries = loadEntries([file('1.json', 'A'), file('2.json', 'B'), file('3.json', 'C')]);
  const remaining = numberEntries(entries.filter((_, i) => i !== 1));
  const added = entryFromFile('new.json', serializePreset(newPreset()));
  added.diskName = null;
  added.baseline = null;
  const appended = numberEntries([...remaining, added]);
  assert.deepEqual(appended.map((e) => e.fileName), ['1.json', '2.json', '3.json']);
  assert.equal(appended[1].preset?.Name, 'C');
  assert.ok(isDirty(appended[2]));
});

test('reverting content does not change the new order or filenames', () => {
  const entries = loadEntries([file('1.json', 'A'), file('2.json', 'B')]);
  const moved = moveEntry(entries, entries[0].id, 1);
  const changed = { ...moved[1], preset: { ...moved[1].preset!, Name: 'Changed' } };
  const original = entryFromFile(changed.diskName!, changed.baseline!);
  const reverted = { ...changed, preset: original.preset, rawText: original.rawText, parseError: original.parseError };
  assert.equal(reverted.fileName, '2.json');
  assert.equal(reverted.preset?.Name, 'A');
  assert.ok(isDirty(reverted));
});

test('invalid JSON is retained verbatim during reordering and numbered saving', () => {
  const entries = loadEntries([{ name: 'bad.json', text: '{broken' }, file('song.json', 'Song')]);
  const moved = moveEntry(entries, entries[0].id, 1);
  assert.equal(contentOf(moved[1]), '{broken');
  assert.ok(moved[1].parseError);
  assert.deepEqual(obsoleteFileNames(moved, ['deleted.json']), ['deleted.json', 'song.json', 'bad.json']);
});

test('obsolete filenames exclude live numbered targets even during swaps and deletions', () => {
  const entries = loadEntries([file('1.json', 'A'), file('2.json', 'B'), file('named.json', 'C')]);
  const reordered = moveEntry(entries, entries[0].id, 1);
  assert.deepEqual(obsoleteFileNames(reordered, ['1.json', 'named.json', 'old.json']), ['named.json', 'old.json']);
  assert.deepEqual(obsoleteFileNames([], ['1.json', '2.json']), ['1.json', '2.json']);
});
