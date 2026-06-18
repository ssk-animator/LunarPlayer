# Competitor Analysis

## VLC Media Player

### Strengths
- Universal format support via FFmpeg
- Mature, stable codebase (20+ years)
- Extensive subtitle engine
- Streaming and network playback
- Plugin ecosystem (Lua scripts)
- Zero-cost, open source (GPLv2)

### Weaknesses
- Software rendering by default; GPU acceleration inconsistent
- UI feels dated (Qt4-era design)
- Frame-accurate seeking unreliable
- No native EXR or DPX sequence support
- No creator-focused tools (histogram, waveform, LUT)
- No version comparison workflow

### Opportunities
- GPU-accelerated rendering pipeline
- Modern, minimal UI that gets out of the way
- Frame-accurate architecture built from ground up
- Creator tools (compare mode, version stacking) as differentiator

---

## mpv

### Strengths
- Excellent GPU acceleration (vo=gpu, vo= vulkan)
- Scriptable via Lua
- High-quality upscaling shaders (FSR, RAISR, etc.)
- Minimal, keyboard-driven interface
- Active development
- Frame-accurate seeking

### Weaknesses
- No native GUI (OSC is basic)
- No timeline thumbnails
- No built-in creator tools
- Configuration-driven, not user-friendly for casual users
- Limited subtitle styling compared to VLC
- No version comparison workflow

### Opportunities
- Ship with a polished Qt6 GUI out of the box
- Integrated creator tools (histogram, waveform, compare mode)
- Timeline with thumbnails and infinite zoom
- Version stack auto-detection for VFX workflows

---

## DJV

### Strengths
- Purpose-built for VFX and animation review
- EXR, DPX, Cineon sequence support
- Frame-accurate with J (back) / K (play) / L (forward) controls
- Side-by-side and wipe compare
- Cross-platform (Qt + OpenGL)

### Weaknesses
- Performance degrades with high-res EXR sequences
- No H.264/H.265 hardware decode
- UI is utilitarian and intimidating to non-artists
- No audio playback
- No subtitle support
- Development has slowed; community is small

### Opportunities
- GPU-accelerated EXR playback that outperforms DJV
- Combined video + sequence player (one tool for all media)
- Modern, approachable UI with the same pro power
- Active development and community

---

## MPC-HC

### Strengths
- Lightweight and fast on Windows
- Excellent DirectShow integration
- MadVR support for high-quality rendering
- Clean, familiar UI
- Low resource usage

### Weaknesses
- Windows only
- No longer actively maintained (clones exist: MPC-BE)
- No creator tools
- No EXR/DPX sequence support
- No version comparison
- Dated codebase

### Opportunities
- Cross-platform alternative with modern rendering
- Continued active development
- Creator tools as differentiator

---

## QuickTime Player

### Strengths
- Clean, minimal UI
- Smooth playback on Apple hardware
- Trimming and sharing built-in
- Familiar to macOS users

### Weaknesses
- macOS only
- Limited format support (no MKV, no VP9, no AV1)
- No frame-accurate stepping (10.5+ improved this)
- No creator tools
- No sequence support
- Apple has de-prioritized development

### Opportunities
- Cross-platform alternative with same polish
- Frame-accurate seeking that actually works
- Creator tools QuickTime will never add
