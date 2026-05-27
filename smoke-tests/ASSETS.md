# Smoke Test Assets

These Workshop scenes are used by the local render regression harness. Install them in Wallpaper Engine before running quick, deep, or release gates that require local assets.

| Scene ID | Workshop | Gates | Required | Purpose | Visual Review Focus |
| --- | --- | --- | --- | --- | --- |
| 3228578419 | https://steamcommunity.com/sharedfiles/filedetails/?id=3228578419 | quick, deep, release | yes | Puppet, flare, particles, alpha-sensitive composition; motion sequence uses a 2-frame temporal tolerance for animation jitter | camera angle, desk/background/character, lens flare intensity, halo glow, sleep particles |
| 3327063360 | https://steamcommunity.com/sharedfiles/filedetails/?id=3327063360 | quick, deep, release | yes | Main video texture, particles, effect chain; sequence-only fixture with a 6-frame temporal tolerance for decoder phase drift | video progression, green particles, background composition, color balance |

Candidate and deferred fixtures are tracked in `smoke-tests/COVERAGE.md` and `smoke-tests/coverage-matrix.json`. Keep this file focused on active smoke-test assets; move a candidate here only after baseline promotion.

Future fixtures should be added only after a local candidate is reviewed and baselines are promoted. Useful gaps are SceneScript-heavy scenes, static model/material/light scenes, small overlay video texture scenes, and separate WE Web or WE Video harness coverage.

Default Steam paths used by the harness:

```bash
~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets
~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960
```

If Steam is installed outside Flatpak, pass `--assets` and `--workshop` to `smoke-tests/run.sh`.
