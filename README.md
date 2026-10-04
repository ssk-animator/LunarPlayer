# Lunar Player

Professional media player for animators, VFX artists, editors, and review workflows. Windows (x64).

> Alpha / pre-release: bugs and incomplete features are expected.

## Download

Get the installer from GitHub Releases (Alpha, pre-release):

https://github.com/ssk-animator/LunarPlayer/releases/tag/v0.1.0-alpha

Download `LunarPlayerInstaller.exe`, run it, accept the UAC prompt, and click Install once. The installer downloads verified Core/AI packages from the same release into `C:\Program Files\Lunar Player`. AI models download separately on first Studio use.

Verify downloads against the SHA-256 values published in the release notes.

## Repository layout

- `src/` — player application source
- `Installer/` — installer + updater sources and packaging scripts
  - `Installer/standalone/` — standalone elevated installer (`LunarPlayerInstaller.exe`, CMake, x64)
  - `Installer/bootstrapper/` — native installer backend (Direct2D UI, downloader, maintenance)
  - `Installer/burn/` — legacy WiX Burn bundle definitions
  - `Installer/scripts/` — `package-release.ps1` (release pipeline), `sign-release.ps1` (signing gate)
  - `Installer/updater/` — auto-updater sources
  - `Installer/ui/` — updater web UI sources
- `assets/`, `icons/` — product artwork
- `docs/`, `tests/`, `scripts/`, `cmake/`, `third_party/` — supporting material

## Building the installer (Windows)

Prerequisites: Visual Studio 2022 (MSVC v143+), CMake 3.20+, Windows 10 SDK.
The legacy Burn path additionally needs WiX Toolset v3.14.

```powershell
# 1. Native backend + tests (Win32)
& "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  Installer\bootstrapper\build_x86\LunarBootstrapper.sln `
  /p:Configuration=Release /p:Platform=Win32

# 2. Full release package (builds the x64 standalone installer, MSIs,
#    sidecars, manifests, hashes)
powershell -ExecutionPolicy Bypass -File Installer\scripts\package-release.ps1 `
  -Version 0.1.0-alpha
```

See `SIGNING.md` for the code-signing policy. Licensed under the MIT License (`LICENSE`).
