# r70: capture demand and cold-entry follow-up

## Fresh local input

`out/perf-20261109-r70/game.log` identifies source
`564eb5b385b6d14a6c55125a15fa8df37fcfc440e302da09df70a4b77ba3a958`
and the production build from 18:52:10 UTC. Its SHA-256 is
`f25fb5896a8b3248ff9ca6b9e17044b1597382af9cbdada51fe6a0b844d141e5`.
It ends at frame 4203 on DXVK 3.1.1 / RTX 4090. The user confirmed that this
route is different from the castle video. Settings previews and the later
effects toggle divide the run; its samples are not a matched before/after
measurement of these changes.

At frame 2286, total model/world capture is 8.229 ms CPU. The normal journal
queues 1,330 packets and submits 868 replay draws after merging. The recorded
GPU intervals are 20.343 ms for GameScene, 3.956 ms for ModelNormals and
2.382 ms for Effects. Frame 2413 queues 1,133 packets, submits 765 and reports
3.149 ms for ModelNormals. Source geometry reads are already zero. GPU
intervals include command-supply gaps and are not exclusive hardware busy
time; nested CPU/GPU scopes must not be added.

All sampled MRT frames fall back, initially on `blend-srgb` and subsequently
on `fixed-pixel`. This change does not remove these necessary coverage
refusals or replace whole-frame replay with an unsafe partial result.

## Selected CPU work

See [shadow-demand-profile-20261109.md](shadow-demand-profile-20261109.md).
The accepted current frame can prove that both directional sources are zero.
With no local shadow source, pose consumer or explicit demand, snapshots are
unnecessary between the existing GI actor updates. At frame 2540, all 791
shadow packets are unused and net model capture is 0.891 ms. The new demand
predicate retains unknown contexts, sun/moon transitions, point/equipment/
particle/flash lights, GI cadence, diagnostics and current-pose requirements.
This is conditional work elimination, not a measured game-FPS gain.

After an MRT transaction has been cancelled, the shared normal native-draw
wrapper now submits directly. It no longer queries depth-write state or the
depth surface for subsequent draws outside the receiver domain. Disabled,
unsupported and permanently refused MRT paths share this guard. The next
frame still begins its ordinary probe/recovery transaction.

The regression extracts the actual production wrapper and admission
predicate. It checks zero state reads, original HRESULT/single submission,
1,000 post-cancellation calls and recovery. The frozen pre-change wrapper
fails this check. The independent lighting-consumer cases still retain
normal capture with materials absent, disabled and enabled.

The existing CPU bucket now closes atmosphere preparation/join before point
shadow rendering. An atmosphere wait can no longer appear as point-map
allocation time. The profiled bound is 31 clock reads; the disabled path
still reads no clock.

## General bounds reuse and GPU work

A new rigid position-program key can now reuse the ready local XYZ envelope
of the same immutable geometry. The existing source-hash bucket only
shortlists candidates: exact identity/owner, position binding, stride and
vertex/index counts still have to match. Each key still evaluates its own
position program with the current constants and camera. No world-space
result, geometry buffer or pose is copied from another draw.

The optional lookup runs once per new entry, visits at most eight candidates
and charges the existing operation/time budget. An unsuccessful search
cannot repeatedly starve the source scan. Only the eight-float source envelope
is copied; the existing entry and byte limits remain. `rigidPeerHits` is
added to the existing sampled bounds log to establish applicability in later
real runs.

The real-program experiment covers 117 stock WMO vertex programs across ten
families. Their two distinct position keys retain identical evaluated outputs
and bounds enclosing every source vertex. With a ready peer, source scans
fall from two to one and built vertices from 192 to 96, with the same two
entries. A one-vertex-budget readiness case fails on the original header and
passes on the new header. Collision, lifetime, stride, offset, admission and
32-incompatible-peer starvation controls are retained.

The independent GPU comparison uses the opaque part of the stock barracks:
34,004 triangles, nine authored lights and 54 cube faces. Ready bounds reduce
submitted draws from 54 to 12 and triangle exposure from 1,836,216 to 408,048.
All 54 R32 face images match bit for bit, with 7,250 occupied pixels, on native
D3D9 and DXVK 3. This proves removal of redundant GPU submissions in that
authored workload. Nonopaque geometry, a complete game scene and the video
are outside this oracle.

The first source scan is still required when no compatible peer is ready.
The old Westguard log's 186 unbounded packets are not all assumed to qualify
for this optimization. No actual game-frame saving or isolated GPU timing is
inferred from these deterministic counts.

## GPU candidate rejected

An isolated GTAO candidate moves horizon arithmetic behind a proof that at
least one sample has nonzero weight. It preserves depth sample counts,
quality and the existing sampling pattern. On DXVK 3, the authored FP16/FP32,
even/odd grid, depth-partition and MSAA matrix passes 1,152 byte-exact cases.
The separate 4K stand also matches all nine images.

The user requested tests alongside the other running WoW client. Paired
elapsed measurements therefore remain contaminated, not isolated performance
acceptance. They also show no consistent win and a substantial near-surface
regression. The candidate is rejected: production AO source, approved
bytecode, sampling budgets and acceptance hashes are unchanged. Private
prototype sources, compiler metadata, images/counts and measurements remain
under `out/perf-20261109-r70/ao-prototype/`.

## Deferred castle report

The user deferred the specific castle-video issue after the additional
playback attempts. No further reproduction or video-specific fix is claimed
in this iteration. The general rigid-source cache optimization is evaluated
separately from that postponed report.

The earlier RTX 3080 capture contains an independent Westguard-region segment
before its later Gjalerbron section. It is useful evidence for entry-related
CPU/GPU work, despite not being synchronized to the 14-second YouTube clip.
The current RTX 4090 route is not substituted for that recording.

The full YouTube stream remains unavailable: the URL-aware Chrome player has
no buffered video and never reaches a decoded frame. New direct-stream and
browser attempts are recorded separately. No claim of having watched the
complete clip is made.

Private evidence and the cold rigid-bound experiment are described in
[castle-entry-bounds-20261109-r70.md](castle-entry-bounds-20261109-r70.md).
CPU producer work outside measured wrapper spans
must not be reclassified as EVENT waiting or fixed by weakening GPU fences.

## Validation and build

Focused Windows regressions pass all ten named capture, demand, bounds,
point-shadow, scene-entry, settings and shader-contract tests. The material
envelope kernel suite passes separately with its O2 and ASan/UBSan variants.
The new peer lifetime/collision/budget test also passes under ASan/UBSan.
Linux Clang checks pass replay bounds, effects-bucket accounting and capture
skip/revalidation; the actual native-normal wrapper test passes directly on
both hosts. This is a focused follow-up, not a repeated full repository suite.

Native normal MRT retains exact RGB/normals, complete rollback and restoration
in 4,698 frames on each of D3D9 and DXVK 1/2/3. The demand cases and complete
point-shadow regression retain their original pixel and failure checks.
The model-normal and rigid-peer regressions both reject their old-code
counterfactuals. No acceptance threshold, sample count or quality setting was
reduced.

The production DLL and `0.3.217-test.20261109.4` Windows ZIP were built through
the normal pinned Zig 0.15.2 script, including shader freshness gates. Package
verification passes, and ZIP `payload/d3d9.dll` equals the production DLL.
No client DLL was installed or game launched by this task.

Source identity: `32ceb6065731e074cc304ab3254e1e268000d12532967e98fc9ece81af297d59`.
Build time: 2026-11-09 19:40:51 UTC.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `renderer/frd9.dll` | 10,833,408 | `779803cc1baa371ab3571f9ded6c4b256ebaf11e873db9346cb7451278cd7a82` |
| `releases/Northlight-0.3.217-test.20261109.4-Windows.zip` | 20,810,421 | `e3e1396267fd8ec2efe683b907f567c762c77f4ecabc0be9c1e05e232affa3ab` |

The [machine-readable receipt](renderer-throughput-20261109-r70-validation.json)
records the focused evidence hashes, deterministic work counts and the
concurrent-game and deferred-video limitations.
