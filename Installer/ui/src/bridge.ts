// InstallerBridge — narrow native <-> React contract (see
// docs/InstallerArchitecture.md). The React UI may ONLY call these
// operations; no filesystem/process/shell access from JavaScript.

export interface ComponentInfo {
  id: 'core' | 'ffmpeg' | 'aiStudio' | 'aiModels';
  name: string;
  description: string;
  required: boolean;
  selected: boolean;
  sizeBytes: number; // computed from the real payload, never hardcoded
  sizeLabel: string;
}

export interface InstallationInfo {
  installPath: string;
  components: ComponentInfo[];
  requiredBytes: number;
  availableBytes: number;
  version: string;
}

export interface ProgressState {
  phase: string;
  detail: string;
  percent: number; // 0-100, real Burn progress — never faked
}

export interface InstallerBridge {
  getInstallationInfo(): Promise<InstallationInfo>;
  browseForFolder(currentPath: string): Promise<string | null>;
  getDiskSpace(path: string): Promise<number>;
  selectComponents(ids: string[]): Promise<void>;
  startInstall(): Promise<void>;
  cancelInstall(): Promise<void>;
  onProgress(cb: (p: ProgressState) => void): () => void;
  launchPlayer(): Promise<void>;
}

declare global {
  interface Window {
    chrome?: {
      webview?: {
        postMessage(msg: unknown): void;
        addEventListener(
          type: 'message',
          cb: (e: { data: unknown }) => void,
        ): void;
        removeEventListener?(
          type: 'message',
          cb: (e: { data: unknown }) => void,
        ): void;
        hostObjects?: {
          installer?: Record<string, (...args: never[]) => Promise<unknown>>;
        };
      };
    };
    __LUNAR_MOCK_BRIDGE__?: boolean;
  }
}

function realBridge(): InstallerBridge | null {
  const wv = window.chrome?.webview;
  if (!wv) return null;
  let nextId = 1;
  const pending = new Map<
    number,
    { resolve: (v: never) => void; reject: (e: Error) => void }
  >();
  const progressListeners = new Set<(p: ProgressState) => void>();
  // Install outcome waiters: StartInstall only *begins* the Burn
  // Plan/Apply sequence (the native ack is NOT completion). The promise
  // returned by startInstall resolves on applyComplete and rejects on
  // applyError, so progress is always real Burn progress — never faked.
  const outcomeWaiters = new Set<{
    resolve: () => void;
    reject: (e: Error) => void;
  }>();
  let lastError = '';
  const settleOutcome = (err: Error | null) => {
    if (outcomeWaiters.size === 0) return;
    const waiters = [...outcomeWaiters];
    outcomeWaiters.clear();
    for (const w of waiters) {
      if (err) w.reject(err);
      else w.resolve();
    }
  };
  wv.addEventListener('message', (e: { data: unknown }) => {
    const d = e.data as {
      id?: number;
      type?: string;
      result?: never;
      error?: string;
      progress?: ProgressState;
      hresult?: number;
      cancelled?: boolean;
      code?: number;
      package?: string;
      message?: string;
    };
    if (d && typeof d.id === 'number' && pending.has(d.id)) {
      const p = pending.get(d.id);
      if (p) {
        pending.delete(d.id);
        if (d.error !== undefined) p.reject(new Error(d.error));
        else p.resolve(d.result as never);
      }
    } else if (d && d.type === 'progress' && d.progress) {
      progressListeners.forEach((cb) => cb(d.progress as ProgressState));
    } else if (d && d.type === 'applyComplete') {
      settleOutcome(null);
    } else if (d && d.type === 'applyError') {
      const hr =
        typeof d.hresult === 'number'
          ? ` (hresult 0x${(d.hresult >>> 0).toString(16)})`
          : '';
      const tail = lastError ? `: ${lastError}` : '.';
      lastError = '';
      if (d.cancelled) settleOutcome(new Error(`cancelled${hr}`));
      else
        settleOutcome(
          new Error(`Installation failed${hr}${tail} Previous version (if any) was rolled back untouched.`),
        );
    } else if (d && d.type === 'error') {
      // Mid-apply Burn failure (download/verify/package error): surface
      // honestly in the progress detail; the final verdict still comes
      // from applyComplete/applyError.
      const msg = d.message || `error ${d.code ?? ''}`.trim();
      lastError = `${d.package ? `${d.package}: ` : ''}${msg}`;
      progressListeners.forEach((cb) =>
        cb({ phase: 'Error', detail: lastError, percent: 0 }),
      );
    }
  });
  const call = <T>(method: string, args: unknown): Promise<T> =>
    new Promise<T>((resolve, reject) => {
      const id = nextId++;
      pending.set(id, {
        resolve: resolve as (v: never) => void,
        reject,
      });
      wv.postMessage({ id, method, args });
      setTimeout(() => {
        if (pending.has(id)) {
          pending.delete(id);
          reject(new Error(`bridge timeout: ${method}`));
        }
      }, 120000);
    });
  return {
    getInstallationInfo: () =>
      call<InstallationInfo>('GetInstallationInfo', {}),
    browseForFolder: (p: string) =>
      call<string | null>('BrowseForFolder', { path: p }),
    getDiskSpace: (p: string) => call<number>('GetDiskSpace', { path: p }),
    selectComponents: (ids: string[]) =>
      call<void>('SelectComponents', { ids }),
    startInstall: () =>
      call<void>('StartInstall', {}).then(
        () =>
          new Promise<void>((resolve, reject) => {
            outcomeWaiters.add({ resolve, reject });
          }),
      ),
    cancelInstall: () => call<void>('CancelInstall', {}),
    onProgress: (cb) => {
      progressListeners.add(cb);
      return () => {
        progressListeners.delete(cb);
      };
    },
    launchPlayer: () => call<void>('LaunchPlayer', {}),
  };
}

// DEV-ONLY mock: used when running under `vite dev` (no native host).
// It is unreachable in production builds served by the bootstrapper.
// Every value is labeled MOCK in the console.
function mockBridge(): InstallerBridge {
  console.warn('[Lunar Installer] MOCK BRIDGE — development only');
  let cancelled = false;
  let progressCb: ((p: ProgressState) => void) | null = null;
  const components: ComponentInfo[] = [
    {
      id: 'core',
      name: 'Lunar Player',
      description: 'Application files, libraries and core features',
      required: true,
      selected: true,
      sizeBytes: 450 * 1024 * 1024,
      sizeLabel: '450 MB',
    },
    {
      id: 'ffmpeg',
      name: 'FFmpeg / Media Engine',
      description: 'Video, audio and image format support',
      required: true,
      selected: true,
      sizeBytes: 620 * 1024 * 1024,
      sizeLabel: '620 MB',
    },
    {
      id: 'aiStudio',
      name: 'AI Subtitle Studio',
      description: 'Speech recognition, transcription and translation tools',
      required: false,
      selected: true,
      sizeBytes: 280 * 1024 * 1024,
      sizeLabel: '280 MB',
    },
    {
      id: 'aiModels',
      name: 'AI Models (Download later)',
      description: 'Optional models for advanced AI features',
      required: false,
      selected: false,
      sizeBytes: 0,
      sizeLabel: '—',
    },
  ];
  return {
    async getInstallationInfo() {
      return {
        installPath: 'C:\\Program Files\\Lunar Player',
        components,
        requiredBytes: components
          .filter((c) => c.selected)
          .reduce((s, c) => s + c.sizeBytes, 0),
        availableBytes: 128 * 1024 * 1024 * 1024,
        version: '1.0.0',
      };
    },
    async browseForFolder(p) {
      return p;
    },
    async getDiskSpace() {
      return 128 * 1024 * 1024 * 1024;
    },
    async selectComponents(ids) {
      for (const c of components)
        if (!c.required) c.selected = ids.includes(c.id);
    },
    async startInstall() {
      cancelled = false;
      const phases = [
        'Preparing installation',
        'Checking prerequisites',
        'Installing Lunar Player',
        'Installing FFmpeg / Media Engine',
        'Registering application',
        'Finalizing',
      ];
      for (let i = 0; i <= 100 && !cancelled; i += 2) {
        await new Promise((r) => setTimeout(r, 60));
        const phase = phases[Math.min(phases.length - 1, i / 18)];
        progressCb?.({ phase, detail: 'MOCK progress', percent: i });
      }
      if (cancelled) throw new Error('cancelled (mock)');
    },
    async cancelInstall() {
      cancelled = true;
    },
    onProgress(cb) {
      progressCb = cb;
      return () => {
        progressCb = null;
      };
    },
    async launchPlayer() {},
  };
}

export function getBridge(): InstallerBridge {
  return realBridge() ?? mockBridge();
}

export function isMockBridge(): boolean {
  return realBridge() === null;
}

// ---- Updater client (same message-RPC transport, updater methods) ----
export interface UpdateState {
  installedVersion: string;
  installPath: string;
  channel: string;
}

export interface UpdateAvailable {
  updateAvailable: boolean;
  version: string;
  size?: number;
}

export interface UpdaterBridge {
  getUpdateState(): Promise<UpdateState>;
  checkForUpdate(): Promise<UpdateAvailable>;
  downloadUpdate(onBytes: (received: number, total: number) => void): Promise<void>;
  verifyUpdate(): Promise<{ verified: boolean; publisher: string }>;
  waitForPlayerExit(): Promise<void>;
  launchInstaller(): Promise<void>;
  relaunchPlayer(): Promise<void>;
  cancelDownload(): Promise<void>;
}

export function getUpdaterBridge(): UpdaterBridge | null {
  const wv = window.chrome?.webview;
  if (!wv) return null;
  let nextId = 1;
  const pending = new Map<
    number,
    { resolve: (v: never) => void; reject: (e: Error) => void }
  >();
  const byteListeners = new Set<(r: number, t: number) => void>();
  wv.addEventListener('message', (e: { data: unknown }) => {
    const d = e.data as {
      id?: number;
      type?: string;
      result?: never;
      error?: string;
      progress?: { received?: number; total?: number };
    };
    if (d && typeof d.id === 'number' && pending.has(d.id)) {
      const p = pending.get(d.id);
      if (p) {
        pending.delete(d.id);
        if (d.error !== undefined) p.reject(new Error(d.error));
        else p.resolve(d.result as never);
      }
    } else if (d && d.type === 'progress' && d.progress) {
      byteListeners.forEach((cb) =>
        cb(d.progress?.received ?? 0, d.progress?.total ?? 0),
      );
    }
  });
  const call = <T>(method: string, args: unknown): Promise<T> =>
    new Promise<T>((resolve, reject) => {
      const id = nextId++;
      pending.set(id, {
        resolve: resolve as (v: never) => void,
        reject,
      });
      wv.postMessage({ id, method, args });
      setTimeout(() => {
        if (pending.has(id)) {
          pending.delete(id);
          reject(new Error(`bridge timeout: ${method}`));
        }
      }, 600000);
    });
  return {
    getUpdateState: () => call<UpdateState>('GetUpdateState', {}),
    checkForUpdate: () => call<UpdateAvailable>('CheckForUpdate', {}),
    downloadUpdate: (onBytes) => {
      byteListeners.add(onBytes);
      return call<void>('DownloadUpdate', {}).finally(() => {
        byteListeners.delete(onBytes);
      });
    },
    verifyUpdate: () =>
      call<{ verified: boolean; publisher: string }>('VerifyUpdate', {}),
    waitForPlayerExit: () => call<void>('WaitForPlayerExit', {}),
    launchInstaller: () => call<void>('LaunchInstaller', {}),
    relaunchPlayer: () => call<void>('RelaunchPlayer', {}),
    cancelDownload: () => call<void>('CancelDownload', {}),
  };
}
