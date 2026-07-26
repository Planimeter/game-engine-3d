# Planimeter Game Engine 3D — Session Memory

## Code Review Completed: 2026-07-26

A comprehensive code review of the Planimeter Game Engine 3D project was performed. All source files in `src/` and `third_party/` were read and analyzed. See full review summary below.

---

## Architecture Summary

**Layered module architecture** with C99/C++20, Vulkan graphics via volk+VMA, SDL3 windowing, OpenAL audio, PhysFS filesystem, FreeType+HarfBuzz fonts, Assimp 3D model loading, GLM math library, and optional CEF (Chromium 145) browser integration.

### Key Modules
| Module | API Header | Vulkan Impl | Null/Stub Impl |
|--------|-----------|-------------|----------------|
| Graphics | `src/graphics.h` | `src/graphics_vulkan.cpp` | `src/graphics_null.c`, `src/graphics_opengl_sdl.c` |
| Window/Input | `src/window.h`, `src/event.h` | `src/window_sdl.c`, `src/event_sdl.c` | `src/window_null.c`, `src/event_null.c` |
| Audio | `src/audio.h` | `src/audio_openal.c` | `src/audio_null.c` |
| Font/Text | `src/font.h`, `src/text.h` | `src/font.c`, `src/text.c` | — (font has no stub) |
| Model | `src/model.h` | `src/model_assimp.cpp` | `src/model_null.c` |
| Filesystem | `src/filesystem.h` | `src/filesystem_physfs.c` | `src/filesystem_null.c`, `src/filesystem_posix.c` |
| Timer | `src/timer.h` | `src/timer_sdl.c` | `src/timer_null.c` |
| Image | `src/image.h` | `src/image_stb.c` | `src/image_null.c` |
| Job System | `src/job.h` | `src/job_pthread.c` | `src/job_null.c` |

### Game Loop Pattern (`main_sdl.c`)
```
event_poll() → timer_step() → job_submit(update) → job_wait(update) 
           → job_submit(draw)  → job_wait(draw)  → graphics_present()
```

---

## Fixed Issues

### ✅ P0 #1 — Audio Source Leak (FIXED)
- **Commit:** cf5065e7
- **Fix:** Added `audio_stop_source(int source)` and `audio_get_source_state(int source)` to all audio backends. Sources are now properly stoppable and freeable, preventing OpenAL source exhaustion.

### ✅ P0 #2 — Timer Unit Mismatch (RETRACTED)
- **Commit:** 110d0a19
- **Resolution:** Original assessment was incorrect — timer units are consistent throughout (milliseconds). No bug exists. Applied trivial SDL3 best-practice fix: `SDL_GetTicks()` → `SDL_GetTicks64()`.

---

## Critical Issues Found

### P1 — High Priority
1. **No Uniform Buffer Upload API** — Graphics material system only stores one mat4 locally. No mechanism to bind uniform buffers for per-frame data (view/projection matrices, lights). Add `graphics_binduniformbuffer(slot, Buffer)`.
2. **Font `font_print()` Allocation Pattern** — malloc/free vertex/index buffers on every call. Pre-allocate and resize incrementally.
3. **Batching System Re-shapes Text** — `font_end_batch()` recomputes all glyph shaping even though it was done during `font_batch_print()`. Cache shaped results instead.

### P2 — Medium Priority
4. **No Render Target / Framebuffer Abstraction** — Hardcoded to swapchain image only. No post-processing, shadow maps, or deferred rendering possible.
5. **No Skeletal Animation in Model Loader** — Assimp provides full animation data (`mAnimations`, `mBones`) but it's ignored. Only static meshes load.
6. **Font Atlas Can't Grow** — 1024×1024 atlas is fixed size. Once full, new glyphs silently fail.

### P3 — Low Priority / Nice-to-Have
7. **Global `g_jobSystem`** — Makes multi-window and editor+game co-existence difficult. Consider per-engine instances.
8. **No Memory Tracking** — Engine has no allocator wrapper or leak detection.
9. **No Install Target in CMake** — Missing `install()` rules.

---

## Job System Design Notes

The job system (`job_pthread.c`, ~23KB) is the most sophisticated component and shows genuine systems engineering:

- **O(1) handle lookup**: Handle encodes slot index + generation counter (bits [63:48] = slot, bits [47:0] = generation). No linear scan.
- **Wait-that-does-work**: `job_wait()` executes pending work via `job_try_get_work()` instead of spinning or sleeping. Directly inspired by Molecular Matters and Naughty Dog GDC talks.
- **Parent-child unfinishedJobs**: Parent cannot complete until all children finish. TOCTOU-safe registration (increment parent counter first, verify slot not recycled).
- **Work-stealing**: Workers steal from queue backs with random start index for load balancing.
- **Condition variable sleep/wake**: Workers block efficiently on empty queues.

**Limitations:**
- `MAX_JOBS = 65536` with no monitoring — deeply nested job graphs could exhaust slots silently.
- `MAX_WORKERS = 16`, `MAX_JOB_QUEUE = 1024` are hardcoded constants.

---

## Build System Notes

- CMake 3.21+, auto-downloads CEF from Spotify's builds server
- Windows: links winmm, setupapi, version, imm32, crypt32, wintrust
- macOS: links AVFoundation, CoreAudio, AudioToolbox, Foundation, CoreGraphics, CoreHaptics, CoreMedia, CoreVideo, ForceFeedback, GameController, IOKit, Carbon, Metal, AppKit, QuartzCore, UniformTypeIdentifiers
- Linux/Unix: pthreads + pkg-config for XCB/X11/Wayland
- CEF requires temporary `BUILD_SHARED_LIBS = ON` (commented as temporary)

---

## Files Read (Complete List)

### Headers (all src/)
- `framework.h`, `graphics.h`, `window.h`, `event.h`, `audio.h`, `model.h`, `font.h`, `text.h`, `image.h`, `timer.h`, `filesystem.h`, `job.h`

### Core Implementations
- `main_sdl.c`, `window_sdl.c`, `event_sdl.c`, `graphics_vulkan.cpp` (105KB), `audio_openal.c`, `filesystem_physfs.c`, `font.c`, `framework.c`, `image_stb.c`, `job_pthread.c`, `model_assimp.cpp`, `text.c`, `timer_sdl.c`

### Stub/Null Implementations
- `graphics_null.c`, `graphics_opengl_sdl.c`, `window_null.c`, `event_null.c`, `audio_null.c`, `filesystem_null.c`, `filesystem_posix.c`, `model_null.c`, `image_null.c`, `timer_null.c`, `job_null.c`, `main_null.c`

### Build & Config
- `CMakeLists.txt`, `README.md`

### Shaders (directory listing only)
- `src/shaders/`: default.frag, default2d.vert, default3d.vert, depth.frag, depth.vert, pbr-frag.glsl, pbr-vert.glsl, skybox.frag, skybox.vert, text.frag, text.frag.spv, text.vert, text.vert.spv, triangle.frag, triangle.frag.spv, triangle.vert, triangle.vert.spv

---

## Rating: 7/10
Solid foundation with clear path to production readiness. The module separation is clean, the abstraction layering is mature, and the job system implementation shows genuine understanding of industry-standard patterns. Main gaps are rendering infrastructure (uniforms, render targets) and audio resource management.
