import { useEffect, useState } from 'react';
import { getUpdaterBridge, type UpdateState } from '../bridge';
import { Button } from '@/components/ui/button';

type UPhase =
  | 'checking'
  | 'uptodate'
  | 'available'
  | 'downloading'
  | 'verifying'
  | 'ready'
  | 'waiting-player'
  | 'launched'
  | 'error';

function formatBytes(b: number): string {
  if (b <= 0) return '—';
  const gb = b / (1024 * 1024 * 1024);
  if (gb >= 1) return `${gb.toFixed(2)} GB`;
  return `${Math.round(b / (1024 * 1024))} MB`;
}

export default function UpdaterApp() {
  const [phase, setPhase] = useState<UPhase>('checking');
  const [state, setState] = useState<UpdateState | null>(null);
  const [version, setVersion] = useState('');
  const [size, setSize] = useState(0);
  const [received, setReceived] = useState(0);
  const [total, setTotal] = useState(0);
  const [publisher, setPublisher] = useState('');
  const [error, setError] = useState('');
  const bridge = getUpdaterBridge();

  useEffect(() => {
    if (!bridge) {
      setError('No native updater backend.');
      setPhase('error');
      return;
    }
    bridge
      .getUpdateState()
      .then((s) => {
        setState(s);
        return bridge.checkForUpdate();
      })
      .then((u) => {
        if (u.updateAvailable) {
          setVersion(u.version);
          setSize(u.size ?? 0);
          setPhase('available');
        } else {
          setPhase('uptodate');
        }
      })
      .catch((e: unknown) => {
        setError(e instanceof Error ? e.message : String(e));
        setPhase('error');
      });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const download = async () => {
    if (!bridge) return;
    setPhase('downloading');
    try {
      await bridge.downloadUpdate((r, t) => {
        setReceived(r);
        setTotal(t);
      });
      setPhase('verifying');
      const v = await bridge.verifyUpdate();
      setPublisher(v.publisher);
      setPhase('ready');
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : String(e);
      if (/cancel/i.test(msg)) setPhase('available');
      else {
        setError(msg);
        setPhase('error');
      }
    }
  };

  const install = async () => {
    if (!bridge) return;
    setPhase('waiting-player');
    try {
      await bridge.waitForPlayerExit();
      await bridge.launchInstaller();
      setPhase('launched');
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : String(e));
      setPhase('error');
    }
  };

  const pct =
    total > 0 ? Math.round((received / total) * 100) : 0;

  return (
    <div className="flex h-screen flex-col bg-[#0b1322] px-10 py-8 font-sans text-slate-100">
      <header className="mb-2 flex items-center gap-2">
        <span className="text-lg">🌙</span>
        <span className="text-sm text-slate-300">Lunar Player Updater</span>
      </header>

      {phase === 'checking' && (
        <p className="mt-8 text-slate-300">
          Checking for updates{state ? ` (installed ${state.installedVersion})` : ''}…
        </p>
      )}
      {phase === 'uptodate' && (
        <>
          <h1 className="mt-8 text-3xl font-bold">You&apos;re up to date</h1>
          <p className="mt-1 text-sm text-slate-400">
            Lunar Player {state?.installedVersion} is the latest {state?.channel} release.
          </p>
        </>
      )}
      {phase === 'available' && (
        <>
          <h1 className="mt-8 text-3xl font-bold">
            Lunar Player {version} available
          </h1>
          <p className="mt-1 text-sm text-slate-400">
            Download size {formatBytes(size)}. The installer handles the
            upgrade; your settings are preserved.
          </p>
          <div className="mt-6">
            <Button
              onClick={download}
              className="bg-[#2f80ed] px-8 hover:bg-[#3f8bff]"
            >
              Update Now
            </Button>
          </div>
        </>
      )}
      {phase === 'downloading' && (
        <>
          <h1 className="mt-8 text-3xl font-bold">Downloading</h1>
          <div className="mt-4 h-2 overflow-hidden rounded-full bg-slate-800">
            <div
              className="h-full rounded-full bg-[#2f80ed]"
              style={{ width: `${pct}%` }}
            />
          </div>
          <p className="mt-2 text-sm text-slate-300">
            {formatBytes(received)} / {formatBytes(total)} · {pct}%
          </p>
          <div className="mt-4">
            <Button
              variant="outline"
              className="border-slate-700 bg-transparent hover:bg-slate-800"
              onClick={() => bridge?.cancelDownload()}
            >
              Cancel
            </Button>
          </div>
        </>
      )}
      {phase === 'verifying' && (
        <p className="mt-8 text-slate-300">
          Verifying size, SHA-256 and publisher signature…
        </p>
      )}
      {phase === 'ready' && (
        <>
          <h1 className="mt-8 text-3xl font-bold">Ready to install</h1>
          <p className="mt-1 text-sm text-slate-400">
            Verified{publisher ? ` (${publisher})` : ''}. Close Lunar Player
            if it is running, then continue.
          </p>
          <div className="mt-6">
            <Button
              onClick={install}
              className="bg-[#2f80ed] px-8 hover:bg-[#3f8bff]"
            >
              Install Update
            </Button>
          </div>
        </>
      )}
      {phase === 'waiting-player' && (
        <p className="mt-8 text-slate-300">
          Waiting for Lunar Player to exit…
        </p>
      )}
      {phase === 'launched' && (
        <>
          <h1 className="mt-8 text-3xl font-bold">Installer launched</h1>
          <p className="mt-1 text-sm text-slate-400">
            This updater will now exit. The installer owns the upgrade from
            here.
          </p>
        </>
      )}
      {phase === 'error' && (
        <>
          <h1 className="mt-8 text-3xl font-bold">Update failed</h1>
          <p className="mt-1 max-w-[520px] text-sm text-slate-400">{error}</p>
          <div className="mt-6">
            <Button
              className="bg-[#2f80ed] hover:bg-[#3f8bff]"
              onClick={() => setPhase('checking')}
            >
              Retry
            </Button>
          </div>
        </>
      )}
    </div>
  );
}
