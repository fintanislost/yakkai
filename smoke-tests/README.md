# Smoke Test Reference Screenshots

Captured from the scene harness at known-good master state for regression comparison.

## 3228578419 — Sleeping Arona (Blue Archive)

**File:** `3228578419-sleeping-arona.png`
**Type:** Puppet scene (14-bone skinned crop-sheet puppet)
**Workshop:** `steamapps/workshop/content/431960/3228578419/scene.pkg`
**Key features:**
- Puppet mesh character (ARONA_CROP_SHEET) with direct rendering (effects stripped)
- Rainbow lens flare arc (workshop/2188505192 flare layers with full effect chain)
- Sparkle particles, Z sleep letters, halo glow
- Background layers (desk, wall, chair) with pulse effects
- Capture delay: 8000ms, fill: crop

## 3327063360 — Shiroko Night of Stars (Blue Archive)

**File:** `3327063360-shiroko-video.png`
**Type:** Video texture scene (embedded MP4, 3840x2160, 60fps)
**Workshop:** `steamapps/workshop/content/431960/3327063360/scene.pkg`
**Key features:**
- Animated MP4 video background decoded via FFmpeg
- Full effect chain with HLSL screen-space UV fix
- Particle effects (green glow dots)
- Solidlayer overlays rendering through effect pipeline
- Capture delay: 20000ms (video needs longer to load), fill: crop

## How to recapture

```bash
# Arona
build/native/scene_harness/paper_scene_harness --backend paper \
  --source ~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3228578419/scene.pkg \
  --assets ~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets \
  --fill crop --capture smoke-tests/3228578419-sleeping-arona.png --capture-delay-ms 8000

# Shiroko
build/native/scene_harness/paper_scene_harness --backend paper \
  --source ~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3327063360/scene.pkg \
  --assets ~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets \
  --fill crop --capture smoke-tests/3327063360-shiroko-video.png --capture-delay-ms 20000
```

## Git state

Baselined at commit `8c0d7a4` on master (2026-03-28).
