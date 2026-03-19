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
