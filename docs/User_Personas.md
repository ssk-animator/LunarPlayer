# User Personas

## Persona 1 — Consumer

**Name:** Alex
**Occupation:** Student / Casual viewer

**Needs:**
- Movies
- Anime
- TV Shows

**Pain Points:**
- Slow opening — double-clicking a video file should show video instantly
- Subtitle issues — wrong encoding, out of sync, missing fonts
- Complicated UI — too many buttons, panels, menus
- Codec confusion — "Why won't this file play?"

**Lunar Player Fit:**
- Instant open (<1 second)
- Zero-clutter UI — just transport controls
- FFmpeg backend for broad format support
- Reliable subtitle rendering with sensible defaults

---

## Persona 2 — Video Editor

**Name:** Jordan
**Occupation:** Freelance video editor

**Needs:**
- Frame stepping — J/K/L or arrow keys, one frame at a time
- Accurate seeking — land on the exact frame, not a keyframe
- Quick review — open cuts, transitions, effects frame-by-frame
- Screenshots — export exact frames as PNG

**Pain Points:**
- Inaccurate frame navigation — seeking snaps to nearest keyframe instead of exact frame
- Slow startup — waiting for the player to load
- No frame counter — hard to communicate "at 01:23:15.12" to collaborators

**Lunar Player Fit:**
- Frame-accurate decoding pipeline
- Frame counter display
- Instant open
- Screenshot export (P1)

---

## Persona 3 — 3D Animator

**Name:** Morgan
**Occupation:** 3D animator / VFX artist

**Needs:**
- EXR sequence review — play back rendered frames at full float precision
- Version comparison — compare v001 vs v002 side-by-side
- High-res support — 4K, 8K, anamorphic EXR sequences
- A/B wipe — split-screen comparison of two versions

**Pain Points:**
- DJV performance — stutters on 4K+ EXR sequences
- Missing compare tools — no built-in diff or version stacking
- No audio — can't sync animation to sound
- No H.264 decode — have to switch players for video dailies

**Lunar Player Fit:**
- GPU-accelerated EXR playback (P3)
- Version stack auto-detection (P3)
- Difference viewer (P3)
- Combined sequence + video playback

---

## Persona 4 — Colorist

**Name:** Sam
**Occupation:** Colorist / finishing artist

**Needs:**
- Color accuracy — pixel-perfect color representation
- LUT support — apply 1D and 3D LUTs for preview
- Histogram / waveform — scopes for exposure and color analysis
- Reference comparison — compare graded vs ungraded

**Pain Points:**
- Inconsistent color management — each player interprets color differently
- No scopes — have to use Resolve or Nuke just to check levels
- No LUT loading — can't preview with grading LUT applied

**Lunar Player Fit:**
- Color-accurate rendering pipeline (P2)
- LUT support (P2)
- Histogram and waveform scopes (P2)
- Compare mode for reference (P2)
