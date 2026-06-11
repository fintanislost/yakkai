# Smoke Test Assets

These Workshop scenes are used by the local render regression harness. Install them in Wallpaper Engine before running quick, deep, or release gates that require local assets.

| Scene ID | Workshop | Gates | Required | Purpose | Visual Review Focus |
| --- | --- | --- | --- | --- | --- |
| 3228578419 | https://steamcommunity.com/sharedfiles/filedetails/?id=3228578419 | quick, deep, release | yes | Puppet, flare, particles, alpha-sensitive composition; motion sequence uses a 2-frame temporal tolerance for animation jitter | camera angle, desk/background/character, lens flare intensity, halo glow, sleep particles |
| 3327063360 | https://steamcommunity.com/sharedfiles/filedetails/?id=3327063360 | quick, deep, release | yes | Main video texture, particles, effect chain; sequence-only fixture with a 6-frame temporal tolerance for decoder phase drift | video progression, green particles, background composition, color balance |
| 1591277437 | https://steamcommunity.com/sharedfiles/filedetails/?id=1591277437 | deep | no | Godrays, shake, pulse, and stale final-presentation artifact regression coverage | godray placement and brightness, diagonal beam absence, character/background composition, color balance |
| 2788691565 | https://steamcommunity.com/sharedfiles/filedetails/?id=2788691565 | deep | no | Overlay video texture, water-effect motion, and particles; sequence-only fixture with a 4-frame temporal tolerance for animation/video phase drift | water motion, overlay video region, character composition, blue glow balance |
| 3326873240 | https://steamcommunity.com/sharedfiles/filedetails/?id=3326873240 | deep | no | SceneScript and time-mode variants with expected animated sky/video phase drift in still captures | time-period color state, foreground/background elements, video texture, missing media panel layers |
| 3301291394 | https://steamcommunity.com/sharedfiles/filedetails/?id=3301291394 | deep | no | SceneScript Clock/Date text binding discovery, generated text-layer representation, and user property gating | clock/date generated text logs, shoulder artifact absence, character/background composition, sunset color balance |
| 1576514332 | https://steamcommunity.com/sharedfiles/filedetails/?id=1576514332 | deep | no | Static model, material, lighting, composelayer, and particle sprite material coverage | camera framing, runner silhouette, foreground models, city lights, cloud/sky composition, particle lightning |

Candidate and deferred fixtures are tracked in `smoke-tests/COVERAGE.md` and `smoke-tests/coverage-matrix.json`. Keep this file focused on active smoke-test assets; move a candidate here only after baseline promotion.

Future fixtures should be added only after a local candidate is reviewed and baselines are promoted. Useful gaps are additional SceneScript-heavy scenes, static model/material/light scenes, small overlay video texture scenes, and broader WE Web or WE Video harness coverage beyond the current candidates.

Default Steam paths used by the harness:

```bash
~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets
~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960
```

If Steam is installed outside Flatpak, pass `--assets` and `--workshop` to `smoke-tests/run.sh`.
