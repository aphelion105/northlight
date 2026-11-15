# Town, ziggurat and Zangarmarsh QA, 15 November 2026

The two supplied logs in `qa_report/` use build identity
`e71a8209e970431b9909274e9d201728eaa94f90c671861ceceae6a4e52305cf`
(14 November, 13:30:43 UTC) and DXVK 3.1.1. They predate several material,
composite and shader optimizations already shipped in 0.3.218.4. They are useful
for finding remaining bottlenecks, not a benchmark of the current DLL.

| Input | SHA-256 |
|---|---|
| `northlight-renderer (5).log`, town/NPCs and ziggurat | `873d15bdba4a6b33524967482f173d69d6b231021e4a2d970b6de1e53295d574` |
| `northlight-renderer (6).log`, Zangarmarsh rotation | `21759e688244f72e5dc14ebdab594da6a7f39677f191610470dc7d370f919afc` |

Numeric extraction and test artifacts are under `.local/out/qa-20261115/`.
The filenames/scenarios follow the supplied QA description; the first log
contains two world-entry sections. Loading frames with `applied=0` are excluded
from the in-world spike discussion below.

## Where the time goes

Sampled active-effect frames, median / 95th percentile / maximum, milliseconds:

| Work | Town/ziggurat | Zangarmarsh |
|---|---:|---:|
| CPU profile total | 3.460 / 13.187 / 19.198 | 16.138 / 19.976 / 22.614 |
| CPU capture, including material and normal work | 1.099 / 7.899 / 14.409 | 8.859 / 11.837 / 12.436 |
| CPU draw admission | 0.265 / 1.716 / 2.494 | 2.452 / 4.752 / 5.045 |
| CPU effect submission | 0.926 / 2.353 / 4.851 | 2.648 / 4.573 / 7.074 |
| GPU effect interval total | 1.772 / 3.057 / 5.405 | 2.959 / 3.762 / 5.122 |
| GPU native-scene interval | 6.310 / 20.152 / 31.306 | 27.911 / 31.497 / 35.601 |

There are 109/35 CPU and 86/28 GPU effect samples respectively. These are
periodic diagnostic samples, not every frame or an FPS distribution. CPU and
GPU overlap; do not add the table rows. GPU timestamp intervals include gaps
while the CPU feeds commands, so the native-scene interval is not proof that
native shading alone takes 28 ms.

Within Zangarmarsh material capture, the largest median exclusive scopes are
snapshot construction (0.976 ms), bounds preparation (0.899), capture setup
(0.675), pose envelopes (0.493), admission (0.463), deferred flush (0.366) and
constants (0.344). This is consistent with substantial CPU geometry work.
Current code already has rigid-range admission, grass packet batching and
cheaper deferred state comparisons; see the preceding material/cache reports.
Those changes are retained, not counted as new improvements from this review.

The largest median GPU effect in Zangarmarsh is local point-shadow rendering
(0.642 ms, p95 1.239, max 1.993), followed by AO (0.469), WorldComposite
(0.335), fog march (0.304), local lighting (0.244) and fused world lighting/GI
(0.160). Character shadow quality is not reduced by this change. In the first
log fog march reaches 1.262 ms, but does not explain its 140+ ms stalls.

## Queue stalls that remain in current code

Near the end of the rotation log the command-stream interval reports:

- 6.498 ms of producer synchronization per frame, 36.12 synchronous calls per
  frame and 16.71 unshadowed buffer passes per frame;
- 28,596 range stores and **zero range-cache hits** over the run, with only
  0.25 MiB retained out of its existing 8 MiB global allowance;
- two synchronization storms, the later one beginning with 107 calls/frame
  (line 9805). Lock/unlock dominate its attribution;
- whole-buffer refusal totals averaging about 37.34 MiB per refusal, above
  the existing 24 MiB per-buffer large-shadow ceiling. The separate large
  allowance already holds 34.5 MiB out of 36 MiB.

The current `BufferRangeShadow` still retained only one contiguous region.
Writing a separate region discarded the previous exact bytes. Alternating
updates could therefore repeatedly fall back to synchronous Lock/Unlock,
despite a mostly empty range-cache budget. Increasing the full-buffer budgets
would consume more of the 32-bit address space without fixing this policy.

The logs do not contain per-lock offsets: zero hits demonstrates ineffective
reuse, but does not prove every miss was an alternating-region write. The new
test reproduces that specific failure, not a complete replay of either log.

The first log also has applied frames 2352/2457/3944 lasting 141–148 ms, with
106–117 ms of producer-idle time. Late rotation frames 3549–3551 and 3894 have
55–79 ms total time, with long scene/idle intervals. CPU starvation, resource
loading and synchronization must be distinguished from expensive shader work.
This patch does not claim to eliminate every such pause.

## Implementation

Oversized dynamic VB/IB buffers now retain up to eight exact byte ranges under
the existing 4 MiB per-buffer retained-data and 8 MiB global allocation budgets.
Range metadata is allocated only for buffers that use this path. Adjacent or
overlapping writes can extend a range; distant writes retain separate ranges.
All overlapping copies receive the latest bytes. Unknown holes never become
known data merely because the surrounding ranges were written.

Repeated partial writes use owned staging blocks and ordered deferred uploads.
Uncached ranges, readonly locks, allocation failure and GPU-written buffers
keep their existing exact fallback. DISCARD permits undefined bytes only while
all later writes remain retained; eviction revokes that proof. No draw, index,
vertex, alpha coverage, shader or quality setting is removed or approximated.

## Verification and next run

`stream-buffers` is the focused suite:

```sh
python scripts/run_tests.py --suite stream-buffers --strict
```

The CPU fixture compares the previous release commit `43c9f510` with current
code. For 512 partial writes across eight sparse regions, explicit producer
waits fall from 1,024 to zero. Backend bytes are checked after replay. Additional
cases cover randomized partial writes, contiguous appends, overlaps, unknown
holes, DISCARD, eviction, shared budgets, GPU mutation and reentrant pumped
waits. The fake backend's elapsed times are not a game-performance estimate.

The real Windows D3D9/DXVK test renders indexed geometry after 1,024 sparse VB/IB
updates. The frozen implementation makes 2,048 synchronous calls; current code
makes zero. All eight regions have their expected final colors, preserved
positions and indices, and the complete images match byte for byte. This is
an actual GPU image/queue contract, not a measured gameplay FPS gain. Forever
is allowed to remain running during these checks.

In a new rotation run, compare `rangeShadow hits/stores`, `pass[noshadow]`,
`syncMs`, sync storms, CPU capture and frame percentiles with the same settings
and route. A persistently low hit rate would require lock-offset tracing before
changing cache capacity or claiming that the recorded spikes are fixed. The
remaining large CPU capture scopes need a current-build profile because the
supplied old build predates the existing capture optimizations.
