# Planimeter Game Engine 3D — Session Memory

## Code Reviews

### Review #1: 2026-07-26
Initial comprehensive code review. All source files in `src/` and `third_party/` were read and analyzed.

### Review #2: 2026-07-27
Follow-up review verifying prior fixes and discovering new critical rendering pipeline issues.

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
4. **`font_print()` Malloc/Free on Every Call** — Allocates and frees temporary vertex/index buffers every call, despite having pre-allocated GPU buffers. Causes heap churn.
5. **Batching System Re-shapes Text Twice** — `font_end_batch()` shapes all glyphs in pass 1 (to count vertices) then shapes them all over again in pass 2 (to build geometry). Cache shaped results.
6. **`graphics_material_set_texture` Ignores Texture Name** — The `name` parameter is cast to `(void)`. Always binds to slot 0. Makes multi-texture materials impossible.
7. **Shader Variant System Is a No-Op** — `graphics_createshader()` ignores `defines`/`defineCount`. `graphics_get_shader_variant()` returns the base shader unchanged. No shader permutation support.

### P2 — Medium Priority
8. **No Framebuffer/Render Target Abstraction** — Hardcoded to swapchain only. No offscreen rendering, shadow maps, or post-processing.
9. **No Skeletal Animation** — Assimp animation data (`mAnimations`, `mBones`) is completely ignored.
10. **Font Atlas Can't Grow** — 1024×1024 fixed atlas. When full, glyphs silently fail (`font_pack_glyph()` returns 0).
11. **`graphics_transition_image()` Only Handles 2 Transitions** — Only supports `UNDEFINED→TRANSFER_DST` and `TRANSFER_DST→SHADER_READ_ONLY`. Missing depth, color-attachment, and present transitions.
12. **Swapchain Recreation Gap** — `graphics_resize()` doesn't re-create render pass or pipelines on swapchain rebuild.
13. **Joystick/Game Controller Events Unhandled** — All joystick/gamepad event cases fall through to `default: break;` with no processing.

### P3 — Low Priority
14. **Global `g_jobSystem`** — Prevents multi-window or editor+game coexistence.
15. **No Memory Tracking** — No allocator wrapper or leak detection.
16. **No CMake Install Target** — Missing `install()` rules.
17. **`font_print()` Unused Parameters** — `r` (rotation), `ox`, `oy`, `kx`, `ky` are all cast to `(void)`. Text rotation/alignment/kerning not implemented.

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

## Files Read (Complete List)

### Headers (all src/)
- `framework.h`, `graphics.h`, `window.h`, `event.h`, `audio.h`, `model.h`, `font.h`, `text.h`, `image.h`, `timer.h`, `filesystem.h`, `job.h`

### Core Implementations
- `main_sdl.c`, `window_sdl.c`, `event_sdl.c`, `graphics_vulkan.cpp` (2750 lines), `audio_openal.c`, `filesystem_physfs.c`, `font.c`, `framework.c`, `image_stb.c`, `job_pthread.c`, `model_assimp.cpp`, `text.c`, `timer_sdl.c`

### Stub/Null Implementations
- `graphics_null.c`, `graphics_opengl_sdl.c`, `window_null.c`, `event_null.c`, `audio_null.c`, `filesystem_null.c`, `filesystem_posix.c`, `model_null.c`, `image_null.c`, `timer_null.c`, `job_null.c`, `main_null.c`

### Build & Config
- `CMakeLists.txt`, `README.md`

---

## Rating: 5/10
Solid foundations with clean module separation, mature abstraction layering, and a genuinely well-engineered job system. However, **critical rendering pipeline defects** (transform matrices discarded, no uniform buffer binding, material data never uploaded) mean the engine cannot render meaningful 3D content. These P0 issues must be resolved before any 3D application can function.
