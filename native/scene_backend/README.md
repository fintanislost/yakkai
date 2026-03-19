# Scene Backend

This directory contains the repo-owned native Wallpaper Engine scene backend for Paper Company.

Current state:
- `io.papercompany.scene` builds a repo-owned `PaperSceneViewer` QML type backed by vendored native scene sources when `PAPER_ENABLE_VENDORED_SCENE_BACKEND=ON`.
- The build now stages that QML module into `wallpapers/io.papercompany.gradient/contents/imports/io/papercompany/scene/` so `Wallpaper Engine Scene Native` can load it without depending on a system-installed scene plugin.
- The built module now reaches native renderer initialization in the standalone harness, including:
  - Vulkan/GL setup
  - scene parsing
  - render-graph compilation
  - finishing-pass shader compilation
  - first draw
  - external-texture import
- `vendor/upstream_debug/` contains the working source base for that backend, including the missing `glExtra.cpp` path that was not present in `references/`.
- External texture import is now instrumented more precisely:
  - the old `GL_INVALID_ENUM` is observed as stale GL state before the next sync point
  - imported textures are still created successfully afterward
- Explicit per-image external semaphore interop is now active in the standalone harness:
  - Vulkan signals a `ready` semaphore for each exported image
  - GL waits on `ready` before sampling
  - GL signals `release` after rendering
  - Vulkan waits on `release` before reusing the image
- The backend is still experimental, but the remaining scene issue is no longer basic memory-FD or semaphore plumbing.

Planned next steps:
1. Retest the repo-owned backend visually on host with the explicit semaphore path active.
2. Keep the QML API aligned with the current scene runtime shell:
   - `source`
   - `assets`
   - `fillMode`
   - `muted`
   - `mouseInputEnabled`
3. Keep the vendored tree separate from the wrapper until the standalone harness and the wallpaper package isolate the remaining external-texture issue cleanly.
4. Iterate on renderer and presentation behavior now that the wallpaper package is loading the repo-owned module.

The stable Plasma package should keep using the safe `Wallpaper Engine Scene` mode while this backend evolves.
