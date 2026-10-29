# Dear ImGui (vendored)

Unmodified files from [Dear ImGui](https://github.com/ocornut/imgui) (MIT, see `licenses/imgui.txt`;
shipped as `LICENSES/Dear-ImGui.txt` in the packages), used by the settings overlay in `src/overlay/`.

- Release tag: `v1.91.9`
- Upstream commit: `97428e8ac99e339ce05eee531cf55b77b29ea709`
- Archive: `https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9.tar.gz`
  (sha256 `3872a5f90df78fced023c1945f4466b654fd74573370b77b17742149763a7a7c`)

Copied: `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`, `imgui.h`,
`imgui_internal.h`, `imconfig.h`, `imstb_rectpack.h`, `imstb_textedit.h`, `imstb_truetype.h` and
`backends/imgui_impl_dx9.{h,cpp}` (flattened into this directory). No demo, no platform backend:
the overlay feeds input itself (`src/overlay/settings_overlay.cpp`).

Font: the overlay embeds `misc/fonts/Roboto-Medium.ttf` of the same tag
(`https://raw.githubusercontent.com/ocornut/imgui/97428e8ac99e339ce05eee531cf55b77b29ea709/misc/fonts/Roboto-Medium.ttf`,
sha256 `8559132c89ad51d8a2ba5b171887a44a7ba93776e205f553573de228e64b45f8`, Apache-2.0: `licenses/roboto.txt`, shipped as
`LICENSES/Roboto.txt`). The TTF is not kept here: `scripts/generate_overlay_font.py <Roboto-Medium.ttf>` writes it,
compressed, to `src/generated/overlay_font_roboto.h`.

Configuration lives outside these files, in `src/overlay/northlight_imconfig.h`, which
`scripts/build_renderer.py` passes as `IMGUI_USER_CONFIG` to every translation unit (no Win32
helpers, no file functions, no demo or debug windows; a failed assertion throws).

To update: download the new tag into an empty directory, replace the files above, update the tag,
commit and checksum here, then rebuild and run `scripts/run_tests.py test_settings_overlay`.
