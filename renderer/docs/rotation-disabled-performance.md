# Northlight: terrain retention and exact actor evaluation

Fresh client run ending 23 October 2026 at 00:50 local time. The user installed `1404591678bee9f2d265efed1b7cf1c6966bce46`; this investigation did not launch the game, install a DLL or modify settings/assets.

The largest sampled material-Off hitch is CPU terrain preparation: **11.520 ms** at context frame 11684, with 525 cache misses and 531 byte-pressure evictions. Other views hit the independent 4096-entry ceiling while memory remains available. The new build retains more exact snapshots within the same fully charged 32 MiB ceiling. A second change specializes two stock actor skinning programs without changing their results, eligibility, update cadence or rendering quality. All production shaders remain unchanged.

## Evidence and comparison limits

Fresh log SHA-256: `abedd725589925ae40468c3dc7b8d55db8ac8d7231bee4dc28c787160791687d`. Previous log: `c7d91da6b7154546a3f70d6f85ded70733f041ec3f5cd673526c407a14a50689`. Installed DLL: `6fc8d53526aad3cbde780c65ec1d3521aaeadd22ca5808992d1cf0ebca3621ab`. Immutable inputs and hashes are in `validation/inputs.json` in the DLL package.

Materials are sampled On at frames 0–10414, Off at 10541–12319, then On at 12446–12700. Only **13 of 86 master-On samples actually submit materials**. World capture has 28 samples; world GPU profiling has 27, starting at frame 9398. Early login/menu samples cannot represent enabled in-world performance. AO and water-mask activity change later in the run despite identical saved quality settings. Rotation paths and visible sets also differ between runs. The new log therefore proves where work occurs and whether previous fixes operate; it does not establish a controlled FPS delta.

Previous terrain source-index sharing works: the final working set has 1182 unique blocks for 4096 references, avoiding 6.445 MiB of duplicate storage. Inactive native material GPU timing also works: all 15 Off samples suppress those spans, with query CPU median 0.0001 ms, maximum 0.0004 ms, versus median 0.2163 ms in the preceding run's Off interval. Native CPU reference scopes and global GPU profiling remain.

## Foreground bottleneck and implemented terrain fix

| Context frame | Materials | Terrain preparation | Misses | Evictions / cause | Retained bytes |
|---|---|---:|---:|---|---:|
| 9652 | On | 7.918 ms | 373 | 373 / entry count | 33,116,772 |
| 9779 | On | 11.266 ms | 562 | 562 / entry count | 31,526,644 |
| 10668 | Off | 1.078 ms | 6 | 6 / entry count | 32,849,280 |
| 11684 | Off | 11.520 ms | 525 | 531 / bytes | 33,547,592 |

At 9652/9779 unchanged source contracts are evicted because there are already 4096 entries. At 11684 the byte ceiling causes replacement. These misses require source reads, snapshot construction and subsequent GPU arena work even with materials Off. At 10668, 589 actual buffer revision changes are efficiently revalidated, reusing 581 snapshots; reading a genuinely changed source is required for correctness.

The entry ceiling becomes **8192**, with fixed slot arrays still charged inside the unchanged **33,554,432-byte total ceiling**. Exact source byte offsets now use Northlight's existing 16/32-bit index container: offsets through 65535 use 16 bits, larger offsets use 32 bits. Calculations retain 64-bit overflow/range checks, and actual allocated storage is charged. Full source/layout keys, lifetime/revision validation, immutable snapshots and fallback reads remain. No positions, triangles, coverage, attributes or precision are removed.

A controlled x86 fixture compares frozen `1404591`, an 8192-slot-only variant and the final implementation. It rotates through yaw sectors and revisits draws in reverse order, checking complete geometry digests:

| Fixture | Baseline warm misses | Final warm hits / misses | Source rereads eliminated | Repeated vertex preparation eliminated | Final retained bytes |
|---|---:|---:|---:|---:|---:|
| 4800 one-chunk draws | 5632 | 9600 / 0 | 28,182,528 B | 16,332,800 B | 19,967,976 |
| 4200 two-chunk draws | 1296 | 8400 / 0 | 12,985,920 B | 7,516,800 B | 32,559,912 |

The slot-only variant still has 1328 misses in the byte-bound case, demonstrating why both changes are necessary. Geometry digests match across all variants and five repetitions. Whole-fixture CPU medians, including cold construction and digest work, improve **135.112→75.595 ms (44.05%)** and **156.397→129.096 ms (17.46%)**. These are component measurements, not game FPS or per-frame savings.

## Material and actor work

Normal/height maps and material source geometry are warm in the active material samples; repeated map generation is not the observed cause. Active material samples have median pose fallback 0.5708 ms, envelope work 0.2910 ms and remaining bounds work 0.4716 ms. The mode labels alone do not prove displacement execution: recorded GPU displacement height draws are zero throughout this run.

The captured settings have a 48 yd normal range and a shared 8 yd occlusion range; cavity and self-shadow are disabled. This run therefore cannot establish their visual effect. The optimization does not alter those settings or extend rendering beyond their existing admission rules.

Actual client shaders include a six-operation single-bone program and an eighteen-operation weighted program with its first multiply operands reversed. They previously fell through to the generic CPU evaluator. Exact evaluators now preserve operation/operand order, definitions, SM1 floor versus SM2/3 round behavior, unsupported fallbacks, failure outputs and aliased outputs. Strict evaluation keeps the generic path for the new templates. Existing `kernel.skin` remains the only original envelope eligibility flag; the new evaluator flags do not authorize a different bounding approximation. Pose/read budgets, current transforms and update cadence remain unchanged. The shared evaluator also serves background actor/GI work.

Completed fallback vertex counts now distinguish generic, single-bone, commuted weighted and existing-template evaluation. These counters add no clocks and will show the route used by the next real client run. The accompanying material report contains the final exact-output corpus and uncontended component timings.

The stock SM1/2/3 corpus contains 5594 programs, with 5136 compiled position programs, 660 single-bone matches and 175 commuted weighted matches. Both optimized and debug runs pass **714752 comparisons each**, including 135606 identical failures, 357376 Strict cases, 89344 aliased-output cases and 156032 new-template output-byte checks including failure outputs. Original envelope eligibility and metadata sizes remain unchanged.

Uncontended paired medians for 8192 vertices improve single-bone cases from 0.540–0.673 ms to 0.100–0.131 ms (79.9–81.5% less) and the commuted weighted case from 1.797419 ms to 0.249469 ms (86.1% less). Controls are disclosed: unmatched rigid work changes 0.305963→0.310113 ms (+1.36%, 4.15 microseconds), and the existing weighted path 0.232456→0.233200 ms (+0.32%, 0.744 microseconds). An earlier multi-flag dispatch experiment had an 8.6% rigid regression; it was replaced by the final single-byte discriminator and its earlier benchmark log is superseded. No percentage here predicts whole-frame FPS; new route counters must establish actual client usage.

## Three independent directions: five topics each

| Direction | Five investigated topics and disposition |
|---|---|
| World / GI / streaming | Entry and byte pressure: implemented; source-index sharing: verified, broader interning unjustified; actor decode/pose/hash/BVH: shared exact evaluator addressed separately; static geometry/page construction: background cost, unsafe stale reuse rejected; retirement/handoff/upload: bounded, lifetime/fence policy retained. |
| Materials / deformation | Inactive timestamp suppression: verified; current-pose bounds/envelope/fallback: exact single-bone and commuted weighted evaluators implemented; deferred capture/source/resident/state reuse: measured; normal/height map generation and retention: warm, no rebuild fix justified; displacement/POM/native admission: traced, per-route counters added. Final details and measurements are in `material-report.md`. |
| AO / fog / postprocessing | AO blur/history and rotational stability: retained; FogMarch/cloud cost: measured; split FogBlur/composite attribution: verified; water-mask receiver dependencies: retained for correctness; FogBlur receiver-distance common-expression experiment: rejected after GPU timing regression. |

The world worker's expensive actor and static build numbers are background job timings, not per-frame foreground costs. Settled warm terrain captures read/upload zero source bytes and take approximately 0.305–0.315 ms. Cache retention targets revisit churn; first-visible geometry and real source mutations still require work. Complete five-topic reports and raw evidence accompany the package.

## Why AO or query polling was not changed

At the largest Off hitch, frame 11684, replay DrawIndexedPrimitive totals 15.4852 ms, original native draw CPU totals 0.2780 ms, and GameScene reports 13.284 ms while AO is absent. The query trip is 17.6324 ms: 14.9492 ms before replay, 2.6617 ms in the query task and 0.0215 ms afterward. Terrain preparation explains most of the submission delay. These overlapping CPU/GPU/query intervals must not be added; GPU timestamps include command-stream gaps and resource dependencies.

Ordinary postprocessing costs are led by FogMarch around 0.88 ms and clouds around 0.66 ms. The new split FogBlur timing works. When water-mask redraws change from zero to 29 later in the run, blur/composite and returning AO costs also change. Those passes need the correct receivers even when cosmetic water is disabled. Bypassing the mask or changing alpha/lifetime policy would change output.

A new FogBlur common-expression candidate reduces static slots from 347 to 303 but is slower by 13.9–16.5% in repeated dry cases. Its 594 matrix images plus 36 repeats are byte-exact, but it is **excluded from this DLL**. Fewer instructions alone are insufficient evidence of a speedup. Existing AO/fog shaders, samples, ranges, history, MSAA and material quality remain unchanged.

## Verification and next client run

Terrain tests cover optimized/debug behavior, real 8192-entry pressure, lowered byte caps, 65535/65536/131070 offset boundaries, unknown/tracked revisions, immutable lifetime, allocation failure and last-reference accounting. Eight affected suites pass, including 3600-frame arena equivalence. Eighty allocation-failure positions leak neither allocations nor cache charge. Existing source-index sharing regressions also pass. Windows debug runs are not advertised as sanitizer coverage.

The new world log lines append the actual renderer frame, the pressure-adjusted byte limit and compact/wide offset storage, eliminating ambiguous joins in future logs. Production render coverage and source validation remain intact. The final integration record supplies the x86 release, export, exact material capture and final DLL hash checks.

For the next run, hold materials, AO, clouds, water settings, camera radius and the rotation path constant. Inspect count/byte evictions, misses, source reads, replacement/upload bytes, terrain CPU and the new actor-route counters. A working set larger than 32 MiB can still require eviction. New in-game FPS remains unmeasured until the user tests this DLL.

Final integration passes: x86 PE32 release with all 17 original exports; 140 M2/terrain/WMO baseline capture comparisons; exact actor oracles and shared regressions; eight affected terrain suites; six AO/post CPU regressions. The 630 byte-exact FogBlur candidate images are rejected-experiment evidence, not a shipped shader optimization. DLL SHA-256: `962fc0df155949bcbf5b480957e8ca9e69564e984f4521e8637995704996ea66`.
