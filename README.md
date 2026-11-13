# Northlight renderer

<img width="50%" height="1058" alt="NaМ€yttoМ€kuva 2026-10-10 kello 22 38 31" src="https://github.com/user-attachments/assets/c1996381-8dc2-481c-97c9-e3f1316a08bf" /><img width="50%" height="768" alt="image" src="https://github.com/user-attachments/assets/20cc99fb-216c-4430-901b-35b5b5df30d9" />
<img width="50%" height="768" alt="image" src="https://github.com/user-attachments/assets/e1181ddd-953d-4fd0-a703-eb97e5969ed2" /><img width="50%" height="1696" alt="image" src="https://github.com/user-attachments/assets/7b2c2595-f32b-42b0-bf67-95f7ee9ad570" />



A Direct3D 9 extension for the World of Warcraft 3.3.5a client: a proxy `d3d9.dll`
(built as `frd9.dll`) that adds lighting, shadows, fog, GI and sky effects. The repository also
holds the offline asset builders (MPQ patches, world cache) and their validation.

The repository holds no game files: every tool that needs game data reads it from your own
3.3.5a client. Licensed under the [MIT License](LICENSE).

The changelog, release notes and validation records are not part of the public source.

Download this fork's releases: https://github.com/aphelion105/northlight/releases

The Windows installer includes the matching `d3d9.dll` and `northlight-assets.bin`.
Releases with imported Forever atmosphere also include `forever-atmosphere.bin`;
keep all three files together when installing a DLL update manually.

## What Northlight adds

Every effect below is on by default and is drawn on top of the game's own frame.

- **Sun and moon.** Its own sun and moon discs on an orbit that follows the game's clock; the game's
  sun and moon billboards are hidden. The sun glows in the game's own sun colour for the zone and
  time, and a veil softens trees, towers and ridges in front of it; fog and horizon haze take the
  same hue toward the sun.
- **Shadows.** Sun and moon shadows in two cascades (about 48 and 192 yards around the player), with
  a cached static layer for terrain, buildings, trees and props, and distant terrain shadows out to
  928 yards (up to 4096 in the zones listed in `shadow-range-profiles.ini`). Characters, creatures, mounts,
  doors, ships and other moving objects cast shadows too; near the player they get their own finer shadow
  map and a soft filter that is sharp at the feet and softer further from the caster, so character and mount
  shadows are crisp and their edges no longer shimmer while the model animates (`ActorShadowDetail`, 0.3.209). The game's baked terrain shadows are
  replaced, its round blob shadows under characters are kept, fainter (`BlobShadowStrength`), and where the sun is blocked the
  game's painted sunlight is removed smoothly, without facet steps, and the thin light line it left on the ground along wall feet and under characters in shadow is mostly gone: the lighting upsample now follows the scene colour there (0.3.208).
- **Global illumination.** Sky light and bounced light from probes ray traced against the world
  geometry on a background thread, about 76 yards around the camera (52 in the Balanced and Performance
  presets); characters add their own bounce and occlusion (Quality preset).
- **Experimental grass shadows.** Set `GrassShadows=1` in `northlight-quality.ini` and restart WoW
  to enable bounded nearby native grass casters; `ActorShadows=1` and shadows must also be on.
  It defaults to `1` in Quality and `0` in Balanced/Performance. `GrassShadows=0` skips
  the additional capture and replay work; sampled receiver diagnostics remain available.
- **Ambient occlusion.** Screen-space AO with contact shading and a light bloom.
- **Fog and air.** Volumetric sun and moon light with light shafts through the shadows; soft haze on
  the far landscape and the lowest sky in the game's own fog colour; regional ground fog in forests,
  wetlands and basins, denser at night, derived from the map. The sun light comes from the game's sky
  block; the game darkens that light towards the sun (its sun glare) after copying its colours, and a read that lands
  between the copy and the darkening keeps the last darkened colours, so the sun light and its glow in the fog no
  longer flash brighter for a frame in Duskwood (0.3.207). The darkening is predicted from the game's own glare
  value, so matching reads skip the hold; the hold stays for underwater, spell flashes and frames where the client skips the darkening.
  In maps without a world cache (battlegrounds, arenas, and the dungeons and raids that were not added to it), the geometry builder checks the cache once
  per entry and skips the repeated build attempts (0.3.210). These maps get no Northlight shadows, so the game's own
  shadows and its round blob shadows under characters are drawn as the game draws them there. Dungeons and raids are an optional part of the cache:
  on the first install, the installer asks which of six groups (Classic, TBC and Wrath dungeons and raids; about 0.3-1.3 GB and 1-2
  build minutes each) to build. Updates keep the saved choice; `--instances` changes it.
  Current caches are reused, and light-only updates keep instance geometry. Changing the choice never rebuilds the continents.
- **Effect switches.** F11 offers **All Northlight effects**, **World shadows**, **Atmospheric fog** and **Local light illumination**. The master switch pauses scene capture, geometry/GI workers and effect passes through the same bypass as F10, while retaining settings, caches, HDR presentation and the menu. Turning off all seven main scene groups also enters that bypass automatically. F7/F8/F9 and F10 remain independent temporary switches.
- **Lamps.** Up to 48 nearby lamps (Quality; `LocalLightLimit`, 0..128; 0 disables local lighting), lanterns, braziers and fires light the ground and walls around
  them and glow in the fog. Lamps in view take the slots first, and nearby lamps on screen are batched together. In direct sun lamps dim. Local shadows are enabled by default:
  64 compact sources by default, adjustable from 16 to 128 with `PointShadowLimit` in the Shadows menu.
  Torches, candles, lamps and compact fires cast shadows; diffuse glows, runes and sparkles do not.
  Shadows work day and night, fading where their source is weak compared with sunlight at the
  receiver. Each pass removes only the blocked contribution added by Northlight's matching direct
  light; it does not subtract an estimate of baked light. Overlapping lamp glows are capped once.
- **Lighting art layer.** An MPQ patch (`patch-z`) built from your client's own `Light*.dbc`: retuned
  outdoor clear-weather light and fog colours, warmer Mulgore, denser Stormwind day fog. Storm weather
  gets darker light and fog bands. Sky models
  that paint their own sun or moon into the clear-weather sky lose it.
- **Weather.** A heavy-rain look: procedural rain and snow textures in the art layer, darker storm light
  and fog bands, and Northlight's own haze, fog, light shafts, sun and moon, shadows, GI and lamps follow
  detected rain or snow (F10 turns it off). With `WeatherMemory=1` (default) detection reads the game's own weather state
  from memory (type including sand, intensity; plain reads, nothing is hooked or patched), so the art layer is not required for
  detection; the effects still follow the weather only on frames where the sky is drawn (indoors they fade out). It falls back to
  the textures the art layer ships when that read fails, and `WeatherMemory=0` uses only those and reads nothing from the weather state (they need the Northlight art layer; `Weather`, `RainFog`). Because the game's weather state does not depend on the `weatherDensity` setting, the rain look also comes on when the game's own rain particles are turned off; `Weather=0` (or F10) turns Northlight's weather look off. The game's rain streaks are drawn alpha-blended so they stay crisp instead of taking the background's colour, and the game's mist puffs, which darkened night and storm rain into black balls, are left out while it rains; snow and sand storms keep them.
- **Camera weather.** Rain adds refractive drops that collect water along curved rivulets (`RainRivulets`). Snow adds crystalline flakes and an independent frost vignette that grows with snow accumulation and storm intensity, and melts near fire (`SnowFrost`, `SnowFrostStrength`). Sandstorms add windblown ochre grains and sandy edge deposits (`SandLens`). Each has size/strength controls in the settings window and in the [standalone weather lab](renderer/docs/weather-lab.md). Camera effects require `Weather`, hide under shelter and skip drawing when disabled or invisible.
- **Fog clouds.** Low fog banks that drift with the wind, thicker and faster in rain, few and faint in clear
  weather and only in forest and grass zones then (its own half-resolution pass; `FogClouds`, `FogCloudDensity`). Ctrl+Shift+F7 (fog) and F10 turn
  them off with the rest; the Performance preset has them off. The fog is also smoothed over frames so lamp glows
  and shafts do not shimmer while moving.
- **Water.** The game's water is drawn unchanged; a liquid mask keeps the relighting and AO off the
  surface, and fog is measured to the water surface.
- **Settings.** `northlight-quality.ini` has three presets (Quality, the default, Balanced and
  Performance) and about 30 keys for shadows, GI, lamps and haze; `celestial-profiles.ini` sets the
  sun and moon look per zone.
- **Indirect lighting.** In Lighting & AO, GI influence defaults to 120 (full
  ambient replacement); 60 restores the old half blend. GI brightness accepts
  negative and positive percentages (-100..200). Northrend aurora adds a subtle
  green/purple sky fill between sunset and moonrise, transported through the
  existing GI probes and blocked by geometry. Its strength is adjustable (0 disables it).
- **Hotkeys** (with Ctrl+Shift): F7 fog and haze, F8 GI, F9 shadows, F10 all effects, F11 the settings window, F12 debug views
  (shadows, GI, fog volume). On a Mac, first remove macOS's own Control+F7 shortcut. The settings window (Ctrl+Shift+F11) is
  drawn by the renderer over the game; it takes the mouse only over the window and the keyboard only while one of its text
  fields is active. `[Renderer] SettingsOverlay=0` in `northlight-renderer.ini` turns it off; `[Renderer] SettingsOverlayKey`
  (default `Ctrl+Shift+F11`, F1-F12 with Ctrl/Shift/Alt) changes its key.
- **Night brightness.** `NightBrightness` (0..100, default 50; 0 = Northlight's full night darkness) gives back the night ambient light the art layer takes away: 100 restores the original game's night ambient (the art layer scales it by .82 with a cool tint). It is added like light, so moon shadows, lamps and colours keep their contrast, and it assumes the Northlight art layer is installed. It ramps in at dusk and out at dawn; daytime, the sky, water and places without a sky of their own (dungeons, raids) are unchanged.
- **Font mods.** Lexara, TweakWoW2 (HD Font on) and the AwesomeWotLK MSDF fork replace the game's UI shaders,
  which Northlight uses to find where the world ends and the UI begins. Northlight then learns the mod's UI draws
  instead: effects start about 2 seconds after entering the world, and the log shows
  `EFFECT boundary fallback active: UI shaders replaced by another module`. From then on `NameMask` also recognises
  floating names drawn with the mod's shaders (Lexara, TweakWoW2), so they keep their colour too.
- **Platforms and install.** macOS with WoWSilicon (preloaded as `mods/d3d9.dll`), Linux with Wine or Proton
  (the Windows payload; see [Install on Linux](#install-on-linux-wine-or-proton)) and Windows (a
  game-folder `d3d9.dll` on the bundled DXVK 3.1.1, with DXVK 2.7.1 as the `dxvk2` alternative backend for AMD RX 5000/6000 and
  older drivers (`Install.cmd --backend dxvk2`; on a driver DXVK 3 does not support, install with `--backend dxvk2`, Northlight never switches by itself; a reinstall without `--backend` keeps `dxvk2` or `native`), the system D3D9, or an existing `d3d9.dll`). The
  installer never writes `wow.exe`. It builds the world cache (terrain, models, lamps and fog
  regions) and the lighting art layer from your own client on your machine, about 10-40 minutes and
  at least 8 GB of RAM; nothing from the game is shipped. The packages bundle their own Python and StormLib, and uninstall restores every change.

## Development

See [the development guide](docs/development.md) for repository structure, setup, build and validation commands, and [the architecture map](docs/architecture.md) for runtime ownership and contracts.

## Install on macOS (WoWSilicon)

With the game closed:

```sh
python3 <repository>/renderer_status.py status
python3 <repository>/renderer_status.py on      # or: off
python3 <repository>/renderer_status.py on --client <client folder>
```

The client is `--client PATH`, else `NORTHLIGHT_CLIENT`, else `northlight.local.ini` `[paths] client`,
else the repository's parent folder if it holds `Wow.exe` and `Data/`; with none of these the tool
stops with an error that names them.
`on` installs `renderer/frd9.dll` as `mods/d3d9.dll`, and `off` restores the recorded
transaction. Add `--dry-run` to preview. The Windows package template is in
`renderer/windows-package/`.

## Install on Linux (Wine or Proton)

The Linux package (`Northlight-<version>-Linux.zip`) is for players who run the game under Wine or
Proton (Lutris, Bottles, Steam or plain Wine). **It has not been tested in game yet**; reports are welcome.
The game gets the Windows payload (the game-folder `d3d9.dll` and the bundled DXVK 3.1.1 and 2.7.1 in
`renderer-backends/`), while the installer, the world cache and the art layer run natively on x86_64 Linux
(glibc 2.17 or newer) with the package's own Python (python-build-standalone) and StormLib (`libstorm.so`).

With the game closed, unzip the package outside the game folder and run, in that folder:

```sh
bash install.sh --client ~/Games/WoW          # the folder that holds Wow.exe; asked when left out
bash install.sh --client ~/Games/WoW --backend dxvk2   # DXVK 2.7.1 for drivers DXVK 3 does not support
bash uninstall.sh --client ~/Games/WoW
```

Then make Wine load the game-folder `d3d9.dll` once, with the override `d3d9=n,b`
(`WINEDLLOVERRIDES="d3d9=n,b"`):

- Lutris: Configure > Runner options > DLL overrides: `d3d9` = `n,b` (Lutris builds `WINEDLLOVERRIDES`
  from this list).
- Bottles: the bottle's settings > DLL Overrides (under Advanced in recent versions): `d3d9` = Native, then Builtin.
- Steam/Proton (a non-Steam game): Launch options `WINEDLLOVERRIDES="d3d9=n,b" %command%`.
- Plain Wine: `WINEDLLOVERRIDES="d3d9=n,b" wine Wow.exe`, or winecfg > Applications > `Wow.exe` > Libraries.

Northlight loads its own DXVK from `renderer-backends/`, so the DXVK option of Lutris, Bottles, Proton or
the prefix does not matter. Leave it as it is, and do not copy a DXVK `d3d9.dll` into the game folder.

On native Windows with bundled DXVK 3.1.1 and an NVIDIA adapter, Northlight disables Vulkan present-wait features to avoid a driver wait that can stall the client when its window is obscured. This can affect frame pacing. Set `[Renderer] DxvkPresentWaitWorkaround=0` in `northlight-renderer.ini` to restore the driver's present-wait path.
WineD3D modes (`PROTON_USE_WINED3D=1`) bypass Northlight. 32-bit (lib32/i386) Vulkan drivers are needed,
as for any DXVK game. Because Linux names are case-sensitive and Wine's are not, the installer stops
when the game folder already has a case variant of a name it writes (for example `D3D9.dll`). The player
instructions are in `renderer/linux-package/README.txt`.
