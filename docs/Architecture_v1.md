# Architecture v1

## High-Level Data Flow (Video)

```
File
 │
 ▼
Media Loader      — Parses container, opens streams
 │
 ▼
FFmpeg Decoder    — Decodes video frames (CPU or GPU via hwaccel)
 │
 ▼
Frame Queue       — Bounded ring buffer of decoded AVFrame pointers
 │
 ▼
GPU Renderer       — Uploads frames to GPU, color conversion, presentation
 │
 ▼
Screen
```

## Audio Pipeline

```
FFmpeg (audio stream)
 │
 ▼
Audio Queue       — Bounded ring buffer of decoded audio samples
 │
 ▼
Audio Output      — Cross-platform audio (Qt6 QAudioOutput or RtAudio)
 │
 ▼
Speakers
```

## Timeline Engine

```
Playback Position — Master clock driven by audio or video PTS
 │
 ▼
Timeline Engine    — Zoom level, visible range, thumbnail generation
 │
 ▼
Thumbnail Engine   — Async, on-demand thumbnail extraction for hover preview
```

## Threading Model

```
Main Thread / UI  — Qt6 event loop, transport controls, window management
Decoder Thread    — FFmpeg decode loop, pushes frames to queues
Renderer Thread   — GPU upload + presentation (Vulkan queue)
Audio Thread      — Audio output callback (pulls from audio queue)
Thumbnail Thread  — Async thumbnail extraction (low priority)
```

## Key Design Decisions

- **Decoder and renderer are decoupled** via Frame Queue. This lets decode run ahead of display.
- **Frame Queue is bounded** to prevent memory exhaustion on long files.
- **Audio is the master clock** when available; video PTS syncs to audio.
- **Vulkan rendering** uses a dedicated thread with its own VkQueue.
- **All pixel format conversion** happens on GPU via shaders (not libswscale).
