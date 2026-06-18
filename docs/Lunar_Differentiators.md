# Lunar Differentiators

## Instant Open

**Target:** < 1 second from double-click to first frame displayed.

No splash screen. No loading bar. No "building index..." No pre-processing. Open the file, decode the first frame, show it. Everything else happens in background threads.

---

## Infinite Timeline

Like Blender's Video Sequence Editor — the timeline zooms in to frame level or out to show the full duration. No fixed zoom levels. Scroll to any precision. The thumbnail engine populates visible regions on demand, never the full file.

---

## Creator Mode

**Press:** F3

Default UI is a clean, minimal player — transport bar, seek bar, fullscreen toggle. Press F3 to reveal creator tools: histogram, waveform, LUT panel, compare mode, frame counter, version stack browser. Press F3 again to hide them all. Zero clutter by default, full power on demand.

---

## Version Compare

Place multiple versions of the same shot in a folder:

```
shot_v001.exr
shot_v002.exr
shot_v003.exr
```

Lunar Player auto-detects the version stack. Navigate between versions with Up/Down arrows. Compare side-by-side, split-wipe, or A/B toggle. Difference mode highlights changed pixels.

---

## Massive File Support

**Target:** 100GB+ files without pre-processing or indexing.

No "importing..." step. No pre-built index files. No RAM limit on file size. The decoder reads only what's needed for the current display. The frame queue bounds memory usage. Files of any size open instantly.
