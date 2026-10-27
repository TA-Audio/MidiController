import { useState } from 'react';
import type { Preset, SwitchAction, SwitchKey } from './preset';
import { DEFAULT_CHANNEL, GESTURE_KEYS, LCD_COLUMNS, SWITCH_KEYS, emptyAction, lcdSwitchRow, newFileInfo, parsePreset, serializePreset, validatePreset } from './preset';
import { CcList, Checkbox, NumberField, PcList, TextField } from './fields';

interface Props {
  preset: Preset;
  onChange: (p: Preset) => void;
  midiFiles: string[];
}

function guessChannel(p: Preset): number {
  const all = [
    ...p.OnLoad.PC,
    ...p.OnLoad.CC,
    ...p.OnExit.CC,
    ...SWITCH_KEYS.flatMap((k) => [...p[k].PC, ...p[k].CC]),
  ];
  return all[0]?.Channel ?? p.FileInfo?.Channel ?? DEFAULT_CHANNEL;
}

function LcdPreview({ preset }: { preset: Preset }) {
  const rows = [
    preset.Name.slice(0, LCD_COLUMNS).padEnd(LCD_COLUMNS),
    ''.padEnd(LCD_COLUMNS),
    ''.padEnd(LCD_COLUMNS),
    lcdSwitchRow(preset),
  ];
  return (
    <div className="lcd" aria-label="LCD preview">
      {rows.map((r, i) => (
        <div key={i}>{r}</div>
      ))}
    </div>
  );
}

function ActionEditor({ title, action, onChange, channel }: { title: string; action: SwitchAction; onChange: (a: SwitchAction) => void; channel: number }) {
  return (
    <section className="card">
      <h4>{title}</h4>
      <TextField label="Action label" value={action.Name} onChange={(Name) => onChange({ ...action, Name })} maxHint={6} />
      <PcList title="Program Change" list={action.PC} onChange={(PC) => onChange({ ...action, PC })} defaultChannel={channel} />
      <CcList title="Control Change" list={action.CC} onChange={(CC) => onChange({ ...action, CC })} defaultChannel={channel} />
    </section>
  );
}

function SwitchCard({ k, preset, update, channel }: { k: SwitchKey; preset: Preset; update: (p: Preset) => void; channel: number }) {
  const sw = preset[k];
  const set = (patch: Partial<typeof sw>) => update({ ...preset, [k]: { ...sw, ...patch } });
  const fileMode = k === 'Switch1' && preset.FileInfo !== null;
  return (
    <section className="card">
      <h3>{k.replace('Switch', 'Switch ')}</h3>
      {fileMode && <p className="hint">A MIDI file is assigned, so this switch acts as Play / Stop and shows “Play” on the LCD.</p>}
      <fieldset className="preset-fields" disabled={fileMode}>
      <div className="row">
        <TextField label="Label" value={sw.Name} onChange={(v) => set({ Name: v })} maxHint={6} />
        <Checkbox label="Toggle" title="Alternate between On and Off states" checked={sw.Toggle} onChange={(v) => set({
          Toggle: v, TapTempo: false, Sequence: undefined,
          On: v ? sw.On : undefined, Off: v ? sw.Off : undefined,
        })} />
      </div>
      <PcList title="Program Change" list={sw.PC} onChange={(PC) => set({ PC })} defaultChannel={channel} />
      <CcList
        title="Control Change"
        list={sw.CC}
        onChange={(CC) => set({ CC })}
        defaultChannel={channel}
        valueHint={sw.Toggle ? 'Toggle mode: values are overridden to 127 (on) / 0 (off).' : undefined}
      />
      <div className="row">
        <Checkbox label="Tap tempo" checked={!!sw.TapTempo} onChange={(v) => set({ TapTempo: v, Toggle: v ? false : sw.Toggle, Sequence: v ? undefined : sw.Sequence, On: v ? undefined : sw.On, Off: v ? undefined : sw.Off })} />
        <Checkbox label="Explicit On/Off actions" checked={!!sw.On || !!sw.Off}
          onChange={(v) => set({ On: v ? emptyAction() : undefined, Off: v ? emptyAction() : undefined, Toggle: v || sw.Toggle, TapTempo: false, Sequence: undefined })} />
        <Checkbox label="Sequence mode" checked={sw.Sequence !== undefined}
          onChange={(v) => set({ Sequence: v ? [emptyAction()] : undefined, Toggle: false, TapTempo: false, On: undefined, Off: undefined })} />
      </div>
      {(sw.On || sw.Off || sw.Sequence || sw.TapTempo) && <p className="hint">The primary action uses this mode instead of the legacy PC/CC lists above. Tap tempo uses 2–4 taps, 30–300 BPM, and is temporary. Sequences reset when changing presets.</p>}
      {sw.On && <ActionEditor title="On state" action={sw.On} channel={channel} onChange={(On) => set({ On })} />}
      {sw.Off && <ActionEditor title="Off state" action={sw.Off} channel={channel} onChange={(Off) => set({ Off })} />}
      {sw.Sequence?.map((action, i) => (
        <div key={i}>
          <ActionEditor title={`Step ${i + 1}`} action={action} channel={channel} onChange={(next) => set({ Sequence: sw.Sequence!.map((a, n) => n === i ? next : a) })} />
          <button type="button" className="small" disabled={i === 0} onClick={() => {
            const steps = [...sw.Sequence!];
            [steps[i - 1], steps[i]] = [steps[i], steps[i - 1]];
            set({ Sequence: steps });
          }}>Move step up</button>
          <button type="button" className="small danger" disabled={sw.Sequence!.length <= 1} onClick={() => set({ Sequence: sw.Sequence!.filter((_, n) => n !== i) })}>Remove step</button>
        </div>
      ))}
      {sw.Sequence && <button type="button" className="small" disabled={sw.Sequence.length >= 16} onClick={() => set({ Sequence: [...sw.Sequence!, emptyAction()] })}>Add step</button>}
      <h4>Switch gestures</h4>
      <p className="hint">Without gestures the primary action fires immediately on press. With gestures it fires on release; double-tap waits 300 ms, and a 600 ms long press replaces the primary action. Press/Release actions always run on their respective edges. Use empty primary lists for a momentary-only switch.</p>
      {GESTURE_KEYS.map((gesture) => (
        <div key={gesture}>
          <Checkbox label={gesture} checked={sw.Gestures?.[gesture] !== undefined} onChange={(enabled) => {
            const gestures = { ...sw.Gestures };
            if (enabled) gestures[gesture] = emptyAction();
            else delete gestures[gesture];
            set({ Gestures: Object.keys(gestures).length ? gestures : undefined });
          }} />
          {sw.Gestures?.[gesture] && <ActionEditor title={gesture} action={sw.Gestures[gesture]!} channel={channel} onChange={(action) => set({ Gestures: { ...sw.Gestures, [gesture]: action } })} />}
        </div>
      ))}
      </fieldset>
    </section>
  );
}

function RawJson({ preset, onChange }: Props) {
  const serialized = serializePreset(preset);
  const [text, setText] = useState(serialized);
  const [error, setError] = useState<string | null>(null);
  const [prevSerialized, setPrevSerialized] = useState(serialized);
  if (prevSerialized !== serialized) {
    setPrevSerialized(serialized);
    setText(serialized);
    setError(null);
  }
  return (
    <section className="card">
      <h3>Raw JSON</h3>
      <textarea className="code" spellCheck={false} value={text} onChange={(e) => setText(e.target.value)} rows={Math.min(40, text.split('\n').length + 1)} />
      {error && <p className="error">{error}</p>}
      <div className="row">
        <button
          type="button"
          disabled={text === serialized}
          onClick={() => {
            try {
              onChange(parsePreset(text));
              setError(null);
            } catch (e) {
              setError((e as Error).message);
            }
          }}
        >
          Apply JSON
        </button>
        <button type="button" className="secondary" disabled={text === serialized} onClick={() => setText(serialized)}>
          Revert
        </button>
      </div>
    </section>
  );
}

export function PresetEditor({ preset, onChange, midiFiles }: Props) {
  const [tab, setTab] = useState<'form' | 'json'>('form');
  const issues = validatePreset(preset);
  const channel = guessChannel(preset);
  const f = preset.FileInfo;
  const setFile = (patch: Partial<NonNullable<typeof f>>) => f && onChange({ ...preset, FileInfo: { ...f, ...patch } });

  return (
    <div className="editor">
      <div className="editor-top">
        <LcdPreview preset={preset} />
        <div className="issues">
          {issues.length === 0 ? (
            <p className="ok">✓ No problems found</p>
          ) : (
            <ul>
              {issues.map((i, n) => (
                <li key={n} className={i.level}>
                  {i.level === 'error' ? '✕' : '⚠'} {i.message}
                </li>
              ))}
            </ul>
          )}
        </div>
      </div>

      <div className="tabs">
        <button type="button" className={tab === 'form' ? 'active' : ''} onClick={() => setTab('form')}>
          Editor
        </button>
        <button type="button" className={tab === 'json' ? 'active' : ''} onClick={() => setTab('json')}>
          JSON
        </button>
      </div>

      {tab === 'json' ? (
        <RawJson preset={preset} onChange={onChange} midiFiles={midiFiles} />
      ) : (
        <>
          <section className="card">
            <h3>General</h3>
            <div className="row">
              <TextField label="Preset name" value={preset.Name} onChange={(v) => onChange({ ...preset, Name: v })} maxHint={LCD_COLUMNS} />
            </div>
            <div className="row">
              <NumberField
                label="BPM"
                optional
                placeholder="none"
                min={1}
                max={65535}
                value={preset.BPM}
                onChange={(v) => onChange({ ...preset, BPM: v })}
                width="8rem"
              />
              <Checkbox
                label="Send MIDI Clock"
                title="Send MIDI Start and 24-PPQN Clock over DIN when no MIDI file is playing"
                checked={preset.SendMidiClock}
                onChange={(v) => onChange({ ...preset, SendMidiClock: v, BPM: v && preset.BPM === null ? 120 : preset.BPM })}
              />
            </div>
          </section>

          <section className="card">
            <div className="msg-list-header">
              <h3>MIDI File Playback</h3>
              <Checkbox label="Enabled" checked={f !== null} onChange={(v) => onChange({ ...preset, FileInfo: v ? newFileInfo() : null })} />
            </div>
            {f ? (
              <>
                <div className="row">
                  <TextField label="File name" value={f.FileName} onChange={(v) => setFile({ FileName: v })} list="midi-files" placeholder="song.mid" />
                  <datalist id="midi-files">
                    {midiFiles.map((m) => (
                      <option key={m} value={m} />
                    ))}
                  </datalist>
                </div>
                <div className="row">
                  <NumberField label="BPM override" optional placeholder="from file" min={1} max={65535} value={f.BPM} onChange={(v) => setFile({ BPM: v })} width="8rem" />
                  <NumberField label="Output channel" min={1} max={16} value={f.Channel} onChange={(v) => setFile({ Channel: v ?? DEFAULT_CHANNEL })} width="8rem" />
                  <Checkbox
                    label="Send MIDI Clock"
                    title="Send MIDI Start/Clock/Stop over DIN while the file plays"
                    checked={f.SendMidiClock}
                    onChange={(v) => setFile({ SendMidiClock: v })}
                  />
                </div>
                <CcList title="Stop CC (sent when playback stops)" list={f.StopCC} onChange={(StopCC) => setFile({ StopCC })} defaultChannel={channel} />
              </>
            ) : (
              <p className="muted">Assign a .mid file to make Switch 1 a Play / Stop control.</p>
            )}
          </section>

          <section className="card">
            <h3>On Load</h3>
            <PcList title="Program Change" list={preset.OnLoad.PC} onChange={(PC) => onChange({ ...preset, OnLoad: { ...preset.OnLoad, PC } })} defaultChannel={channel} />
            <CcList title="Control Change" list={preset.OnLoad.CC} onChange={(CC) => onChange({ ...preset, OnLoad: { ...preset.OnLoad, CC } })} defaultChannel={channel} />
          </section>

          <section className="card">
            <h3>On Exit</h3>
            <p className="hint">Control Change messages sent when navigating away from this preset.</p>
            <CcList title="Control Change" list={preset.OnExit.CC} onChange={(CC) => onChange({ ...preset, OnExit: { CC } })} defaultChannel={channel} />
          </section>

          <div className="switches">
            {SWITCH_KEYS.map((k) => (
              <SwitchCard key={k} k={k} preset={preset} update={onChange} channel={channel} />
            ))}
          </div>

          {Object.keys(preset.extra).length > 0 && (
            <p className="muted">Additional fields preserved: {Object.keys(preset.extra).join(', ')}</p>
          )}
        </>
      )}
    </div>
  );
}
