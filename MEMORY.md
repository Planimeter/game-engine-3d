# Planimeter Game Engine 3D — Session Memory

## Code Reviews

### Review #1: 2026-07-26
Initial comprehensive code review. All source files in `src/` and `third_party/` were read and analyzed.

### Review #2: 2026-07-27
Follow-up review verifying prior fixes and discovering new critical rendering pipeline issues.

---

## Fixes Applied (11 total)

### Fix #1 — SDL_Vulkan_CreateSurface return value unchecked
- `src/window_sdl.c`, `src/window.h`, `src/window_null.c`
- Changed return type to `int`, validate SDL result

### Fix #2 — WAV audioFormat not validated
- `src/audio_openal.c`
- Added PCM-only validation (format==1), reject non-PCM

### Fix #3 — VK_NULL_HANDLE semaphores destroyed
- `src/graphics_vulkan.cpp`
- Added `VK_NULL_HANDLE` guards before vkDestroySemaphore

### Fix #4 — OpenAL source not cleaned up on config failure
- `src/audio_openal.c`
- Added error check after alSourcei/alSourcef; delete source on failure

### Fix #5 — frameTime on stack (dangling pointer risk)
- `src/main_sdl.c`
- Made `g_frameTime` file-scope static for async job safety

### Fix #6 — Swapchain resize didn't recreate semaphores, render pass, or pipeline layout
- `src/graphics_vulkan.cpp`
- `graphics_resize()` now recreates semaphores, shaders, render pass, and graphics pipeline
- Added `VK_NULL_HANDLE` guard + vkDestroy before creating new render pass and pipeline layout

### Fix #7 — graphics_material_set_texture ignored name parameter
- `src/graphics_vulkan.cpp`
- Added `MaterialTexture[8]` array to `GPUMaterial`, store by name, bind all on setmaterial

### Fix #8 — Joystick/gamepad events fell through to default
- `src/event_sdl.c`, `src/framework.h`, `src/framework.c`
- Wired 12 SDL event types to 12 new framework callback stubs

### Fix #9 — graphics_transition_image only handled 2/9 transitions
- `src/graphics_vulkan.cpp`
- Expanded to 9 transitions with correct stage/access masks per pair; added depth aspect support

### Fix #10 — font_print malloc/free on every call
- `src/font.c`
- Added scratch buffers to `Font` struct, grow on demand, reuse across calls

### Fix #11 — font_end_batch shaped text twice
- `src/font.c`
- Single shape pass stores results in `ShapedLine` array; geometry built from stored data

---

## Architecture Summary

**Layered module architecture** with C99/C++20, Vulkan graphics via volk+VMA, SDL3 windowing, OpenAL audio, PhysFS filesystem, FreeType+HarfBuzz fonts, Assimp 3D model loading, GLM math library, and optional CEF (Chromium 145) browser integration.

### Key Modules
| Module | API Header | Primary Impl | Null/Stub Impl |
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

## 🔴 Critical Issues

### P0 — Blocker (Rendering Broken)
1. **Model Transform Matrices Ignored** — `graphics_drawmodel()` and `graphics_draw_instanced()` both cast `transform4x4` to `(void)`. The matrix is never uploaded to the GPU. Every model renders at origin with identity transform.
2. **Material mat4 Never Uploaded to GPU** — `graphics_material_set_mat4()` stores a 4×4 matrix in `GPUMaterial.mat4`, but the pipeline layout only has a bindless descriptor set for images — no push constants, no UBO descriptor. The matrix lives in CPU memory forever.
3. **No Uniform Buffer Binding API** — `graphics_createuniformbuffer()` exists but there is no `graphics_binduniformbuffer(slot, Buffer)` in the header or implementation. Cannot pass view/projection/light data to shaders.

### P1 — High Priority
4. **Shader Variant System Is a No-Op** — `graphics_createshader()` ignores `defines`/`defineCount`. `graphics_get_shader_variant()` returns the base shader unchanged. No shader permutation support.

### P2 — Medium Priority
5. **No Framebuffer/Render Target Abstraction** — Hardcoded to swapchain only. No offscreen rendering, shadow maps, or post-processing.
6. **No Skeletal Animation** — Assimp animation data (`mAnimations`, `mBones`) is completely ignored.
7. **Font Atlas Can't Grow** — 1024×1024 fixed atlas. When full, glyphs silently fail (`font_pack_glyph()` returns 0).

### P3 — Low Priority
8. **Global `g_jobSystem`** — Prevents multi-window or editor+game coexistence.

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
- CEF wrapper requires temporary `BUILD_SHARED_LIBS = OFF` (CMake handles save/restore)

---

## Font Rendering Research (2026-07-26)

### Sources Consulted
1. **Evan Wallace (2016)** — "Text Rendering Hates You" — Fundamental GPU text challenges
2. **Behdad Esfahbod (2024)** — "State of Text Rendering" — Comprehensive overview of open-source text stack (FreeType + HarfBuzz is the industry standard: Chrome, Android, Firefox, Adobe all use it)
3. **osor.io (2025)** — "GPU Glyph Rasterization" — Runtime curve rasterization with temporal accumulation (cutting edge, overkill for this engine)

### Approaches (ranked by effort)
| Approach | Effort | Quality | Used By |
|----------|--------|---------|---------|
| Multi-Atlas (texture array) | ~2 hrs | Good (same as current) | Many games |
| Dynamic Atlas Growth (resize+blit) | ~4 hrs | Good | Some engines |
| MSDF (multi-channel SDF) | 1-2 days | Excellent (sharp at any size) | Qt 6.7, Godot, mapbox |
| GPU Curve Rasterization | 1-2 weeks | Perfect (resolution independent) | Pathfinder, osor.io |

### Recommendation
- **Short-term**: Multi-atlas for quick fix (texture array, round-robin allocation)
- **Long-term**: MSDF for production-quality text
- Engine already has industry-standard FreeType + HarfBuzz stack

---

## Files Read (Complete List)

### Headers (all src/)
- `framework.h`, `graphics.h`, `window.h`, `event.h`, `audio.h`, `model.h`, `font.h`, `text.h`, `image.h`, `timer.h`, `filesystem.h`, `job.h`

### Core Implementations
- `main_sdl.c`, `window_sdl.c`, `event_sdl.c`, `graphics_vulkan.cpp` (2846 lines), `audio_openal.c`, `filesystem_physfs.c`, `font.c` (1013 lines), `framework.c`, `image_stb.c`, `job_pthread.c`, `model_assimp.cpp`, `text.c`, `timer_sdl.c`

### Stub/Null Implementations
- `graphics_null.c`, `graphics_opengl_sdl.c`, `window_null.c`, `event_null.c`, `audio_null.c`, `filesystem_null.c`, `filesystem_posix.c`, `model_null.c`, `image_null.c`, `timer_null.c`, `job_null.c`, `main_null.c`

### Build & Config
- `CMakeLists.txt`, `README.md`

---

## Rating: 5/10
Solid foundations with clean module separation, mature abstraction layering, and a genuinely well-engineered job system. However, **critical rendering pipeline defects** (transform matrices discarded, no uniform buffer binding, material data never uploaded) mean the engine cannot render meaningful 3D content. These P0 issues must be resolved before any 3D application can function.
