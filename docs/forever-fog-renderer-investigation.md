# Forever fog renderer: local binary evidence

Inspected 13 November 2026, Forever 1.60.1.70888. This is a static investigation
of the installed client's shader programs, not a frame capture or a performance
measurement. No renderer implementation has been ported by this investigation.

## Provenance and limits

- Installation: `D:\Battle.net\Games\World of Warcraft\_forever_`.
- `Wow.exe` SHA-256:
  `054a166a86c2ab79f49366d319458a77c0653f0410c6add534385796f1dc32a9`.
- The existing audited executable copy in the private output directory matches
  the installed executable. The installed files were opened read-only.
- The on-disk executable's code section is packed: sampled 1 MiB windows have
  approximately 8 bits/byte entropy. It does not provide usable static call
  paths for the fog renderer. No unpacking or client modification was attempted.
- The latest existing `gx.log` reports shader family SM6; `Config.wtf` selects
  D3D12 and volume fog quality 4. Neither establishes which individual fog
  variants execute in a particular scene.
- CASC files were read through the existing CascLib 3.0. Shader names were mapped
  through [wowdev's public listfile release `202611110023`](https://github.com/wowdev/wow-listfile/releases/tag/202611110023). Community filenames
  can be inferred; FileDataID and extracted bytecode hashes identify the evidence.
- Extracted BLS containers have `HSXG` magic, version `0x1000e`. Their zlib
  blocks must be concatenated before parsing DXBC: individual programs can span
  multiple 16 KiB blocks. All 120 declared variants in the 46 examined packages
  were recovered and disassembled, including SM5 and SM6 versions.
- Private outputs are under
  `<NORTHLIGHT_OUT>/forever-fog-architecture-70888/` in the original local output
  root. `inventory.json`, `modern-shader-extraction.json`,
  `shader-disassembly.json` and `dxbc/` retain hashes, variants and disassembly.
  Proprietary shader bytes and disassembly remain outside Git and packages.

The previous atmosphere import imported DB2 parameters. Its 48-interval
Northlight shader is not a reconstruction of this renderer's execution model.

## Observed shader contracts

| Stage / FileDataID (SM5, SM6) | Direct observation |
|---|---|
| Material injection / 6227846, 6227849 | Eight variants. `RWTexture3D<float4>` output; workgroups 4x4x4. The more complex variants read noise and shadow resources. Density and scattering are accumulated per volume cell. |
| Global fog / 7674322, 7674323 | Eight variants with 4x4x4 workgroups and 3D output. SM6 retains `GlobalFogParams[36]` and `InteriorFogLookup` type information. Noise/shadow inputs differ between variants. |
| Local scattering / 6227845, 6227848 | Reads and updates a 3D UAV. The shader computes a clustered light-list index and clamps its list length to 32. Light records have a 192-byte stride. Workgroups are 8x8x1. |
| Temporal blend / 6227847, 6227850 | Three variants read current and history 3D textures and write a 3D UAV. They transform a volume sample into the history view and reject coordinates outside the previous volume. Accepted history contributes 0.95, current data 0.05. Two variants clamp history against a current neighbourhood. |
| Integration / 5349620, 5349621 | Eight variants integrate columns of a 3D texture, writing accumulated RGB and `1 - transmittance` at each slice. Four slice counts are present: 64, 96, 128 and 192. Each has a variant with a separate history/output texture and an in-place variant. The history variants also contain 0.95/0.05 mixing. Workgroups are 8x8x1. |
| Composite / 5517466, 5517469 | Samples screen depth and a 3D fog texture. The depth coordinate is the cube root of normalized distance. There is no per-pixel density/shadow raymarch in this program. |

The temporal shader constructs distance using the cube of normalized slice
position; the composite uses its inverse. This establishes a camera-frustum
volume with nonlinear depth spacing, rather than a world-fixed Cartesian cache.
It does not establish the volume's XY dimensions or the selected quality mapping.

Useful exact private evidence locations:

- `dxbc/shaders__compute__dx_5_0__volumefogtemporalblend-0.asm`: 3D resources,
  reprojection, bounds rejection and the 0.95/0.05 mix.
- The `-1` and `-2` temporal variants add respectively four XY and eight XYZ
  neighbourhood samples for the clamp.
- `dxbc/shaders__compute__dx_5_0__volumefogcompute-0.asm`: 64 output stores,
  ending at slice 63; `-2`, `-4`, `-6` have 96, 128, 192 stores.
- `dxbc/shaders__compute__dx_5_0__volumefoglightscatter-0.asm`: the 33-element
  cluster-list stride, 32-light bound and 192-byte light records.
- `dxbc/shaders__pixel__dx_5_0__volumefogcomposite-0.asm`: screen depth plus
  cube-root depth mapping and one volume lookup.
- Corresponding SM6 `.ll` files confirm the texture dimensions, workgroup
  sizes, constants and resource types in the installed SM6 shader family.

These are available shader contracts. Their exact CPU scheduling, binding order
and selection in a live frame have not been observed. The presence of two
history-capable stages does not prove both run in sequence in the same frame.

## What this changes for Northlight

The evidence supports separating expensive volume evaluation from final screen
integration. A useful design reference is:

1. Evaluate density/scattering and relevant lights on a frustum volume grid.
2. Maintain history in that volume's coordinates, with proper 3D reprojection,
   rejection and neighbourhood validation.
3. Integrate columns into an accumulated fog volume.
4. Resolve current screen pixels from their depth and the integrated volume.

This is an inferred dependency model from shader inputs/outputs. The actual
Forever pass order still requires a live trace.

Northlight currently evaluates its imported atmosphere with up to 48 intervals
per half-resolution screen pixel, then applies 2D history and blur. Moving the
expensive density/shadow work to fewer volume cells could reduce repeated work;
the benefit and image error are unmeasured. The existing local-light fix must
remain: local lamp/spell radiance cannot enter a history reprojected by background
surface depth. A validated 3D history would be a different ownership contract.

Porting the execution primitives directly is not possible in the existing
D3D9/SM3 backend: it has no compute dispatch or RWTexture3D shader interface.
A compatible prototype would need raster passes into a 2D slice atlas, volume
sampling with slice interpolation, and an explicit integration strategy. Atlas
storage, sampling bandwidth, pass count and 32-bit memory pressure must be
measured before adoption. Keep the current implementation as the image reference.

The reference does **not** prove that fog updates only when the sun changes.
Per-cell current inputs, frame-dependent sample jitter and reprojected history
are present. It supports temporal reuse, not a claim that all lighting work is
skipped between solar changes. A world-space static-light cache would be an
additional Northlight design, requiring its own invalidation proof and benchmark.

## Still unknown

- Live volume XY dimensions, actual texture formats and memory allocations.
- Which of the eight integration and three temporal variants quality 4 selects.
- Actual pass order, dispatch frequency and history-reset conditions.
- Whether any static-medium or light contributions are updated partially.
- Before/after GPU time or gameplay image parity for a Northlight prototype.

Confirm these through a live frame and resource trace before claiming that
Forever's update policy, performance or exact visual output has been reproduced.
