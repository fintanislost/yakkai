# Scene Harness

This app is the standalone debugging surface for `Wallpaper Engine Scene Native`.

Why it exists:
- native scene bugs should be reproducible outside `plasmashell`
- scene backend launches should be testable without risking a Plasma crash
- renderer state should be inspectable before the backend is wired back into the wallpaper package

Current backends:
- `system`: uses the currently installed `com.github.catsout.wallpaperEngineKde` `SceneViewer`
- `paper`: uses the repo-owned `io.papercompany.scene` native backend module

Example usage after building:

```bash
./paper_scene_harness \
  --backend system \
  --source /path/to/scene.json \
  --assets /path/to/wallpaper_engine/assets \
  --fill crop
```

Useful flags:
- `--backend system|paper`
- `--source /absolute/path/to/scene.json`
- `--assets /absolute/path/to/wallpaper_engine/assets`
- `--fill crop|fit|stretch`
- `--mouse`
- `--unmuted`

The harness is expected to evolve faster than the Plasma package. Keep backend experiments here first, then move stable behavior back into `Wallpaper Engine Scene Native`.

The repo-owned `paper` backend now shares the same fill-mode semantics as the wallpaper package:
- `crop` -> `AspectCrop`
- `fit` -> `AspectFit`
- `stretch` -> `Stretch`

Current status:
- `system` reaches external-texture import and reproduces the known `GL_INVALID_ENUM` path outside Plasma.
- `paper` now reaches:
  - scene parsing
  - render-graph compilation
  - finishing-pass shader compilation
  - first draw
  - external-texture import
- `paper` now also exercises explicit per-image external semaphore interop:
  - GL imports per-image `ready` and `release` semaphores
  - GL waits before sampling and signals release after rendering
  - Vulkan waits before reusing an image
- the current repo-owned blocker is no longer shader compilation
- the harness now shows imported textures being created successfully in the repo-owned backend
- the previous `GL_INVALID_ENUM` signal is stale GL state seen before the next sync point, not a confirmed `glImportMemoryFdEXT` failure
- if visual issues remain after this point, the next likely native issue is renderer/presentation behavior rather than missing interop plumbing
