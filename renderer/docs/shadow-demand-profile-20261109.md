# Shadow capture demand follow-up

The observed client log identifies source
`564eb5b385b6d14a6c55125a15fa8df37fcfc440e302da09df70a4b77ba3a958`
(commit `34891add`), built at 2026-11-09 18:52:10 UTC, on DXVK 3.1.1.
The private copy is `out/cage-shadow-perf-20261109/game.log`, SHA-256
`f25fb5896a8b3248ff9ca6b9e17044b1597382af9cbdada51fe6a0b844d141e5`.
Its final frame is 4203. `log-analysis.json` in the same directory retains the
parsed samples.

All 13 sampled world profiles report no directional cascade draw. Seven
samples still capture models; six later samples follow an effects toggle.
The active samples spend 0.398–1.927 ms net in model capture. At frame 2540,
both directional source colors and the selected point-light count are zero,
and all 791 captured packets are unused (0.891 ms net capture). This is
evidence of conditional unused work, not a measured post-change FPS gain.

The log does not establish significant exact-alpha wrapper overhead. Only one
sampled point workload, frame 2667, draws model replays: 12 replay draws and
12 unions. The six `POINT_COLD` samples take 0.006–0.541 ms and report zero
allocation attempts. Larger CPU `point` bucket intervals also include the
atmosphere worker join; they cannot be called point-allocation stalls. The
alpha, depth and scissor wrapper remains unchanged.

The capture decision previously treated the actor-shadow feature flag as
per-frame demand. It now skips model snapshots between GI actor updates when
the current accepted context proves both directional sources zero and no
local shadow source needs them. Unknown celestial/context/map state stays
conservative. Map, equipment and particle point sources, flashes, model-light
pose requirements, explicit capture demand and diagnostics preserve capture.
The existing 200 ms GI cadence, quality limits and shadow failure/retry paths
are retained. Context is accepted once per frame and cleared at `endFrame`,
so a new frame's sun/moon activation re-enables fresh capture.

One existing effects-bucket marker now closes atmosphere work/join before the
point-shadow pass. Its clock-read bound becomes 31 on profiled frames; the
disabled path reads no clock. Sum/span accounting remains covered by the
effects-bucket fake-clock regression.

`out/cage-shadow-perf-20261109/demand-gpu/validation.json` passes on native
D3D9 and DXVK 3: 21 demand-state cases per backend and the existing complete
point-shadow controls (cube images, prewarm, batching, skinned sources,
partial cycles, reset and failure behavior). The report explicitly records
`timing_valid=false`; concurrent game execution was allowed only for
correctness. CPU particle/quality tests pass, as do capture wiring,
snapshot-revalidation and effects-bucket O2/sanitizer tests in
`linux-final/test-output/20261109-212031/`.
