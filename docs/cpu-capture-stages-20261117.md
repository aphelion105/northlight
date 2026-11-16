# CPU capture stages, 17 November 2026

This follows [range capture](range-capture-20261117.md). The frozen inputs are
`gameplay-latest.log` and `gameplay-prev.log` under the private
`.local/out/cpu-stages-20261117/` output directory. They precede this build;
their different scenes/settings do not establish an improvement between builds.

The earlier session contains 293 CPU profile samples: capture p95 3.649 ms,
cleanup p95 0.850 ms. Material bounds/pose p95 is 0.411 ms, pose-envelope work
0.347 ms and deferred flush 0.255 ms. These overlapping/independently sampled
spans must not be added. The later log includes menu/empty-world samples.

## Removed repeated work

**Geometry aliases.** A successful complete byte/layout comparison can now be
reused for the same immutable source and canonical owners, declaration, program
and domain. A bounded table holds 64 weak owner pairs. Every new owner still
requires the original complete comparison. Copies carrying an old identity,
changed bytes/indices/layout, pointer reuse and unknown sources retain that
comparison. Reset and entry eviction clear the table; the latter also ends the
entry's COM declaration-lifetime proof. The current native source identity is
retained before private-geometry canonicalization as before.

**Current pose.** A 16-slot certificate table reuses a successfully evaluated
envelope only for the same immutable envelope serial, current constant-bank
epochs and bit-identical camera basis. The producer supplies a certificate only
when its complete CPU bank matches the owning extension device's typed mirror
under the current draw gate. Independently supplied banks and unheld/unknown
readers keep the ordinary evaluator. Dependency blocks, Reset, BOOL/INT writes,
source reconstruction, source-provider replacement and basis changes invalidate
the proof conservatively. A changed pose is evaluated normally. Existing time
admission and the original logical operation charge still apply on every hit.
The envelope's outward rounding and rendered vertices are unchanged.

**Native-cache maintenance.** A separate intrusive chain contains exactly the
entries whose expiration depends on a CPU owner. Frame advance visits that
chain instead of the entire native table when even one legacy entry exists.
Both link fields are charged through the existing entry metadata calculation.
Pending uploads, batch removal, lane transfer, eviction, frame rewind and Reset
retain the same ownership rules.

The additions contain no vertex/index/texture payload or new GPU allocation.
Pose certificates add 4,624 fixed bytes to each x86 envelope-cache object in
the fixture. Geometry alias records contain only weak CPU owners and scalar
keys; their retained control/object allocations are bounded by 64 records.
Native maintenance adds at most 64 KiB of charged x86 metadata at 8,192 entries.
No cache budget, range, quality setting or capture frequency is increased.

## Controlled comparison

`test_cpu_capture_stages.py --baseline-ref 38c664ee --pairs 7` compiles complete
frozen production headers and current headers with the same Zig/O2 options.
It alternates baseline/current order and excludes the first warmup pair.
The initial seven-pair medians were:

| CPU component | Before, total ms | Current, total ms | Factor |
|---|---:|---:|---:|
| 4,096 repeated geometry-alias lookups | 196.447 | 1.901 | 103.3x |
| 1,024 pose requests, eight consumers per current palette/basis | 2.816 | 0.646 | 4.36x |
| 2,048 frame advances, 6,000 certified and two legacy entries | 35.350 | 0.021 | 1,699x |

The last component was already small: about 0.017 ms per frame in this fixture.
These deliberately reuse-heavy component cases do not establish multipliers for
the complete draw hook, every CPU stage, or game FPS. New meshes, changing poses,
different camera bases and independent sources retain their original work.
The fixture checks equal output digests, bit-identical pose bounds against an
uncertified evaluator, all palette blocks, invalid/unheld epochs, camera/source
changes, operation refusal, owner expiration, frame rewind and reset. O0 is a
correctness check, not a performance result. No sanitizer claim is made here.

## Next live measurement

`MATERIAL_GEOMETRY_SOURCE` now reports `poseCertifiedHits` and
`geometryAliasMemoHits`. `CPU cleanup split` reports world, water, surface,
materials, normals and remaining cleanup time on sampled RenderProfile frames;
owner diagnostics remain included. These scopes sit inside existing cleanup,
not alongside it. Off-profile frames add no clock reads for this breakdown.

Admission, constant capture, native submissions, material setup/restore,
effect submission and worker preparation still need their own matched-route
comparison. Their current counters/timers are retained. A component coefficient
cannot substitute for measuring their contribution in the game.
