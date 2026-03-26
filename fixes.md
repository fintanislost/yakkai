# Fixes

This document tracks the practical fixes and debugging outcomes that got `Paper Gradient` to a working state for local video, Wallpaper Engine video, and first-pass Wallpaper Engine web testing.

## Wallpaper Engine Scene backend limit

Problem:
- The guarded experimental `Wallpaper Engine Scene Native` path now launches cleanly, but scene results still vary by content type.
- Some scenes render with visible presentation issues on the host.
- Some scenes, including the default Wallpaper Engine `arsenal` project, originally reached first frame while still showing effectively blank output.

Evidence:
- QML-side scene geometry matches the native backend swapchain size:
  - `scene runtime geometry ... item=2560x1440`
  - native backend: `set swapchain image size: 2560x1440`
- The experimental guard now restarts the native scene runtime on scene-source, assets-path, fill-mode, and mouse-input changes, so scene swaps and setting changes get a clean launch instead of mutating a live renderer instance.
- The runtime consistently reaches `firstFrame`, which means the scene backend is not simply failing to start.
- Arsenal-specific diagnostics now show sane camera/fill sizing:
  - `render fill: scene=arsenal ... ortho=1920x1080 output=2560x1440 ...`
- Arsenal initially compiled as:
  - `compileRenderGraph: begin nodes=0`
- The Arsenal scene source is model-based. Its `scene.json` contains a `model` object for `models/pistols/pistols.mdl`, while the repo-owned parser originally only dispatched `image`, `particle`, `sound`, and `light` objects.

Diagnosis:
- The previous hot-swap lifecycle bug in the QML wrapper was real and has been fixed, but it is no longer the main blocker for Arsenal.
- Arsenal’s visible content is model-based, so a parser path for Wallpaper Engine `model` scene objects was required before the scene could do anything useful.
- The repo-owned backend now has an additive experimental model fallback:
  - parses `model` objects into a dedicated scene-object type
  - uses the authored Wallpaper Engine scene camera for the perspective camera node
  - parses the `.mdl` mesh and reuses the existing mesh path
  - applies a diffuse-only `genericimage2` fallback material so current image/particle functionality stays untouched
- Arsenal then exposed a narrower parser gap inside the fallback:
  - `pistols.mdl` uses a 56-byte static vertex layout instead of the older skinned MDL layouts
  - that layout decodes as position, normal, tangent, uv0, uv1
  - the parser now accepts it and maps `position + uv0` into the existing diffuse-only mesh path while synthesizing neutral skinning data so older paths remain unchanged
  - static-layout models now return after index decode instead of falling into the legacy puppet/bone section parser that belongs to the older MDL variants
- Arsenal also exposed a camera-framing gap:
  - model scenes can declare `camera.paths` scripts, and Arsenal's `scene.json` points to `scripts/camera_00.json`
  - the repo-owned fallback was originally using only the static `scene.camera` block, which can frame the model far off target
  - model scenes now prefer the first keyframe from the first declared camera-path script when building the experimental perspective camera
  - that same camera-path data is now also carried into the live runtime, where the experimental perspective camera interpolates between authored path keyframes and advances across authored path segments instead of staying frozen at `transforms[0]`
  - `SceneNode` transform setters now mark nodes dirty when those live camera-path updates change translate/rotation, so the cached scene-graph transforms and view matrices actually follow the animated camera
  - that camera path also carries an authored `up` vector, and the fallback now uses it to preserve camera roll instead of collapsing the perspective camera to yaw/pitch only
  - the renderer's actual perspective view-matrix path was still rebuilding `LookAt(...)` with a hardcoded world-up vector, so camera roll and the fallback's camera-plane reframe were being scored against one basis but rendered with another; the view-matrix build now uses the camera node's rotated `up` axis too
  - the same `LookAt(...)` helper was also assembling a forward camera transform instead of the inverse camera view transform, which pushed rotated perspective model scenes behind the clip plane; it now writes the camera basis vectors into the matrix rows with the matching `-dot(axis, eye)` translation terms
  - the renderer was also overwriting the perspective camera FOV during fill-mode updates with a value derived from the orthographic camera height (`56.7381` for Arsenal) instead of keeping the scene/default perspective FOV (`50`)
  - fill-mode updates now preserve the existing perspective FOV and only update the output aspect ratio
- Arsenal's static MDL also turned out to be multi-section:
  - after the first decoded mesh block, `pistols.mdl` continues with additional material-backed static mesh sections such as `planks.json`, `Pistol0101_D.json`, and `goldmetal.json`
  - the fallback now parses all of those static submesh sections instead of stopping after the first one
  - each parsed static submesh now gets its own diffuse-only fallback material and child mesh node under the shared model transform node
  - those child submesh passes now preserve the previously rendered color target contents instead of letting later opaque passes discard earlier submesh output
  - the fallback no longer relies on only the raw static-MDL basis vs a guarded Y-axis flip; it now scores all proper axis-basis remaps against the active perspective camera and picks the best projected fit
  - that basis chooser is now also submesh-aware enough to penalize candidates that drag a huge thin low-vertex backdrop quad in front of the denser model body, which matters for scenes like Arsenal where the wood-planks section can otherwise dominate the frame
  - the experimental static fallback now excludes those same large thin low-vertex backdrop planes from basis/framing heuristics when denser body submeshes are present, but still attaches them for the final render so Arsenal's `planks.json` background returns without dominating basis selection
  - those backdrop-classified submeshes now also render before the denser weapon submeshes in the diffuse-only fallback, so the restored wood planks stay behind the guns and knife instead of painting over them
  - that chosen basis is now applied directly to static vertex positions, normals, and tangents before the authored model transform, so static fallback framing is no longer limited by the scene graph's single Euler rotation node
  - after that basis choice, the fallback now measures the actual projected model width and height under the authored perspective camera and only scales when those projected bounds still overflow the target frame, instead of shrinking from a coarse bounding-radius heuristic
  - when the selected static fallback model still lands visibly off-center after basis selection, the fallback can now apply a camera-plane reframe to the authored perspective camera instead of dragging the model relative to the authored scene lights
  - that projected-bounds auto-fit now runs against the post-reframe authored camera as well, which keeps Arsenal visible without leaving the static fallback oversized after the old radius-based shrink pass was removed
  - the static model path had also never seeded `g_Brightness`, `g_UserAlpha`, or `g_Color4`, which meant both `genericimage2` and the simpler `generic` path could start from black output even when geometry and textures were otherwise correct; the model fallback now sets those defaults explicitly
  - after the view-matrix, render-target, and uniform-default fixes landed, Arsenal's authored `generic` materials proved usable again even with `lightmap` and `normalmap` combos, so the static fallback now keeps those materials on the authored `generic` path instead of flattening them back to diffuse-only
  - the static fallback still strips `reflection` from those authored `generic` materials because it does not build the reflection render targets they expect
  - renderer-side diagnostics then showed the remaining black-screen case was not texture upload, UVs, or draw submission: the Arsenal fallback was uploading the correct diffuse textures, binding `g_Texture0`, and issuing nonzero indexed draws, but each pass only had a single 96-byte uniform buffer bound
  - that 96-byte buffer is exactly the vertex-side `genericimage2` block (`g_ModelViewProjectionMatrix`, `g_Texture0Rotation`, `g_Texture0Translation`), which exposed a renderer bug rather than a parser/material one: `CustomShaderPass` only allocated, populated, and pushed `ref.blocks.front()` instead of every reflected uniform block for the pass
  - the Vulkan reflection/render path now tracks per-block bindings and allocates, fills, and binds every reflected uniform buffer block, so fragment-side uniforms like `g_Brightness` and `g_UserAlpha` are no longer dropped when a shader splits vertex and fragment uniforms into separate reflected UBOs
  - the remaining Arsenal black-screen case still survived that UBO fix, while the source diffuse sidecars (`knife_df.tga`, `Pistol0101_D.tga`, `goldmetal.png`) proved to be non-black, so the static diffuse-only fallback now prefers explicit raster sidecars like `.tga` and `.png` when they exist instead of forcing every model material through the `.tex` decode path
  - Arsenal's material JSONs also exposed that `WPMaterial` defaults missing `blending` to `translucent`, while these diffuse alpha channels often carry detail or mask data instead of real opacity; the static diffuse-only fallback now forces opaque blending so those channels no longer fade the model toward black
  - the renderer graph was also still forcing `_rt_default` into preserve/load mode from the very first static submesh pass, which meant model-only scenes could start by loading undefined render-target contents instead of clearing to a known base before later submeshes accumulated
  - `_rt_default` now only preserves output after the first write version exists, and first writes to temp/spec render targets now start from `VK_ATTACHMENT_LOAD_OP_CLEAR` instead of an undefined load path
  - those first opaque model passes also keep the cleared alpha channel instead of writing model diffuse alpha into the target, because the diffuse sidecars often store non-opacity data in alpha and that could still collapse the final wallpaper output to black/transparent
  - the shader-value updater still populates the model/light uniforms that the authored `generic` shader expects, including eye position, normal matrix, ambient/skylight colors, and `g_LightsColorRadius`, for the simpler static materials that remain on that path
- Separate host-side presentation issues may still exist for supported 2D scenes, and model scenes still do not have full material or lighting fidelity.
- Some Blue Archive scenes also embed Wallpaper Engine media-integration UI layers whose `scale`, `visible`, `alpha`, or `color` depend on authored property scripts. The backend still does not execute those scripts, so script-heavy utility layers such as `solidlayer`, `projectlayer`, `fullscreenlayer`, and workshop UI cards that explicitly reference media-integration state are now suppressed by default instead of rendering their raw fallback card geometry on top of the scene.
- The follow-up hierarchy fix there was equally important: child image/model/light/particle nodes whose authored parent never materializes are now kept deferred instead of being attached to the root scene graph as a temporary fallback, which had let suppressed media-info sublayers keep rendering as orphaned quads.
- Blue Archive Yuuka then exposed a separate image-effect compositing bug: first writes into intermediate `_rt_*` effect buffers were still clearing to the scene background color with alpha `1.0`, so sparse alpha layers using `waterwaves` or `foliagesway` could turn into pale rectangular quads before the effect shader sampled them. Temp/spec effect targets now clear to transparent instead, while the main scene color targets still clear to the authored scene background.
- Blue Archive placement then exposed a scene-parallax math bug: the camera-parallax updater was adding a permanent `(nodePos - cameraPos)` term before applying `parallaxDepth`, so centered scenes could still drift off their authored placement even when the mouse was centered. The updater now treats scene parallax as mouse-relative only, which keeps Yuuka and similar 2D scenes anchored near their authored center while still responding to cursor motion.
- Sleeping Arona exposed a separate parser gap in scene hierarchy construction: its `SKY LAYER==TRANSFORM`, `WALL==TRANSFORM`, `CHAIRS==TRANSFORM`, `DESK==TRANSFORM`, and `ARONA==TRANSFORM` objects are pure transform/group anchors with no drawable payload and no explicit `solid: true`. The parser now materializes those bare transform anchors too, so their children are no longer dropped for missing parent IDs during scene-graph assembly.
- The scene scanner had already been collecting default Wallpaper Engine scene user properties from each `project.json`, but the native scene runtime never received them. `WEScenePropertiesJson` now follows the same config/runtime path as the web-property bridge, reaches `SceneWallpaper`, and is active during scene parsing.
- The wallpaper config schema now persists `WEScenePropertiesJson`, and the native scene loader also falls back to the selected scene's `project.json` when that config value is still empty. That keeps Blue Archive defaults active even across stale saved selections.
- With those defaults available, the parser now resolves a small but important subset of structured Wallpaper Engine property wrappers at parse time instead of always falling back to their baked `"value"` field: direct `user` overrides, `user.condition` visibility checks, `engine.userProperties.loadingintro`, and the repeated Blue Archive `timeofday` / `shared.sunset` / `shared.night` script patterns. That restores the authored default state for Sleeping Arona-style scenes more accurately without attempting a full JS runtime.
- Sleeping Arona then exposed a second wrapper-parsing gap: many `origin`, `scale`, and `visible` fields arrive as animation/script objects whose actual authored default lives in their `.value` payload, often as a vector string or numeric `0`/`1`. The generic JSON reader now unwraps those `.value` payloads for transform arrays and coerces numeric/string booleans, so those layers stop falling back to zero/default placement.
- Sleeping Arona then exposed a native-runtime limitation rather than a raw parse bug: its `loadingintro` user property defaults to `true`, but the intro only dismisses correctly in Wallpaper Engine because later runtime script behavior takes over. The native scene fallback now forces `loadingintro=false` while activating default scene properties so those one-shot overlays do not remain permanently visible.
- Sleeping Arona also relies on a helper script to pause its `Arona Drool` puppet animation layer on startup. Because the native backend does not execute that script, the layer could free-run and distort the main character pose. The parser now scans startup scripts for `getAnimationLayer(...).pause()` patterns and applies those pause defaults directly to matching puppet layers in native fallback.
- Sleeping Arona then exposed a separate puppet parser compatibility gap: its `ARONA_CROP_SHEET_puppet.mdl` uses `MDLA0005` with large per-animation trailer blobs between `Arona Drool` and `Idle`, so the old parser walked into that trailer and decoded garbage animation headers like `fps=0`. The MDLA reader now rescans forward for the next structurally valid animation header instead of assuming animations are contiguous or only separated by the older tiny trailer blocks.
- Sleeping Arona then exposed a crop-sheet puppet prepass gap: `ARONA_CROP_SHEET` ships a companion `materials/ARONA_CROP_SHEET_channelmap.json` shader that expects the native backend to feed `puppettexturechannels` with the authored packed crop layout. The parser now detects those `*_channelmap.json` sidecars, builds a dedicated packed-UV mesh for the prepass, and promotes that first stage to the authored `puppettexturechannels` shader instead of rendering an unskinned fallback card into the effect chain.
- The remaining distortion there turned out to be in the packed UV decode rather than the vertex positions. `ARONA_CROP_SHEET_puppet.mdl` already stores positions in centered image space, roughly matching the authored `4160 x 2923` crop-sheet size, but the extra packed channel-map field is unsigned normalized image UV data, not signed normalized data. The parser now decodes that field as `UNORM16x2`, which stops `puppettexturechannels` from sampling the wrong parts of the channel map and turning the Sleeping Arona crop-sheet into giant geometric shards.
- The same Sleeping Arona prepass also showed that a global `g_BlendMap=0.002` fallback is too blunt: it makes `puppettexturechannels` keep almost the whole base crop-sheet visible, which still looks like giant translucent shards even after the UV decode is fixed. The native fallback now seeds `g_BlendMap` from the blend slots the puppet MDL actually uses, activating those rows and leaving the others at zero instead of filling the entire map with a single epsilon.
- The follow-up there was that the active-row weight is not just a discard threshold in the authored `puppettexturechannels` double-buffered path. The shader also uses that same value as the actual `mix(base, albedo, v_TexCoordBlend.z)` factor, so seeding active rows with a tiny epsilon keeps almost the entire base crop sheet visible and reproduces the giant translucent Sleeping Arona cards. The native fallback now drives used blend slots at `1.0` and leaves inactive slots at `0`, which matches the authored shader contract much more closely.
- The next refinement there was that “all blend slots used anywhere in the mesh” is still too broad for Sleeping Arona. `puppettexturechannels` indexes `g_BlendMap` through the vertex bone slot, so the native fallback now seeds only the bones that actually change in the currently visible puppet animation layers and leaves the untouched slots at zero. The original genericimage4 puppet now stays visible as a separate base display layer, so the channel-map path only needs to carry the animated overlay regions instead of the whole crop sheet.
- The next issue there was that `puppettexturechannels.vert` has no authored skinning path of its own. The native loader now has a defensive `g_Bones` / `a_BlendWeights` injection available when a `puppettexturechannels` material actually resolves `SKINNING`, and that helper is keyed off resolved combo state rather than only the material JSON. It also matches the shader source structurally instead of depending on one exact source line layout, so the injected declarations survive indentation or formatting changes.
- Sleeping Arona itself turned out not to want that forced skinning path on the channel-map prepass. `ARONA_CROP_SHEET_channelmap` now stays on the authored unskinned `puppettexturechannels` shader and uses the authored packed prepass mesh directly instead of CPU-posing or shader-skinning that stage. That keeps the shader contract intact and avoids double-posing the crop-sheet before the final puppet render.
- The next Sleeping Arona distortion then became a composition-order problem. The packaged scene structure is closer to `channel-map prepass -> card effects -> final genericimage4 puppet stage` than to publishing the last effect card directly, because the authored `pulse`/`waterwaves`/`shake` passes are plain fullscreen-style image filters while `ARONA_CROP_SHEET` itself is still a puppet material. The native pipeline now routes the unskinned channel-map prepass through the authored image-effect ping-pong chain and then finishes on a final `genericimage4` puppet stage again.
- That still left one structural mismatch in the native effect resolver: when the last effect stage itself was the synthetic skinned `genericimage4` puppet pass, the resolver would publish that same node back into the scene a second time. Sleeping Arona now keeps the effect chain offscreen and uses a separate final `genericimage4` puppet display node that samples the last authored effect render target once in the main scene, which better matches `prepass -> offscreen image effects -> final displayed puppet`.
- The plain non-channelmap puppet route turned out to be structurally wrong too: it was rendering the full puppet into the offscreen effect chain, but then either publishing the last authored flat-card effect stage directly or remapping that flattened result back through puppet UVs. The native fallback now still renders the full puppet into the offscreen source pass and keeps the authored image effects on flat cards, but it samples the last authored effect render target from a standalone flat final display card with the original crop alpha instead of showing the raw last effect card or reprojecting it through the puppet mesh.
- Sleeping Arona also has a separate visible `Adjustable Composition Layer` utility image that uses `models/util/composelayer.json` as a broad `_rt_FullFrameBuffer` screen-copy strip over the background. In native fallback that layer was visually overwhelming the scene, so the parser now skips that one compose-layer object whenever the scene contains `ARONA_CROP_SHEET`.
- That final synthetic puppet stage then exposed a shader-preprocess ordering bug: effect-stage materials were merging their authored combos into `WPShaderInfo` only after shader preprocessing had already happened, so `genericimage4` puppet passes inside image-effect chains could silently compile without `SKINNING/BONECOUNT` even while attached to the puppet mesh. Those combos are now merged before preprocessing, which lets the final Sleeping Arona puppet stage take the correct skinned path instead of behaving like an unskinned giant-card pass.
- The next confirmed Sleeping Arona blocker was no longer shader compilation or missing `g_Bones`; the final `genericimage4` puppet stage was compiling as skinned, receiving the puppet layer, and uploading bone matrices, but it was still sampling the effected crop-sheet texture with the original source-atlas UVs. The final crop-sheet puppet mesh now derives its `a_TexCoord` from the centered image-space vertex positions instead, so the last skinned stage samples the image-effect ping-pong result in crop-sheet space rather than atlas space.
- The same standalone final Sleeping Arona puppet display now uses that image-space UV mesh path consistently when it samples the last offscreen effect render target. That keeps the final `genericimage4` pass in the same crop-sheet/image coordinate space that the `puppettexturechannels` prepass and flat-card image effects already use, instead of remapping the effect result through the original source-atlas UV layout.
- The latest Sleeping Arona route replaces that image-space remap with a cleaner UV-space contract. `puppettexturechannels` now renders on a flat prepass mesh whose positions come from the original crop-sheet UVs themselves, so the offscreen ping-pong result stays in the same UV space as `ARONA_CROP_SHEET`. The final standalone `genericimage4` overlay can therefore sample the last effect render target through the authored puppet UV mesh again instead of projecting it through a second image-space UV approximation.
- Sleeping Arona then exposed a separate image-effect transform gap: the final effected card was only copying the source layer's local translate/scale/rotation, not its authored parent hierarchy, so parented crop-sheet layers could come back from the effect chain on the wrong world-space quad. The effect resolver now clears any temporary parent on intermediate effect passes but reapplies the original image node's parent chain to the last effected output before it returns to the main scene.
- Sleeping Arona also exposed a sequencing bug in the native image-effect resolver: every authored effect object in a layer was being redirected to the main scene as if it were a final display pass. Multi-effect stacks now keep their intermediate outputs inside the ping-pong chain and only publish the very last authored effect stage back to the scene, which is much closer to Wallpaper Engine's expected composition model.
- The current Sleeping Arona blocker is no longer obviously in geometry, missing uniforms, or dropped effect stages. The native texture loader now logs parsed format, size, and simple per-channel coverage stats for `ARONA_CROP_SHEET` and `ARONA_CROP_SHEET_channelmap` so the runtime can distinguish a bad prepass/effect contract from a bad source texture decode using `loggs.txt` alone.
- The latest Sleeping Arona refinement keeps the final standalone `genericimage4` overlay but no longer trusts the offscreen effect RT alpha on its own. That overlay now samples both `ARONA_CROP_SHEET_channelmap` and the original `ARONA_CROP_SHEET` alpha alongside the last effect render target, multiplying the final color/alpha by both authored masks so the broad flat-card effect coverage can no longer spill across the whole crop-sheet card when only a sparse animated overlay region should remain visible.
- The next Sleeping Arona refinement then narrowed that final overlay geometry again: the standalone `genericimage4` overlay now renders only the puppet triangles where the active `g_BlendMap` slot set meaningfully owns most of the corners, instead of treating every triangle with the same primary slot or only a tiny mixed active weight as overlay-owned. That leaves the untouched body on the separate base display node and limits the offscreen channel-map/effect result to the genuinely animated overlay region it is supposed to affect.
- The Sleeping Arona channel-map seeding now keeps startup-paused puppet layers eligible for the frame-0 overlay pose as long as they are still visible. That matches Wallpaper Engine's `pause()` behavior more closely: the layer should stop advancing, not disappear from the rendered channel-map state.
- The matching Sleeping Arona base display originally tried to use the complement of that same weighted fully-active triangle set, but in practice that produced large coarse complement polygons across the body. The base display now stays on the full authored puppet mesh again and suppresses the overlay-owned region with an inverse channel-map alpha mask in the fragment shader, while only the channel-map/effect result remains restricted to the filtered overlay region.
- The latest Sleeping Arona overlay refinement now keeps that weighted active-region subset on an image-space mesh instead of sending it back through the full authored puppet UV mesh. That keeps the final overlay in the same flat crop-sheet coordinate space as the offscreen effect chain while the untouched body remains on the separate base puppet display.
- That filtered image-space overlay path also had a plain remap-table bug: it initialized remapped vertex indices with a `uint32_t` sentinel but one branch still checked against `uint16_t::max()`, so the filtered builder always treated remapped vertices as invalid and silently fell back to the full authored puppet UV mesh. The sentinel check now uses the correct `uint32_t` value, so the weighted overlay subset can actually build.
- The standalone Sleeping Arona base/final display nodes were also copying the full image-layer transform even though they were parented under the already-transformed source `spImgNode`. Those child display nodes now stay local-identity, which stops the crop-sheet transform and alignment from being applied twice.
- That standalone Sleeping Arona split also has to survive the offscreen-effect setup resetting `spImgNode` to identity before the child display nodes are appended. The parser now snapshots the pre-effect image transform and reapplies it explicitly to the standalone base/final display nodes, so they keep the authored world placement even though their parent becomes the offscreen source node.
- Sleeping Arona still has a zero-slot fallback for scenes that truly have no visible channel-map-driven puppet layers, but a startup-paused visible layer like `Arona Drool` no longer triggers that branch by itself. That keeps the authored channel-map/effect route alive for the paused frame-0 pose instead of collapsing immediately to the plain puppet base.
- The latest Sleeping Arona correction narrows that companion `*_channelmap.json` path back to an explicit-material check. The native backend no longer promotes a sibling sidecar just because a matching filename exists next to a `genericimage4` puppet asset; the special `puppettexturechannels` route is now reserved for source materials that explicitly request it.
- The ordinary Sleeping Arona `genericimage4 + effects` path is now pinned back to the upstream renderer model instead of the custom standalone-display experiment. The source card stays flat, a synthetic puppet `genericimage4` node is appended as the last authored effect stage, and the effect resolver publishes that last node back with the puppet mesh just like upstream `wallpaper-engine-kde-plugin`.
- That upstream-compatible baseline still leaves Sleeping Arona visibly broken in native Plasma runs, so the current practical fallback now disables the local `ARONA_CROP_SHEET` card-effect chain entirely and renders that layer as a plain puppet image instead of continuing to route it through the authored flat-card effect publish path. This is intentionally scene-specific and prioritized as a display-quality fallback rather than a renderer-accuracy claim.
- To stop guessing at that path from screenshots alone, the native renderer can now dump three targeted `ARONA_CROP_SHEET` render targets into `/tmp/papercompany-debug/`: the copied source puppet RT before effects, the last effect ping-pong RT, and the final `_rt_default` scene RT after the frame completes.
- That new prepass also exposed a renderer safety bug: uniform uploads were writing the full authored payload even when the reflected block member was smaller, which could overrun UBO storage for authored array values like `g_BlendMap`. The Vulkan custom-shader pass now clamps those uploads to the reflected member size and logs the mismatch instead of blindly overwriting adjacent uniform data.
- The same crop-sheet work also exposed a shader-compiler stage bug in the vendored Vulkan backend: GLSL fragment sources were still being registered as vertex-stage inputs during `glslang` parse, so authored fragment-only operations like `discard` or `gl_FragColor` could fail in otherwise valid scene materials. The compiler now forwards the real unit stage into `setEnvInput(...)`, which lets `puppettexturechannels` and similar fragment shaders compile under their correct stage.

Result:
- `Wallpaper Engine Scene Native` should still be treated as experimental and backend-limited.
- The repo-owned backend no longer drops model-based scenes outright. It now attempts a guarded static-model fallback instead.
- Model scenes are still partial:
  - authored camera is used
  - diffuse texture fallback is used
  - full PBR material fidelity, multi-material mapping, and richer model-scene features are not implemented yet
- Arsenal is no longer categorized as a crash case or a pure parser miss; it is now the primary validation case for the experimental model fallback.

## Repo-owned native scene backend status

Problem:
- The repo-owned `io.papercompany.scene` backend now builds and loads in the standalone harness, but it does not yet reach the same external-texture import phase as the installed system backend.

What was fixed:
- Vendored the missing native scene backend sources under [native/scene_backend/vendor/upstream_debug](/home/peter/repos/papercompany/native/scene_backend/vendor/upstream_debug).
- Replaced the original `PaperSceneViewer` stub with a repo-owned wrapper around the vendored `SceneObject`.
- Added the missing local `glslang` translation units needed to eliminate unresolved symbols from [libpapercompany_scene_backend.so](/tmp/papercompany-cmake-check/native/scene_backend/libpapercompany_scene_backend.so).
- Replaced the mixed vendored-source/system-library `glslang` path with a fully vendored `glslang` subproject build in [native/scene_backend/CMakeLists.txt](/home/peter/repos/papercompany/native/scene_backend/CMakeLists.txt).
- Added standalone harness diagnostics in [main.cpp](/home/peter/repos/papercompany/native/scene_harness/src/main.cpp) and [Main.qml](/home/peter/repos/papercompany/native/scene_harness/qml/Main.qml).
- Added native renderer logging through:
  - [SceneWallpaper.cpp](/home/peter/repos/papercompany/native/scene_backend/vendor/upstream_debug/src/SceneWallpaper.cpp)
  - [VulkanRender.cpp](/home/peter/repos/papercompany/native/scene_backend/vendor/upstream_debug/src/VulkanRender/VulkanRender.cpp)
  - [FinPass.cpp](/home/peter/repos/papercompany/native/scene_backend/vendor/upstream_debug/src/VulkanRender/FinPass.cpp)
  - [Shader.cpp](/home/peter/repos/papercompany/native/scene_backend/vendor/upstream_debug/src/Vulkan/Shader.cpp)

Current evidence:
- `ldd -r` is now clean for the repo-owned scene backend shared library.
- The harness reaches:
  - Vulkan/GL initialization
  - scene parsing
  - render-graph compilation
  - finishing-pass shader setup
  - first draw
  - external-texture import
- The previous repo-owned stop inside `CompileAndLinkShaderUnits(...)` was resolved by switching to the fully vendored `glslang` build.
- The repo-owned backend now reaches the same renderer/import phase as the system backend and successfully imports external textures in the harness:
  - `wekde.scene: receive external texture(...)`
  - `gl: importing external texture ...`
  - `gl: imported external texture ...`
- The previously suspicious `GL_INVALID_ENUM` is now attributed to stale GL state observed before the next consumer-side sync point, not to a hard failure inside `glImportMemoryFdEXT`.
- Explicit external semaphore interop is now wired through the repo-owned backend in the harness:
  - Vulkan exports a per-image `ready` and `release` semaphore FD alongside each external image
  - GL imports those semaphores once per texture
  - GL waits on `ready` before sampling the imported texture
  - GL signals `release` after rendering the frame that used that texture
  - Vulkan waits on `release` before reusing that image
- The old brute-force `glFinish()` path now only remains as a fallback when explicit semaphore interop is unavailable for a texture.

Inference:
- The highest-probability compiler-side blocker really was `glslang` source/ABI mismatch.
- With that removed, the remaining blocker is no longer shader compilation or basic memory-FD import.
- The likely remaining issue, if the scene is still visually wrong on host, is no longer the absence of a GL/Vulkan synchronization handshake.
- At that point the next debugging focus should shift to scene presentation details or the native renderer itself rather than basic interop plumbing.

Result:
- The repo-owned backend is now beyond the scaffold stage and reaches first frame, successful external-texture import, and active per-image external semaphore handoff in the standalone harness.
- The native backend now stages its `io.papercompany.scene` QML module into `wallpapers/io.papercompany.gradient/contents/imports/io/papercompany/scene/` during the CMake build.
- `Wallpaper Engine Scene Native` now loads that staged repo-owned module through a local QML import instead of depending on the system `SceneViewer` install.
- The scene guard now tears down and recreates the experimental runtime when the selected scene source or assets root changes, which avoids hot-swapping a live `PaperSceneViewer` instance across different scenes.
- The repeated `waiting on external ready semaphore` / `submitted external image` / `queued external release` pattern is the expected steady-state render loop for an animated scene, not by itself an infinite recursion bug.
- Those high-frequency native scene logs are now rate-limited so live Plasma runs stay readable while keeping the synchronization path visible.
- Hard `assert(...)` paths in the vendored MDL/puppet parser are being converted to logged validation failures so malformed scene content can fail soft instead of crashing `plasmashell`.
- The next native step should be host-side visual retesting and then, if needed, renderer/presentation debugging rather than more low-level interop plumbing.

## Wallpaper Engine Web bridge

Problem:
- The earlier web bridge depended on a `QWebChannel` bootstrap inside `QtWebEngine`.
- On the host, the runtime logged `QWebChannel is not defined`.
- Wallpaper Engine web pages were loading, but their property bridge was unreliable or missing.

Fix:
- Reworked [WebBackground.qml](/home/peter/repos/papercompany/wallpapers/io.papercompany.gradient/contents/ui/WebBackground.qml) to use a direct JavaScript compatibility shim instead of a live `QWebChannel` dependency.
- The shim now creates and maintains:
  - `window.wpeQml`
  - `window.wallpaperRegisterAudioListener(...)`
  - cached property delivery into `window.wallpaperPropertyListener`
- Property payloads are now pushed with `runJavaScript(...)` after page load and delivered directly into the page contract used by Wallpaper Engine web projects.

Result:
- The core bridge bootstrap no longer depends on `QWebChannel`.
- Remaining WE web failures are now page-specific or graphics-stack-specific, which is easier to reason about.

## Stale Plasma QML cache

Problem:
- Even after updating the package, `plasmashell` kept logging strings that no longer existed in the installed [WebBackground.qml](/home/peter/.local/share/plasma/wallpapers/io.papercompany.gradient/contents/ui/WebBackground.qml).
- That showed Plasma was still running an older in-memory or cached QML instance.

Fix:
- Clear the Plasma QML cache and restart `plasmashell` after significant runtime changes:

```bash
rm -rf ~/.cache/plasmashell/qmlcache ~/.cache/plasmashell/qtpipelinecache-*
killall plasmashell
plasmashell --replace &
```

Result:
- Plasma reloads the current wallpaper code from disk.
- Updated WE web bridge behavior becomes visible immediately.

## VM WebGL limitation

Problem:
- Inside the VM, WE web wallpapers hit:
  - `WebGL1 blocklisted`
  - `egl: failed to create dri2 screen`
  - `eglInitialize failed: EGL_NOT_INITIALIZED`
- Some pages also displayed an explicit `WebGL is unavailable` warning.

Diagnosis:
- The VM issue was not the wallpaper plugin itself.
- The guest graphics path was the blocker.
- The libvirt XML was structurally correct for virgl, but the host-side render path was not usable for this test stack.

Result:
- The VM is acceptable for non-WebGL checks and general workflow validation.
- It is not the right place to validate WE web wallpapers that require working WebGL on this hardware/software stack.

## Host-side validation path

Decision:
- Pivoted WE web testing to the main machine instead of the VM.

Why:
- The host avoids the VM virgl/SPICE/EGL/WebGL bottleneck.
- Host failures are more likely to reflect real compatibility issues in the plugin or the target wallpaper.

## Known remaining realities

- Some Wallpaper Engine web wallpapers still fail for page-specific reasons unrelated to the bridge.
- Some WE web wallpapers require WebGL and will not work in environments where Chromium/`QtWebEngine` blocklists or cannot initialize that graphics path.
- `Wallpaper Engine Web` is functional, but not yet a full reimplementation of the upstream Wallpaper Engine runtime.

## Useful commands

Update the wallpaper package during development:

```bash
kpackagetool6 -t Plasma/Wallpaper -u /home/peter/repos/papercompany/wallpapers/io.papercompany.gradient
```

Watch runtime logs:

```bash
journalctl --user -f | grep "Paper Gradient"
```

Do a temporary package-structure smoke test:

```bash
tmp_root="$(mktemp -d)"
kpackagetool6 -t Plasma/Wallpaper -i "/home/peter/repos/papercompany/wallpapers/io.papercompany.gradient" -p "$tmp_root"
```
