# Lunar Player code-signing policy

## Production artifacts are built from this repository

Every production binary (`LunarPlayerInstaller.exe`, `LunarPlayer.exe`,
`LunarPlayerUninstaller.exe`, MSIs) is built from the source in this
repository through `Installer/scripts/package-release.ps1`. No production
binary is produced from unpublished source.

## Distribution

Production artifacts are distributed exclusively through GitHub Releases
on this repository. Each release publishes SHA-256 hashes (and sizes) for
every downloadable file in its release notes. Verify downloads against
those hashes before running them.

## No self-signed production signatures

Self-signed certificates are never used for production releases. They are
not trusted by Windows on users' PCs and change nothing about SmartScreen.
Test-only certificates may appear inside throwaway test harnesses (e.g.
`Installer/scripts/test-updater-live.ps1` generates a 2-day local test CA
at runtime); those never ship.

## Intended signer: SignPath Foundation

Public Windows binaries are intended to be signed through SignPath
Foundation's free code-signing program for open-source projects
(OV-level certificate issued to the Foundation, publisher reads as
SignPath Foundation), pending project acceptance. Until a signed binary
is published, Windows SmartScreen will warn on first run because the
installer is unsigned — this is expected operating-system behavior for
new, unrecognized executables, and reputation accrues to consistently
signed releases over time.

## Verifying a release

1. Check the file's Authenticode signature (`Get-AuthenticodeSignature`)
   once signed binaries are published.
2. Compare the file's SHA-256 against the release notes.
3. Confirm the binary was built from the tagged source in this repository.
