# Installed renderer and local work audit, 13 November 2026

The audited production tree was `ffad4335affd1a95ded7bbf474d703695fc19dd0`.
Before the shadow-history fix described below, the installed DLL exactly matched
`renderer/frd9.dll` and contained the current source identity
`3d5fc76ec52cf4e85cb2fb5dfb82787b1be5c22faff088b1a4a3a8fb8a41ce96`.
The latest game log reported that same identity. The DLL/resource pairing also
passed verification. This establishes which code ran; version text alone does not.

Evidence is preserved under `.local/out/test-output/performance-20261113-latest/`:
the two logs, quality settings, installed-build-audit.json, worktree inventories,
source hashes, exceptional-source-diffs.json and unreachable Git commit inventory.
These are ignored local artifacts; retain them when cleaning outputs.

## Scope and findings

The inventory covered 28,405 C++/header/inline/HLSL/Python/patch candidates in
`.local/out`, `.local/worktrees`, `.local/runtime-data` and `.local/test-fixtures`,
excluding third-party tools and compiler caches. Of 27,561 candidates sharing a
tracked basename, 24,560 were byte-identical to the current file. Production-path
classification found 7,419 current copies, 1,123 reachable historical copies,
612 local-only copies and six other-ref copies. The exceptional content variants
were compared against the corresponding production files. Old baselines, negative
controls and generated asset forwarding headers are not missing implementation.

The audit also inspected ten worktrees, older root `out/`, `releases/` and
`qa_report/` locations, 101 non-ancestor non-merge commits and an inventory of 167
unreachable commits. The latest unreachable material-preflight commit `cb8ae250`
retains the same material/replay implementation as HEAD except the later native
capture NOP guard; its shadow differences are the subsequently integrated variable
far-radius fixes. Unreachable history is evidence to compare, not a newer release.
This is an inventory/hash/diff and targeted call-path audit, not a claim to have
executed every old branch or read every candidate line.

No missing production material-capture optimization was identified in this scope.
The only production basenames found solely in old copies were
`native_fixture_shadow.h` and `wmo_lamp_shadow.h`: their emitter-geometry exclusion
was deliberately removed at the user's request. Do not restore it as lost work.

| Area | Evidence in the current implementation / session |
|---|---|
| M2 material preflight | `fcbedd92` is an ancestor. Core water/material/replay/model-normal files are unchanged since it, except the later native capture guard. |
| Early material range rejection | Renderer frame 1270: 312 early rejects; coarse preflight 541 attempts, 32 valid/rejected, 0.1001 ms budget. |
| Snapshot handoff | Same frame: 799 normal draws reused 799 source handoffs, zero additional source-read bytes; 709 compacted draws, 286 merged, 513 submitted. |
| Pose metadata and constants | 1,323 metadata hits, zero misses; 1,612,512 constant bytes avoided, zero mismatches in that sample. |
| Model-normal MRT | Present, but this session's MSAA target rejects it with `reason=target-msaa`. This is a fallback, not missing code. |
| Shared POM | Present but deliberately disabled by default after earlier memory/cost measurements; do not enable solely because an old experiment used it. |
| Directional shadows | Cached static maps, instanced casters, replay pose/state reuse, bounds culling, near/far cadence, actor map and static direction crossfades are present. Cache/replay counters prove the paths ran. |
| Fog | Imported atmosphere loaded: 216 placements, 2,374 keys, 17 outlines. Current no-noise/actor/transition variants remain compiled. |
| HDR / Outland / moon | The prior HDR foreground/emissive paths remain; the later planet classification, preserved expansion lighting and moon size/glow fixes extend those paths. See the sky and Forever audit documents. |

One historical behavior did change: `44765ea8` introduced adjustable shadow
contact filtering with a default one-texel radius, replacing the older 2.5/2
near/far radii. Older comments about 25/16 base taps and a 15% maximum single-tap
change no longer describe the default filter. This is a quality/performance
tradeoff, not an absent compiled shader. No default was changed during this audit.

## Performance limits and next target

The saved session ran at 3840x2160 with MSAA 2. Materials were enabled in an
earlier phase and disabled in a later phase; those phases are not a matched A/B.
Across the mixed samples, effect GPU time had median 3.734 ms; AO 0.770 ms,
world composite 0.483 ms, removal smoothing 0.466 ms and fog march 0.242 ms.
Material-enabled CPU capture spikes reached roughly 4.8–5.3 ms, despite the
existing early-reject and snapshot optimizations. Diagnostic capture frames and
loading/teardown outliers must be excluded from gameplay performance comparisons.

AO is the largest stable individual effect kernel in this log. Material capture
still merits profiling with the same scene/settings, but restoring already
executing optimizations will not solve that cost. The shadow correction below
does not establish a game FPS improvement or justify lowering rendering quality.

## Current follow-up

See [the lunar-shadow investigation](shadow-history-20261113.md) for the proven
history sampler-state bug, GPU regression and remaining visual confirmation.
See [the Forever data audit](forever-data-audit.md) for the preserved extraction
and fields still absent from the importer.
