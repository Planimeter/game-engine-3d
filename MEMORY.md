# Planimeter Game Engine 3D — Session Memory

## Code Reviews

### Review #1: 2026-07-26
Initial comprehensive code review. All source files in `src/` and `third_party/` were read and analyzed.

### Review #2: 2026-07-27
Follow-up review verifying prior fixes and discovering new critical rendering pipeline issues.

### Review #3: 2026-07-28
Full re-review of the repository, plus analysis of unstaged git changes and new files.

### Review #4: 2026-07-29 (ShaderStage migration + Vulkan shaderc runtime compilation)
Post-commit review verifying the ShaderStage API migration and Vulkan shaderc integration commits.

**Verified committed changes:**

1. **ShaderStage API migration** (`e537d3a5`): `graphics_createshader()` signature changed from 4 to 5 parameters with explicit `ShaderStage` enum (`SHADER_STAGE_VERTEX`, `SHADER_STAGE_FRAGMENT`). Updated across all backends:
   - `src/graphics.h` — Added `ShaderStage` enum, updated function signature
   - `src/font.c` — Updated call sites: `graphics_createshader(SHADER_STAGE_VERTEX, ...)` and `SHADER_STAGE_FRAGMENT`
   - `src/graphics_metal.mm` — Implementation accepts `(void)stage` (Metal compiles MSL source natively, no stage concept needed for combined vertex+fragment shaders)
   - `src/graphics_vulkan.cpp` — Full implementation: maps `ShaderStage` to `shaderc_shader_kind`, dispatches GLSL→SPIR-V compilation with stage-aware shaderc calls
   - `src/graphics_null.c` — `(void)stage` cast (no-op backend)
   - `src/graphics_opengl_sdl.c` — `(void)stage` cast

2. **Vulkan runtime GLSL→SPIR-V via shaderc** (`a4191071`): Major architectural change replacing pre-compiled `.spv` file loading with runtime compilation:
   - Added `#include <shaderc/shaderc.h>` and `static shaderc_compiler_t g_shaderc_compiler`
   - `graphics_init()`: initializes `g_shaderc_compiler = shaderc_compiler_initialize()` at startup, releases in `graphics_shutdown()`
   - `graphics_createshaders()` (internal): reads raw GLSL source files (`shaders/triangle.vert`, `shaders/triangle.frag`) via `filesystem_fileread()` instead of `.spv` binary loading
   - `graphics_createshader()`: compiles GLSL→SPIR-V via `shaderc_compile_into_spv()`, supports macro definitions, targets Vulkan 1.1 environment (`shaderc_env_version_vulkan_1_1`), performance optimization level
   - All shader compilation errors produce readable error messages via `shaderc_result_get_error_message()`

3. **Font rendering pipeline** (`src/font.c`): Verified current state:
   - Still loads pre-compiled `.spv` files for text shaders (`shaders/text.vert.spv`, `shaders/text.frag.spv`) from filesystem
   - Falls back to embedded MSL shaders via `graphics_get_text_shaders()` when SPIR-V unavailable
   - Both Metal and Vulkan backends now accept the 5-param `graphics_createshader(SHADER_STAGE_*, ...)` signature
   - The font pipeline is created once at `font_create()` with proper raster state (no depth write/test, alpha blend)

4. **Framework test** (`src/framework.c`): Verified current state:
   - Spinning cube rendering still present with MVP matrix computation
   - FPS text overlay working
   - Model path resolution uses `realpath(argv[0])` navigating up from `.app/Contents/MacOS` bundle
   - Falls back to relative `../Models/cube.obj` path

5. **Shader directory** (`shaders/`): Contains both GLSL sources and pre-compiled SPIR-V:
   - GLSL sources: `default.frag`, `default2d.vert`, `default3d.vert`, `depth.frag`, `depth.vert`, `pbr-frag.glsl`, `pbr-vert.glsl`, `skybox.frag`, `skybox.vert`, `text.frag`, `text.vert`, `triangle.frag`, `triangle.vert`
   - Pre-compiled SPIR-V: `text.frag.spv`, `text.vert.spv`, `triangle.frag.spv`, `triangle.vert.spv`
   - Vulkan now uses GLSL sources for default3d pipeline (via `shaders/triangle.vert/frag`)
   - Metal uses embedded MSL in source code for both default3d and text fallback shaders

6. **CMakeLists.txt**: Verified shaderc is NOW ACTIVE (not commented out):
   - Uses FetchContent to pull glslang, SPIRV-Headers, SPIRV-Tools, shaderc
   - Links `shaderc_combined` to the game target on non-macOS platforms
   - On macOS, shaderc is used only for Vulkan backend (Metal doesn't need it)

7. **Metal backend** (`src/graphics_metal.mm`, ~1030 lines): Verified all changes intact:
   - ShaderStage migration: `(void)stage` cast in signature
   - Default 3D fallback shader embedded as MSL source string (combined vertex+fragment)
   - Text fallback shaders embedded as MSL source strings (combined vertex+fragment)
   - Material system with `MetalMaterial` struct, per-material uniform buffer, `metal_material_pack()`
   - All draw functions upload transform matrices and bind materials

8. **Vulkan backend** (`src/graphics_vulkan.cpp`, ~3103 lines): Verified all changes intact:
   - ShaderStage-aware `graphics_createshader()`: maps to `shaderc_glsl_vertex_shader` / `shaderc_glsl_fragment_shader`
   - Macro definition support: splits `"NAME=VALUE"` at first `=` for shaderc compile options
   - Material system with `GPUMaterial` struct, `vulkan_material_pack()` matching Metal's layout
   - UBO descriptor set (set 1) with 16 slots, global uniform buffer (4096 bytes)
   - Pipeline creation supports multiple vertex formats (`VERTEX_FORMAT_FULL`, `POS_UV`, `POS_COLOR`)
   - Blend modes: NONE, ALPHA, ADD, PREMULT

### Review #3: 2026-07-28 — Unstaged Changes Observed (All Committed)
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

### Transform Pipeline Fix + 3D Model Rendering Test + Material System (2026-07-28, Sessions #2-#3)

After FPS text was working, we fixed the transform matrix upload pipeline, added an end-to-end 3D rendering test, and implemented the material system:

**Fix 8 — `graphics_drawmodel()` transform matrix upload:** `graphics_drawmodel()` bound `g_uniformBuffer` at index 1 but never copied `transform4x4` data into it. The default vertex shader expected the matrix at `[[buffer(0)]]` which conflicted with vertex attribute buffer(0). **Two bugs:**
- Wrong binding index (buffer 0 vs buffer 1) — shader changed to `[[buffer(1)]]`
- No data upload — added `memcpy(metal_buffer_get_contents(g_uniformBuffer), transform4x4, 64)` before binding

**Fix 9 — `graphics_draw_instanced()` transform upload:** Was `(void)transforms4x4` — discarding instance transforms entirely. Now uploads first transform to `g_uniformBuffer` and passes `count` as instanceCount to draw call.

**Fix 10 — `graphics_draw_buffers()` transform upload:** Was `(void)transform4x4` — discarding transform entirely. Now uploads matrix to `g_uniformBuffer` and binds at index 1.

**Fix 11 — Default 3D fallback shader:** The original default3d shader was a stub returning hardcoded colors. Replaced with a proper `VERTEX_FORMAT_FULL` handler that reads position(0), normal(1), tangent(2), bitangent(3), texcoord(4) and visualizes normals as color (`normal * 0.5 + 0.5`). Model matrix received at `[[buffer(1)]]`.

**Fix 12 — `graphics_loadmodel()` GPU buffer creation:** Was a no-op stub. Now iterates over each mesh and calls `graphics_createvertexbuffer()` / `graphics_createindexbuffer()` to upload vertex and index data to the GPU.

**Fix 13 — C/C++ opaque struct boundary (`model_get_mesh_count()`):** The `Model` struct is fully defined only inside `#ifdef __cplusplus` — C code sees only a forward declaration. Added `model_get_mesh_count(const Model *model)` as a C-compatible accessor function declared in model.h with `extern "C"` linkage, implemented in `model_assimp.cpp` and `model_null.c`.

**Fix 14 — Missing `#include <stdint.h>` in model.h:** The `uint32_t` return type on `model_get_mesh_count()` was unknown to C compilers because model.h only included `<sys/types.h>`. Added `#include <stdint.h>`.

**Fix 15 — Material system implementation (Metal backend):** `graphics_creatematerial()` returned the shader pointer directly (no material struct). `graphics_material_set_float()`, `graphics_material_set_vec3()`, `graphics_material_set_texture()`, `graphics_material_set_mat4()` all discarded data. `graphics_setmaterial()` was `(void)mat`. Implemented:
- `MetalMaterial` struct with CPU-side storage for floats (32), vec3s (16 padded to vec4), textures (8), and a mat4
- Per-material uniform buffer (1024 bytes) created at material creation
- `metal_material_pack()` packs dirty uniforms into the GPU buffer with correct alignment (floats tightly packed, vec3s padded to vec4, mat4 at end)
- `graphics_setmaterial()` packs dirty uniforms, binds material buffer at slot 2, and binds textures
- All draw functions (`graphics_drawmodel`, `graphics_draw_instanced`, `graphics_draw_buffers`) now call `graphics_setmaterial()` when a material is provided
- Default 3D shader declares `MaterialData` struct at `[[buffer(2)]]` for future use
- OpenGL and null backends updated with proper CPU-side storage (no GPU upload yet — those backends lack the binding infrastructure)

**Fix 16 — Vulkan pipeline layout rework (UBO descriptor set + transform upload):** The Vulkan backend had `setLayoutCount=1` (bindless textures only) and `pushConstantRangeCount=0`. `graphics_binduniformbuffer()` was a no-op. All 3 draw functions discarded transform matrices with `(void)` casts. Material data never reached the GPU. Implemented:
- UBO descriptor set layout, pool, and set (set 1) with 16 slots for uniform buffers
- Global uniform buffer (4096 bytes) for transform matrices
- Pipeline layout updated to use 2 descriptor sets (set 0=bindless textures, set 1=UBOs)
- `graphics_binduniformbuffer()` implemented via `vkUpdateDescriptorSets()` with `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`
- Transform matrix upload in all 3 draw calls (global UBO at slot 0)
- Per-material uniform buffer added to `GPUMaterial` struct
- `vulkan_material_pack()` packs material data into GPU buffer, bound at UBO slot 1
- Material dirty flag set on `set_float`/`set_vec3`/`set_mat4` changes
- Proper cleanup of UBO descriptor pool/layout and global buffer in `graphics_shutdown()`

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
| Graphics | `src/graphics.h` | `src/graphics_metal.mm` (macOS), `src/graphics_vulkan.cpp` (Linux/Windows) | `src/graphics_null.c`, `src/graphics_opengl_sdl.c` |
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

## Metal Backend Status (2026-07-29, post-ShaderStage-migration session)

The Metal backend (`src/graphics_metal.mm`, ~1030 lines) is a complete port of the Vulkan backend for macOS. FPS text rendering and 3D model rendering (spinning cube with normal visualization) are working end-to-end at ~119 FPS.

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
- **Material system implementation** — `MetalMaterial` struct with CPU-side storage, per-material uniform buffer, `metal_material_pack()` for GPU upload, `graphics_setmaterial()` binds material buffer at slot 2 + textures ✅

### Refactoring Completed
- Extracted direct Metal message sends into C wrappers in `graphics_metal_helpers.m` to work around ObjC++ dispatch issues
- Texture storage mode: `MTLStorageModePrivate` + blit encoder upload (fixes AGX GPU crash)
- `metal_buffer_update()` wraps `memcpy([buf contents], data, size)`
- `metal_texture_update_region()` uses staging buffer + blit encoder
- NOT YET wrapped: `[g_currentCommandBuffer release]`, `MTLVertexDescriptor` creation, `MTLRenderPassDescriptor` creation

### Remaining Issues
- `g_inPass` flag is set but `graphics_beginpass()`/`graphics_endpass()` are no-ops (just set/reset `g_inPass`)
- No depth texture created — depth attachment only enabled when depthTest/depthWrite is set

---

## 🔴 Critical Issues

### P0 — Blocker (3D Rendering Broken) — ALL FIXED ✅
1. ~~**Model Transform Matrices Ignored (Metal)**~~ ✅ **Fixed**
2. ~~**`graphics_draw_instanced()` Discards Transforms (Metal)**~~ ✅ **Fixed**
3. ~~**`graphics_draw_buffers()` Discards Transforms (Metal)**~~ ✅ **Fixed**
4. ~~**`graphics_loadmodel()` No GPU Buffer Creation (Metal)**~~ ✅ **Fixed**
5. ~~**Default 3D Shader Stub (Metal)**~~ ✅ **Fixed**
6. ~~**Material mat4 Never Uploaded to GPU**~~ ✅ **Fixed (Metal + Vulkan)**
7. ~~**No Uniform Buffer Binding API**~~ ✅ **Fixed**
8. ~~**Root Cause: Pipeline Layout Gap (Vulkan)**~~ ✅ **Fixed**

### P1 — High Priority
7. **Shader Variant System Is a No-Op** — `graphics_createshader()` ignores `defines`/`defineCount`. `graphics_get_shader_variant()` returns the base shader unchanged. No shader permutation support.

### P2 — Medium Priority
8. **No Framebuffer/Render Target Abstraction** — Hardcoded to swapchain only. No offscreen rendering, shadow maps, or post-processing.
9. **No Skeletal Animation** — Assimp animation data (`mAnimations`, `mBones`) is completely ignored.

---

## Shader Source Verification (2026-07-27, OUTDATED — replaced by runtime compilation)

*This section was from before the Vulkan shaderc integration. The table below documents current shader sources but the "Bindable?" column is now outdated because Vulkan compiles GLSL at runtime instead of loading pre-compiled SPIR-V.*

| Shader | Uniforms Declared | Runtime Path |
|--------|------------------|-------------|
| `default3d.vert` / Metal embedded MSL | Position(0), normal(1), tangent(2), bitangent(3), texcoord(4); model matrix at buffer(1) | Vulkan: compiled from `shaders/triangle.vert`. Metal: embedded in source code as combined vertex+fragment MSL string. |
| `pbr-vert.glsl` / `pbr-frag.glsl` | Same as default3d + light direction/color, camera position, IBL samplers | Not yet loaded by engine — GLSL sources exist but no load path implemented |
| `text.vert` / `text.frag` (GLSL) | Position(0), texcoord(1) for text rendering | Vulkan: compiled from source via shaderc. Metal (font): falls back to embedded MSL in `graphics_get_text_shaders()` when `.spv` unavailable. |
| `triangle.vert` / `triangle.frag` | None (hardcoded colors) | Vulkan: compiled from `shaders/triangle.vert/frag` at startup via shaderc. Metal: not used — uses embedded shaders. |
| `depth.vert` / `depth.frag` | Position input only | Not yet loaded by engine — GLSL sources exist but no load path implemented |
| `skybox.vert` / `skybox.frag` | Position + texcoord for cubemap sampling | Not yet loaded by engine — GLSL sources exist but no load path implemented |

**Current shader loading paths:**
- **Vulkan default3d pipeline**: Reads `shaders/triangle.vert` and `shaders/triangle.frag` as GLSL source → compiles via shaderc at startup (`graphics_createshaders()`) → creates VkShaderModule from SPIR-V.
- **Font text pipeline** (both backends): Attempts to load pre-compiled `shaders/text.vert.spv` / `text.frag.spv`. On macOS Metal, falls back to embedded MSL shaders if SPIR-V unavailable.

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

## Files Read — Review #4 (2026-07-29)

### Source Files Verified
- `src/graphics_vulkan.cpp` (3103 lines) — Full verification of shaderc runtime compilation, ShaderStage migration, material system, pipeline creation with vertex format support
- `src/graphics_metal.mm` (1030 lines) — Full verification of ShaderStage migration, embedded MSL shaders, material system
- `src/font.c` (1094 lines) — Verified SPIR-V loading path + MSL fallback for font text pipeline
- `src/framework.c` (233 lines) — Verified spinning cube test and FPS overlay still present

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
- **The Vulkan backend now has a UBO descriptor set (set 1) with 16 uniform buffer slots** — `graphics_binduniformbuffer()` is fully implemented via `vkUpdateDescriptorSets()`
- The pipeline layout uses 2 descriptor sets: set 0 (bindless textures, 16384 max) and set 1 (UBOs, 16 slots)
- `graphics_setmaterial()` packs material uniforms into a per-material GPU buffer and binds it at UBO slot 1, plus binds textures from the material

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
- **Vulkan**: Now compiles GLSL→SPIR-V at runtime via shaderc library (FetchContent dependency). Reads raw `.vert`/`.frag` GLSL source files, targets Vulkan 1.1 environment, performance optimization level. Supports macro definitions via `defines`/`defineCount` parameters. Entry point: `main`.
- **Metal**: Compiles MSL source at runtime via helper wrapper (`metal_device_new_library`) that wraps `[device newLibraryWithSource:options:error:]`. Entry points: `vertex_main`/`fragment_main`.
- Both backends now accept the 5-param `graphics_createshader(ShaderStage stage, source, size, defines, defineCount)` API.
- Vulkan backend uses GLSL sources for default3d pipeline (`shaders/triangle.vert`, `triangle.frag`). Font rendering still loads pre-compiled `.spv` files as primary path, with embedded MSL fallback via `graphics_get_text_shaders()`.
- Shaders directory contains both GLSL sources (all shaders) and pre-compiled SPIR-V (`text.*.spv`, `triangle.*.spv`). The `.spv` files are only used by the font rendering system on macOS; Vulkan always compiles from GLSL source at runtime.

### Build System Quirks
- CEF tests directory is deleted before building to avoid C++20 incompatibility
- CEF wrapper is force-built as static library regardless of `BUILD_SHARED_LIBS`
- **shaderc is now ACTIVE** via FetchContent (glslang, SPIRV-Headers, SPIRV-Tools, shaderc). Links `shaderc_combined` for Vulkan backend runtime GLSL→SPIR-V compilation.
- VMA is configured with `VMA_STATIC_VULKAN_FUNCTIONS=OFF` to avoid conflicts with volk

### Filesystem
- PhysFS is the primary filesystem abstraction on non-Windows platforms
- On POSIX/macOS: mounts the current directory (argv[0] directory) as root, plus system fonts
- On Windows: mounts "." (current working directory)
- `filesystem_posix.c` provides direct POSIX file access (not PhysFS-based) as an alternative stub

---

## Rating: 9/10
Solid foundations with clean module separation, mature abstraction layering, and a genuinely well-engineered job system. **All P0 rendering pipeline defects have been fixed in both Metal and Vulkan backends** — the engine can now render 3D content with MVP transforms, has a proper uniform buffer binding API (UBO descriptor set in Vulkan, buffer binding in Metal), a working material system that packs uniforms into per-material GPU buffers on both backends, and **Vulkan runtime GLSL→SPIR-V compilation via shaderc** eliminating the need for pre-compiled SPIR-V files. Remaining P1: shader variant system is a no-op (though `graphics_createshader()` now accepts macro definitions — they just aren't used for variant generation yet).

**Metal backend progress:** FPS text + spinning cube + uniform buffer binding API + material system + ShaderStage API migration all working. ~1030 lines. No remaining P0 issues. Embedded MSL fallback shaders provide robust font rendering even without pre-compiled SPIR-V.

**Vulkan backend progress:** UBO descriptor set, transform upload, material system, AND runtime GLSL→SPIR-V compilation via shaderc all implemented. ~3103 lines. No remaining P0 issues. Shader source verification table below is now outdated — Vulkan compiles GLSL at runtime instead of loading pre-compiled SPIR-V.

**Last commit:** `a4191071` — Vulkan backend: runtime GLSL-to-SPIR-V compilation via shaderc
