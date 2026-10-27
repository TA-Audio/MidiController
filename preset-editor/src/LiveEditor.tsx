import { useEffect, useRef, useState } from 'react';
import type { Preset } from './preset';
import { parsePreset, serializePreset, validatePreset } from './preset';
import { LiveMidiClient } from './liveMidi';
import type { LiveListing } from './liveMidi';
import { PresetEditor } from './PresetEditor';

export function LiveEditor() {
  const [access, setAccess] = useState<MIDIAccess | null>(null);
  const [inputs, setInputs] = useState<MIDIInput[]>([]);
  const [outputs, setOutputs] = useState<MIDIOutput[]>([]);
  const [inputId, setInputId] = useState('');
  const [outputId, setOutputId] = useState('');
  const client = useRef<LiveMidiClient | null>(null);
  const [connected, setConnected] = useState(false);
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState('');
  const [listing, setListing] = useState<LiveListing>({ presets: [], midiFiles: [] });
  const [name, setName] = useState('');
  const [preset, setPreset] = useState<Preset | null>(null);
  const [baseline, setBaseline] = useState('');
  const [revision, setRevision] = useState(0);
  const [activeName, setActiveName] = useState('');
  const dirty = preset !== null && serializePreset(preset) !== baseline;

  useEffect(() => () => client.current?.close(), []);
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
  const load = async (fileName: string) => {
    if (!client.current) throw new Error('Connect a Synapse controller first.');
    const result = await client.current.read(fileName);
    const parsed = parsePreset(result.text);
    setName(fileName);
    setPreset(parsed);
    setBaseline(serializePreset(parsed));
    setRevision(result.revision);
    setActiveName('');
    setStatus(`Loaded ${fileName}. Saving does not activate the preset; use Activate after saving.`);
  };
  const discard = () => !dirty || window.confirm('Discard unsaved live preset changes?');

  return (
    <details className="card live-editor">
      <summary>Live USB editor and DAW connection</summary>
      <p className="hint">Use the Teensy device USB port and firmware built with Serial + MIDI. Stop file playback before loading or saving. This editor updates existing preset files in place; use folder editing for additions, deletion and renumbering. USB Computer messages can be MIDI-mapped in your DAW.</p>
      {!access ? (
        <button type="button" disabled={busy || !navigator.requestMIDIAccess} onClick={() => void run(async () => {
          if (!navigator.requestMIDIAccess) throw new Error('Web MIDI requires a supported browser such as Chrome/Edge and HTTPS or localhost.');
          const midi = await navigator.requestMIDIAccess({ sysex: true });
          setAccess(midi);
          setStatus('Choose the controller input and output ports, then Connect.');
        })}>Enable USB MIDI access</button>
      ) : (
        <div className="row">
          <label className="field">MIDI input
            <select value={inputId} disabled={busy || connected} onChange={(e) => setInputId(e.target.value)}>
              <option value="">Select input</option>
              {inputs.map((p) => <option key={p.id} value={p.id}>{p.name}</option>)}
            </select>
          </label>
          <label className="field">MIDI output
            <select value={outputId} disabled={busy || connected} onChange={(e) => setOutputId(e.target.value)}>
              <option value="">Select output</option>
              {outputs.map((p) => <option key={p.id} value={p.id}>{p.name}</option>)}
            </select>
          </label>
          <button type="button" disabled={busy || !inputId || !outputId} onClick={() => void run(async () => {
            if (connected) {
              if (!discard()) return;
              client.current?.close();
              client.current = null;
              setConnected(false);
              setPreset(null);
              setActiveName('');
              setStatus('Disconnected.');
              return;
            }
            const input = access.inputs.get(inputId);
            const output = access.outputs.get(outputId);
            if (!input || !output) throw new Error('The selected MIDI ports are no longer available.');
            client.current?.close();
            const connection = new LiveMidiClient(input, output, () => {
              setConnected(false);
              setActiveName('');
              setStatus('USB controller disconnected. Unsaved changes remain here; reconnect before saving.');
            });
            client.current = connection;
            try {
              await connection.hello();
              const files = await connection.list();
              setListing(files);
              setConnected(true);
              setActiveName('');
              setStatus(files.midiFilesTruncated ? 'Connected. MIDI file suggestions limited to 128 entries.' : 'Connected to Synapse.');
            } catch (e) {
              connection.close();
              throw e;
            }
          })}>{connected ? 'Disconnect' : 'Connect'}</button>
        </div>
      )}
      {!navigator.requestMIDIAccess && <p className="error">Web MIDI is unavailable. Use Chrome/Edge over HTTPS or localhost.</p>}
      {connected && (
        <div className="row">
          <label className="field">Controller preset
            <select value={name} disabled={busy} onChange={(e) => {
              const fileName = e.target.value;
              if (fileName && discard()) void run(() => load(fileName));
            }}>
              <option value="">Select preset</option>
              {listing.presets.map((n) => <option key={n} value={n}>{n}</option>)}
            </select>
          </label>
          <button type="button" disabled={busy || !name} onClick={() => { if (discard()) void run(() => load(name)); }}>Reload from controller</button>
          <button type="button" disabled={busy || !preset || !dirty} onClick={() => void run(async () => {
            if (!preset || !client.current) return;
            const errors = validatePreset(preset).filter((i) => i.level === 'error');
            if (errors.length) throw new Error(errors.map((i) => i.message).join(' '));
            const text = serializePreset(preset);
            const nextRevision = await client.current.save(name, text, revision);
            setRevision(nextRevision);
            setBaseline(text);
            setActiveName('');
            setStatus(`Saved ${name} to the controller SD card. Use Activate to apply it.`);
          })}>Save to controller{dirty ? ' *' : ''}</button>
          <button type="button" disabled={busy || !preset || dirty} onClick={() => void run(async () => {
            await client.current!.activate(listing.presets.indexOf(name), name);
            setActiveName(name);
            setStatus(`Activated ${name}; OnLoad messages sent.`);
          })}>Activate saved preset</button>
          {[1, 2, 3].map((n) => <button key={n} type="button" disabled={busy || dirty || activeName !== name} onClick={() => void run(async () => {
            await client.current!.trigger(n, name);
            setStatus(`Triggered switch ${n} primary action. Hardware switches also support gestures.`);
          })}>Test switch {n}</button>)}
        </div>
      )}
      {status && <p role="status" className={status.startsWith('Error:') ? 'error' : 'hint'}>{status}</p>}
      {preset && <fieldset className="preset-fields" disabled={busy || !connected}>
        <PresetEditor key={name} preset={preset} onChange={setPreset} midiFiles={listing.midiFiles} />
      </fieldset>}
    </details>
  );
}
