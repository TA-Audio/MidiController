import { useState } from 'react';
import type { CcMessage, PcMessage } from './preset';
import { DEFAULT_CHANNEL } from './preset';

interface NumberFieldProps {
  label: string;
  value: number | null;
  onChange: (v: number | null) => void;
  min?: number;
  max?: number;
  optional?: boolean;
  placeholder?: string;
  width?: string;
}

export function NumberField({ label, value, onChange, min, max, optional, placeholder, width }: NumberFieldProps) {
  const [text, setText] = useState(value === null ? '' : String(value));
  const [prevValue, setPrevValue] = useState(value);
  if (prevValue !== value) {
    setPrevValue(value);
    setText(value === null ? '' : String(value));
  }
  const n = text === '' ? null : Number(text);
  const invalid =
    (n === null && !optional) ||
    (n !== null && (!Number.isInteger(n) || (min !== undefined && n < min) || (max !== undefined && n > max)));
  return (
    <label className="field" style={width ? { width } : undefined}>
      <span>{label}</span>
      <input
        type="number"
        className={invalid ? 'invalid' : undefined}
        value={text}
        min={min}
        max={max}
        placeholder={placeholder}
        onChange={(e) => {
          setText(e.target.value);
          if (e.target.value === '') {
            if (optional) onChange(null);
          } else if (!Number.isNaN(Number(e.target.value))) {
            onChange(Number(e.target.value));
          }
        }}
        onBlur={() => setText(value === null ? '' : String(value))}
      />
    </label>
  );
}

export function TextField(props: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  maxHint?: number;
  list?: string;
  placeholder?: string;
}) {
  const over = props.maxHint !== undefined && props.value.length > props.maxHint;
  return (
    <label className="field grow">
      <span>
        {props.label}
        {props.maxHint !== undefined && (
          <em className={over ? 'over' : undefined}>
            {' '}
            {props.value.length}/{props.maxHint}
          </em>
        )}
      </span>
      <input
        type="text"
        value={props.value}
        list={props.list}
        placeholder={props.placeholder}
        onChange={(e) => props.onChange(e.target.value)}
      />
    </label>
  );
}

export function Checkbox(props: { label: string; checked: boolean; onChange: (v: boolean) => void; title?: string }) {
  return (
    <label className="checkbox" title={props.title}>
      <input type="checkbox" checked={props.checked} onChange={(e) => props.onChange(e.target.checked)} />
      {props.label}
    </label>
  );
}

function updateAt<T>(list: T[], i: number, patch: Partial<T>): T[] {
  return list.map((m, j) => (j === i ? { ...m, ...patch } : m));
}

function move<T>(list: T[], i: number, delta: number): T[] {
  const j = i + delta;
  if (j < 0 || j >= list.length) return list;
  const copy = [...list];
  [copy[i], copy[j]] = [copy[j], copy[i]];
  return copy;
}

function RowActions({ i, count, onMove, onRemove }: { i: number; count: number; onMove: (d: number) => void; onRemove: () => void }) {
  return (
    <div className="row-actions">
      <button type="button" className="icon" title="Move up" disabled={i === 0} onClick={() => onMove(-1)}>
        ↑
      </button>
      <button type="button" className="icon" title="Move down" disabled={i === count - 1} onClick={() => onMove(1)}>
        ↓
      </button>
      <button type="button" className="icon danger" title="Remove" onClick={onRemove}>
        ✕
      </button>
    </div>
  );
}

export function PcList({ title, list, onChange, defaultChannel }: { title: string; list: PcMessage[]; onChange: (l: PcMessage[]) => void; defaultChannel: number }) {
  return (
    <div className="msg-list">
      <div className="msg-list-header">
        <h4>{title}</h4>
        <button type="button" className="small" onClick={() => onChange([...list, { PC: 1, Channel: defaultChannel, USB: false }])}>
          + Program Change
        </button>
      </div>
      {list.length === 0 && <p className="muted">None</p>}
      {list.map((m, i) => (
        <div className="msg-row" key={i}>
          <NumberField label="Program (1–128)" value={m.PC} min={1} max={128} onChange={(v) => onChange(updateAt(list, i, { PC: v ?? 1 }))} />
          <NumberField label="Channel" value={m.Channel} min={1} max={16} onChange={(v) => onChange(updateAt(list, i, { Channel: v ?? DEFAULT_CHANNEL }))} />
          <Checkbox label="USB Host" title="Connected USB pedal" checked={m.USB} onChange={(v) => onChange(updateAt(list, i, { USB: v, USBDevice: false }))} />
          <Checkbox label="USB Computer" title="DAW via Teensy device USB port" checked={!!m.USBDevice} onChange={(v) => onChange(updateAt(list, i, { USBDevice: v, USB: false }))} />
          <RowActions i={i} count={list.length} onMove={(d) => onChange(move(list, i, d))} onRemove={() => onChange(list.filter((_, j) => j !== i))} />
        </div>
      ))}
    </div>
  );
}

export function CcList({
  title,
  list,
  onChange,
  defaultChannel,
  valueHint,
}: {
  title: string;
  list: CcMessage[];
  onChange: (l: CcMessage[]) => void;
  defaultChannel: number;
  valueHint?: string;
}) {
  return (
    <div className="msg-list">
      <div className="msg-list-header">
        <h4>{title}</h4>
        <button type="button" className="small" onClick={() => onChange([...list, { CC: 0, Value: 127, Channel: defaultChannel, USB: false }])}>
          + Control Change
        </button>
      </div>
      {valueHint && list.length > 0 && <p className="hint">{valueHint}</p>}
      {list.length === 0 && <p className="muted">None</p>}
      {list.map((m, i) => (
        <div className="msg-row" key={i}>
          <NumberField label="CC (0–127)" value={m.CC} min={0} max={127} onChange={(v) => onChange(updateAt(list, i, { CC: v ?? 0 }))} />
          <NumberField label="Value (0–127)" value={m.Value} min={0} max={127} onChange={(v) => onChange(updateAt(list, i, { Value: v ?? 0 }))} />
          <NumberField label="Channel" value={m.Channel} min={1} max={16} onChange={(v) => onChange(updateAt(list, i, { Channel: v ?? DEFAULT_CHANNEL }))} />
          <Checkbox label="USB Host" title="Connected USB pedal" checked={m.USB} onChange={(v) => onChange(updateAt(list, i, { USB: v, USBDevice: false }))} />
          <Checkbox label="USB Computer" title="DAW via Teensy device USB port" checked={!!m.USBDevice} onChange={(v) => onChange(updateAt(list, i, { USBDevice: v, USB: false }))} />
          <RowActions i={i} count={list.length} onMove={(d) => onChange(move(list, i, d))} onRemove={() => onChange(list.filter((_, j) => j !== i))} />
        </div>
      ))}
    </div>
  );
}
