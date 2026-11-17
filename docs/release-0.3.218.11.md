# Northlight 0.3.218.11

Capture/cache optimizations and a temporal-fog fix for moving foliage.

## Changes since 0.3.218.10

- Reuse geometry captures across disjoint VB/IB writes, proven geometry aliases
  and current-pose certificates. Reduce native-cache maintenance under entry pressure.
- Keep displacement height fields in DEFAULT GPU resources and release unused
  CPU float arrays after upload. Preserve exact CPU fallback and device Reset.
- Avoid redundant initialization of cold capture buffers while preserving their
  amortized growth. Keep declaration keys compact and select LRU victims without
  scanning the cache.
- Preserve attempted-material refusal keys for the whole frame allowance.
  Pointer collisions no longer spend repeated cold turns on the same failures
  and starve unrelated valid materials.
- Reject foreground fog history when the current sample is clear sky, preventing
  fog trails around animated trees and foliage.

Component measurements and their limits are documented in
[capture stages](cpu-capture-stages-20261117.md),
[GPU height ownership](cpu-gpu-ownership-20261117.md),
[pipeline buffers](pipeline-buffers-20261117.md),
[material refusals](cache-refusals-20261117.md) and
[fog history](shadows-and-tree-history-20261117.md).
They do not establish a gameplay FPS multiplier. The release manifest records
the exact commit, renderer/resource identities, package checksums and validation.

## Download and update

The release includes Windows, macOS and Linux installer archives. macOS/Linux
archive contents are verified on Windows; gameplay on those platforms has not
been validated here. Existing personal settings are preserved by the installer.

Close the client before updating. Run `Install.cmd` on Windows,
`Install Northlight.command` on macOS, or `install.sh` on Linux.
For a manual renderer update, replace both `d3d9.dll` and
`northlight-assets.bin`; they are a matched pair. Use `SHA256SUMS.txt`
to verify the downloaded files. Compact captures still require the complete
session folder, including `.capture-blobs`.
