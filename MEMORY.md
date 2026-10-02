# Planimeter Game Engine 3D — Session Memory

## Architecture Summary

**Layered module architecture** with C99/C++20, Vulkan graphics via volk+VMA, SDL3 windowing, OpenAL audio, PhysFS filesystem, FreeType+HarfBuzz fonts, Assimp 3D model loading, GLM math library, and optional CEF (Chromium 145) browser integration.

### Key Modules
| Module | API Header | Primary Impl | Null/Stub Impl |
|--------|-----------|-------------|----------------|
| Graphics | `src/graphics.h` | `src/graphics_metal.mm` (macOS), `src/graphics_vulkan.cpp` (Linux/Windows) | `src/graphics_null.c` |
| Window/Input | `src/window.h`, `src/event.h` | `src/window_sdl.c`, `src/event_sdl.c` | `src/window_null.c`, `src/event_null.c` |
| Audio | `src/audio.h` | `src/audio_openal.c` | `src/audio_null.c` |
| Font/Text | `src/font.h`, `src/text.h` | `src/font.c`, `src/text.c` | — (font has no stub) |
| Model | `src/model.h` | `src/model_assimp.cpp` | `src/model_null.c` |
| Math (C wrappers) | `src/math_c.h` | `src/math_glm.cpp` | `src/math_null.c` |
| Camera | `src/camera.h` | `src/camera.c` | — |
| Scene graph | `src/scene.h` | `src/scene.c` | — |
| Filesystem | `src/filesystem.h` | `src/filesystem_physfs.c` | `src/filesystem_null.c`, `src/filesystem_posix.c` |
| Timer | `src/timer.h` | `src/timer_sdl.c` | `src/timer_null.c` |
| Image | `src/image.h` | `src/image_stb.c` | `src/image_null.c` |
| Job System | `src/job.h` | `src/job_pthread.c` | `src/job_null.c` |

### Game Loop Pattern (`main_sdl.c`)
```
event_poll() → timer_step() → job_submit(update) → job_wait(update) 
           → job_submit(draw)  → job_wait(draw)  → graphics_present()
```

## Metal Backend Status (2026-10-01)

The Metal backend (`src/graphics_metal.mm`, ~1170 lines) is a complete port of the Vulkan backend for macOS. FPS text rendering and 3D model rendering (spinning cube with Blinn-Phong lighting and depth testing) are working end-to-end.

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
- Transform matrix upload in all 3 draw calls — `graphics_drawmodel()` copies {mvp, world} (128B) via `metal_encoder_set_vertex_bytes()` (encode-time copy, immune to later CPU writes); `graphics_draw_instanced()` now uploads the full per-instance array to a cached buffer (see Instanced Drawing); `graphics_draw_buffers()` still uses the legacy shared `g_uniformBuffer` upload (see Remaining Issues)
- Default 3D fallback shader with `VERTEX_FORMAT_FULL` support (position, normal, tangent, bitangent, texcoord)
- `graphics_loadmodel()` GPU buffer creation (vertex + index buffer upload per mesh)
- End-to-end 3D model rendering test (spinning cube with MVP matrix)
- `graphics_binduniformbuffer()` API (binds a Buffer to both vertex and fragment stages at a given slot)
- `model_get_mesh_count()` C-compatible accessor for opaque Model struct
- Material system — `MetalMaterial` struct with CPU-side storage, per-material uniform buffer, `metal_material_pack()` for GPU upload, `graphics_setmaterial()` binds material buffer at slot 2 + textures
- Material setters update-by-name — `MetalMaterial` carries `floatNames`/`vec3Names`/`textureNames`; `graphics_material_set_float/vec3/texture()` search by `strcmp` and update in place, else append with a `graphics_copy_name()` helper (mirrors the Vulkan backend). Before this fix every call appended, so per-frame `cameraPos` updates would have overflowed and frozen. Default material records names too.
- Depth testing — every pass (swapchain + offscreen) carries a `Depth32Float` attachment: swapchain lazily creates/resizes `g_depthTexture`, offscreen passes use `internalDepth` or a user depth texture; `graphics_createpipeline()` always declares `depthAttachmentPixelFormat` so pipeline/pass formats match
- Blinn-Phong lighting in the embedded MSL default shader — floats[0..2] = ambient/specular/shininess, vec3s[0..2] = lightDir (points away from light)/cameraPos/albedo; world matrix now comes from `TransformData.model` at buffer(1) (per-draw), `MaterialData.extraMatrix` retained for layout parity but no longer read
- Framework lit material (`g_litMaterial` in `framework.c`) — orange albedo, per-frame cameraPos + per-node world matrix
- `graphics_setmaterial()` fallback — draw calls bind `g_defaultMaterial` when passed NULL, and use the embedded default shader when a material has no shader

### Refactoring Completed
- Extracted direct Metal message sends into C wrappers in `graphics_metal_helpers.m` to work around ObjC++ dispatch issues
- Texture storage mode: `MTLStorageModePrivate` + blit encoder upload (fixes AGX GPU crash)
- `metal_buffer_update()` wraps `memcpy([buf contents], data, size)`
- `metal_texture_update_region()` uses staging buffer + blit encoder
- `metal_encoder_set_buffer()` sets the buffer on BOTH vertex and fragment stages — render encoders have no `setBuffer:offset:atIndex:` (only compute/blit do), which previously crashed with `unrecognized selector` as soon as a material was bound

### Remaining Issues
- ~~`graphics_draw_instanced()` only uploads the first transform matrix to the GPU — all instances render with the same transform. Instance data array not yet uploaded.~~ — fixed 2026-10-02, see "Instanced Drawing (2026-10-02)".
- `graphics_draw_buffers()` still writes the legacy shared uniform path (single `g_uniformBuffer` region / descriptor binding 0). `graphics_drawmodel()` got the per-draw fix and `graphics_draw_instanced()` now carries its own transforms. (`graphics_draw_buffers()` today is font/text only and its text shaders ignore the transform.)
- ~~Retina/pixel-density handling wrong~~ — fixed 2026-10-01: `SDL_WINDOW_HIGH_PIXEL_DENSITY` window flag + `NSHighResolutionCapable` in `cmake/MacOSXBundleInfo.plist.in` (CMake's default plist omits it) + `window_getpixeldensity()` + font rasterization/coords scaled by density. Verified: 640×480-pt window now renders at 1280×960.

## Camera & Scene System (2026-10-01)

Fly camera + fixed-pool scene graph replacing the hardcoded look-at in `framework_draw`.

### New modules
- `src/camera.c|h` — `Camera {position[3], yaw, pitch, fovY, zNear, zFar}` (radians). `camera_view()` builds `math_lookat` from yaw/pitch basis (yaw 0 = down -Z, positive turns right; pitch clamped ±~89°, yaw wrapped to ±π); `camera_projection()` wraps `math_perspective`; `camera_basis()` returns forward/right/up (up derived from pitch so look-at never degenerates); `camera_look(dx,dy,sens)`; `camera_move(fwd,right,up,dist)` strides along the basis; `camera_zoom(wheelY)` adjusts fovY clamped to 20°–100°.
- `src/scene.c|h` — `Scene` is a fixed pool of `SCENE_MAX_NODES 64` `SceneNode {parent, position, rotation(euler XYZ radians), scale, world[16], active}`. `scene_create(parent)` claims an inactive slot (-1 = root); `scene_setparent()` rejects cycles by walking the proposed parent's ancestor chain; `scene_update()` resolves each active node recursively (depth-guarded): `world = parentWorld * T * R_x * R_y * R_z * S` using `math_translate/rotate/scale/multiply`; `scene_world()` returns NULL for inactive handles.

### Framework wiring (`framework.c`)
- Scene: spinning root node (`rotation.y = elapsed*0.5`) with 3 children at (±1.3, 0, 0) and (0, 1.3, 0), each scaled 0.5 — children orbit the root, proving parent-child composition. 4 cubes drawn per frame, each with its own mvp and world matrix pushed to `g_litMaterial`.
- Controls: WASD strafe/fly + E/Q up/down (3 u/s, applied in `framework_update`), right-mouse-drag look (0.0025 rad/px, engages `window_setmouserelative(1)` — new `window.h`/`window_sdl.c` wrapper for `SDL_SetWindowRelativeMouseMode`), wheel zoom. Scancode names from `SDL_GetScancodeName` ("W" etc.), ignores repeats.
- Per frame: `cameraPos` vec3 re-set on `g_litMaterial` (works because Metal setters are update-by-name now); `camera_projection`/`camera_view` replace the hardcoded perspective/look-at.
- Verified on Metal: 4 cubes spinning, keyboard strafe works, lit correctly, FPS 59–119.

### Per-draw uniform aliasing fix (critical, both backends)
Previously `graphics_drawmodel` wrote mvp into one shared `g_uniformBuffer` region and the material's model mat4 into one shared pack — but the GPU reads uniforms when the frame *executes*, after all CPU writes, so N draws in one frame would all render with the last draw's matrices. Fixed only in `graphics_drawmodel()` (the only multi-draw path):
- **Metal**: bundles `{mvp[16], model[16]}` = 128B and copies it into the command buffer with new helper `metal_encoder_set_vertex_bytes()` (`setVertexBytes:length:atIndex:`, index 1); MSL `TransformData {float4x4 mvp; float4x4 model;}` at buffer(1), normals/worldPos from `transform.model`. Material block unchanged (pack still writes `extraMatrix`, now unread).
- **Vulkan**: `VkPushConstantRange {VERTEX, 0, 128}` added to the single pipeline layout (`graphics_creategraphicspipeline`); `graphics_drawmodel` records `vkCmdPushConstants` with `{mvp, model}` *after* binding the pipeline (material's `mat4`, identity if mat NULL). `shaders/triangle.vert` now reads `layout(push_constant) uniform Transform {mat4 mvp; mat4 model;}` — binding0 UBO removed from the shader (descriptor layout keeps the binding, unused). `.spv` regenerated (`glslangValidator -V` + `spirv-val` clean), file passes `clang++ -fsyntax-only`.
- API unchanged: `graphics_drawmodel(model, mat, mvp)` + `graphics_material_set_mat4(mat, world)` per node still works — backends read the CPU-side mat4 at draw time.

### font.c SPIR-V guard (fixes startup noise)
`font_load_text_shaders()` on Apple now returns 1 immediately without loading `text.*.spv` — previously SPIR-V bytes were fed to the MSL compiler every startup, printing two bogus `Metal shader compilation failed ... <U+0003><U+0002>#<U+0007>` errors (SPIR-V magic 0x07230203) before the embedded MSL fallback succeeded.

## Vulkan Backend Status (2026-07-29)

The Vulkan backend (`src/graphics_vulkan.cpp`, ~3103 lines) is the primary graphics implementation for Windows and Linux.

### Implemented
- Full Vulkan pipeline: instance → physical device → logical device → swapchain → render pass → framebuffers → command buffers → semaphores/fences
- VMA for GPU memory allocation with proper memory type selection
- Runtime GLSL→SPIR-V compilation via shaderc (no pre-compiled `.spv` files needed)
- `ShaderStage` enum (`SHADER_STAGE_VERTEX`, `SHADER_STAGE_FRAGMENT`) — maps to `shaderc_shader_kind`
- Macro definition support: splits `"NAME=VALUE"` at first `=` for shaderc compile options
- Material system with `GPUMaterial` struct, `vulkan_material_pack()` matching Metal's layout
- UBO descriptor set (set 1) as 16 discrete bindings (binding = slot, 1 UBO each) — `graphics_binduniformbuffer()` writes `dstBinding = slot`; matches the GLSL `layout(set = 1, binding = N)` blocks
- Default 3D pipeline reads `shaders/triangle.vert`/`triangle.frag` — 3D Blinn-Phong pair; per-draw {mvp, model} come from a `layout(push_constant)` block (128B vertex-stage range in the pipeline layout, pushed by `graphics_drawmodel`), Material at set1/binding1, y-flip for Vulkan NDC; both validate clean with `glslangValidator -V` + `spirv-val`
- Pipeline creation supports multiple vertex formats (`VERTEX_FORMAT_FULL`, `POS_UV`, `POS_COLOR`)
- Blend modes: NONE, ALPHA, ADD, PREMULT

### Remaining Issues
- ~~Vulkan backend only syntax-checked on macOS~~ — now runtime-verified via MoltenVK, see "Vulkan on MoltenVK (2026-10-01)"
- Pre-existing compile break fixed: `GPUBuffer` typedef used at line 134 but declared ~1700 — moved the typedef up so the file compiles again

## Instanced Drawing (2026-10-02)

`graphics_draw_instanced()` uploaded only the first transform, so every instance rendered with the same one and all copies landed on top of each other. Two real bugs, plus a Metal-specific surprise:

- **No view-projection parameter.** The API took only per-instance transforms, so nothing ever placed instances in the world. Added `viewProjection4x4` to the signature in `graphics.h` and all three backends; the shared view-projection is supplied separately from the per-instance model matrices.
- **Instance buffer destroyed while still in use.** Metal created the instance buffer per draw and released it immediately after encoding — the in-flight command buffer still read it, so all instances sampled recycled memory. Now a static cached buffer that grows on demand (`g_instanceBuffer`).
- **Metal ignores a per-instance `MTLVertexDescriptor` layout here.** Declaring `layouts[1].stepFunction = MTLVertexStepFunctionPerInstance` plus `[[attribute(5-8)]]` produced a valid pipeline that Metal validation reported *no* errors on, but every instance sampled the same transform. Working approach: bind the instance buffer at `buffer(1)` and index it in the shader with `[[instance_id]]`. `VERTEX_FORMAT_INSTANCE` therefore just reuses the `VERTEX_FORMAT_FULL` descriptor — no per-instance layout, no extra attributes.
- Added a dedicated instance vertex shader per backend: embedded MSL `instance3dCombined` in `graphics_metal.mm` (`g_instanceVertShader`), and `shaders/triangle_instanced.vert` for Vulkan (validates clean with `glslangValidator -V` + `spirv-val`).
- `framework.c` draws a 10×10 grid (100 cubes in one `graphics_draw_instanced()` call, `g_gridMaterial`) so the path stays exercised.

Verified on Metal at 59-60 FPS: instances render at distinct transforms.

**Known breakage / open questions:** the Vulkan instancing path crashed on MoltenVK with `EXC_BAD_ACCESS` inside `MVKShaderModule::getMTLFunction` during pipeline creation. The suspected cause was the raw `layout(location = 5..8)` instance vertex inputs, so `triangle_instanced.vert` was rewritten to read a uniform array at set 1 binding 2 indexed by `gl_InstanceIndex` (`MAX_INSTANCES 256`, mirrored in `graphics_vulkan.cpp`), with the transforms uploaded to a cached buffer bound at UBO slot 2. **That rewrite was never actually confirmed to fix anything** — enabling `_DEBUG` to get validation output introduced the unrelated `volkInitialize` startup crash above, which masked the original. Treat the uniform-array version as plausible but unverified.

Also: enabling `_DEBUG` with `-O0` trips a pre-existing `std::system_error: mutex lock failed` in the job system (`job_pthread.c`), unrelated to Vulkan.

## Vulkan on MoltenVK (2026-10-01)

First runtime test of the Vulkan backend, on macOS through Homebrew's MoltenVK. Five real bugs, all invisible to `clang++ -fsyntax-only`:

- **`GAME_USE_VULKAN=ON`** CMake option builds `graphics_vulkan.cpp` on macOS instead of Metal, defines `GRAPHICS_VULKAN` for `window_sdl.c` (requests `SDL_WINDOW_VULKAN` + real surface) and `font.c` (keeps the SPIR-V text shaders instead of the MSL fallback), and adds `-Wl,-rpath,/opt/homebrew/lib` so volk/SDL can dlopen the loader by leaf name.
- **`GAME_VULKAN_VALIDATION=ON`** CMake option defines `_DEBUG`, which gates the validation-layer probe in `graphics_createinstance()`. Beware: a `-O0 -D_DEBUG` build also trips a pre-existing `std::system_error: mutex lock failed` in the job system, unrelated to Vulkan — so enable validation without forcing `-O0`.
- **`volkInitialize()` must precede every Vulkan call, including the `_DEBUG` validation probe.** `graphics_createinstance()` used to call `vkEnumerateInstanceLayerProperties` ~30 lines *before* `volkInitialize()`, so any `_DEBUG` build jumped to a null function pointer and crashed at startup with `EXC_BAD_ACCESS (address=0x0)`. Only the `_DEBUG` path hit it, which is why the release build ran fine. volkInitialize is now the first thing the function does.
- **Run command needs the ICD**: the loader does not search Homebrew's prefix, so `VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json ./build/game.app/Contents/MacOS/game`. Without it: "Installed Vulkan Portability library doesn't implement the VK_KHR_surface extension" + `vkCreateInstance -9`.
- **`graphics_createshader` ate SPIR-V**: shaderc only takes GLSL, so the font's `text.*.spv` blew up in the GLSL preprocessor. Now sniffs the SPIR-V magic (`\x03\x02\x23\x07`) and goes straight to `vkCreateShaderModule`.
- **`GPUModel` type-pun**: `graphics_loadmodel` returned a `GPUModel*` cast to `Model*`, so `model_get_mesh_count()` read a pointer field as a count (printed `1015160656 meshes`). Rewritten to match Metal — the real `Model*` is returned and GPU buffers live in `Mesh {vertexBuffer, indexBuffer, vertexBufferMemory, indexBufferMemory}`, so CPU mesh data now survives instead of being destroyed after upload.
- **No depth state on the default pipeline**: `pDepthStencilState` was NULL while the render pass had a depth attachment, which per spec makes every draw discard all fragments. Added `depthTestEnable/depthWriteEnable/compareOp = LESS` to `graphics_creategraphicspipeline`.
- **Material bound too early**: `graphics_drawmodel` called `graphics_setmaterial()` before `graphics_predraw()` had issued `vkCmdBeginRenderPass`/`vkCmdBindDescriptorSets`. Vulkan records commands, so the descriptor binding landed before the pass began and the material never reached the GPU (everything rendered near-black). The bind now happens inside `graphics_drawmodel`.
- **Face winding**: both pipelines used `VK_FRONT_FACE_CLOCKWISE`, but the vertex shaders negate y for Vulkan's y-down clip space, which reverses screen-space winding — back-face culling was eating the outside faces. Both now use `VK_FRONT_FACE_COUNTER_CLOCKWISE`.
- `shaders/text.vert` also needed the same y-flip as `triangle.vert`, otherwise text drew bottom-left and mirrored.
- Result: 4-cube scene + FPS text render correctly through MoltenVK at 60–94 FPS.

---

## 🔴 Critical Issues

### P2 — Medium Priority

- **Shader Variant System Is a No-Op**
  Assimp animation data (`mAnimations`, `mBones`) is completely ignored. The `Model` struct has no animation fields, and `model_assimp.cpp` doesn't process `aiScene::mAnimations`.

- **~~No Error Return Codes — All Errors Call `exit()`~~** — partially fixed 2026-10-02: shader/buffer paths now return NULL/failure. Both backends already returned NULL from `graphics_create*buffer`/`graphics_createshader` (this issue was overstated for those); the Vulkan `exit()`s that mattered were in `graphics_createshader` (shaderc failure, module create) and `graphics_createshaders` (default-pipeline compile), now `return NULL` / `return 0` with callers logging and aborting startup or the resize instead of hard-exiting mid-frame. `graphics_createpipeline` now rejects NULL shaders. Still `exit(EXIT_FAILURE)`: Metal `graphics_init` (no device/view/layer — genuinely fatal at startup) and Vulkan's device/swapchain/renderpass init paths.

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

### Atlas implementation (2026-10-01)
- 4×1024 pages, shelf packing, glyphs grouped per-atlas at draw time; when all pages fill, the LRU page is evicted (cleared, its glyphs dropped so they re-rasterize on next use) instead of failing forever
- Glyph resolution is single-threaded (warm-up pass in `font_print`/`font_end_batch` before the parallel vertex build) so eviction can't race worker threads or invalidate UVs mid-build
- `FONT_ATLAS_STRESS=<n>` env var at startup force-loads glyph ids 0..n-1 (0 = whole font) and prints atlas/eviction counts — used to verify eviction (Geneva full font: 3742 glyphs, 2 pages, 1 eviction under a temporary cap of 2; production cap 4, 0 evictions)

---

## Additional Observations

### Engine State at Runtime
- `framework.c` creates a test font and displays an FPS counter, plus renders a 4-cube scene graph (spinning root + 3 orbiting children) through a fly camera (WASD/EQ, right-drag look, wheel zoom) with Blinn-Phong directional lighting
- Matrix math is provided by `math_c.h` / `math_glm.cpp` wrapping real GLM calls (SIMD, etc.) behind `extern "C"` functions
- 3D model rendering is working via the Metal backend with MVP matrix pipeline
- The default 3D shader applies Blinn-Phong lighting (ambient + diffuse + specular) with material-provided light direction, camera position, and albedo; camera position is re-uploaded to the material every frame

### Memory Management
- VMA (Vulkan Memory Allocator) is used for all GPU allocations
- Staging buffers for texture uploads use VMA with `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT`
- `graphics_createmodelgpu()` maps, copies, and unmaps each mesh's vertex/index data — no persistent mapping
- No GPU mipmap generation (mipLevels = 1 everywhere)

### Portable API Design
- Graphics API is C99 with opaque handles (`typedef void* Shader, Texture, Material, Buffer, RenderPass, Pipeline`)
- Dual backend: `#ifdef APPLE` selects Metal, else Vulkan
- The Metal backend is slightly less feature-complete than Vulkan — but all core rendering paths (model drawing, text, materials, depth testing, lighting) are working
- Some backends (Null) exist but are not wired into the build system as primary backends

### Shader Compilation
- **Vulkan**: Compiles GLSL→SPIR-V at runtime via shaderc library. Reads raw `.vert`/`.frag` GLSL source files, targets Vulkan 1.1 environment, performance optimization level. Supports macro definitions via `defines`/`defineCount`. Entry point: `main`.
- **Metal**: Compiles MSL source at runtime via helper wrapper (`metal_device_new_library`). Entry points: `vertex_main`/`fragment_main`.
- Both backends accept the 5-param `graphics_createshader(ShaderStage stage, source, size, defines, defineCount)` API.
- Shaders directory contains both GLSL sources (all shaders) and pre-compiled SPIR-V (`text.*.spv`, `triangle.*.spv`). The `.spv` files are no longer used at runtime on macOS (`font_load_text_shaders()` skips them on Apple — they were being fed to the MSL compiler and failing); Vulkan always compiles from GLSL source at runtime via shaderc.

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

The most urgent remaining issue is the **Shader Variant System no-op** (P2). The framebuffer/render target abstraction has been implemented on both backends — `graphics_createpass()` now creates offscreen render passes with user-provided texture attachments, `graphics_beginpass(NULL)` resumes swapchain rendering, and `graphics_destroypass()` cleans up. This unlocks shadow maps, post-processing, and deferred rendering patterns.
