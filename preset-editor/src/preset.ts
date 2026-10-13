export interface PcMessage {
  PC: number;
  Channel: number;
  USB: boolean;
}

export interface CcMessage {
  CC: number;
  Value: number;
  Channel: number;
  USB: boolean;
}

export interface SwitchConfig {
  Name: string;
  Toggle: boolean;
  PC: PcMessage[];
  CC: CcMessage[];
}

export interface FileInfo {
  FileName: string;
  BPM: number | null;
  Channel: number;
  SendMidiClock: boolean;
  StopCC: CcMessage[];
}

export interface Preset {
  Name: string;
  BPM: number | null;
  SendMidiClock: boolean;
  OnLoad: { PC: PcMessage[]; CC: CcMessage[] };
  Switch1: SwitchConfig;
  Switch2: SwitchConfig;
  Switch3: SwitchConfig;
  FileInfo: FileInfo | null;
  /** Unknown top-level keys preserved from the source file. */
  extra: Record<string, unknown>;
}

export const SWITCH_KEYS = ['Switch1', 'Switch2', 'Switch3'] as const;
export type SwitchKey = (typeof SWITCH_KEYS)[number];

export const LCD_COLUMNS = 20;
export const MAX_PRESETS = 150;
export const DEFAULT_CHANNEL = 1;

const KNOWN_KEYS = new Set(['Name', 'BPM', 'SendMidiClock', 'OnLoad', 'Switch1', 'Switch2', 'Switch3', 'FileInfo']);

type Json = Record<string, unknown>;

const isObj = (v: unknown): v is Json => typeof v === 'object' && v !== null && !Array.isArray(v);
const num = (v: unknown, fallback: number): number => (typeof v === 'number' && Number.isFinite(v) ? v : fallback);
const optNum = (v: unknown): number | null => (typeof v === 'number' && Number.isFinite(v) ? v : null);

function parsePcList(v: unknown): PcMessage[] {
  if (!Array.isArray(v)) return [];
  return v.filter(isObj).map((m) => ({
    PC: num(m.PC, 1),
    Channel: num(m.Channel, DEFAULT_CHANNEL),
    USB: m.USB === true,
  }));
}

function parseCcList(v: unknown): CcMessage[] {
  if (!Array.isArray(v)) return [];
  return v.filter(isObj).map((m) => ({
    CC: num(m.CC, 0),
    Value: num(m.Value, 127),
    Channel: num(m.Channel, DEFAULT_CHANNEL),
    USB: m.USB === true,
  }));
}

function parseSwitch(v: unknown): SwitchConfig {
  const s = isObj(v) ? v : {};
  return {
    Name: typeof s.Name === 'string' ? s.Name : '',
    Toggle: s.Toggle === true,
    PC: parsePcList(s.PC),
    CC: parseCcList(s.CC),
  };
}

function parseFileInfo(v: unknown): FileInfo | null {
  if (!isObj(v)) return null;
  return {
    FileName: typeof v.FileName === 'string' ? v.FileName : '',
    BPM: optNum(v.BPM),
    Channel: num(v.Channel, DEFAULT_CHANNEL),
    SendMidiClock: v.SendMidiClock === true,
    StopCC: parseCcList(v.StopCC),
  };
}

export function parsePreset(text: string): Preset {
  const raw: unknown = JSON.parse(text);
  if (!isObj(raw)) throw new Error('Preset file must contain a JSON object.');
  const onLoad = isObj(raw.OnLoad) ? raw.OnLoad : {};
  const extra: Json = {};
  for (const [k, v] of Object.entries(raw)) if (!KNOWN_KEYS.has(k)) extra[k] = v;
  return {
    Name: typeof raw.Name === 'string' ? raw.Name : '',
    BPM: optNum(raw.BPM),
    SendMidiClock: raw.SendMidiClock === true,
    OnLoad: { PC: parsePcList(onLoad.PC), CC: parseCcList(onLoad.CC) },
    Switch1: parseSwitch(raw.Switch1),
    Switch2: parseSwitch(raw.Switch2),
    Switch3: parseSwitch(raw.Switch3),
    FileInfo: parseFileInfo(raw.FileInfo),
    extra,
  };
}

const pcOut = (list: PcMessage[]) =>
  list.length === 0 ? null : list.map((m) => ({ PC: m.PC, Channel: m.Channel, ...(m.USB ? { USB: true } : {}) }));

const ccOut = (list: CcMessage[]) =>
  list.length === 0
    ? null
    : list.map((m) => ({ CC: m.CC, Value: m.Value, Channel: m.Channel, ...(m.USB ? { USB: true } : {}) }));

const switchOut = (s: SwitchConfig) => ({ Name: s.Name, Toggle: s.Toggle, CC: ccOut(s.CC), PC: pcOut(s.PC) });

export function serializePreset(p: Preset): string {
  const out: Json = { Name: p.Name };
  if (p.BPM !== null) out.BPM = p.BPM;
  if (p.SendMidiClock || p.BPM !== null) out.SendMidiClock = p.SendMidiClock;
  if (p.FileInfo) {
    const f = p.FileInfo;
    out.FileInfo = {
      FileName: f.FileName,
      ...(f.BPM !== null ? { BPM: f.BPM } : {}),
      Channel: f.Channel,
      SendMidiClock: f.SendMidiClock,
      StopCC: ccOut(f.StopCC),
    };
  }
  out.OnLoad = { PC: pcOut(p.OnLoad.PC), CC: ccOut(p.OnLoad.CC) };
  for (const k of SWITCH_KEYS) out[k] = switchOut(p[k]);
  Object.assign(out, p.extra);
  return JSON.stringify(out, null, 2) + '\n';
}

export const emptySwitch = (): SwitchConfig => ({ Name: '', Toggle: false, PC: [], CC: [] });

export const newPreset = (name = 'New Preset'): Preset => ({
  Name: name,
  BPM: null,
  SendMidiClock: false,
  OnLoad: { PC: [], CC: [] },
  Switch1: emptySwitch(),
  Switch2: emptySwitch(),
  Switch3: emptySwitch(),
  FileInfo: null,
  extra: {},
});

export const newFileInfo = (): FileInfo => ({
  FileName: '',
  BPM: null,
  Channel: DEFAULT_CHANNEL,
  SendMidiClock: false,
  StopCC: [],
});

export interface Issue {
  level: 'error' | 'warning';
  message: string;
}

const inRange = (v: number, min: number, max: number) => Number.isInteger(v) && v >= min && v <= max;

function checkPc(list: PcMessage[], where: string, issues: Issue[]) {
  list.forEach((m, i) => {
    if (!inRange(m.PC, 1, 128)) issues.push({ level: 'error', message: `${where} PC #${i + 1}: program must be 1–128.` });
    if (!inRange(m.Channel, 1, 16)) issues.push({ level: 'error', message: `${where} PC #${i + 1}: channel must be 1–16.` });
  });
}

function checkCc(list: CcMessage[], where: string, issues: Issue[]) {
  list.forEach((m, i) => {
    if (!inRange(m.CC, 0, 127)) issues.push({ level: 'error', message: `${where} CC #${i + 1}: controller must be 0–127.` });
    if (!inRange(m.Value, 0, 127)) issues.push({ level: 'error', message: `${where} CC #${i + 1}: value must be 0–127.` });
    if (!inRange(m.Channel, 1, 16)) issues.push({ level: 'error', message: `${where} CC #${i + 1}: channel must be 1–16.` });
  });
}

export function switchLabel(p: Preset, key: SwitchKey): string {
  return key === 'Switch1' && p.FileInfo ? 'Play' : p[key].Name;
}

export function validatePreset(p: Preset): Issue[] {
  const issues: Issue[] = [];
  if (!p.Name.trim()) issues.push({ level: 'warning', message: 'Preset name is empty.' });
  if (p.BPM !== null && !inRange(p.BPM, 1, 65535)) issues.push({ level: 'error', message: 'Preset BPM must be 1–65535.' });
  if (p.SendMidiClock && p.BPM === null) issues.push({ level: 'error', message: 'Preset BPM is required when Send MIDI Clock is enabled.' });

  checkPc(p.OnLoad.PC, 'On Load', issues);
  checkCc(p.OnLoad.CC, 'On Load', issues);
  for (const k of SWITCH_KEYS) {
    checkPc(p[k].PC, k, issues);
    checkCc(p[k].CC, k, issues);
  }

  const labelLen = SWITCH_KEYS.reduce((n, k) => n + switchLabel(p, k).length, 0);
  if (labelLen > LCD_COLUMNS)
    issues.push({ level: 'warning', message: `Switch labels total ${labelLen} characters; only ${LCD_COLUMNS} fit on the LCD.` });

  if (p.FileInfo) {
    const f = p.FileInfo;
    if (!f.FileName.trim()) issues.push({ level: 'error', message: 'MIDI file name is required.' });
    else if (!/\.midi?$/i.test(f.FileName)) issues.push({ level: 'warning', message: 'MIDI file name usually ends in .mid.' });
    if (f.BPM !== null && !inRange(f.BPM, 1, 65535)) issues.push({ level: 'error', message: 'MIDI file BPM must be 1–65535.' });
    if (!inRange(f.Channel, 1, 16)) issues.push({ level: 'error', message: 'MIDI file channel must be 1–16.' });
    checkCc(f.StopCC, 'Stop', issues);
    const sw1 = p.Switch1;
    if (sw1.PC.length || sw1.CC.length)
      issues.push({ level: 'warning', message: 'Switch1 messages are ignored while a MIDI file is assigned (Switch1 is Play/Stop).' });
  }
  return issues;
}

/** Numeric-aware sort to match the firmware's preset ordering. */
export function compareFileNames(a: string, b: string): number {
  const na = parseInt(a, 10);
  const nb = parseInt(b, 10);
  if (!Number.isNaN(na) && !Number.isNaN(nb) && na !== nb) return na - nb;
  return a.localeCompare(b, undefined, { numeric: true });
}

export function nextFileName(existing: string[]): string {
  const max = existing.reduce((m, f) => Math.max(m, parseInt(f, 10) || 0), 0);
  return `${max + 1}.json`;
}

export function lcdSwitchRow(p: Preset): string {
  const [a, b, c] = SWITCH_KEYS.map((k) => switchLabel(p, k));
  const total = a.length + b.length + c.length;
  const left = total < LCD_COLUMNS ? Math.floor((LCD_COLUMNS - total) / 2) : 0;
  const right = total < LCD_COLUMNS ? Math.floor((LCD_COLUMNS - total + 1) / 2) : 0;
  return (a + ' '.repeat(left) + b + ' '.repeat(right) + c).slice(0, LCD_COLUMNS).padEnd(LCD_COLUMNS);
}
