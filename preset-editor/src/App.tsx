import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { Preset } from './preset';
import { MAX_PRESETS, compareFileNames, newPreset, nextFileName, parsePreset, serializePreset, validatePreset } from './preset';
import type { FolderSource } from './folder';
import { deleteFile, downloadZip, ensureWritable, pickFolder, readDirectory, readFileList, supportsDirectoryAccess, writeFile } from './folder';
import { PresetEditor } from './PresetEditor';

interface Entry {
  id: string;
  fileName: string;
  /** Name of the file on disk, or null if not yet saved. */
  diskName: string | null;
  /** Content considered "clean" (normalized for valid presets, raw for invalid ones). */
  baseline: string | null;
  preset: Preset | null;
  rawText: string;
  parseError: string | null;
}

let idCounter = 0;
const newId = () => `p${++idCounter}`;

function entryFromFile(name: string, text: string): Entry {
  try {
    const preset = parsePreset(text);
    return { id: newId(), fileName: name, diskName: name, baseline: serializePreset(preset), preset, rawText: text, parseError: null };
  } catch (e) {
    return { id: newId(), fileName: name, diskName: name, baseline: text, preset: null, rawText: text, parseError: (e as Error).message };
  }
}

const contentOf = (e: Entry) => (e.preset ? serializePreset(e.preset) : e.rawText);
const isDirty = (e: Entry) => e.diskName !== e.fileName || contentOf(e) !== e.baseline;

export default function App() {
  const [folder, setFolder] = useState<FolderSource | null>(null);
  const [entries, setEntries] = useState<Entry[]>([]);
  const [removed, setRemoved] = useState<string[]>([]);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [filter, setFilter] = useState('');
  const [status, setStatus] = useState<{ kind: 'info' | 'error'; text: string } | null>(null);
  const [busy, setBusy] = useState(false);
  const fallbackInput = useRef<HTMLInputElement>(null);

  const sorted = useMemo(() => [...entries].sort((a, b) => compareFileNames(a.fileName, b.fileName)), [entries]);
  const selected = entries.find((e) => e.id === selectedId) ?? null;
  const dirtyCount = entries.filter(isDirty).length + removed.length;

  useEffect(() => {
    const handler = (ev: BeforeUnloadEvent) => {
      if (dirtyCount > 0) ev.preventDefault();
    };
    window.addEventListener('beforeunload', handler);
    return () => window.removeEventListener('beforeunload', handler);
  }, [dirtyCount]);

  const load = (src: FolderSource) => {
    const list = src.presetFiles.map((f) => entryFromFile(f.name, f.text)).sort((a, b) => compareFileNames(a.fileName, b.fileName));
    setFolder(src);
    setEntries(list);
    setRemoved([]);
    setSelectedId(list[0]?.id ?? null);
    const invalid = list.filter((e) => e.parseError).length;
    setStatus({
      kind: 'info',
      text: `Loaded ${list.length} preset${list.length === 1 ? '' : 's'} from “${src.name}”${invalid ? ` (${invalid} with invalid JSON)` : ''}.`,
    });
  };

  const confirmDiscard = () => dirtyCount === 0 || window.confirm('You have unsaved changes. Discard them?');

  const openFolder = async () => {
    if (!confirmDiscard()) return;
    if (!supportsDirectoryAccess) {
      fallbackInput.current?.click();
      return;
    }
    try {
      setBusy(true);
      load(await pickFolder());
    } catch (e) {
      if ((e as DOMException).name !== 'AbortError') setStatus({ kind: 'error', text: (e as Error).message });
    } finally {
      setBusy(false);
    }
  };

  const reload = async () => {
    if (!folder?.handle || !confirmDiscard()) return;
    try {
      setBusy(true);
      load(await readDirectory(folder.handle));
    } catch (e) {
      setStatus({ kind: 'error', text: (e as Error).message });
    } finally {
      setBusy(false);
    }
  };

  const updateEntry = useCallback((id: string, patch: Partial<Entry>) => {
    setEntries((list) => list.map((e) => (e.id === id ? { ...e, ...patch } : e)));
  }, []);

  const addEntry = (preset: Preset) => {
    const entry: Entry = {
      id: newId(),
      fileName: nextFileName(entries.map((e) => e.fileName)),
      diskName: null,
      baseline: null,
      preset,
      rawText: '',
      parseError: null,
    };
    setEntries((list) => [...list, entry]);
    setSelectedId(entry.id);
  };

  const duplicate = () => {
    if (!selected?.preset) return;
    const copy = parsePreset(serializePreset(selected.preset));
    addEntry({ ...copy, Name: `${copy.Name} copy` });
  };

  const remove = () => {
    if (!selected || !window.confirm(`Delete ${selected.fileName}? The file is removed from the folder when you save.`)) return;
    const idx = sorted.findIndex((e) => e.id === selected.id);
    if (selected.diskName) setRemoved((r) => [...r, selected.diskName!]);
    setEntries((list) => list.filter((e) => e.id !== selected.id));
    const next = sorted[idx + 1] ?? sorted[idx - 1];
    setSelectedId(next && next.id !== selected.id ? next.id : null);
  };

  const fileNameError = (e: Entry): string | null => {
    if (!/^[^\\/:*?"<>|]+\.json$/i.test(e.fileName)) return 'File name must end in .json and contain no path characters.';
    if (entries.some((o) => o.id !== e.id && o.fileName.toLowerCase() === e.fileName.toLowerCase())) return 'Another preset already uses this file name.';
    return null;
  };

  const save = async () => {
    if (!folder || busy) return;
    const problems = entries.filter((e) => fileNameError(e));
    if (problems.length) {
      setSelectedId(problems[0].id);
      setStatus({ kind: 'error', text: `Fix the file name of ${problems[0].fileName} before saving.` });
      return;
    }
    const withErrors = entries.filter((e) => e.preset && validatePreset(e.preset).some((i) => i.level === 'error'));
    if (withErrors.length && !window.confirm(`${withErrors.length} preset(s) have errors (e.g. ${withErrors[0].fileName}). Save anyway?`)) return;

    setBusy(true);
    try {
      if (folder.handle) {
        await ensureWritable(folder.handle);
        const dirty = entries.filter(isDirty);
        for (const e of dirty) await writeFile(folder.handle, e.fileName, contentOf(e));
        const current = new Set(entries.map((e) => e.fileName.toLowerCase()));
        const stale = [...removed, ...dirty.filter((e) => e.diskName && e.diskName !== e.fileName).map((e) => e.diskName!)];
        const toDelete = [...new Set(stale)].filter((n) => !current.has(n.toLowerCase()));
        for (const n of toDelete) await deleteFile(folder.handle, n);
        setStatus({ kind: 'info', text: `Saved ${dirty.length} file(s)${toDelete.length ? ` and deleted ${toDelete.length}` : ''} to “${folder.name}”.` });
      } else {
        await downloadZip(folder.name, sorted.map((e) => ({ name: e.fileName, text: contentOf(e) })));
        setStatus({
          kind: 'info',
          text: `Downloaded ${folder.name}.zip with ${entries.length} preset(s). Your browser can't write to folders directly — extract the zip over the folder${removed.length ? ` and delete: ${removed.join(', ')}` : ''}.`,
        });
      }
      setEntries((list) => list.map((e) => ({ ...e, diskName: e.fileName, baseline: contentOf(e) })));
      setRemoved([]);
    } catch (e) {
      setStatus({ kind: 'error', text: `Save failed: ${(e as Error).message}` });
    } finally {
      setBusy(false);
    }
  };

  const saveRef = useRef(save);
  useEffect(() => {
    saveRef.current = save;
  });
  useEffect(() => {
    const handler = (ev: KeyboardEvent) => {
      if ((ev.ctrlKey || ev.metaKey) && ev.key.toLowerCase() === 's') {
        ev.preventDefault();
        void saveRef.current();
      }
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, []);

  const q = filter.trim().toLowerCase();
  const visible = q ? sorted.filter((e) => e.fileName.toLowerCase().includes(q) || (e.preset?.Name ?? '').toLowerCase().includes(q)) : sorted;
  const selectedNameError = selected ? fileNameError(selected) : null;

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <strong>SYNAPSE</strong> Preset Editor
        </div>
        <div className="actions">
          <button type="button" onClick={openFolder} disabled={busy}>
            📂 {folder ? 'Open another folder' : 'Open preset folder'}
          </button>
          {folder?.handle && (
            <button type="button" className="secondary" onClick={reload} disabled={busy}>
              ⟳ Reload
            </button>
          )}
          {folder && (
            <button type="button" className="primary" onClick={save} disabled={busy || (folder.handle !== null && dirtyCount === 0)}>
              💾 {folder.handle ? `Save to folder${dirtyCount ? ` (${dirtyCount})` : ''}` : 'Download zip'}
            </button>
          )}
        </div>
        <input
          ref={fallbackInput}
          type="file"
          hidden
          multiple
          {...{ webkitdirectory: '' }}
          onChange={async (ev) => {
            if (ev.target.files?.length) load(await readFileList(ev.target.files));
            ev.target.value = '';
          }}
        />
      </header>

      {status && (
        <div className={`status ${status.kind}`}>
          <span>{status.text}</span>
          <button type="button" className="icon" onClick={() => setStatus(null)} title="Dismiss">
            ✕
          </button>
        </div>
      )}

      {!folder ? (
        <main className="welcome">
          <h1>Manage your MIDI controller presets</h1>
          <p>
            Select the folder containing your preset <code>.json</code> files (for example the root of the SD card). Every preset is loaded so you can edit it,
            then saved straight back to the same folder.
          </p>
          <button type="button" className="primary large" onClick={openFolder} disabled={busy}>
            📂 Open preset folder
          </button>
          {!supportsDirectoryAccess && (
            <p className="hint">
              Your browser doesn't support writing to local folders. You can still load and edit presets; saving downloads a zip. Use Chrome or Edge for direct
              save.
            </p>
          )}
          <p className="muted">Files never leave your computer — everything runs locally in your browser.</p>
        </main>
      ) : (
        <div className="layout">
          <aside className="sidebar">
            <div className="sidebar-tools">
              <input type="search" placeholder="Filter presets…" value={filter} onChange={(e) => setFilter(e.target.value)} />
              <div className="row">
                <button type="button" className="small" onClick={() => addEntry(newPreset())}>
                  + New
                </button>
                <button type="button" className="small secondary" onClick={duplicate} disabled={!selected?.preset}>
                  Duplicate
                </button>
                <button type="button" className="small danger" onClick={remove} disabled={!selected}>
                  Delete
                </button>
              </div>
              <p className="muted small-text">
                {entries.length} preset{entries.length === 1 ? '' : 's'}
                {entries.length > MAX_PRESETS && <span className="over"> — only the first {MAX_PRESETS} load on the device</span>}
              </p>
            </div>
            <ul className="preset-list">
              {visible.map((e) => {
                const hasError = e.parseError !== null || (e.preset !== null && validatePreset(e.preset).some((i) => i.level === 'error'));
                return (
                  <li key={e.id}>
                    <button type="button" className={e.id === selectedId ? 'selected' : ''} onClick={() => setSelectedId(e.id)}>
                      <span className="file">{e.fileName.replace(/\.json$/i, '')}</span>
                      <span className="name">{e.preset ? e.preset.Name || <em>Unnamed</em> : <em>Invalid JSON</em>}</span>
                      {hasError && (
                        <span className="badge error" title="Has errors">
                          !
                        </span>
                      )}
                      {isDirty(e) && (
                        <span className="badge dirty" title="Unsaved changes">
                          ●
                        </span>
                      )}
                    </button>
                  </li>
                );
              })}
              {visible.length === 0 && <li className="muted pad">No presets</li>}
            </ul>
          </aside>

          <main className="content">
            {!selected ? (
              <p className="muted pad">Select a preset or create a new one.</p>
            ) : (
              <>
                <div className="file-bar">
                  <label className="field">
                    <span>File name</span>
                    <input
                      type="text"
                      className={selectedNameError ? 'invalid' : undefined}
                      value={selected.fileName}
                      onChange={(ev) => updateEntry(selected.id, { fileName: ev.target.value })}
                    />
                  </label>
                  {selectedNameError && <span className="error">{selectedNameError}</span>}
                  {!selectedNameError && Number.isNaN(parseInt(selected.fileName, 10)) && (
                    <span className="hint">Use a numeric file name (e.g. 12.json) to control the order on the device.</span>
                  )}
                  {isDirty(selected) && selected.diskName && (
                    <button
                      type="button"
                      className="small secondary"
                      onClick={() => {
                        const original = entryFromFile(selected.diskName!, selected.baseline ?? '');
                        updateEntry(selected.id, {
                          fileName: selected.diskName!,
                          preset: original.preset,
                          rawText: original.rawText,
                          parseError: original.parseError,
                        });
                      }}
                    >
                      Revert changes
                    </button>
                  )}
                </div>

                {selected.preset ? (
                  <PresetEditor key={selected.id} preset={selected.preset} midiFiles={folder.midiFiles} onChange={(p) => updateEntry(selected.id, { preset: p })} />
                ) : (
                  <section className="card">
                    <h3>Invalid JSON</h3>
                    <p className="error">{selected.parseError}</p>
                    <p>Fix the JSON below, then parse it to edit with the form — or replace it with an empty preset.</p>
                    <textarea
                      className="code"
                      spellCheck={false}
                      rows={24}
                      value={selected.rawText}
                      onChange={(ev) => updateEntry(selected.id, { rawText: ev.target.value })}
                    />
                    <div className="row">
                      <button
                        type="button"
                        onClick={() => {
                          try {
                            updateEntry(selected.id, { preset: parsePreset(selected.rawText), parseError: null });
                          } catch (err) {
                            updateEntry(selected.id, { parseError: (err as Error).message });
                          }
                        }}
                      >
                        Parse JSON
                      </button>
                      <button type="button" className="secondary" onClick={() => updateEntry(selected.id, { preset: newPreset(), parseError: null })}>
                        Replace with empty preset
                      </button>
                    </div>
                  </section>
                )}
              </>
            )}
          </main>
        </div>
      )}
    </div>
  );
}
