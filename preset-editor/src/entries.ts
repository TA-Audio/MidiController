import type { Preset } from './preset.ts';
import { compareFileNames, parsePreset, serializePreset } from './preset.ts';
import type { LoadedFile } from './folder.ts';

export interface Entry {
  id: string;
  fileName: string;
  diskName: string | null;
  baseline: string | null;
  preset: Preset | null;
  rawText: string;
  parseError: string | null;
}

let idCounter = 0;
export const newId = () => `p${++idCounter}`;

export function entryFromFile(name: string, text: string): Entry {
  try {
    const preset = parsePreset(text);
    return { id: newId(), fileName: name, diskName: name, baseline: serializePreset(preset), preset, rawText: text, parseError: null };
  } catch (e) {
    return { id: newId(), fileName: name, diskName: name, baseline: text, preset: null, rawText: text, parseError: (e as Error).message };
  }
}

export const contentOf = (e: Entry) => (e.preset ? serializePreset(e.preset) : e.rawText);
export const isDirty = (e: Entry) => e.diskName !== e.fileName || contentOf(e) !== e.baseline;

export function numberEntries(entries: Entry[]): Entry[] {
  return entries.map((entry, index) => ({ ...entry, fileName: `${index + 1}.json` }));
}

export function loadEntries(files: LoadedFile[]): Entry[] {
  return numberEntries(files.map((f) => entryFromFile(f.name, f.text)).sort((a, b) => compareFileNames(a.fileName, b.fileName)));
}

export function moveEntry(entries: Entry[], id: string, target: number): Entry[] {
  const source = entries.findIndex((e) => e.id === id);
  if (source < 0 || target < 0 || target >= entries.length || source === target) return entries;
  const reordered = [...entries];
  const [entry] = reordered.splice(source, 1);
  reordered.splice(target, 0, entry);
  return numberEntries(reordered);
}

export function obsoleteFileNames(entries: Entry[], removed: string[]): string[] {
  const current = new Set(entries.map((e) => e.fileName));
  return [...new Set([...removed, ...entries.flatMap((e) => (e.diskName ? [e.diskName] : []))])].filter((name) => !current.has(name));
}
