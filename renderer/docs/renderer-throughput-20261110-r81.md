# r81: reuse the current draw's geometry for shadow capture

Baseline: r80, `e146ffa2ff559f97f165d5880ce5c905932c371e`.
Upstream main, fetched for this comparison:
`e19ffee601acbad2b991917109bcb6d8b2f7d5ca`.
Measured and production build source identity:
`6b398f2183a442d92a7ebe485fe3ffe0bf5a15caf2fb7de5ebd34ee8954ae5e5`.

## Change

Buffered model draws pass the immutable raw geometry already captured for
materials/normals to shadow capture. A resident, write-tracked snapshot can
reuse the existing source-pool identity index and its exact mesh object,
avoiding a second set of stream/index queries, metadata lookup and key build.
The caller supplies it only within the same serialized draw invocation.
Declaration, draw arguments and tracking providers must match. An evicted,
reconstructed, untracked or mismatching source takes the ordinary read path;
the current native declaration is still queried and failures still stop capture.

Both routes call the same resident-entry service function, preserving byte
charges, regular/priority draw limits, near reserve and LRU behavior. Material,
normal and shadow admission, pose, alpha and visibility calculations remain
independent. No new cache, mesh fields, texture, mask, shader, setting or quality
change is introduced. Sampled model metadata now reports `drawHandoffs`.

## Actual capture CPU comparison

Ryzen 9 9950X3D / RTX 4090 / DXVK 3.1.1. Three ABBA rounds, six runs per side,
32 measured frames after 16 warmup frames. Each frame acquires a source and
captures 512 repeated rigid draws through production WorldRenderer, including
packet recycling. Medians of run medians, ms:

| Control | Before | With handoff |
| --- | ---: | ---: |
| Clean upstream | 0.17808 | 0.13595 |
| Clean r80 | 0.17228 | 0.13673 |
| Same executable, handoff disabled/enabled | 0.18025 | 0.13625 |

This saves about 24% versus upstream and 21% versus r80 in this fixture.
Geometry queries fall from 3,072 to 1,536 per frame; all 512 packets and the
884,736-byte shadow admission charge remain identical. A separate ABBA control
without a handoff on either side gives r80 0.17613 and r81 0.17518 ms: no
measured ordinary-path regression.

Captured packet bytes and six 256x256 R32F point-shadow faces are bit-exact
before/after. Native D3D9 and DXVK 2/3 pass, including missing-pool,
reconstructed-object, declaration-mismatch and declaration-query-failure controls.
Rasterization here checks correctness; these are **CPU capture timings**.
The fixture excludes material processing, skinning and real model diversity.

## Common 4K graph versus upstream

Actual renderer/shaders, frozen 3840x2160 scene/depth, authored caster geometry,
regional fog and resident GI probes; SDR. Three ABBA rounds, six runs per side,
32 measured frames after 16 warmup frames. Valid native M2 lamps are supplied
to both versions; the shadowed mode explicitly enables one PCF point shadow
and asserts a usable cube/update on every measured frame.

| Workload | CPU upstream → PR, ms | GPU upstream → PR, ms | Draws upstream → PR |
| --- | --- | --- | --- |
| AO/blur/composite | 0.02105 → 0.01308 | 0.54067 → 0.54244 | 7 → 7 |
| Regional fog | 0.02943 → 0.02180 | 0.84890 → 0.73500 | 12 → 11 |
| GI, fog, directional/point shadows, lamps | 0.04588 → 0.03553 | 1.09792 → 1.01709 | 31 → 24 |
| GI, fog, lamps, shadows off | 0.03295 → 0.02458 | 0.99610 → 0.89446 | 17 → 15 |

Base GPU differs by 0.00177 ms / 0.33%; the other modes improve by about
13.4%, 7.4% and 10.2%. This is equal-input/settings comparison: existing PR
lighting/fog semantics differ from upstream, so upstream output equivalence
is not claimed.

The previous r80 fixture left upstream point shadows at their disabled
default, used authored lights excluded by its point selection, and supplied
daytime weighting that skips its night-only cube path. Its shadowed row did
not establish an equal point-shadow workload. The corrected row above
supersedes that comparison. Selection fade is advanced deterministically;
smoke checks fail before further builds if the shadow workload is missing.

Against r80, all four GPU modes remain within 0.06%, with the same draw/copy
counts and bit-exact final SDR images across all repeated runs. The new CPU
handoff does not change the screen graph. The cumulative GPU improvements
include the existing shared lighting/GI draw and cached point-face reuse.

## Limits, verification and reproduction

These separate fixtures must not be added into a total frame cost. The graph
excludes native draw hooks, model/material capture, real caster density,
HDR/weather and presentation. GPU timestamps may include submission gaps.
**A total 2–3 ms Northlight budget is not established.** Available QA/client
logs still identify r73; no gameplay log of this build is available.

Fourteen related regression scripts pass, including 3,840 budget comparisons,
16/32-bit indices, sparse streams, nonindexed draws, 3,072/4,096 draw caps,
write generations, eviction, allocation recovery, lookup collisions,
prediction counterfactuals, terrain routing and ASan/UBSan. Additional x86
O2/O0 shared-source lifetime and material-geometry checks pass. No client
files are changed and no game is launched.

```powershell
python tests/benchmark_capture_handoff.py --upstream $upstream --baseline $r80Checkout --backend $backend --verify-backend C:/Windows/SysWOW64/d3d9.dll --output $newCaptureOutput --rounds 3
python tests/benchmark_upstream_pipeline.py --upstream $upstream --baseline $r80Checkout --capture $capture --backend $backend --output $newPipelineOutput --rounds 3
```

Receipts: ignored `out/optimization-20261110-r81/`, particularly
`capture-native-lamp/validation.json`, `fallback/validation.json`,
`pipeline-comparison/validation.json`, `pipeline-comparison/pixel-validation.json`,
`regressions/validation.json`, `lifecycle.log`, `material-geometry.log`
and `environment.json`. The checked environment had no WoW process running;
no game process was stopped for these tests.

Production Zig 0.15.2 build and Windows package
`0.3.217-test.20261110.6` pass compiler preflight, shader freshness,
32-bit PE/17-export checks, payload equality and package verification.
Build time: `2026-11-10 02:58:58 UTC`. Receipts: `artifacts.json` and
`package-verification/verification.json` in the same ignored output directory.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `Northlight-0.3.217-test.20261110.6/d3d9.dll` | 10,847,744 | `2247cc4c65183f60610bd8751a7d3e54fd99921eab5e53b8b4a66c3c2b344712` |
| `Northlight-0.3.217-test.20261110.6-Windows.zip` | 20,818,516 | `6ed05219261d856ea3de3ddccec797c37bbe2fae3ca075cb9f459325f5778b74` |
