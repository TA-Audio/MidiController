import { useCallback, useEffect, useRef, useState } from 'react';
import type { Preset } from './preset';
import { MAX_PRESETS, newPreset, parsePreset, serializePreset, validatePreset } from './preset';
import type { FolderSource } from './folder';
import { downloadZip, pickFolder, readDirectory, readFileList, saveNumberedFiles, supportsDirectoryAccess } from './folder';
import { PresetEditor } from './PresetEditor';
import { LiveEditor } from './LiveEditor';
import type { Entry } from './entries';
import { contentOf, entryFromFile, isDirty, loadEntries, moveEntry, newId, numberEntries, obsoleteFileNames } from './entries';

export default function App() {
  const [folder, setFolder] = useState<FolderSource | null>(null);
  const [entries, setEntries] = useState<Entry[]>([]);
  const [removed, setRemoved] = useState<string[]>([]);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [filter, setFilter] = useState('');
  const [status, setStatus] = useState<{ kind: 'info' | 'error'; text: string } | null>(null);
  const [busy, setBusy] = useState(false);
  const [dragId, setDragId] = useState<string | null>(null);
  const [dropId, setDropId] = useState<string | null>(null);
  const fallbackInput = useRef<HTMLInputElement>(null);

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
    const list = loadEntries(src.presetFiles);
    setFolder(src);
    setEntries(list);
    setRemoved([]);
    setSelectedId(list[0]?.id ?? null);
    setFilter('');
    setDragId(null);
    setDropId(null);
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
      fileName: `${entries.length + 1}.json`,
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
    const idx = entries.findIndex((e) => e.id === selected.id);
    if (selected.diskName) setRemoved((r) => [...r, selected.diskName!]);
    setEntries((list) => numberEntries(list.filter((e) => e.id !== selected.id)));
    const next = entries[idx + 1] ?? entries[idx - 1];
    setSelectedId(next && next.id !== selected.id ? next.id : null);
  };

  const save = async () => {
    if (!folder || busy) return;
    const withErrors = entries.filter((e) => e.preset && validatePreset(e.preset).some((i) => i.level === 'error'));
    if (withErrors.length && !window.confirm(`${withErrors.length} preset(s) have errors (e.g. ${withErrors[0].fileName}). Save anyway?`)) return;

    setBusy(true);
    const files = entries.map((e) => ({ name: e.fileName, text: contentOf(e) }));
    const obsolete = obsoleteFileNames(entries, [...removed, ...folder.presetFiles.map((f) => f.name)]);
    try {
      if (folder.handle) {
        const deleted = await saveNumberedFiles(folder.handle, files, obsolete);
        setStatus({ kind: 'info', text: `Saved ${files.length} numbered file(s)${deleted ? ` and deleted ${deleted} obsolete file(s)` : ''} to “${folder.name}”.` });
      } else {
        await downloadZip(folder.name, files);
        setStatus({
          kind: 'info',
          text: `Downloaded ${folder.name}.zip with ${entries.length} numbered preset(s). Your browser can't write to folders directly. ${obsolete.length ? `Before extracting the zip, delete these obsolete preset files: ${obsolete.join(', ')}. Then extract` : 'Extract'} the zip over the folder. Keep your MIDI files.`,
        });
      }
      setFolder({
        ...folder,
        presetFiles: folder.handle ? files : [...new Map([...folder.presetFiles, ...files].map((f) => [f.name, f])).values()],
      });
      setEntries((list) => list.map((e) => ({ ...e, diskName: e.fileName, baseline: contentOf(e) })));
      setRemoved([]);
    } catch (e) {
      if (folder.handle) setRemoved((list) => [...new Set([...list, ...files.map((f) => f.name)])]);
      setStatus({ kind: 'error', text: `Save failed: ${(e as Error).message}. Some files may already have been written. Your changes are still unsaved; retry saving before reloading.` });
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
  const visible = q ? entries.filter((e) => e.fileName.toLowerCase().includes(q) || (e.preset?.Name ?? '').toLowerCase().includes(q)) : entries;
  const selectedIndex = entries.findIndex((e) => e.id === selectedId);
  const canReorder = !busy && q.length === 0;
  const reorder = (id: string, target: number) => {
    if (!canReorder) return;
    setEntries((list) => moveEntry(list, id, target));
  };

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

      <LiveEditor />

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
                <button type="button" className="small" onClick={() => addEntry(newPreset())} disabled={busy}>
                  + New
                </button>
                <button type="button" className="small secondary" onClick={duplicate} disabled={busy || !selected?.preset}>
                  Duplicate
                </button>
                <button type="button" className="small danger" onClick={remove} disabled={busy || !selected}>
                  Delete
                </button>
              </div>
              <div className="row">
                <button
                  type="button"
                  className="small secondary"
                  disabled={!canReorder || selectedIndex <= 0}
                  onClick={() => selectedId && reorder(selectedId, selectedIndex - 1)}
                >
                  Move up
                </button>
                <button
                  type="button"
                  className="small secondary"
                  disabled={!canReorder || selectedIndex < 0 || selectedIndex >= entries.length - 1}
                  onClick={() => selectedId && reorder(selectedId, selectedIndex + 1)}
                >
                  Move down
                </button>
              </div>
              <p className="hint small-text">
                {q ? 'Clear the filter to reorder presets.' : 'Drag a preset to reorder, or select it and use Move up/down. Files save as 1.json, 2.json, etc.'}
              </p>
              <p className="muted small-text">
                {entries.length} preset{entries.length === 1 ? '' : 's'}
                {entries.length > MAX_PRESETS && <span className="over"> — only the first {MAX_PRESETS} load on the device</span>}
              </p>
            </div>
            <ul className="preset-list">
              {visible.map((e) => {
                const hasError = e.parseError !== null || (e.preset !== null && validatePreset(e.preset).some((i) => i.level === 'error'));
                return (
                  <li
                    key={e.id}
                    className={`${dragId === e.id ? 'dragging' : ''} ${dropId === e.id ? 'drop-target' : ''}`}
                    draggable={canReorder}
                    onDragStart={(ev) => {
                      if (!canReorder) {
                        ev.preventDefault();
                        return;
                      }
                      ev.dataTransfer.setData('text/plain', e.id);
                      ev.dataTransfer.effectAllowed = 'move';
                      setDragId(e.id);
                    }}
                    onDragOver={(ev) => {
                      if (!canReorder || !dragId || dragId === e.id) return;
                      ev.preventDefault();
                      ev.dataTransfer.dropEffect = 'move';
                      setDropId(e.id);
                    }}
                    onDrop={(ev) => {
                      ev.preventDefault();
                      if (dragId && ev.dataTransfer.getData('text/plain') === dragId) {
                        reorder(dragId, entries.findIndex((entry) => entry.id === e.id));
                      }
                      setDragId(null);
                      setDropId(null);
                    }}
                    onDragEnd={() => {
                      setDragId(null);
                      setDropId(null);
                    }}
                  >
                    <button type="button" className={e.id === selectedId ? 'selected' : ''} onClick={() => setSelectedId(e.id)} disabled={busy}>
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
              <fieldset className="preset-fields" disabled={busy}>
                <div className="file-bar">
                  <label className="field">
                    <span>File name</span>
                    <input type="text" value={selected.fileName} readOnly />
                  </label>
                  <span className="hint">File number follows the preset order. The display name is edited below.</span>
                  {contentOf(selected) !== selected.baseline && selected.diskName && (
                    <button
                      type="button"
                      className="small secondary"
                      onClick={() => {
                        const original = entryFromFile(selected.diskName!, selected.baseline ?? '');
                        updateEntry(selected.id, {
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
              </fieldset>
            )}
          </main>
        </div>
      )}
    </div>
  );
}
