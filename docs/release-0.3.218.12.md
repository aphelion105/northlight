# Northlight 0.3.218.12

Restore the fast volumetric fog path under CPU/cache pressure and remove
repeated work from geometry capture and local shadows.

## Changes since 0.3.218.10

- Retain resident fog atlases under address-space pressure. Admit cold DEFAULT
  targets using the existing memory reserve, with capability/allocation fallback
  and Reset handling preserved. The matched 4K noisy actor-shadow component
  measures about 9–10× faster than its pressure-selected fallback on native D3D9
  and DXVK. This is not a whole-game FPS claim.
- Skip optional pose/source reads for programs with no coarse-envelope consumer;
  preserve mandatory geometry capture and SingleBone far-range proofs.
- Partition local-shadow geometry spatially, reject distant batches through an
  immutable bounds tree, and avoid content hashing for moving lights that must
  redraw their faces. Preserve submitted geometry and conservative fallbacks.
- Reduce repeated actor replay setup with opaque-run batching and palette-range
  rebasing. Preserve deformation, opacity barriers and shadow coverage.
- Accelerate exact single-bone bounds and cold displacement construction; reuse
  capture storage and certificates and avoid redundant cache maintenance.
- Keep displacement height fields on the GPU with the existing CPU fallback.
- Reject stale fog history around moving foliage and skip provably empty actor
  shadow intervals without reducing integration quality.
- Explicitly capture newly created state blocks so native D3D9 restores high
  pixel-shader constants on the first effect frame as well as subsequent frames.

The release also includes the capture/cache and temporal-fog fixes documented
in [0.3.218.11](release-0.3.218.11.md). Measurements and their scope are recorded
in [fog and capture](fog-capture-pipeline-20261117.md),
[pipeline preparation](pipeline-redundancy-20261117.md),
[effect toggles](effects-toggle-optimization-20261117.md), and
[local shadows/capture](local-shadow-capture-speedup-20261117.md).
No representative live-route FPS multiplier is claimed.

## Download and update

Windows, macOS and Linux installer archives include the matched renderer/assets
pair and converted atmosphere table. macOS/Linux archive contents are verified
on Windows; gameplay on those platforms has not been validated here. Existing
personal settings are preserved by the installer.

Close the client before updating. Run `Install.cmd` on Windows,
`Install Northlight.command` on macOS, or `install.sh` on Linux.
For a manual update replace both `d3d9.dll` and `northlight-assets.bin`.
Verify downloads with `SHA256SUMS.txt`. Compact captures still require the
complete session folder, including `.capture-blobs`.

`release-manifest.json` records the committed source, renderer/resource hashes,
package checksums and validation scope. Component checks do not establish game
FPS, and the manifest distinguishes the checks run for this release.
