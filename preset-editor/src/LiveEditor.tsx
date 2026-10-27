import { useEffect, useEffectEvent, useRef, useState } from 'react';
import { MAX_PRESETS, newPreset, parsePreset, serializePreset, validatePreset } from './preset';
import { contentOf, entryFromFile, isDirty, loadEntries, moveEntry, newId, numberEntries } from './entries';
import type { Entry } from './entries';
import { LiveMidiClient } from './liveMidi';
import { PresetEditor } from './PresetEditor';
import { matchMidiPort, MIDI_SETTINGS_KEY, portIdentity, readMidiSettings } from './midiSettings';
import type { MidiSettings } from './midiSettings';

export function LiveEditor() {
  const [saved] = useState(() => {
    try { return { ports: readMidiSettings(localStorage), error: '' }; }
    catch (e) { return { ports: { input: null, output: null }, error: `Error: Cannot restore MIDI settings: ${String(e)}` }; }
  });
  const [settings, setSettings] = useState<MidiSettings>(saved.ports);
  const [access, setAccess] = useState<MIDIAccess | null>(null);
  const [inputs, setInputs] = useState<MIDIInput[]>([]);
  const [outputs, setOutputs] = useState<MIDIOutput[]>([]);
  const [inputId, setInputId] = useState(saved.ports.input?.id ?? '');
  const [outputId, setOutputId] = useState(saved.ports.output?.id ?? '');
  const [autoConnect, setAutoConnect] = useState(true);
  const attempted = useRef('');
  const mounted = useRef(true);
  const client = useRef<LiveMidiClient | null>(null);
  const [connected, setConnected] = useState(false);
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState(saved.error);
  const [entries, setEntries] = useState<Entry[]>([]);
  const [removed, setRemoved] = useState(false);
  const [loaded, setLoaded] = useState(false);
  const [selectedId, setSelectedId] = useState('');
  const [midiFiles, setMidiFiles] = useState<string[]>([]);
  const [revision, setRevision] = useState(0);
  const [activeName, setActiveName] = useState('');
  const [filter, setFilter] = useState('');
  const [dragId, setDragId] = useState<string | null>(null);
  const [mustReload, setMustReload] = useState(false);
  const selected = entries.find((entry) => entry.id === selectedId);
  const selectedIndex = entries.findIndex((entry) => entry.id === selectedId);
  const dirty = removed || entries.some(isDirty);
  const editable = connected && !busy && loaded && !mustReload;
  const canReorder = editable && !filter.trim();
  const visible = entries.filter((entry) => `${entry.fileName} ${entry.preset?.Name ?? ''}`.toLowerCase().includes(filter.trim().toLowerCase()));

  useEffect(() => {
    mounted.current = true;
    return () => { mounted.current = false; client.current?.close(); };
  }, []);
  useEffect(() => {
    let cancelled = false;
    const restoreAccess = async () => {
      if (!navigator.requestMIDIAccess || !navigator.permissions) return;
      try {
        const permission = await navigator.permissions.query({ name: 'midi', sysex: true } as PermissionDescriptor);
        if (permission.state !== 'granted' || cancelled) return;
        const midi = await navigator.requestMIDIAccess({ sysex: true });
        if (!cancelled) setAccess(midi);
      } catch (e) {
        if (!cancelled) setStatus(`Error: Automatic MIDI access unavailable: ${String(e)}. Use Enable USB MIDI access.`);
      }
    };
    void restoreAccess();
    return () => { cancelled = true; };
  }, []);
  useEffect(() => {
    if (!access) return;
    const refresh = () => {
      setInputs(Array.from(access.inputs.values()));
      setOutputs(Array.from(access.outputs.values()));
    };
    refresh();
    access.addEventListener('statechange', refresh);
    return () => access.removeEventListener('statechange', refresh);
  }, [access]);
  useEffect(() => {
    const beforeUnload = (ev: BeforeUnloadEvent) => { if (dirty) ev.preventDefault(); };
    window.addEventListener('beforeunload', beforeUnload);
    return () => window.removeEventListener('beforeunload', beforeUnload);
  }, [dirty]);

  const run = async (action: () => Promise<void>) => {
    setBusy(true);
    try { await action(); }
    catch (e) { setStatus(`Error: ${(e as Error).message}`); }
    finally { setBusy(false); }
  };
  const load = async (connection: LiveMidiClient) => {
    setStatus('Loading all controller presets...');
    await connection.request(9);
    const result = await connection.readSet();
    if (!mounted.current || client.current !== connection) return;
    const list = loadEntries(result.files);
    setEntries(list);
    setSelectedId(list[0]?.id ?? '');
    setMidiFiles(result.listing.midiFiles);
    setRevision(result.revision);
    setRemoved(false);
    setLoaded(true);
    setMustReload(false);
    setActiveName('');
    setFilter('');
    setStatus(`Loaded ${list.length} controller presets. Saves number files in this order; MIDI files stay untouched.`);
  };
  const remember = (next: MidiSettings) => {
    setSettings(next);
    try { localStorage.setItem(MIDI_SETTINGS_KEY, JSON.stringify(next)); }
    catch (e) { setStatus(`Error: Cannot remember MIDI device settings: ${String(e)}`); }
  };
  const connect = async (input: MIDIInput, output: MIDIOutput) => {
    if (input.state !== 'connected' || output.state !== 'connected') throw new Error('The selected MIDI ports are no longer available.');
    client.current?.close();
    const connection = new LiveMidiClient(input, output, () => {
      attempted.current = '';
      setConnected(false);
      setActiveName('');
      setStatus('USB controller disconnected. Unsaved changes remain here.');
    });
    client.current = connection;
    try {
      await input.open();
      await output.open();
      await connection.hello();
      if (!connection.supportsPresetSets) throw new Error('Update controller firmware for full USB preset management.');
      if (!loaded) await load(connection);
      else {
        await connection.request(9);
        setStatus('Reconnected. Unsaved changes retained; saves check the controller revision.');
      }
      if (!mounted.current || client.current !== connection) { connection.close(); return; }
      setConnected(true);
      setActiveName('');
      remember({ input: portIdentity(input), output: portIdentity(output) });
    } catch (e) { connection.close(); throw e; }
  };
  const automaticConnect = useEffectEvent((input: MIDIInput, output: MIDIOutput) => {
    setInputId(input.id);
    setOutputId(output.id);
    void run(() => connect(input, output));
  });
  useEffect(() => {
    if (!access || !autoConnect || connected || busy) return;
    const input = matchMidiPort(inputs, settings.input);
    const output = matchMidiPort(outputs, settings.output);
    if (!input || !output) { attempted.current = ''; return; }
    const key = JSON.stringify([input.id, output.id]);
    if (attempted.current === key) return;
    attempted.current = key;
    automaticConnect(input, output);
  }, [access, inputs, outputs, settings, autoConnect, connected, busy]);
  const discard = () => !dirty || window.confirm('Discard unsaved USB preset changes?');
  const add = (copy = false) => {
    if (!editable || entries.length >= MAX_PRESETS) return;
    const preset = copy && selected?.preset ? parsePreset(serializePreset(selected.preset)) : newPreset();
    if (copy) preset.Name += ' copy';
    const entry: Entry = { id: newId(), fileName: `${entries.length + 1}.json`, diskName: null, baseline: null, preset, rawText: '', parseError: null };
    setEntries([...entries, entry]);
    setSelectedId(entry.id);
    setActiveName('');
  };
  const reorder = (id: string, index: number) => {
    if (!canReorder) return;
    setEntries((list) => moveEntry(list, id, index));
    setActiveName('');
  };

  return (
    <details className="card live-editor">
      <summary>Live USB editor and DAW connection</summary>
      <p className="hint">Manage all presets through the Teensy device USB port with Serial + MIDI firmware. Stop file playback before loading or saving. Save all writes 1.json, 2.json, etc. in list order and removes obsolete preset files, leaving MIDI files untouched. Saving preserves the running rig; Activate applies a saved preset.</p>
      <label><input type="checkbox" checked={autoConnect} onChange={(e) => {
        attempted.current = '';
        setAutoConnect(e.target.checked);
      }} /> Automatically connect to the remembered controller</label>
      {!access ? (
        <button type="button" disabled={busy || !navigator.requestMIDIAccess} onClick={() => void run(async () => {
          setAccess(await navigator.requestMIDIAccess({ sysex: true }));
          setStatus('Choose the controller input and output ports, then Connect.');
        })}>Enable USB MIDI access</button>
      ) : (
        <div className="row">
          <label className="field">MIDI input
            <select value={inputId} disabled={busy || connected} onChange={(e) => {
              setInputId(e.target.value);
              const port = access.inputs.get(e.target.value);
              remember({ ...settings, input: port ? portIdentity(port) : null });
            }}>
              <option value="">Select input</option>
              {inputs.map((p) => <option key={p.id} value={p.id}>{p.name}</option>)}
            </select>
          </label>
          <label className="field">MIDI output
            <select value={outputId} disabled={busy || connected} onChange={(e) => {
              setOutputId(e.target.value);
              const port = access.outputs.get(e.target.value);
              remember({ ...settings, output: port ? portIdentity(port) : null });
            }}>
              <option value="">Select output</option>
              {outputs.map((p) => <option key={p.id} value={p.id}>{p.name}</option>)}
            </select>
          </label>
          <button type="button" disabled={busy || !inputId || !outputId} onClick={() => void run(async () => {
            if (connected) {
              if (!discard()) return;
              setAutoConnect(false);
              client.current?.close();
              client.current = null;
              setConnected(false);
              setEntries([]);
              setRemoved(false);
              setLoaded(false);
              setActiveName('');
              setStatus('Disconnected.');
              return;
            }
            const input = access.inputs.get(inputId), output = access.outputs.get(outputId);
            if (!input || !output) throw new Error('The selected MIDI ports are no longer available.');
            await connect(input, output);
          })}>{connected ? 'Disconnect' : 'Connect'}</button>
        </div>
      )}
      {!navigator.requestMIDIAccess && <p className="error">Web MIDI is unavailable. Use Chrome/Edge over HTTPS or localhost.</p>}
      {connected && (
        <div className="row">
          <button type="button" disabled={busy} onClick={() => { if (discard()) void run(() => load(client.current!)); }}>Reload controller set</button>
          <button type="button" disabled={!editable || !dirty} onClick={() => void run(async () => {
            const invalid = entries.find((entry) => !entry.preset || validatePreset(entry.preset).some((i) => i.level === 'error'));
            if (invalid) throw new Error(`Fix errors in ${invalid.fileName} before saving.`);
            if (entries.length === 0 && !window.confirm('Delete all controller presets? MIDI files will be kept.')) return;
            try {
              setStatus(`Saving ${entries.length} presets. Keep the controller connected until the save finishes.`);
              const nextRevision = await client.current!.saveSet(entries.map(contentOf), revision, entries.map((entry) => entry.diskName ?? ''));
              setRevision(nextRevision);
              setEntries((list) => list.map((entry) => ({ ...entry, diskName: entry.fileName, baseline: contentOf(entry) })));
              setRemoved(false);
              setActiveName('');
              setStatus(`Saved ${entries.length} numbered presets to the controller. Running rig unchanged; Activate to apply.`);
            } catch (e) {
              setMustReload(true);
              setActiveName('');
              throw e;
            }
          })}>Save all to controller{dirty ? ' *' : ''}</button>
          <button type="button" disabled={!editable || !selected || dirty} onClick={() => void run(async () => {
            await client.current!.activate(selectedIndex, selected!.fileName);
            setActiveName(selected!.fileName);
            setStatus(`Activated ${selected!.fileName}; OnLoad messages sent.`);
          })}>Activate saved preset</button>
          {[1, 2, 3].map((n) => <button key={n} type="button" disabled={!editable || dirty || !selected || activeName !== selected.fileName} onClick={() => void run(async () => {
            await client.current!.trigger(n, selected!.fileName);
            setStatus(`Triggered switch ${n} primary action.`);
          })}>Test switch {n}</button>)}
        </div>
      )}
      {status && <p role="status" className={status.startsWith('Error:') ? 'error' : 'hint'}>{status}</p>}
      {mustReload && <p className="error">Reload the controller set before further changes. Unsaved edits are retained until you confirm discarding them.</p>}
      {loaded && <div className="layout">
        <aside className="sidebar">
          <input type="search" aria-label="Filter USB presets" placeholder="Filter USB presets" value={filter} disabled={!editable} onChange={(e) => setFilter(e.target.value)} />
          <div className="row">
            <button type="button" disabled={!editable || entries.length >= MAX_PRESETS} onClick={() => add()}>New preset</button>
            <button type="button" disabled={!editable || !selected?.preset || entries.length >= MAX_PRESETS} onClick={() => add(true)}>Duplicate preset</button>
            <button type="button" disabled={!editable || !selected} onClick={() => {
              if (!selected || !window.confirm(`Delete ${selected.fileName}? It is removed from the controller when you Save all.`)) return;
              const list = numberEntries(entries.filter((entry) => entry.id !== selected.id));
              setEntries(list);
              setRemoved(true);
              setSelectedId(list[Math.min(selectedIndex, list.length - 1)]?.id ?? '');
              setActiveName('');
            }}>Delete preset</button>
            <button type="button" disabled={!canReorder || selectedIndex <= 0} onClick={() => reorder(selectedId, selectedIndex - 1)}>Move up</button>
            <button type="button" disabled={!canReorder || selectedIndex < 0 || selectedIndex >= entries.length - 1} onClick={() => reorder(selectedId, selectedIndex + 1)}>Move down</button>
          </div>
          <p className="hint">{filter.trim() ? 'Clear the filter to reorder.' : 'Drag presets to reorder, or use Move up/down.'} {entries.length}/{MAX_PRESETS} presets.</p>
          <ul className="preset-list">
            {visible.map((entry) => <li key={entry.id} draggable={canReorder}
              onDragStart={(ev) => {
                if (!canReorder) { ev.preventDefault(); return; }
                ev.dataTransfer.setData('text/plain', entry.id);
                setDragId(entry.id);
              }}
              onDragOver={(ev) => { if (canReorder && dragId) ev.preventDefault(); }}
              onDrop={(ev) => {
                ev.preventDefault();
                if (dragId && ev.dataTransfer.getData('text/plain') === dragId) reorder(dragId, entries.findIndex((e) => e.id === entry.id));
                setDragId(null);
              }} onDragEnd={() => setDragId(null)}>
              <button type="button" disabled={!editable} className={entry.id === selectedId ? 'selected' : ''} onClick={() => { setSelectedId(entry.id); setActiveName(''); }}>
                <span className="file">{entry.fileName}</span>
                <span className="name">{entry.preset?.Name || 'Invalid/unnamed preset'}{isDirty(entry) ? ' *' : ''}</span>
              </button>
            </li>)}
          </ul>
        </aside>
        <main className="content">
          {!selected ? <p>Select a preset or create a new one.</p> :
            <fieldset className="preset-fields" disabled={!editable}>
              {selected.preset ? <PresetEditor key={selected.id} preset={selected.preset} midiFiles={midiFiles}
                onChange={(preset) => setEntries((list) => list.map((entry) => entry.id === selected.id ? { ...entry, preset } : entry))} /> :
                <label className="field">Invalid preset JSON: {selected.parseError}
                  <textarea value={selected.rawText} onChange={(e) => {
                    const parsed = entryFromFile(selected.fileName, e.target.value);
                    setEntries((list) => list.map((entry) => entry.id === selected.id ? { ...entry, preset: parsed.preset, rawText: e.target.value, parseError: parsed.parseError } : entry));
                  }} />
                </label>}
            </fieldset>}
        </main>
      </div>}
    </details>
  );
}
