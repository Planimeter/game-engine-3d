# Planimeter Game Engine 3D — Session Memory

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

## Metal Backend Status (2026-07-29)

The Metal backend (`src/graphics_metal.mm`, ~1030 lines) is a complete port of the Vulkan backend for macOS. FPS text rendering and 3D model rendering (spinning cube with normal visualization) are working end-to-end at ~119 FPS.

### Implemented
- Device initialization, command queue, swapchain via CAMetalLayer
- Basic shader compilation (`vertex_main`/`fragment_main` entry points)
- Vertex/index buffer creation, texture creation and updates
- Texture binding + sampler binding (`graphics_bindtexture()`)
- Pipeline creation (vertex descriptor, depth-stencil state, render pipeline state)
- Render loop (predraw → draw calls → postdraw → present)
- FPS text rendering via embedded MSL fallback shaders
- Font atlas texture upload via staging buffer + blit encoder
- Alpha channel glyph rendering (FreeType alpha in `.a` channel)
- NDC-correct glyph positioning (Y-axis flip fixed)
- Transform matrix upload in all 3 draw calls (`graphics_drawmodel`, `graphics_draw_instanced`, `graphics_draw_buffers`)
- Default 3D fallback shader with `VERTEX_FORMAT_FULL` support (position, normal, tangent, bitangent, texcoord)
- `graphics_loadmodel()` GPU buffer creation (vertex + index buffer upload per mesh)
- End-to-end 3D model rendering test (spinning cube with MVP matrix, normal visualization)
- `graphics_binduniformbuffer()` API (binds a Buffer to both vertex and fragment stages at a given slot)
- `model_get_mesh_count()` C-compatible accessor for opaque Model struct
- Material system — `MetalMaterial` struct with CPU-side storage, per-material uniform buffer, `metal_material_pack()` for GPU upload, `graphics_setmaterial()` binds material buffer at slot 2 + textures

### Refactoring Completed
- Extracted direct Metal message sends into C wrappers in `graphics_metal_helpers.m` to work around ObjC++ dispatch issues
- Texture storage mode: `MTLStorageModePrivate` + blit encoder upload (fixes AGX GPU crash)
- `metal_buffer_update()` wraps `memcpy([buf contents], data, size)`
- `metal_texture_update_region()` uses staging buffer + blit encoder

### Remaining Issues
- `g_inPass` flag is set but `graphics_beginpass()`/`graphics_endpass()` are no-ops (just set/reset `g_inPass`)
- No depth texture created — depth attachment only enabled when depthTest/depthWrite is set
- `graphics_draw_instanced()` only uploads the first transform matrix to the GPU — all instances render with the same transform. Instance data array not yet uploaded.

## Vulkan Backend Status (2026-07-29)

The Vulkan backend (`src/graphics_vulkan.cpp`, ~3103 lines) is the primary graphics implementation for Windows and Linux.

### Implemented
- Full Vulkan pipeline: instance → physical device → logical device → swapchain → render pass → framebuffers → command buffers → semaphores/fences
- VMA for GPU memory allocation with proper memory type selection
- Runtime GLSL→SPIR-V compilation via shaderc (no pre-compiled `.spv` files needed)
- `ShaderStage` enum (`SHADER_STAGE_VERTEX`, `SHADER_STAGE_FRAGMENT`) — maps to `shaderc_shader_kind`
- Macro definition support: splits `"NAME=VALUE"` at first `=` for shaderc compile options
- Material system with `GPUMaterial` struct, `vulkan_material_pack()` matching Metal's layout
- UBO descriptor set (set 1) with 16 slots, global uniform buffer (4096 bytes)
- Pipeline creation supports multiple vertex formats (`VERTEX_FORMAT_FULL`, `POS_UV`, `POS_COLOR`)
- Blend modes: NONE, ALPHA, ADD, PREMULT

### Remaining Issues
- `graphics_beginpass()`/`graphics_endpass()` are no-op stubs on both backends
- Render pass always includes depth attachment even when no pipeline uses depth testing

---

## 🔴 Critical Issues

### P2 — Medium Priority

- **Shader Variant System Is a No-Op**
  `graphics_createshader()` accepts macro definitions and passes them to shaderc on Vulkan, but `graphics_get_shader_variant()` returns the base shader unchanged (both backends). Defines are silently discarded for variant generation.
  - Fix: Either implement the variant system (cache compiled variants by define set hash) or deprecate the `defines` parameter.

- **No Framebuffer/Render Target Abstraction**
  Hardcoded to swapchain only. No offscreen rendering, shadow maps, or post-processing. `graphics_createpass()` is a no-op stub on both backends.
  - Fix: Implement `graphics_createpass()` to create offscreen framebuffers with user-provided texture attachments.

- **No Skeletal Animation**
  Assimp animation data (`mAnimations`, `mBones`) is completely ignored. The `Model` struct has no animation fields, and `model_assimp.cpp` doesn't process `aiScene::mAnimations`.

- **Vulkan: Shader Recompilation on Resize** (`src/graphics_vulkan.cpp`)
  `graphics_resize()` calls `graphics_createshaders()` which re-reads GLSL source and recompiles via shaderc, even though the shader source hasn't changed. Adds unnecessary latency during window resize.
  - Fix: Cache compiled SPIR-V in memory after first compilation; only recompile if source hash changes.

- **Metal `graphics_present()` Synchronously Waits for GPU**
  `metal_command_buffer_wait()` blocks the CPU until the GPU finishes rendering, defeating async triple-buffered rendering. Should submit to next frame's command buffer while GPU processes current one.

- **Metal Shader Entry Points Are Hardcoded**
  All shaders must define `vertex_main` and `fragment_main`. Vulkan uses `"main"`. No support for compute shaders or custom entry points. Should be configurable.

- **Batch System Jobs Never Wired Up** (`src/font.c`)
  The batching system defines `shape_text_job` and `build_vertices_job` functions with job system integration, but `font_end_batch()` processes lines sequentially without using the job system. The parallel shaping infrastructure is defined but dead code.

- **No Error Return Codes — All Errors Call `exit()`**
  Shader compilation failures, buffer creation failures, and resize errors all call `exit(EXIT_FAILURE)`. For a game engine, returning NULL or an enum error code would be more appropriate so the application can handle gracefully.

- **Font Atlas Texture Upload Per-Glyph Performance** (`src/font.c`)
  Each glyph update calls `graphics_updatetexture()` individually. For a font with 100+ unique glyphs, this means 100+ GPU texture upload commands. A bulk upload after all glyphs are loaded would be significantly more efficient.

- **`graphics_opengl_sdl.c` Dead Code**
  The file exists but is not included in `CMakeLists.txt`'s `SOURCES` list. Not wired into the build system.

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

## Font Rendering Research

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

## Additional Observations

### Engine State at Runtime
- `framework.c` creates a test font and displays an FPS counter, plus loads and renders a spinning cube with normal-visualization colors
- Matrix math is provided by `math_c.h` / `math_glm.cpp` wrapping real GLM calls (SIMD, etc.) behind `extern "C"` functions
- 3D model rendering is working via the Metal backend with MVP matrix pipeline
- The default 3D shader visualizes normals as color (`normal * 0.5 + 0.5`) — debug quality, no proper lighting yet

### Memory Management
- VMA (Vulkan Memory Allocator) is used for all GPU allocations
- Staging buffers for texture uploads use VMA with `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT`
- `graphics_createmodelgpu()` maps, copies, and unmaps each mesh's vertex/index data — no persistent mapping
- No GPU mipmap generation (mipLevels = 1 everywhere)

### Portable API Design
- Graphics API is C99 with opaque handles (`typedef void* Shader, Texture, Material, Buffer, RenderPass, Pipeline`)
- Dual backend: `#ifdef APPLE` selects Metal, else Vulkan
- The Metal backend is slightly less feature-complete — `graphics_beginpass()`/`graphics_endpass()` are no-ops and the render pass abstraction is not yet implemented — but all core rendering paths (model drawing, text, materials) are working
- Some backends (OpenGL SDL, Null) exist but are not wired into the build system as primary backends

### Shader Compilation
- **Vulkan**: Compiles GLSL→SPIR-V at runtime via shaderc library. Reads raw `.vert`/`.frag` GLSL source files, targets Vulkan 1.1 environment, performance optimization level. Supports macro definitions via `defines`/`defineCount`. Entry point: `main`.
- **Metal**: Compiles MSL source at runtime via helper wrapper (`metal_device_new_library`). Entry points: `vertex_main`/`fragment_main`.
- Both backends accept the 5-param `graphics_createshader(ShaderStage stage, source, size, defines, defineCount)` API.
- Shaders directory contains both GLSL sources (all shaders) and pre-compiled SPIR-V (`text.*.spv`, `triangle.*.spv`). The `.spv` files are only used by the font rendering system on macOS; Vulkan always compiles from GLSL source at runtime.

### Build System Quirks
- CEF tests directory is deleted before building to avoid C++20 incompatibility
- CEF wrapper is force-built as static library regardless of `BUILD_SHARED_LIBS`
- shaderc is ACTIVE via FetchContent (glslang, SPIRV-Headers, SPIRV-Tools, shaderc)
- VMA is configured with `VMA_STATIC_VULKAN_FUNCTIONS=OFF` to avoid conflicts with volk

### Filesystem
- PhysFS is the primary filesystem abstraction on non-Windows platforms
- On POSIX/macOS: mounts the current directory (argv[0] directory) as root, plus system fonts
- On Windows: mounts "." (current working directory)
- `filesystem_posix.c` provides direct POSIX file access (not PhysFS-based) as an alternative stub

---

## Rating: 9/10

Solid foundations with clean module separation, mature abstraction layering, and a genuinely well-engineered job system. All P0 rendering pipeline defects have been fixed in both Metal and Vulkan backends — the engine can now render 3D content with MVP transforms, has a proper uniform buffer binding API (UBO descriptor set in Vulkan, buffer binding in Metal), a working material system that packs uniforms into per-material GPU buffers on both backends, and Vulkan runtime GLSL→SPIR-V compilation via shaderc eliminating the need for pre-compiled SPIR-V files.

The most urgent remaining issues are the **Shader Variant System no-op** and **framebuffer abstraction** — both P2. The second priority is implementing `graphics_createpass()` for offscreen rendering (P2), which unlocks shadow maps, post-processing, and deferred rendering patterns.
