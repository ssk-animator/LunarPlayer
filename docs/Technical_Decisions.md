# Technical Decisions

## Decision 1 — Language

**Choose:** C++20

**Reason:** Performance critical. Zero-cost abstractions, deterministic resource management, and direct GPU API access. C++20 provides concepts, coroutines, spans, and improved constexpr — all useful for a media pipeline.

---

## Decision 2 — Media Backend

**Choose:** FFmpeg (libavformat, libavcodec, libavutil, libswscale, libswresample)

**Reason:** Industry standard. Every major player (VLC, mpv, MPC-HC, DJV) uses FFmpeg internally. Support for practically every codec and container. Active development. LGPL/GPL licensed.

---

## Decision 3 — UI Framework

**Choose:** Qt6

**Reason:** Cross-platform (Windows, macOS, Linux). Mature, well-documented, with strong GPU integration via QVulkanWindow and QOpenGLWidget. Signals/slots map naturally to media player events. Qt6's CMake integration is first-class.

---

## Decision 4 — Graphics API

**Primary:** Vulkan

**Fallback:** OpenGL 4.6

**Reason:** Vulkan is future-proof — explicit GPU control, lower driver overhead, better multithreading, and cross-platform (Windows, Linux, macOS via MoltenVK). OpenGL 4.6 serves as fallback for older hardware.

---

## Decision 5 — Project Structure

```
LunarPlayer/
├── docs/           # Documentation
├── src/
│   ├── core/       # Application, config, logging, platform abstraction
│   ├── decoder/    # FFmpeg wrapper, frame decoding, audio decoding
│   ├── renderer/   # Vulkan/OpenGL rendering, shaders, presentation
│   ├── timeline/   # Playback position, thumbnail engine, zoom
│   ├── ui/         # Qt6 widgets, windows, transport controls
│   ├── subtitles/  # Subtitle parsing and rendering
│   └── review/     # Creator tools (compare, diff, histogram, etc.)
├── tests/          # Unit and integration tests
└── assets/         # Icons, default shaders, sample configs
```
