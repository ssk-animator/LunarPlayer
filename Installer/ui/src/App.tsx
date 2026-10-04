import { useEffect, useState } from 'react';
import {
  getBridge,
  type ComponentInfo,
  type InstallationInfo,
  type ProgressState,
} from './bridge';
import BlinkingSquares from './components/BlinkingSquares';
import UpdaterApp from './updater/UpdaterApp';
import { Button } from '@/components/ui/button';
import { Checkbox } from '@/components/ui/checkbox';

type Screen =
  | 'install'
  | 'installing'
  | 'complete'
  | 'error'
  | 'cancelled';

function formatBytes(b: number): string {
  if (b <= 0) return '—';
  const gb = b / (1024 * 1024 * 1024);
  if (gb >= 1) return `${gb.toFixed(2)} GB`;
  return `${Math.round(b / (1024 * 1024))} MB`;
}

function ComponentRow({
  c,
  onToggle,
}: {
  c: ComponentInfo;
  onToggle: (id: string, v: boolean) => void;
}) {
  return (
    <label className="flex cursor-pointer items-start gap-3 px-4 py-3">
      <Checkbox
        checked={c.selected}
        disabled={c.required}
        onCheckedChange={(v) => onToggle(c.id, v === true)}
        className="mt-1 border-slate-600 data-[state=checked]:border-[#2f80ed] data-[state=checked]:bg-[#2f80ed]"
      />
      <span className="min-w-0 flex-1">
        <span className="block text-sm font-medium text-slate-100">
          {c.name}
          {c.required && (
            <span className="ml-1 font-normal text-slate-400">(required)</span>
          )}
        </span>
        <span className="block truncate text-xs text-slate-400">
          {c.description}
        </span>
      </span>
      <span className="shrink-0 text-sm text-slate-200">{c.sizeLabel}</span>
    </label>
  );
}

export default function App() {
  // Updater mode (?mode=updater) reuses this bundle with dedicated screens.
  if (
    typeof window !== 'undefined' &&
    new URLSearchParams(window.location.search).get('mode') === 'updater'
  ) {
    return <UpdaterApp />;
  }
  const [screen, setScreen] = useState<Screen>('install');
  const [info, setInfo] = useState<InstallationInfo | null>(null);
  const [progress, setProgress] = useState<ProgressState>({
    phase: '',
    detail: '',
    percent: 0,
  });
  const [error, setError] = useState('');
  const [showAdvanced, setShowAdvanced] = useState(false);
  const bridge = getBridge();

  useEffect(() => {
    bridge
      .getInstallationInfo()
      .then(setInfo)
      .catch((e: unknown) =>
        setError(e instanceof Error ? e.message : String(e)),
      );
    return bridge.onProgress(setProgress);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const toggle = (id: string, v: boolean) => {
    if (!info) return;
    const next = info.components.map((c) =>
      c.id === id && !c.required ? { ...c, selected: v } : c,
    );
    setInfo({ ...info, components: next });
    void bridge.selectComponents(next.filter((c) => c.selected).map((c) => c.id));
  };

  const startInstall = async () => {
    setScreen('installing');
    try {
      await bridge.startInstall();
      setScreen('complete');
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : String(e);
      if (/cancel/i.test(msg)) setScreen('cancelled');
      else {
        setError(msg);
        setScreen('error');
      }
    }
  };

  const cancelInstall = () => {
    void bridge.cancelInstall();
  };

  if (!info && !error) {
    return (
      <div className="flex h-screen items-center justify-center bg-[#0b1322] text-slate-300">
        Loading installer…
      </div>
    );
  }

  return (
    <div className="relative flex h-screen overflow-hidden bg-[#0b1322] font-sans text-slate-100">
      {/* Ambient right-side visual (never dominant). */}
      <div className="pointer-events-none absolute inset-y-0 right-0 w-[42%] opacity-80">
        <BlinkingSquares />
        <div className="absolute inset-0 bg-gradient-to-r from-[#0b1322] via-transparent to-transparent" />
      </div>

      <div className="relative z-10 flex w-full max-w-[880px] flex-col px-10 py-8">
        <header className="mb-2 flex items-center gap-2">
          <span className="text-lg">🌙</span>
          <span className="text-sm text-slate-300">Lunar Player Installer</span>
        </header>

        {screen === 'install' && info && (
          <>
            <h1 className="text-4xl font-bold tracking-tight">
              Install <span className="text-[#2f80ed]">Lunar Player</span>
            </h1>
            <p className="mt-1 text-sm text-slate-400">
              A modern media player for creators, animators, VFX artists and
              editors.
            </p>

            <div className="mt-5 text-sm font-medium">Installation Location</div>
            <div className="mt-1 flex gap-2">
              <input
                value={info.installPath}
                readOnly
                className="h-10 flex-1 rounded-lg border border-slate-700 bg-[#141f36] px-3 text-sm"
              />
              <Button
                variant="outline"
                className="border-slate-700 bg-transparent text-slate-200 hover:bg-slate-800"
                onClick={async () => {
                  const p = await bridge.browseForFolder(info.installPath);
                  if (p) {
                    setInfo({ ...info, installPath: p });
                    void bridge.getDiskSpace(p).then((availableBytes) =>
                      setInfo((prev) =>
                        prev ? { ...prev, availableBytes } : prev,
                      ),
                    );
                  }
                }}
              >
                Browse…
              </Button>
            </div>

            <div className="mt-4 overflow-hidden rounded-xl border border-slate-800">
              <div className="flex items-center justify-between px-4 pt-3">
                <span className="text-sm font-semibold">Core Components</span>
                <span className="text-xs text-slate-500">
                  Required for basic functionality
                </span>
              </div>
              {info.components
                .filter((c) => c.required)
                .map((c) => (
                  <ComponentRow key={c.id} c={c} onToggle={toggle} />
                ))}
              <div className="mx-4 border-t border-slate-800" />
              <div className="flex items-center justify-between px-4 pt-3">
                <span className="text-sm font-semibold">
                  Optional Components
                </span>
                <span className="text-xs text-slate-500">
                  Enhance your experience
                </span>
              </div>
              {info.components
                .filter((c) => !c.required)
                .map((c) => (
                  <ComponentRow key={c.id} c={c} onToggle={toggle} />
                ))}
            </div>

            <button
              onClick={() => setShowAdvanced((v) => !v)}
              className="mt-3 flex items-center gap-2 rounded-lg border border-slate-800 px-4 py-2.5 text-left text-sm hover:bg-slate-900"
            >
              <span>⚙</span> Advanced options
              <span className="ml-auto">{showAdvanced ? '▾' : '▸'}</span>
            </button>
            {showAdvanced && (
              <div className="mt-2 rounded-lg border border-slate-800 px-4 py-3 text-xs text-slate-400">
                Version {info.version} · Stable channel · Installs for all
                users (elevation required) · Creates Start Menu shortcut ·
                Previous settings in HKCU are preserved on upgrade.
              </div>
            )}

            <div className="mt-auto flex items-end justify-between pt-4">
              <div className="text-sm text-slate-300">
                <div>
                  Required space:{' '}
                  {formatBytes(
                    info.components
                      .filter((c) => c.selected)
                      .reduce((s, c) => s + c.sizeBytes, 0),
                  )}
                </div>
                <div>Available space: {formatBytes(info.availableBytes)}</div>
              </div>
              <div className="flex gap-2">
                <Button
                  variant="outline"
                  className="border-slate-700 bg-transparent hover:bg-slate-800"
                >
                  Cancel
                </Button>
                <Button
                  onClick={startInstall}
                  className="bg-[#2f80ed] px-8 hover:bg-[#3f8bff]"
                >
                  Install →
                </Button>
              </div>
            </div>
          </>
        )}

        {screen === 'installing' && (
          <div className="flex flex-1 flex-col justify-center">
            <h1 className="text-3xl font-bold">Installing Lunar Player</h1>
            <p className="mt-1 text-sm text-slate-400">{progress.phase}</p>
            <div className="mt-4 h-2 overflow-hidden rounded-full bg-slate-800">
              <div
                className="h-full rounded-full bg-[#2f80ed] transition-[width]"
                style={{ width: `${progress.percent}%` }}
              />
            </div>
            <div className="mt-2 text-sm text-slate-300">
              {progress.detail}
            </div>
            <div className="mt-1 text-2xl font-semibold">
              {progress.percent}%
            </div>
            <div className="mt-6">
              <Button
                variant="outline"
                className="border-slate-700 bg-transparent hover:bg-slate-800"
                onClick={cancelInstall}
              >
                Cancel
              </Button>
            </div>
          </div>
        )}

        {screen === 'complete' && (
          <div className="flex flex-1 flex-col items-start justify-center">
            <div className="text-4xl">✓</div>
            <h1 className="mt-2 text-3xl font-bold">Lunar Player is ready</h1>
            <p className="mt-1 text-sm text-slate-400">
              Installation completed successfully.
            </p>
            {info && (
              <p className="mt-1 text-sm text-slate-400">
                Version {info.version}
              </p>
            )}
            <Button
              className="mt-6 bg-[#2f80ed] px-8 hover:bg-[#3f8bff]"
              onClick={() => void bridge.launchPlayer()}
            >
              Launch Lunar Player
            </Button>
          </div>
        )}

        {(screen === 'error' || screen === 'cancelled') && (
          <div className="flex flex-1 flex-col items-start justify-center">
            <h1 className="text-3xl font-bold">
              {screen === 'error' ? 'Installation failed' : 'Cancelled'}
            </h1>
            <p className="mt-1 max-w-[520px] text-sm text-slate-400">
              {screen === 'error'
                ? error ||
                  'An error occurred. Previous version (if any) was rolled back untouched.'
                : 'The installation was cancelled before any changes were applied.'}
            </p>
            <Button
              className="mt-6 bg-[#2f80ed] hover:bg-[#3f8bff]"
              onClick={() => setScreen('install')}
            >
              Back
            </Button>
          </div>
        )}
      </div>
    </div>
  );
}
