# Inventory atlas investigation

The reported corruption is not yet reproduced or fixed. The installed c9269f9 DLL
SHA256 is `0d02a3f048c9ee0ce686beac215d9c4607bd428de9f9103e80949ebb4b9701b5`.
Its newly installed glow archives contain M2 only, no BLP/DDS. All 6,308 icon
providers resolve to stock locale archives; eight sampled BLPs are byte identical
to stock and decode correctly. The intact hearthstone is DXT1 and the intact
pumpkin DXT3, so the screenshot does not establish a format-only failure.

The pinned build-12340 primary source has a native square-icon atlas path:
[Texture.cpp](https://github.com/SatyPardus/wowdecomp/blob/b1c71b8bd108823bca952f16c6f83a7ad33b9648/Engine/Source/Services/Texture.cpp),
[CGxDeviceD3d.cpp](https://github.com/SatyPardus/wowdecomp/blob/b1c71b8bd108823bca952f16c6f83a7ad33b9648/Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.cpp).
The native upload uses the locked surface's returned pitch. This investigation
does not copy that source or game assets into the repository.

## Reproducible checks

`tests/validate_ui_atlas_gpu.py` uses a hidden SDK window and the actual local
DXVK D3D9 backend; it never launches the game. It covers DXT1/3/5 managed 512px
atlases, 64 derived-surface partial tile writes, repeated and reversed writes,
reusable source memory, shadow eviction/readback, NO_DIRTY_UPDATE/AddDirtyRect,
and SYSTEMMEM-to-DEFAULT UpdateTexture. A copied installed proxy can be supplied
with `--proxy`; its config/backend must already be beside the helper executable.
The baseline backend/stream and copied installed DLL tests checked 9,437,184
pixels without a mismatch. The new diagnostic also compares actual GPU pixels
with the independent BC decoder and verifies sampler restoration and a deliberate
source-hash mismatch. These checks do not reproduce world-to-UI render history.

`test_atlas_low_memory.cpp` runs through the real StreamDevice implementation
with deterministic allocation failures and an independent-pitch fake backend:
fresh shadow OOM falls back to staging; written-shadow OOM falls back to raw
LockRect; admission refusal plus staging OOM also falls back to raw LockRect.
Original rect, flags, backend pitch and bytes survive, at O0 and O2.
An injected asynchronous backend E_OUTOFMEMORY demonstrates an existing blind
spot: the game already received successful Unlock, but replay rejected the write.
The diagnostic preserves the attempted hash, HRESULT and resulting mismatch.
This is a proven failure mode, not evidence that it caused the user's screenshot.

## Ctrl+Shift+F12 capture

With Diagnostics enabled, the existing shortcut additionally captures up to four
recently drawn candidate atlases, at most four keypress captures per device.
Metadata is bounded to 32 managed 512px DXT1/3/5 textures identified by actual
64px tile uploads. Only hashes and draw metadata are retained during play.
No periodic GPU readbacks, COM references or texture byte cache are added.

Files under `northlight-diagnostics/ui-atlas-<pid>-<tick>-<index>`:

- `.json`: per-tile attempted CPU-upload FNV1a64, raw-content hash, failure HRESULT
  and counts, shader hashes and known original sampler states.
- `.bc`: raw level-0 block-compressed content from an explicit READONLY lock.
- `.fgr`: FGR1 header and actual point-sampled GPU BGRA pixels.

Source/raw mismatch narrows the fault to rejected/changed upload content. Matching
source/raw with raw/GPU disagreement narrows it to the decoder/backend sampling
path. Both matching directs the next investigation toward the original UI draw's
UVs, shader, sampler/state or geometry. The diagnostic uses its own known state;
it does not prove that the original UI draw's state was correct. GPU writes and
unobserved pass-through writes invalidate source knowledge. Unknown tiles and
unmatched resources remain explicitly unknown. No diagnostic exception disables
the renderer; state is restored after successful and failed captures.
