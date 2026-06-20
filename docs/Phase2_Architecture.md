# Phase 2: Renderer Abstraction Layer

## Objective
Decouple video presentation from playback logic by introducing a `Renderer` abstraction. This isolates rendering concerns behind a stable interface so future GPU backends (OpenGL, Vulkan) can be swapped in without touching playback code.

## Current Architecture (Phase 1)

```
FFmpeg decode → MediaSession → QImage → VideoWidget → QPainter → screen
                     ↑                          ↑
               Core playback              Rendering logic
               (sws_scale,                (aspect-ratio, paintGL)
                seeking, frames)
```

The rendering path is inline inside `VideoWidget::paintGL()` — there is no separation between "what to draw" and "how to draw it."

## Target Architecture (Phase 2A)

```
FFmpeg decode → MediaSession → QImage → VideoWidget → RendererFactory
                                                           ↓
                                                     Renderer
                                                        ↓
                                                   CPURenderer
```

Renderer hierarchy for future extension:

```
Renderer (interface)
 ├── CPURenderer        ← Phase 2A (current, QPainter-based)
 └── GLVideoRenderer    ← Phase 2B (future, OpenGL-based)
```

- `VideoWidget` owns a `Renderer*` obtained from `RendererFactory`.
- `MediaSession` is untouched — its API is frozen.
- `Renderer` is an abstract base class; `CPURenderer` is the concrete implementation using QPainter.
- `RendererFactory` selects the renderer implementation; VideoWidget does not know which concrete class is used.
- Future GPU renderers implement the same `Renderer` interface and register with the factory.

## Phase 2A Scope

Phase 2A introduces **architecture only**:

- No rendering technology changes
- No GPU code
- No AVFrame exposure
- No shaders or textures
- No QImage replacement in the pipeline

## Renderer Interface

```cpp
class Renderer {
public:
    virtual ~Renderer() = default;

    virtual bool initialize() = 0;
    virtual void present(const QImage &frame) = 0;
    virtual void paint(QPainter &painter, const QRect &outputRect) = 0;
    virtual void resize(int w, int h) = 0;
    virtual void cleanup() = 0;
    virtual bool isValid() const = 0;
};
```

| Method | When called | Purpose |
|--------|-------------|---------|
| `initialize()` | `VideoWidget::initializeGL()` | Set up renderer resources |
| `present()` | `VideoWidget::setFrame()` | Deliver a new decoded frame |
| `paint()` | `VideoWidget::paintGL()` | Draw the current frame to screen |
| `resize()` | `VideoWidget::resizeGL()` | Handle viewport changes |
| `cleanup()` | `VideoWidget` destructor | Release renderer resources |
| `isValid()` | `VideoWidget::paintGL()` | Check if renderer is ready |

## RendererFactory Interface

```cpp
enum class RendererType { CPU };

class RendererFactory {
public:
    static std::unique_ptr<Renderer> Create(RendererType type);
};
```

Current mapping:

| Type | Returns |
|------|---------|
| `RendererType::CPU` | `std::make_unique<CPURenderer>()` |

Future mapping (Phase 2B):

| Type | Returns |
|------|---------|
| `RendererType::CPU` | `CPURenderer` |
| `RendererType::GPU` | `GLVideoRenderer` |

## Files Changed

### New (Phase 2A)
- `src/renderer/Renderer.h` — Abstract base class interface
- `src/renderer/CPURenderer.h` — CPU renderer declaration
- `src/renderer/CPURenderer.cpp` — QPainter-based implementation
- `src/renderer/RendererFactory.h` — Factory declaration
- `src/renderer/RendererFactory.cpp` — Factory implementation

### Modified
- `src/ui/VideoWidget.h` — `Renderer*` member, `computeOutputRect()` helper, no GPU members
- `src/ui/VideoWidget.cpp` — Routes `setFrame()` → `Renderer::present()`, `paintGL()` → `Renderer::paint()`; uses `RendererFactory::Create()` instead of `new CPURenderer()`
- `CMakeLists.txt` — Adds `Renderer.h`, `CPURenderer`, `RendererFactory` to all targets

### Unchanged
- `src/ui/MainWindow.h` — MediaSession API frozen (no FrameData, no setCacheQImage)
- `src/ui/MainWindow.cpp` — `readFrame()` always creates QImage; all `setFrame()` calls restored
- All test files — No behavioral changes
- `third_party/` — No changes

## Design Decisions

1. **Renderer takes `QPainter&` in `paint()`** — This lets the CPU renderer draw naturally. Future GPU renderers can use `beginNativePainting()`/`endNativePainting()` to escape the QPainter and issue raw OpenGL calls.

2. **MediaSession API is frozen** — No new methods on MediaSession. The `QImage` is still the interchange format between decoder and renderer. A future GPU phase can introduce a `VideoFrame` struct with YUV plane pointers without changing this interface.

3. **Single renderer instance from factory** — `VideoWidget` creates one renderer at startup via `RendererFactory::Create()`. Future phases will select the renderer based on capability (e.g., `GPURenderer` if OpenGL 3.3+ available, else `CPURenderer`).

4. **`present()` vs `paint()` separation** — `present()` is called from the playback thread (via `setFrame()` → `update()`). `paint()` is called from Qt's GUI thread during `paintGL()`. This allows the renderer to decouple frame delivery from frame display.

5. **Factory returns `std::unique_ptr`** — The factory owns the allocation policy. VideoWidget uses `.release()` on the pointer since the Qt OpenGL lifecycle (`makeCurrent`/`doneCurrent`) requires manual management with `new`/`delete`.

## Success Criteria

1. All existing Phase 1 tests pass unmodified (CTest: 3/3)
2. MediaSessionAPI test passes (CTest: +1)
3. All acceptance tests pass (8/8)
4. All manual QA tests pass (11/11)
5. No user-visible behavior changes
6. MediaSession API has zero additions or modifications
7. `Renderer` interface is purely virtual — no GPU code anywhere
8. VideoWidget is implementation-agnostic (no concrete renderer includes)
