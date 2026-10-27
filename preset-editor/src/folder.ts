import JSZip from 'jszip';

declare global {
  interface Window {
    showDirectoryPicker?: (options?: { mode?: 'read' | 'readwrite'; id?: string }) => Promise<FileSystemDirectoryHandle>;
  }
  interface FileSystemDirectoryHandle {
    values(): AsyncIterableIterator<FileSystemHandle>;
    queryPermission?(desc: { mode: 'read' | 'readwrite' }): Promise<PermissionState>;
    requestPermission?(desc: { mode: 'read' | 'readwrite' }): Promise<PermissionState>;
  }
}

export interface LoadedFile {
  name: string;
  text: string;
}

export interface FolderSource {
  name: string;
  handle: FileSystemDirectoryHandle | null;
  presetFiles: LoadedFile[];
  midiFiles: string[];
}

export const supportsDirectoryAccess = typeof window !== 'undefined' && typeof window.showDirectoryPicker === 'function';

const isPreset = (name: string) => /\.json$/i.test(name);
const isMidi = (name: string) => /\.midi?$/i.test(name);

export async function pickFolder(): Promise<FolderSource> {
  const handle = await window.showDirectoryPicker!({ mode: 'readwrite', id: 'presets' });
  return readDirectory(handle);
}

export async function readDirectory(handle: FileSystemDirectoryHandle): Promise<FolderSource> {
  const presetFiles: LoadedFile[] = [];
  const midiFiles: string[] = [];
  for await (const entry of handle.values()) {
    if (entry.kind !== 'file') continue;
    if (isPreset(entry.name)) {
      const file = await (entry as FileSystemFileHandle).getFile();
      presetFiles.push({ name: entry.name, text: await file.text() });
    } else if (isMidi(entry.name)) {
      midiFiles.push(entry.name);
    }
  }
  return { name: handle.name, handle, presetFiles, midiFiles };
}

/** Fallback for browsers without the File System Access API (read-only, saves as a zip download). */
export async function readFileList(files: FileList): Promise<FolderSource> {
  const all = Array.from(files);
  const rootDepth = (f: File) => (f.webkitRelativePath || f.name).split('/').length;
  const minDepth = Math.min(...all.map(rootDepth));
  const topLevel = all.filter((f) => rootDepth(f) === minDepth);
  const presetFiles = await Promise.all(
    topLevel.filter((f) => isPreset(f.name)).map(async (f) => ({ name: f.name, text: await f.text() })),
  );
  const folderName = all[0]?.webkitRelativePath.split('/')[0] || 'presets';
  return {
    name: folderName,
    handle: null,
    presetFiles,
    midiFiles: topLevel.filter((f) => isMidi(f.name)).map((f) => f.name),
  };
}

export async function ensureWritable(handle: FileSystemDirectoryHandle): Promise<void> {
  const opts = { mode: 'readwrite' as const };
  if ((await handle.queryPermission?.(opts)) === 'granted') return;
  if ((await handle.requestPermission?.(opts)) !== 'granted') throw new Error('Write permission to the folder was denied.');
}

export async function writeFile(handle: FileSystemDirectoryHandle, name: string, text: string): Promise<void> {
  const fileHandle = await handle.getFileHandle(name, { create: true });
  const writable = await fileHandle.createWritable();
  await writable.write(text);
  await writable.close();
}

export async function deleteFile(handle: FileSystemDirectoryHandle, name: string): Promise<void> {
  try {
    await handle.removeEntry(name);
  } catch (e) {
    if ((e as DOMException).name !== 'NotFoundError') throw e;
  }
}

export async function saveNumberedFiles(handle: FileSystemDirectoryHandle, files: LoadedFile[], obsolete: string[]): Promise<number> {
  await ensureWritable(handle);
  for (const file of files) await writeFile(handle, file.name, file.text);
  let deleted = 0;
  for (const name of obsolete) {
    if (files.some((f) => f.name === name)) continue;
    // A case-only rename can refer to the destination itself on FAT/Windows.
    const alias = files.find((f) => f.name.toLowerCase() === name.toLowerCase());
    if (alias) {
      try {
        const oldHandle = await handle.getFileHandle(name);
        const newHandle = await handle.getFileHandle(alias.name);
        if (await oldHandle.isSameEntry(newHandle)) continue;
      } catch (e) {
        if ((e as DOMException).name !== 'NotFoundError') throw e;
      }
    }
    await deleteFile(handle, name);
    deleted++;
  }
  return deleted;
}

export function createPresetZip(files: LoadedFile[]): JSZip {
  const zip = new JSZip();
  for (const f of files) zip.file(f.name, f.text);
  return zip;
}

export async function downloadZip(folderName: string, files: LoadedFile[]): Promise<void> {
  const zip = createPresetZip(files);
  const blob = await zip.generateAsync({ type: 'blob' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `${folderName}.zip`;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
