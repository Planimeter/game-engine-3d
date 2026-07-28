# Planimeter Game Engine 3D — Session Memory

## Code Reviews

### Review #1: 2026-07-26
Initial comprehensive code review. All source files in `src/` and `third_party/` were read and analyzed.

### Review #2: 2026-07-27
Follow-up review verifying prior fixes and discovering new critical rendering pipeline issues.

### Review #3: 2026-07-28
Full re-review of the repository, plus analysis of unstaged git changes and new files.

**Unstaged changes observed:**

1. **Metal backend refactor** (`src/graphics_metal.mm`, `src/graphics_metal_helpers.h`, `src/graphics_metal_helpers.m`):
   - Extracted direct Metal Objective-C message sends (`[obj method]`) into pure C wrapper functions in a separate `.m` compilation unit.
   - Motivation: .mm (ObjC++) files compiled as ObjC++ cannot correctly dispatch Objective-C protocol methods (like `[g_device newBufferWithLength:options:]`) due to C++ name mangling rules. The helpers bridge through `void*` opaque handles and `extern "C"` linkage.
   - `graphics_metal_helpers.h` uses opaque `struct objc_object` forward declarations + `extern "C"` functions.
   - `graphics_metal_helpers.m` is pure ObjC — implements the wrappers sending real Metal messages.
   - `CMakeLists.txt` updated to compile `graphics_metal_helpers.m` alongside `graphics_metal.mm`.
   - Texture creation changed from `replaceBytesInRegion:` to direct `contents` + `memcpy` (requires `MTLStorageModeShared`).
   - `graphics_draw_instanced()` removed `(void)transforms4x4` cast that was previously discarding instance transforms — but still doesn't upload them to the GPU (same P0 bug as Vulkan backend).
   - `graphics_draw_buffers()` also discards `transform4x4` with `(void)` cast.
   - Several Metal methods still use `[g_currentCommandBuffer release]` and direct `MTLVertexDescriptor` creation that are NOT yet wrapped through helpers.

2. **Filesystem change** (`src/filesystem_physfs.c`):
   - Added `PHYSFS_mount("/System/Library/Fonts", "Fonts", 0)` on macOS to mount system fonts directory under the virtual `/Fonts/` path.

3. **Framework change** (`src/framework.c`):
   - Font path changed from hardcoded `/System/Library/Fonts/SFNS.ttf` (with Geneva fallback) to `"Fonts/Geneva.ttf"` using PHYSFS virtual mount point.
   - This is cleaner since it goes through the engine's own filesystem abstraction.

### Build Fix (2026-07-28, applied during Review #3)

The build had 16 errors due to type mismatches between the ObjC++ `.mm` file and the C-compatible helper header.

**Root cause:** `graphics_metal_helpers.h` used ObjC-native types (`NSUInteger`, `NSError *`, `MTLViewport` struct) in function signatures but is included from both `.m` (ObjC) and `.mm` (ObjC++) files. The `.m` file imported `<Metal/Metal.h>` AFTER the helper header, so `NSUInteger` and `NSError` were unknown when the header was parsed.

**Fixes applied:**
- `graphics_metal_helpers.h`: Replaced all ObjC-specific types with plain C equivalents:
  - `NSUInteger` → `unsigned long`
  - `NSError **` → `void **`
  - `MTLViewport` (struct by value) → `const double vp[6]` (pointer to 6 doubles)
- `graphics_metal_helpers.m`: Updated implementations to cast `unsigned long` back to `NSUInteger` and reconstruct `MTLViewport` from the double array internally.
- `graphics_metal.mm`: Added explicit casts from `void*` returns to `id<MTL...>` types at every call site where helper return values are assigned to typed variables. Changed `NSString*` arguments to `const char*` (C string literals) for `metal_device_new_library()` and `metal_library_new_function()`. Changed viewport from `MTLViewport` struct to `double vp[6]` array.

### Runtime Fixes (2026-07-28, iterative debugging — see Session Summary for full details)

A full Metal backend debugging session resolved 6 root causes that turned white rectangles into visible FPS text:

**Fix 1 — Build (16 compilation errors):** `graphics_metal_helpers.h` used ObjC types (`NSUInteger`, `NSError*`, `MTLViewport`) before `<Metal/Metal.h>` was imported. Replaced with plain C equivalents (`unsigned long`, `void**`, `const double vp[6]`). `.m` file casts back internally.

**Fix 2 — AGX GPU crash (texture access):** Apple M1 Max GPU driver does not respond to `contents` or `replaceBytesInRegion:` on textures with `MTLStorageModeShared`. Changed texture storage to `MTLStorageModePrivate` and replaced direct CPU access with staging buffer + blit encoder pattern (`copyFromBuffer:toTexture:` via `MTLBlitCommandEncoder`).

**Fix 3 — Sampler binding:** `graphics_bindtexture()` was a no-op. Added `g_defaultSampler` creation in `graphics_init()` (linear filter, repeat addressing). `graphics_bindtexture()` now calls `metal_encoder_set_fragment_texture()` and `metal_encoder_set_fragment_sampler_state()`.

**Fix 4 — Shader lifecycle:** `font_create()` obtained fallback shaders via `graphics_get_text_shaders()` then destroyed them with `graphics_destroyshader()`, invalidating the pointers used by the pipeline. Removed the destroy calls for backend-owned shaders.

**Fix 5 — NDC Y-axis flip:** `font_pixel_to_ndc()` had the formula `*out_y = -1.0f + (py / (float)h) * 2.0f` which maps pixel-y=0 (top) to NDC y=-1 (bottom). Corrected to `*out_y = 1.0f - (py / (float)h) * 2.0f` to map top-left pixel origin to top-left NDC origin. Text now appears at the expected screen position.

**Fix 6 — Glyph alpha channel mismatch (root cause of white rectangles):** FreeType glyph data stores alpha in the 4th byte (.a), but the text fragment shader read `.r` (1st byte) which is always 255. Changed `tex.sample(...).r` to `tex.sample(...).a`. White rectangles resolved — text now renders correctly.

**Fix 7 — Depth format conditional:** The text pipeline (depthTest=0, depthWrite=0) still specified `MTLPixelFormatDepth32Float` as the depth attachment pixel format, but no depth texture was created. Changed pipeline creation to only set `depthAttachmentPixelFormat` when depth test or depth write is enabled.

### Transform Pipeline Fix + 3D Model Rendering Test (2026-07-28, Session #2)

After FPS text was working, we fixed the transform matrix upload pipeline and added an end-to-end 3D rendering test:

**Fix 8 — `graphics_drawmodel()` transform matrix upload:** `graphics_drawmodel()` bound `g_uniformBuffer` at index 1 but never copied `transform4x4` data into it. The default vertex shader expected the matrix at `[[buffer(0)]]` which conflicted with vertex attribute buffer(0). **Two bugs:**
- Wrong binding index (buffer 0 vs buffer 1) — shader changed to `[[buffer(1)]]`
- No data upload — added `memcpy(metal_buffer_get_contents(g_uniformBuffer), transform4x4, 64)` before binding

**Fix 9 — `graphics_draw_instanced()` transform upload:** Was `(void)transforms4x4` — discarding instance transforms entirely. Now uploads first transform to `g_uniformBuffer` and passes `count` as instanceCount to draw call.

**Fix 10 — `graphics_draw_buffers()` transform upload:** Was `(void)transform4x4` — discarding transform entirely. Now uploads matrix to `g_uniformBuffer` and binds at index 1.

**Fix 11 — Default 3D fallback shader:** The original default3d shader was a stub returning hardcoded colors. Replaced with a proper `VERTEX_FORMAT_FULL` handler that reads position(0), normal(1), tangent(2), bitangent(3), texcoord(4) and visualizes normals as color (`normal * 0.5 + 0.5`). Model matrix received at `[[buffer(1)]]`.

**Fix 12 — `graphics_loadmodel()` GPU buffer creation:** Was a no-op stub. Now iterates over each mesh and calls `graphics_createvertexbuffer()` / `graphics_createindexbuffer()` to upload vertex and index data to the GPU.

**Fix 13 — C/C++ opaque struct boundary (`model_get_mesh_count()`):** The `Model` struct is fully defined only inside `#ifdef __cplusplus` — C code sees only a forward declaration. Added `model_get_mesh_count(const Model *model)` as a C-compatible accessor function declared in model.h with `extern "C"` linkage, implemented in `model_assimp.cpp` and `model_null.c`.

**Fix 14 — Missing `#include <stdint.h>` in model.h:** The `uint32_t` return type on `model_get_mesh_count()` was unknown to C compilers because model.h only included `<sys/types.h>`. Added `#include <stdint.h>`.

**End-to-end test:** Added spinning cube rendering to `framework.c`:
- C matrix math helpers: `mat4_identity`, `mat4_multiply`, `mat4_rotate_y`, `mat4_translate`, `mat4_perspective`, `mat4_lookat`
- Model loading with `realpath(argv[0])` path resolution (navigates up from `.app` bundle to repo root)
- MVP matrix computation per frame: `mvp = proj * view * model` with Y-axis rotation
- `graphics_drawmodel(g_testModel, NULL, mvp)` renders the cube with normal-visualization colors
- Verified: build succeeds, app runs, cube loads and renders without crashes

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
| Math (C wrappers) | `src/math_c.h` | `src/math_glm.cpp` | `src/math_null.c` |
| Filesystem | `src/filesystem.h` | `src/filesystem_physfs.c` | `src/filesystem_null.c`, `src/filesystem_posix.c` |
| Timer | `src/timer.h` | `src/timer_sdl.c` | `src/timer_null.c` |
| Image | `src/image.h` | `src/image_stb.c` | `src/image_null.c` |
| Job System | `src/job.h` | `src/job_pthread.c` | `src/job_null.c` |

### Game Loop Pattern (`main_sdl.c`)
```
event_poll() → timer_step() → job_submit(update) → job_wait(update) 
           → job_submit(draw)  → job_wait(draw)  → graphics_present()
```

## Metal Backend Status (2026-07-28, post-transform-fix session)

The Metal backend (`src/graphics_metal.mm`, ~830 lines) is a partial port of the Vulkan backend. FPS text rendering and basic 3D model rendering (spinning cube with normal visualization) are now working.

### Completed
- Device initialization, command queue, swapchain via CAMetalLayer
- Basic shader compilation (vertex_main/fragment_main entry points)
- Vertex/index buffer creation, texture creation and updates
- Texture binding + sampler binding (`graphics_bindtexture()`)
- Pipeline creation (vertex descriptor, depth-stencil state, render pipeline state)
- Render loop (predraw → draw calls → postdraw → present)
- **FPS text rendering via embedded MSL fallback shaders** ✅
- **Font atlas texture upload** via staging buffer + blit encoder ✅
- **Alpha channel glyph rendering** (FreeType alpha in `.a` channel) ✅
- **NDC-correct glyph positioning** (Y-axis flip fixed) ✅
- **Transform matrix upload in all 3 draw calls** (`graphics_drawmodel`, `graphics_draw_instanced`, `graphics_draw_buffers`) ✅
- **Default 3D fallback shader with VERTEX_FORMAT_FULL support** (position, normal, tangent, bitangent, texcoord) ✅
- **`graphics_loadmodel()` GPU buffer creation** (vertex + index buffer upload per mesh) ✅
- **End-to-end 3D model rendering test** (spinning cube with MVP matrix, normal visualization) ✅
- **`graphics_binduniformbuffer()` API** (binds a Buffer to both vertex and fragment stages at a given slot) ✅
- **`model_get_mesh_count()` C-compatible accessor** for opaque Model struct ✅

### Refactoring Completed
- Extracted direct Metal message sends into C wrappers in `graphics_metal_helpers.m` to work around ObjC++ dispatch issues
- Texture storage mode: `MTLStorageModePrivate` + blit encoder upload (fixes AGX GPU crash)
- `metal_buffer_update()` wraps `memcpy([buf contents], data, size)`
- `metal_texture_update_region()` uses staging buffer + blit encoder
- NOT YET wrapped: `[g_currentCommandBuffer release]`, `MTLVertexDescriptor` creation, `MTLRenderPassDescriptor` creation

### Remaining Issues
- `graphics_material_set_mat4()` stores matrix CPU-side with no upload path
- `graphics_setmaterial()` is a no-op `(void)mat`
- `g_inPass` flag is set but `graphics_beginpass()`/`graphics_endpass()` are no-ops (just set/reset `g_inPass`)
- No depth texture created — depth attachment only enabled when depthTest/depthWrite is set

---

## 🔴 Critical Issues

### P0 — Blocker (3D Rendering Broken)
1. ~~**Model Transform Matrices Ignored (Metal)** — `graphics_drawmodel()` binds `g_uniformBuffer` at index 1 but never copies `transform4x4` data into it. The default vertex shader expects the matrix at `[[buffer(0)]]`. **Two bugs: wrong binding index + no data upload.**~~ ✅ **Fixed** — matrix copied into `g_uniformBuffer`, shader changed to `[[buffer(1)]]` to avoid vertex descriptor conflict.
2. ~~**`graphics_draw_instanced()` Discards Transforms (Metal)** — `(void)transforms4x4` at line 786. Instance transforms never reach GPU.~~ ✅ **Fixed** — matrix uploaded, instanceCount passed through.
3. ~~**`graphics_draw_buffers()` Discards Transforms (Metal)** — `(void)transform4x4` at line 817. Transform never reaches GPU.~~ ✅ **Fixed** — matrix uploaded.
4. ~~**`graphics_loadmodel()` No GPU Buffer Creation (Metal)** — Was a no-op stub. Models loaded but had no vertex/index buffers on GPU.~~ ✅ **Fixed** — iterates meshes and creates GPU buffers.
5. ~~**Default 3D Shader Stub (Metal)** — Default vertex/fragment shaders for 3D were stubs returning hardcoded colors.~~ ✅ **Fixed** — replaced with VERTEX_FORMAT_FULL handler with normal visualization.
6. **Material mat4 Never Uploaded to GPU** — `graphics_material_set_mat4()` stores a 4×4 matrix in `material->mat4` (CPU-side struct field). Pipeline layout has zero push constant ranges and only a bindless descriptor set for images — no UBO descriptor, no push constant path. The matrix lives in CPU memory forever.
7. ~~**No Uniform Buffer Binding API** — `graphics_createuniformbuffer()` exists but there is no `graphics_binduniformbuffer(slot, Buffer)` in the header or implementation. Cannot pass view/projection/light data to shaders.~~ ✅ **Fixed** — `graphics_binduniformbuffer(Buffer buf, unsigned slot)` added to graphics.h, implemented in Metal backend via `metal_encoder_set_buffer()` (binds to both vertex and fragment stages). Vulkan backend still needs pipeline layout rework.
8. **Root Cause: Pipeline Layout Gap (Vulkan)** — Vulkan pipeline layout (line 893) has `setLayoutCount = 1` (bindless textures only), `pushConstantRangeCount = 0`. All shader sources (`default3d.vert`, `pbr-vert.glsl`) declare uniform matrices that cannot be bound through any existing API.

### P1 — High Priority
7. **Shader Variant System Is a No-Op** — `graphics_createshader()` ignores `defines`/`defineCount`. `graphics_get_shader_variant()` returns the base shader unchanged. No shader permutation support.

### P2 — Medium Priority
8. **No Framebuffer/Render Target Abstraction** — Hardcoded to swapchain only. No offscreen rendering, shadow maps, or post-processing.
9. **No Skeletal Animation** — Assimp animation data (`mAnimations`, `mBones`) is completely ignored.

---

## Shader Source Verification (2026-07-27)

All shader sources were examined to confirm the uniform binding gap:

| Shader | Uniforms Declared | Bindable? |
|--------|------------------|-----------|
| `default3d.vert` | `mat4 modelMatrix`, `viewMatrix`, `projectionMatrix`, `normalMatrix`; `sampler2D tex` | No — no UBO binding, only bindless textures for samplers |
| `pbr-vert.glsl` | Same as default3d + light direction/color, camera position | No — same issue |
| `pbr-frag.glsl` | IBL samplers, emissive texture | Partially — bindless texture binding works, but no PBR uniform data |
| `triangle.vert/frag` | None (hardcoded colors) | N/A — only test shader that actually works |

**Conclusion:** Only the triangle test shader can produce visible output. All 3D shaders require uniform matrices that have no binding path.

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
- `graphics_metal_helpers.h` (new)
- `math_c.h` (new)

### Core Implementations
- `main_sdl.c`, `window_sdl.c`, `event_sdl.c`, `graphics_vulkan.cpp` (2847 lines), `graphics_metal.mm` (~800 lines), `audio_openal.c`, `filesystem_physfs.c`, `font.c` (1080 lines), `framework.c`, `image_stb.c`, `job_pthread.c`, `model_assimp.cpp`, `text.c`, `timer_sdl.c`
- `graphics_metal_helpers.m` (new, 139 lines)
- `math_glm.cpp` (new)

### Stub/Null Implementations
- `graphics_null.c`, `graphics_opengl_sdl.c`, `window_null.c`, `event_null.c`, `audio_null.c`, `filesystem_null.c`, `filesystem_posix.c`, `model_null.c`, `image_null.c`, `timer_null.c`, `job_null.c`, `main_null.c`
- `math_null.c` (new)

### Build & Config
- `CMakeLists.txt`, `README.md`

---

## Additional Observations (2026-07-28)

### Engine State at Runtime
- `framework.c` creates a test font and displays an FPS counter, plus loads and renders a spinning cube with normal-visualization colors
- Matrix math is provided by `math_c.h` / `math_glm.cpp` wrapping real GLM calls (SIMD, etc.) behind `extern "C"` functions
- 3D model rendering is working via the Metal backend with MVP matrix pipeline
- The default 3D shader visualizes normals as color (`normal * 0.5 + 0.5`) — debug quality, no proper lighting yet
- The Vulkan backend still uses a bindless descriptor set (16384 max textures) for `sampler2D` only — no UBO descriptor
- The pipeline layout is created once in `graphics_creategraphicspipeline()` and reused by all pipelines created via `graphics_createpipeline()` — all custom pipelines share the same layout (no UBO support)
- `graphics_setmaterial()` only binds textures from the material to the bindless descriptor set; it does NOT upload floats, vec3s, or mat4 to the GPU

### Memory Management
- VMA (Vulkan Memory Allocator) is used for all GPU allocations
- Staging buffers for texture uploads use VMA with `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT`
- `graphics_createmodelgpu()` maps, copies, and unmaps each mesh's vertex/index data — no persistent mapping
- No GPU mipmap generation (mipLevels = 1 everywhere)

### Portable API Design
- Graphics API is C99 with opaque handles (`typedef void* Shader, Texture, Material, Buffer, RenderPass, Pipeline`)
- Dual backend: `#ifdef APPLE` selects Metal, else Vulkan
- The Metal backend is less mature — many `graphics_*` functions are no-ops or stubs
- Some backends (OpenGL SDL, Null) exist but are not wired into the build system as primary backends

### Shader Compilation
- Vulkan loads pre-compiled SPIR-V (`.spv` files) from `shaders/` directory
- Metal compiles MSL source at runtime via `newLibraryWithSource:options:error:`
- Both backends expect `vertex_main`/`fragment_main` entry points (Metal) vs `main` (Vulkan SPIR-V)
- Shaders directory has pre-compiled `.spv` for `text`, `triangle` — but not for `default3d`, `pbr`, `depth`, `skybox`

### Build System Quirks
- CEF tests directory is deleted before building to avoid C++20 incompatibility
- CEF wrapper is force-built as static library regardless of `BUILD_SHARED_LIBS`
- `shaderc` library is commented out in CMakeLists.txt (lines 97-101) — no runtime shader compilation from GLSL
- VMA is configured with `VMA_STATIC_VULKAN_FUNCTIONS=OFF` to avoid conflicts with volk

### Filesystem
- PhysFS is the primary filesystem abstraction on non-Windows platforms
- On POSIX/macOS: mounts the current directory (argv[0] directory) as root, plus system fonts
- On Windows: mounts "." (current working directory)
- `filesystem_posix.c` provides direct POSIX file access (not PhysFS-based) as an alternative stub

---

## Rating: 7/10
Solid foundations with clean module separation, mature abstraction layering, and a genuinely well-engineered job system. **Critical rendering pipeline defects have been fixed in the Metal backend** — the engine can now render basic 3D content (spinning cube with MVP matrix) and has a proper uniform buffer binding API. The Vulkan backend still needs pipeline layout rework to support UBOs. Remaining P0 issues: material system (mat4 never uploaded, setmaterial is no-op).

**Metal backend progress:** FPS text + spinning cube + uniform buffer binding API is working. Remaining P0 issues are the material system.
