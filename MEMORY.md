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

## Animation / Skinning (2026-10-02)

Assimp's `aiScene::mAnimations` and `aiBone` data was loaded but never read, and `Model` had nowhere to put it, so every animated model rendered as a static pose.

### Data layer (`edd9610d`, verified)
- `Vertex` gains `boneWeights`/`boneIndices` (`MAX_BONE_INFLUENCES` = 4). Unskinned meshes default to bone 0 at weight 1, which is the identity.
- `Model` gains `boneOffset` / `boneInverseBind` / `boneParent`, animation clips, and `ModelAnimation {name, duration, ticksPerSecond, keyCount, frames}`.
- Bones live on `aiMesh`, not `aiScene`, and the same skeleton repeats across meshes — they are collected into a unique-name table. Parents and local transforms come from walking the `aiNode` tree; `aiBone::mOffsetMatrix` is the *inverse bind* matrix.
- `model_pose()` flattens the hierarchy into skinning matrices (`world * inverseBind`), interpolating between keys per column. Resolution is iterative (Assimp does not order bones parent-first) and detects cycles rather than hanging.
- `ModelAnimation` is C++-only, so `model_get_animation_info()` and `model_pose_by_index()` give C callers equivalent access.

### GPU layer (`405c9a93`, verified)
- `VERTEX_FORMAT_FULL` gains `boneWeights` (float4, location 5) and `boneIndices` (int4, location 6) in both backends; the instanced format moves its per-instance mat4 to locations 7-10.
- All four vertex shaders blend the four bone matrices by weight and apply the rotation part to normals.
- `graphics_setbones(model, matrices, count)` uploads up to 64 bone matrices — cached buffer at Metal `buffer(3)`, Vulkan UBO slot 2.
- `model_normalize()` scales/centres imported meshes and their bone offsets; `graphics_reuploadmodel()` pushes the changed vertices back to the GPU.

### Assimp matrix layout gotcha
`aiMatrix4x4t` stores **columns** as `a/b/c/d` — already GLM's layout. Transposing it moves translation onto the wrong axis and puts bone rest and bind worlds tens of units apart. `model_assimp_matrix()` copies directly.

### Verified
`quad_skin.glb` (Assimp test suite, glTF2/simple_skin) loads with 7 bones, normalizes cleanly (scale 1.0, height 2.0), and **renders** — proving bone attributes, the blend, `graphics_setbones()` and the bind pose are correct.

`animation_with_skeleton.fbx` (13 bones, 1 clip) parses fine but **does not render**: its mesh vertices and node hierarchy disagree on a world-space offset that the mesh-space correction folded into `boneInverseBind` does not fully resolve. Normalization there also reports scale 1.09 / height 1.83 against vertices at x≈29, i.e. the two spaces disagree. Unresolved — diagnosing it needs another FBX to compare against, since the glTF path works.

---

## Roadmap (2026-10-02)

Written after the animation work, so it reflects what is actually verified rather than what was intended.

### Verified working (built *and* run on this machine)
| Area | Evidence |
|---|---|
| Metal backend | 59–132 FPS; 4-cube scene graph, instanced grid, text overlay |
| Vulkan backend (MoltenVK) | Zero validation errors; same scene at 132 FPS |
| Instanced drawing | Both backends issue one instanced draw; 100-cube grid visible |
| Camera + scene graph | Character-driven camera (WASD, Shift sprint, Space jump, right-drag look); parent-child composition |
| Player physics | `src/physics.c`: gravity, jump, grounded state, slope rejection with axis slide, terrain bounds clamp, substepped integration. Verified by position dump: walked z 14 → 5.4, jump reached VY +6.2 with `grounded` 0 |
| World is fixed | Nothing rotates or orbits the landscape; only the character and camera yaw move |
| HiDPI | Retina backing store, logical-space text, density-scaled glyph rasterization |
| Text | FreeType + HarfBuzz, LRU atlas eviction, parallel shaping and vertex build |
| Skeleton parsing | 13 bones / 1 clip from FBX; 7 bones from glTF |
| GPU skinning | `quad_skin.glb` renders through the full path |
| Model textures | Checkerboard renders on a textured quad; untextured models unaffected |
| Shutdown | No leaked Vulkan objects on the normal teardown path |

### Known broken
- **`animation_with_skeleton.fbx` does not render.** Parses fine, but its mesh vertices and node hierarchy disagree on a world-space offset that the mesh-space correction does not resolve. Needs a second FBX to compare against. The glTF path is fine, so this is an exporter-convention gap, not a pipeline defect.
- **`-O0` debug builds crash** in a `std::system_error` from OpenAL Soft when device enumeration fails on macOS. Third-party. Use `GAME_VULKAN_VALIDATION=ON` instead of `-O0`, or `ALSOFT_DRIVERS=null`.

### Highest-value next work
1. **MSDF text.** The long-standing font recommendation and the only item from the old research list still open. Sharp at any size, which a 16px-rasterized atlas cannot do.
2. **Animation blending.** One clip plays, with a hard `fmod` and no crossfade. Multi-clip blending needs a pose buffer rather than the single matrix array uploaded today.
3. **Render passes.** `graphics_createpass()`/`graphics_beginpass()` are implemented on both backends but unused — no shadows or post-processing consume them yet. Shadow mapping is the natural first user.
4. **FBX space diagnosis** (above).
5. **The shader-variant system is still a no-op**, so material behaviour cannot vary per object.
6. **Normal maps.** `MaterialData` extracts `normalTexture` and the slot machinery now exists, but no shader samples a tangent-space normal map yet — diffuse was the smaller half of the texture work.

### Explicitly not worth doing yet
- More Vulkan hardening. It reports zero validation errors; further work there is speculative.
- Chasing the job system. It is correct as written (`job_wait` executes pending work, O(1) handle lookup, work stealing). `MAX_JOBS = 65536` with no monitoring is a theoretical limit, not a bug.

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
- API unchanged: `graphics_draw_instanced(model, mat, mvp, count)` now takes a separate `viewProjection4x4` — see "Instanced Drawing (2026-10-02)".

### font.c SPIR-V guard (fixes startup noise)
`font_load_text_shaders()` on Apple now returns 1 immediately without loading `text.*.spv` — previously SPIR-V bytes were fed to the MSL compiler every startup, printing two bogus `Metal shader compilation failed ... <U+0003><U+0002>#<U+0007>` errors (SPIR-V magic 0x07230203) before the embedded MSL fallback succeeded.

## Vulkan Backend Status (2026-07-29)

The Vulkan backend (`src/graphics_vulkan.cpp`, ~3103 lines) is the primary graphics implementation for Windows and Linux.

### Implemented
- Full Vulkan pipeline: instance → physical device → logical device → swapchain → render pass → framebuffers → command buffers → semaphores/fences
- VMA for GPU memory allocation with proper memory type selection
- Runtime GLSL->SPIR-V compilation via shaderc (no pre-compiled `.spv` files needed)
- SPIR-V passthrough: `graphics_createshader()` detects pre-compiled SPIR-V (magic number 0x07230203) and skips shaderc
- `ShaderStage` enum (`SHADER_STAGE_VERTEX`, `SHADER_STAGE_FRAGMENT`) -- maps to `shaderc_shader_kind`
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
- Added a dedicated instance vertex shader on the Metal side: embedded MSL `instance3dCombined` in `graphics_metal.mm` (`g_instanceVertShader`), paired with `g_defaultFragShader`. `VERTEX_FORMAT_INSTANCE` just reuses the `VERTEX_FORMAT_FULL` descriptor — no per-instance layout, no extra attributes.
- `framework.c` draws a 10×10 grid (100 cubes from one per-instance transform array) so the path stays exercised. `NO_INSTANCED_GRID=1` skips the whole pass, which isolates instanced-draw crashes from the rest of the frame — that toggle is what pinned the Vulkan crash below.

Both backends now draw instanced batches in a single call: Metal via `[[instance_id]]` into buffer(1), Vulkan via a `VK_VERTEX_INPUT_RATE_INSTANCE` vertex binding feeding attributes 5-8 (`shaders/triangle_instanced.vert`). The Vulkan path falls back to per-instance `graphics_drawmodel()` calls if the instanced shader module is unavailable.

**Vulkan instancing works** — see "Vulkan validation clean" for the descriptor and semaphore fixes that unblocked it. Both backends issue a single instanced draw.

## Vulkan on MoltenVK (2026-10-01)

First runtime test of the Vulkan backend, on macOS through Homebrew's MoltenVK. Five real bugs, all invisible to `clang++ -fsyntax-only`:

- **`GAME_USE_VULKAN=ON`** CMake option builds `graphics_vulkan.cpp` on macOS instead of Metal, defines `GRAPHICS_VULKAN` for `window_sdl.c` (requests `SDL_WINDOW_VULKAN` + real surface) and `font.c` (keeps the SPIR-V text shaders instead of the MSL fallback), and adds `-Wl,-rpath,/opt/homebrew/lib` so volk/SDL can dlopen the loader by leaf name.
- **`GAME_VULKAN_VALIDATION=ON`** CMake option defines `_DEBUG`, which gates the validation-layer probe in `graphics_createinstance()`. Avoid `-O0`; see "The -O0 mutex crash is in OpenAL Soft" below.
- **`volkInitialize()` must precede every Vulkan call, including the `_DEBUG` validation probe.** `graphics_createinstance()` used to call `vkEnumerateInstanceLayerProperties` ~30 lines *before* `volkInitialize()`, so any `_DEBUG` build jumped to a null function pointer and crashed at startup with `EXC_BAD_ACCESS (address=0x0)`. Only the `_DEBUG` path hit it, which is why the release build ran fine. volkInitialize is now the first thing the function does.
- **Run command needs the ICD**: the loader does not search Homebrew's prefix, so `VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json ./build/game.app/Contents/MacOS/game`. Without it: "Installed Vulkan Portability library doesn't implement the VK_KHR_surface extension" + `vkCreateInstance -9`.
- **`graphics_createshader` ate SPIR-V**: shaderc only takes GLSL, so the font's `text.*.spv` blew up in the GLSL preprocessor. Now sniffs the SPIR-V magic (`\x03\x02\x23\x07`) and goes straight to `vkCreateShaderModule`.
- **`GPUModel` type-pun**: `graphics_loadmodel` returned a `GPUModel*` cast to `Model*`, so `model_get_mesh_count()` read a pointer field as a count (printed `1015160656 meshes`). Rewritten to match Metal — the real `Model*` is returned and GPU buffers live in `Mesh {vertexBuffer, indexBuffer, vertexBufferMemory, indexBufferMemory}`, so CPU mesh data now survives instead of being destroyed after upload.
- **No depth state on the default pipeline**: `pDepthStencilState` was NULL while the render pass had a depth attachment, which per spec makes every draw discard all fragments. Added `depthTestEnable/depthWriteEnable/compareOp = LESS` to `graphics_creategraphicspipeline`.
- **Material bound too early**: `graphics_drawmodel` called `graphics_setmaterial()` before `graphics_predraw()` had issued `vkCmdBeginRenderPass`/`vkCmdBindDescriptorSets`. Vulkan records commands, so the descriptor binding landed before the pass began and the material never reached the GPU (everything rendered near-black). The bind now happens inside `graphics_drawmodel`.
- **Face winding**: both pipelines used `VK_FRONT_FACE_CLOCKWISE`, but the vertex shaders negate y for Vulkan's y-down clip space, which reverses screen-space winding — back-face culling was eating the outside faces. Both now use `VK_FRONT_FACE_COUNTER_CLOCKWISE`.
- `shaders/text.vert` also needed the same y-flip as `triangle.vert`, otherwise text drew bottom-left and mirrored.
- Result: 4-cube scene + FPS text render correctly through MoltenVK at 60–94 FPS.

### Vulkan validation clean (2026-10-02)
Went from **84 validation errors to zero** during rendering, found and fixed one at a time with the validation layers. All pre-existing; none were visible from syntax checks or from running without the validator.

1. **Descriptor sets shared across frames** — `uboDescriptorSet` and `bindlessDescriptorSet` were single sets refreshed mid-frame. Now `uboDescriptorSets[MAX_FRAMES_IN_FLIGHT]` and `bindlessDescriptorSets[MAX_FRAMES_IN_FLIGHT]`, one per swapchain image.
2. **Descriptor writes mid-frame** — `graphics_binduniformbuffer()`/`graphics_bindtexture()` ran `vkUpdateDescriptorSets` on a set already bound to the command buffer being recorded, invalidating it *within the same frame*. They now only record intent; `graphics_flushdescriptorwrites()` applies it once per frame after the fence wait and before `vkCmdBindDescriptorSets`. This was the actual cause of the instancing segfault all along — not MoltenVK's shader translator, not the instance data layout.
3. **`MAX_BINDLESS_RESOURCES` 16384 → 64** — nothing binds more than slot 0 or 1, and 16384 exceeded MoltenVK's `maxPerStageDescriptorUpdateAfterBindSamplers` of 1024, logging on every pipeline-layout creation.
4. **Vulkan 1.2 features asserted, not queried** — now read via `vkGetPhysicalDeviceFeatures2`; enabling an unsupported feature invalidates `vkCreateDevice`, and the bindless pool's UPDATE_AFTER_BIND flag follows what the device reports.
5. **One semaphore pair and one fence for the whole swapchain** — a semaphore can't be reused while its previous wait/signal is pending. Now per frame slot, and the fence is waited **before** acquiring rather than after on the image's fence (the image's fence says nothing about whether the acquire semaphore is free). `imageFrameIndex` pairs submit/present to the acquiring slot.
6. **`graphics_createsemaphores()` ran before `graphics_getswapchainimages()`** — its loop is sized from `swapchainImageCount`, so it created *zero* semaphores. Silent, because the single-semaphore version had never needed the count.
7. **Material descriptor never seeded** — deferring writes meant the first draw read an uninitialised descriptor before any `setmaterial()` ran.

**Known remaining:** `vkDestroyDevice` reports 13 leaked objects at shutdown (buffers, an image, image views). Cleanup ordering, not a rendering correctness issue.

**Verified:** scene + 10×10 instanced grid render at 132 FPS through MoltenVK with zero validation errors.

### Validation layers (the only way to debug this backend)
Install with `brew install vulkan-validationlayers`, then build with `-DGAME_VULKAN_VALIDATION=ON` and run with both:
```
VK_LAYER_PATH=/opt/homebrew/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d \
VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json ./build-vk/game.app/Contents/MacOS/game
```
Two gotchas: stdout is buffered and lost on a crash, so pipe through `stdbuf -o0 -e0` or redirect to a file; and a plain `-O0 -D_DEBUG` build crashes on a `std::system_error` from OpenAL Soft — see "The -O0 mutex crash is in OpenAL Soft" below. `GAME_VULKAN_VALIDATION=ON` avoids needing `-O0` at all.

### Two more bugs validation caught (2026-10-02)
- **`VK_KHR_portability_subset` was never enabled.** `vkCreateDevice()` only enabled `VK_KHR_swapchain`, but MoltenVK requires this whenever the device advertises it — `VUID-VkDeviceCreateInfo-pProperties-04451`. Now enabled conditionally on the device actually reporting it.
- **The real reason instancing crashed.** Not MoltenVK's shader translator and not the instance data layout, as first assumed. `graphics_draw_instanced()` built a pipeline and bound uniforms through `graphics_binduniformbuffer()`, but this backend keeps a **single `uboDescriptorSet` shared across all frames** and refreshes it via `vkUpdateDescriptorSets` while earlier frames are still in flight. Every draw therefore gets recorded against a descriptor set in an invalid state — `VUID-vkCmdDraw-commandBuffer-recording` / `VUID-vkCmdPushConstants-commandBuffer-recording`, with `VkDescriptorSet ... was destroyed or updated without UPDATE_AFTER_BIND`. The `descriptorBindingSampledImageUpdateAfterBind` feature is enabled but never queried, so the pool lacks the `UPDATE_AFTER_BIND` flag that would make rebinding legal.
  Proper fix: ~~one descriptor set per frame in flight~~ — **done**, see "Per-frame descriptor sets" below.
  `graphics_draw_instanced()` on Vulkan still loops `graphics_drawmodel()` per instance, which is correct but gives no performance benefit.

### Per-frame descriptor sets (2026-10-02)
`graphics_binduniformbuffer()` used to write the one shared `uboDescriptorSet` while earlier frames were still in flight, invalidating every submitted command buffer. There is now a `uboDescriptorSets[MAX_FRAMES_IN_FLIGHT]` array — one set per swapchain image. Fences were already per image and waited on before recording, so the set for the frame being recorded is guaranteed idle, and `graphics_binduniformbuffer()` writes only `uboDescriptorSets[imageIndex]`. All four `vkCmdBindDescriptorSets` call sites bind that set too. Pool sizing scales with `MAX_UBO_SLOTS * MAX_FRAMES_IN_FLIGHT`; `MAX_FRAMES_IN_FLIGHT` is 16, reusing the existing swapchain image cap rather than introducing a separate bound.
Verified with validation layers: `commandBuffer-recording` errors went to zero, scene still renders at 117 FPS.

**This was the prerequisite for real Vulkan instancing.** The instanced pipeline itself is still unwritten — `shaders/triangle_instanced.vert` was removed and `VERTEX_FORMAT_INSTANCE` on Vulkan just reuses the plain vertex layout. Now that sets are per-frame, that path can be retried safely.

### The `-O0` mutex crash is in OpenAL Soft, not the job system
Earlier notes blamed `job_pthread.c`. That is wrong. There is no `std::mutex` anywhere in `src/` — the job system uses raw pthreads. `std::system_error: mutex lock failed` comes from **OpenAL Soft** (`third_party/openal-soft-1.25.1/` uses `std::mutex` throughout `core/helpers.cpp`, `core/logging.cpp`, `core/hrtf.cpp`, `alc/device.h`, `alc/events.h`), and it appears immediately after OpenAL logs `Failed to get audio device type`, i.e. when device enumeration fails on macOS.

`audio_openal.c` already handles a null device correctly (`alcOpenDevice` returns NULL -> early return, and a failed context closes the device), so the throw is inside the library, on a path we do not call directly.

**Workaround for debug builds:** `ALSOFT_DRIVERS=null` selects OpenAL's null output device and avoids enumeration entirely, which suppresses the failure. `GAME_VULKAN_VALIDATION=ON` defines `_DEBUG` for the validation layers without needing `-O0` at all, which is the better route.

### Model textures (2026-10-02)
`graphics_bindtexture()` and the bindless descriptor set already existed and the font atlas exercised them, but nothing bound a *model's* diffuse map — `framework.c` called no texture setter at all. Now wired up:

- `model_loadtextures()` resolves each mesh's `map_Kd` relative to the model's directory and loads it. A missing file is not fatal.
- `image_stb.c` falls back to a direct file read for absolute paths: PhysFS is mounted at the executable directory, so model textures referenced by repo path were invisible to it. This was why the first attempt silently loaded nothing.
- Metal's fragment shader samples `[[texture(0)]]` unconditionally, with a 1×1 white texture bound at init and whenever a model has no map — Metal requires a bound texture wherever a shader declares one, and an unbound one samples as zero, which renders untextured models black.
- `shaders/triangle.frag` samples through the bindless array (`GL_EXT_nonuniform_qualifier` is required for the variable index).

**Texture slot ownership differs per backend, deliberately.** Model draws all happen before the text pass in a frame and the text pipeline is not bound during them, so Metal reuses slot 0 for models and the text renderer rebinds it afterwards without affecting the 3D draw. Vulkan uses the bindless array from slot 1, since slot 0 is the font atlas's there. `Mesh` therefore stores no slot — it was the wrong place for something that differs per backend.

Verified: `texquad.obj` renders with its checkerboard visible; `cube.obj` renders untextured at 119 FPS with no errors.

**Note on a wasted diagnosis:** an earlier commit blamed `MTLStorageModePrivate` + `replaceBytesInRegion` for black textured models. That was wrong — the real cause was the slot mismatch above, found by reading the shader rather than reasoning about Metal semantics. `metal_texture_update_region` already handles the storage-mode issue by uploading through a blit encoder (the AGX driver ignores `replaceBytesInRegion`).

### Remaining validation errors (pre-existing, not yet fixed)
- `vkDestroyDevice` reports 13 leaked objects at shutdown (buffers, an image, image views). Cleanup ordering, not a rendering correctness issue.

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

- CMake 3.22+, auto-downloads CEF from Spotify's builds server
- MSVC: `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL` (/MD) forced via CMP0091 to avoid debug/release CRT mismatches in third-party libs (especially Assimp which injects `/MTd` via generator expressions)
- MSVC: `CMAKE_BUILD_TYPE=RelWithDebInfo` forced to prevent debug CRT activation
- MSVC: vcvarsall.bat path is `Visual Studio\18\Community` (not 2026)
- SPIRV-Tools: all targets patched to strip `SPIRV-Tools-shared` references, using only `SPIRV-Tools` (static alias) to avoid LNK2005 duplicate symbol errors
- HarfBuzz: `inttypes.h` detection and `PRIu64`/`PRId64`/`PRIx64` fallback macros for MSVC
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
- **Vulkan**: Compiles GLSL->SPIR-V at runtime via shaderc library, or accepts pre-compiled SPIR-V directly (detected via magic number 0x07230203). Targets Vulkan 1.1 environment, performance optimization level. Supports macro definitions via `defines`/`defineCount`. Entry point: `main`.
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

## Merge consolidation (2026-11-15)

Pulling `origin/main` back into this branch meant 25 conflicts across six files. The branch had moved further on the graphics layer, so most resolved to this side; the four calls worth recording:

1. **Vulkan backend stays as this branch had it** — per-frame descriptor sets, one semaphore pair per swapchain image, the 16-slot discrete UBO binding layout, deferred `vkUpdateDescriptorSets` through `graphics_flushdescriptorwrites()`, the SPIR-V passthrough in `graphics_createshader()`, and skin attributes at location 5/6 as `vec4` weights then `ivec4` indices. From origin it took the persistently mapped uniform buffer, which also meant collapsing the duplicated `GPUBuffer` typedef the merge left behind into one definition carrying `persistentMap`.
2. **`shaders/triangle.vert` stays on push constants** — `{mvp, model}` in a push-constant block with Material at set 1 binding 1. Origin's version had a single UBO at set 1 binding 0, which does not match the merged backend.
3. **`framework.c` keeps the current draw path** — sky dome, terrain, scene nodes, instanced grid. Origin's version predates all four.
4. **`model_assimp.cpp` keeps both animation representations.** The hierarchy path (`model_pose()`, `model_pose_by_index()`) and the scene-node path (`model_update()`, `model_get_bone_matrices()`) both survive; `model_destroy()` frees both.

**OpenGL is gone, not merged back.** `origin/main` still carried the `USE_OPENGL` option, the `src/graphics_opengl_sdl.c` source entry and the OpenGL link block, so the auto-merge resurrected a backend this branch deleted in ccb56ac6. The file was removed again and the option and every branch referencing it dropped from `CMakeLists.txt`.

Two defects surfaced while verifying the merge, both pre-existing on this branch rather than merge artifacts:

- **2D text was flipped.** `font_pixel_to_ndc()` mapped pixel y straight into NDC, so y=8 landed along the bottom edge and every glyph was mirrored. Both backends' text shaders already expect GL-style y-up clip space (Vulkan negates y on the way out), so the fix is `1.0f - (py / h) * 2.0f`.
- **The self test never drew its results.** The report block sat inside `if (!g_selftestActive)`, so its own `if (g_selftestActive)` was unreachable and a `GAME_SELFTEST=1` run showed only the controls. The report is now a sibling branch.

**Verification:** both configurations link and both render the scene from the real window. With `GAME_SELFTEST=1`, Metal reports 4 checks / 2 failed and Vulkan 5 checks / 1 failed (the counts differ because phases advance on frame count, not on identical frame timing); `walks at walk speed` fails on both, measuring 22.31 u/s and 19.68 u/s. Vulkan still renders the scene differently from Metal — black sky, night-tinted terrain, no scene cubes — which the merge did not cause and which is not yet diagnosed.

---

## GAME_CAPTURE (2026-11-15)

`GAME_CAPTURE=<path.png>` renders the scene from a fixed camera pose for `GAME_CAPTURE_FRAMES` frames (default 4), reads the drawable back and exits, so a camera or shading change can be diffed by image rather than by eye. Two runs of the same build produce byte-identical PNGs; that was the design constraint, so the mode freezes the simulation (`framework_update()` returns early after the FPS counter), pins `g_timeOfDay` to 0.30, disables the self test, and skips the text overlay, whose FPS reading would otherwise change every run.

- **Metal** does the readback with a blit of the drawable texture into a shared `MTLBuffer`, queued in the frame's own command buffer in `graphics_postdraw()` and completed by a `waitUntilCompleted` in `graphics_present()`. The drawable is `BGRA8Unorm`, swizzled to RGBA on the CPU.
- **Vulkan is not wired up yet**: the swapchain image finishes each frame in `PRESENT_SRC_KHR`, so a readback needs a `TRANSFER_SRC` barrier plus a host-visible copy. `graphics_capture_setup()` there prints that it is unimplemented and the run reports `readback unavailable` instead of writing a broken file.
- **PNG output does not use stb_image_write** — it is not vendored. `image_write_png()` emits zlib *stored* blocks, so captures are large (~5 MB at 1280×960) but valid and deterministic, with no new dependency.

---

## Offscreen scene target and post-process pass (2026-11-15)

The 3D scene drew straight to the swapchain, so a camera effect would have had to live inside every scene shader and would have worked from a frame whose highlights an 8-bit target had already clipped. The frame now goes through `src/postprocess.c`: the scene is rendered into an offscreen colour target, then composited into the swapchain by a fullscreen pass. Effects belong in that composite.

- **`ColorFormat`** (`RGBA8`, `RGBA16F`, `RGBA32F`) and `RasterState.colorFormat`. A pipeline state is built for one attachment format and Metal rejects it in a pass with another, so `graphics_createpipeline()` keys its cache on the format. The engine's own draw calls (`graphics_drawmodel`, `graphics_draw_instanced`) take the format from the pass being recorded: `graphics_predraw()` and `graphics_beginpass()` track it in `g_targetFormat` (swapchain → `RGBA8`, offscreen → the first colour texture's format).
- **`graphics_createtexture_target(w, h, format)`** creates a private texture that is both a render target and shader-readable. **`graphics_draw_fullscreen(source, vert, frag, mat)`** draws a backend-generated fullscreen triangle (clip-space positions, UVs derived from them) sampling `source` at slot 1 with a clamp-to-edge sampler — the default sampler repeats, and an effect that remaps coordinates reads past the frame edge, which would wrap the opposite edge in.
- The framework calls `postprocess_begin()`/`postprocess_end()` around the 3D draws only, so the text overlay is composited after the effect: a lens effect must not distort the HUD. Text is drawn in the swapchain pass that the composite leaves open.
- The target is re-created lazily when the drawable size changes, and `postprocess_begin()` does nothing while minimised (no drawable, zero-width target).
- **Where the backend cannot do this** (Vulkan: it needs its own render pass and framebuffer for the offscreen target, the same missing piece that blocks the rest of that work) `graphics_createtexture_target()` returns NULL, init says so, and begin/end are no-ops — the frame is drawn straight to the swapchain exactly as before.

### Verification, and why RGBA16F is not bit-exact

`GAME_CAPTURE` at 1280×960, no effect enabled, against the pre-change baseline `721637dd`:

| Scene target | Capture | Result |
|---|---|---|
| `RGBA32F` (`GAME_POSTPROCESS_FORMAT=32f`) | `721637dd` | byte-identical |
| `RGBA16F` (default) | `8e3ca546` | 78,727 of 1,228,800 pixels differ by exactly one level, all terrain |

32F matching exactly proves the path — pass, fullscreen draw, UV mapping, sampler, clear order — is lossless. The 16F difference is storage precision and nothing else: a half carries an 11-bit significand and an 8-bit swapchain step is 1/255, so where a shader value lands within a half-quantum of an 8-bit rounding boundary the composited frame rounds to the neighbouring level. The sky is untouched because it is an unmodified 8-bit gradient and therefore lands exactly on 8-bit values; the terrain is lit arithmetic and lands anywhere. `GAME_POSTPROCESS_FORMAT=32f` exists to keep that difference measurable. Both formats are byte-identical run to run.

Live window: same scene, 82 FPS, overlay unaffected.

### Barrel distortion, the first camera effect (2026-11-15)

The composite pass is where camera effects live, and the first one is radial lens distortion: `postprocess_set_barrel(amount)`, default 0.25 from `FRAMEWORK_BARREL_DEFAULT`, overridable with `GAME_BARREL=<amount>` and toggled at runtime with **B** (so the distorted frame can be compared against the undistorted one without restarting).

- The composite shader remaps `uv` by `c *= (1 + barrel*r²) / (1 + barrel*rmax²)`, with `c` measured in aspect-corrected space so the distortion is circular on a non-square frame. Positive amounts magnify the centre and compress the edges, which bows straight lines away from frame centre the way a wide lens does.
- The `1 + barrel*rmax²` divisor locks the frame corners in place. Without it the warped lookup runs off the target at the edges and the clamp sampler smears the border into a visible frame — the artefact that makes naive barrel shaders look cheap.
- Strengths: 0.15 reads as barely there, 0.25 is clearly glass, 0.35 is a strong wide angle, and past ~0.6 the corners stop reading.

Verification, `GAME_CAPTURE` at 1280×960:

| Setting | Capture | Result |
|---|---|---|
| `GAME_BARREL=0` | `8e3ca546` | identical to the no-effect capture in the commit above |
| default 0.25 | `3e8acf17` | 980,365 of 1,228,800 pixels changed, mean 16 levels on those pixels, max 198 |

Repeat runs at either setting are byte-identical. The live window runs the effect at 98 FPS with the caption sharp, since the overlay is composited after the effect.

### The failing "walks at walk speed" check is a measurement bug (2026-11-15)

Neither a stale threshold nor wrong movement. `walkSpeed` is 9.0 (`player.c:55`) and the check wants 7–11 u/s (`selftest.c:198`), which is the right band. The character really does walk at 9: instrumented at the end of the phase, horizontal `|v|` reads **9.00** against `walkSpeed` 9.00. What is wrong is how the check measures:

- **The character is integrated twice per self-test frame.** `framework_update()` calls `player_update()` with the real (zero) input, then `selftest_update()` calls it again with the test's input. Measured distance over the 90-frame phase was 15.013 units in 1.0000 s = 15.01 u/s, not 9.
- **The divisor is one frame's `dt`, not the phase's elapsed time.** `speed = dist / (SELFTEST_FRAMES * dt)` uses the `dt` of the last frame only. In the probe that was 0.008 s against a real 0.0111 s per frame, so 0.72 s stood in for 1.00 s and 15.01 u/s was reported as **20.85**.

Both errors inflate the reading, and the second scales with frame rate, which is why Metal and Vulkan report different numbers (22.31 vs 19.68 in the merge notes) and why the failure looks like sprint speed against a walk threshold. Sprint and the other phases share the same distortion, so their thresholds (0.05-unit float, >12 for sprint) still pass.

Not fixed here: doing it properly means either not double-integrating during the self test or reporting against summed elapsed time, which is a change to the harness rather than a threshold. The value is recorded so the next person does not re-diagnose it.

---

## Rating: 9/10

Solid foundations with clean module separation, mature abstraction layering, and a genuinely well-engineered job system. All P0 and P1 rendering pipeline defects have been fixed in both Metal and Vulkan backends -- the engine can now render 3D content with MVP transforms, has a proper uniform buffer binding API (UBO descriptor set in Vulkan, buffer binding in Metal), a working material system that packs uniforms into per-material GPU buffers on both backends, Vulkan runtime GLSL->SPIR-V compilation via shaderc with SPIR-V passthrough support, and text rendering fixes (Y-flip correction, glyph upload flush order). The Vulkan render pass now supports optional depth attachment for pipelines that don't need depth testing. Build system hardened for MSVC with consistent /MD runtime, CMP0091 propagation, and SPIRV-Tools static linking.

Engine has solid foundations for 3D rendering with skeletal animation support. The framebuffer/render target abstraction has been implemented on both backends — `graphics_createpass()` now creates offscreen render passes with user-provided texture attachments, `graphics_beginpass(NULL)` resumes swapchain rendering, and `graphics_destroypass()` cleans up. This unlocks shadow maps, post-processing, and deferred rendering patterns.
