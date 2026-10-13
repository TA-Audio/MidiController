import { useState } from 'react';
import type { Preset, SwitchKey } from './preset';
import { DEFAULT_CHANNEL, LCD_COLUMNS, SWITCH_KEYS, lcdSwitchRow, newFileInfo, parsePreset, serializePreset, validatePreset } from './preset';
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

function SwitchCard({ k, preset, update, channel }: { k: SwitchKey; preset: Preset; update: (p: Preset) => void; channel: number }) {
  const sw = preset[k];
  const set = (patch: Partial<typeof sw>) => update({ ...preset, [k]: { ...sw, ...patch } });
  const fileMode = k === 'Switch1' && preset.FileInfo !== null;
  return (
    <section className="card">
      <h3>{k.replace('Switch', 'Switch ')}</h3>
      {fileMode && <p className="hint">A MIDI file is assigned, so this switch acts as Play / Stop and shows “Play” on the LCD.</p>}
      <div className="row">
        <TextField label="Label" value={sw.Name} onChange={(v) => set({ Name: v })} maxHint={6} />
        <Checkbox label="Toggle" title="Alternate between on (127) and off (0) with each press" checked={sw.Toggle} onChange={(v) => set({ Toggle: v })} />
      </div>
      <PcList title="Program Change" list={sw.PC} onChange={(PC) => set({ PC })} defaultChannel={channel} />
      <CcList
        title="Control Change"
        list={sw.CC}
        onChange={(CC) => set({ CC })}
        defaultChannel={channel}
        valueHint={sw.Toggle ? 'Toggle mode: values are overridden to 127 (on) / 0 (off).' : undefined}
      />
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
