import test from 'node:test';
import assert from 'node:assert/strict';
import { emptyAction, newPreset, parsePreset, serializePreset, validatePreset } from '../src/preset.ts';

test('legacy presets retain their shape and immediate press settings', () => {
  const p = parsePreset('{"Name":"Legacy","Switch1":{"Name":"FX","Toggle":true,"CC":[{"CC":1,"Value":99,"Channel":2,"USB":true}]}}');
  const out = JSON.parse(serializePreset(p));
  assert.equal(out.Switch1.Toggle, true);
  assert.equal(out.Switch1.CC[0].Value, 99);
  for (const key of ['On', 'Off', 'Gestures', 'Sequence', 'TapTempo']) assert.ok(!(key in out.Switch1));
  assert.ok(!validatePreset(p).some((i) => i.level === 'error'));
});

test('explicit toggle labels and distinct PC/CC values survive a round trip', () => {
  const p = newPreset('States');
  p.Switch1.Toggle = true;
  p.Switch1.On = { Name: 'Lead', PC: [{ PC: 9, Channel: 2, USB: false, USBDevice: true }], CC: [] };
  p.Switch1.Off = { Name: 'Clean', PC: [], CC: [{ CC: 7, Value: 30, Channel: 2, USB: false }] };
  const parsed = parsePreset(serializePreset(p));
  assert.deepEqual(parsed, p);
  assert.equal(parsed.Switch1.Off!.CC[0].Value, 30);
  assert.ok(!validatePreset(parsed).some((i) => i.level === 'error'));
});

test('all gestures, sequences and tap tempo survive serialization', () => {
  const p = newPreset();
  p.Switch1.Gestures = { Press: emptyAction(), Release: emptyAction(), LongPress: emptyAction(), DoubleTap: emptyAction() };
  p.Switch2.Sequence = Array.from({ length: 16 }, (_, i) => ({ Name: `Step ${i}`, PC: [{ PC: i + 1, Channel: 1, USB: false }], CC: [] }));
  p.Switch3.TapTempo = true;
  assert.deepEqual(parsePreset(serializePreset(p)), p);
  assert.ok(!validatePreset(p).some((i) => i.level === 'error'));
});

test('invalid feature combinations, missing states and excessive sequences are rejected', () => {
  const p = newPreset();
  p.Switch1.Toggle = true;
  p.Switch1.TapTempo = true;
  p.Switch1.On = emptyAction();
  p.Switch2.Sequence = Array.from({ length: 17 }, emptyAction);
  const errors = validatePreset(p).filter((i) => i.level === 'error');
  assert.ok(errors.some((i) => i.message.includes('both On and Off')));
  assert.ok(errors.some((i) => i.message.includes('just one')));
  assert.ok(errors.some((i) => i.message.includes('1–16')));
});

test('new actions validate MIDI ranges and mutually exclusive USB routes', () => {
  const p = newPreset();
  p.Switch1.Gestures = { LongPress: { Name: '', PC: [{ PC: 129, Channel: 17, USB: true, USBDevice: true }], CC: [] } };
  assert.equal(validatePreset(p).filter((i) => i.level === 'error').length, 3);
});

test('malformed structured actions surface parse errors instead of disappearing', () => {
  for (const fragment of ['"Sequence":{}', '"Gestures":[]', '"On":false', '"Gestures":{"DoubleTap":2}', '"TapTempo":"yes"']) {
    assert.throws(() => parsePreset(`{"Switch1":{${fragment}}}`));
  }
});
