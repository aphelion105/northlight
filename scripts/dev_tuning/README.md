# Developer zone tuner

Tune a zone's look live while standing in it: sun and moon disc and light colours, fog gain and
height, horizon haze, terrain shadow radius and ground fog. Developer-only: it works only with a
dev renderer build and is never shipped.

- Dev DLL: `python3 scripts/build_renderer.py --dev-tuning` writes `renderer/frd9-dev-tuning.dll`.
  It embeds the marker `NORTHLIGHT DEV TUNING BUILD`, and `renderer/build_packages.py` refuses to
  package such a DLL. A normal build has no tuning code at all. Installing it is the developer's
  own decision, by hand: back up `<client>/mods/d3d9.dll` first, then copy the dev DLL there.
- The DLL and this tool share two files in the game root (no sockets):
  `northlight-tuning.ini` (overrides, written here, read by the DLL about every 250 ms) and
  `northlight-tuning-state.json` (zone, day/night, base and effective values, written by the DLL
  about twice a second while in the world). Formats: `src/sky/zone_tuning.h`.

## Run

```sh
python3 scripts/dev_tuning/zone_tuner.py serve                # game root from northlight.local.ini
python3 scripts/dev_tuning/zone_tuner.py serve --client GAME --port 8765
```

`--client` defaults to the client in northlight.local.ini `[paths] client` (the HD client); pass
`--client` to tune with the stock client. `export` takes the same `--client`.

Open http://127.0.0.1:8765/ in a browser window next to the game. The server listens on
127.0.0.1 only. The panel follows the zone the camera is in (never while a slider is being
dragged); **Pin** keeps the current section, and the list at the bottom or the `Map:ZoneID` field
pins another one, also `[default]` (all zones). A marked row is overridden; `x` resets it to the
base value, **Reset zone** removes the whole section. The overrides checkbox writes
`[tuning] enabled` for A/B comparison and keeps the values. "game" under a row shows the value the
DLL reports when it differs from the panel (not applied yet, or overrides off).

Sun and moon values are written live while a slider moves (debounced about 150 ms). terrain_radius
and the ground fog scales make the game rebuild its geometry (about 2 s), so they are written only
when the slider is released or a typed number is entered. When the state reports
`groundFogActive: false` (the zone's ground fog policy has no density, e.g. Tanaris; a DLL without
the field counts as true), the ground fog sliders are disabled: use fog gain, fog height and
horizon haze there. Each write carries a sequence number; the server drops a write older than the
last one it applied to that section, so a slow request cannot undo a newer value.

## Workflow

1. Tune in the game. Every change is saved to `<game root>/northlight-tuning.ini` at once
   (debounced about 150 ms), so nothing is lost when the game or the tool stops.
2. `python3 scripts/dev_tuning/zone_tuner.py export --dry-run` shows the diff;
   `export` merges the overrides into `client-config/celestial-profiles.ini` (sun/moon keys) and
   `client-config/shadow-range-profiles.ini` (terrain_radius). Existing key lines are replaced in
   place, new keys go after the section's last key, a new zone section is appended with a
   `# <Zone name>` comment. A radius set back to 928 is written as 928 in an existing section; a
   new section is not added for 928.
   Ground fog scales have no ini: export prints them per zone for `NorthlightRegionalFog::policy`
   in `src/sky/regional_fog.h`. A zone with no branch of its own in `policy()` needs a new branch
   there first: scaling its zero density changes nothing.
3. The export then runs `client-config/check_client_config.py`, which compares client-config/
   with the game root's live copies; it reports DIFFERS until the new files are installed in the
   game root. Review, commit, and delete or empty `northlight-tuning.ini`.

Zone names come from the client's AreaTable.dbc (StormLib, `mpq.py`), else from the `# name`
comments in the profile ini files.

Test: `python3 tests/test_zone_tuner.py`.
