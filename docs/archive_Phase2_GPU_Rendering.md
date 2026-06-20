# Phase 2: GPU Rendering Pipeline

## Objective
Migrate video presentation from the current CPU/QImage/QPainter path to a GPU-backed rendering pipeline using OpenGL textures. This is infrastructure work — no new user-facing features.

## Current Architecture (Phase 1)
```
FFmpeg → AVFrame (YUV) → sws_scale → QImage (RGB888) → QPainter::drawImage() → screen
```
- CPU-bound: sws_scale converts every frame on the CPU
- QImage is copied per-frame
- QPainter renders via CPU blit
- No GPU texture reuse

## Target Architecture (Phase 2)
```
FFmpeg → AVFrame (YUV) → OpenGL texture upload → GLSL shader (YUV→RGB) → screen
```
- YUV data uploaded directly to GPU as luminance textures
- Color space conversion done in fragment shader
- Zero CPU copy for color conversion
- GPU bilinear filtering for free

## Implementation Plan

### 1. OpenGL Texture Management (`src/renderer/`)
- `GLVideoRenderer` class wrapping:
  - Texture creation/deletion
  - YUV plane upload (3 textures: Y, U, V)
  - Shader compilation (vertex + fragment)
  - Uniform/attribute setup

### 2. Shader Program
- Vertex shader: textured quad
- Fragment shader: YUV→RGB conversion
  - BT.709 / BT.601 matrix support
  - Normalized coordinates

### 3. VideoWidget Upgrade
- Replace QPainter::drawImage() path in `paintGL()`
- Call GLVideoRenderer::render() instead
- Upload only when new frame arrives (via QImage-free signal)
- Keep fallback to QImage path for compatibility

### 4. MediaSession → Renderer Bridge
- Expose AVFrame data directly (YUV planes, strides)
- Add `currentFrameData()` returning YUV + metadata
- Avoid QImage conversion entirely in the fast path

### 5. New Files
```
src/renderer/GLVideoRenderer.h
src/renderer/GLVideoRenderer.cpp
src/renderer/yuv_to_rgb.glsl       (embedded string or external)
```

### 6. Modified Files
```
src/ui/VideoWidget.h     — new members for renderer
src/ui/VideoWidget.cpp   — replace paintGL logic
src/ui/MainWindow.h      — optional: expose AVFrame
CMakeLists.txt           — add renderer/ to build
```

## Success Criteria
1. MP4 open still works
2. Play still works
3. Pause still works
4. Seek still works
5. Close still works
6. All existing tests continue to pass (CTest: 3/3)
7. Video frames rendered through GPU (no QImage in hot path)
8. Equivalent visual output (subjective: no regression)

## Non-Goals (explicitly excluded)
- Timeline features (zoom, thumbnails)
- EXR/raw image support
- Compare/side-by-side mode
- Subtitle engine
- Plugin system
- Audio pipeline changes
- New UI widgets
- HDR/color management (Phase 3)
- Vulkan backend (Phase 3+)

## Test Plan
1. Run `ctest` — all 3 suites must pass unmodified
2. Run `LunarPlayerManualQA.exe` — 11/11 acceptance tests must pass
3. Visual verification with Motul 1920×1080 video
4. Verify no QImage construction in paint path (perf trace)

## Rollback
If GPU pipeline introduces regressions, the QPainter fallback path is retained via `#define USE_GPU_RENDERER 1` toggle in `VideoWidget.h`. Phase 1 rendering is always available.
