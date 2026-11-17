# CPU GI: rejected probes and visibility work

For 6–64-ray static solves, collect the first hits before indirect transport.
The existing back-face and nearest-distance rules reject unusable probes.
Rejected probes keep their exact sample counts, moments and zero SH without
computing secondary bounces or light shadows. Valid probes reuse their first
hits. Moving geometry and larger sample counts retain the original solver.

The 4 MiB relight cache retains rejected probes across lighting changes.
Geometry reset/release, position, seed, ray count, reach and slot ownership
still invalidate reuse. Rejection is independent of sun/sky colours, world-up,
point sources and bounce settings; `invalidAcrossLight` counts these hits.
The additional geometry epoch stays within the existing cache budget.

Local visibility uses an alpha-tested closest-plane query instead of computing
material colour and smooth normals. A shared direction table also removes
bit-identical duplicates on the octahedral boundary. Moving overlays use the
same plane query. Original hit/tie selection and uploaded FP16 planes agree
with the full shaded reference, including shared BLAS/TLAS and fallback trees.

Paired x86 component measurements against `5a7daa33`, reference/candidate/
candidate/reference, ten rounds per process, four solve threads, 512 probes,
32 rays and three bounces on the same 512,401-triangle Northrend fixture:

| CPU work | Before, ms | After, ms | Speedup |
|---|---:|---:|---:|
| Cold GI, probes near surfaces | 21.26 | 17.09 | 1.24× |
| Cold GI, world grid | 10.17 | 6.23 | 1.63× |
| Response fill, world grid | 9.78 | 5.78 | 1.69× |
| Changed sun direction, world grid | 9.63 | 4.33 | 2.22× |
| Changed sun direction, near surfaces | 20.34 | 15.01 | 1.36× |
| Local visibility, world grid | 2.89 | 2.33 | 1.24× |

Serialized CPU probes, responses and changing-light results matched every
float bit against the frozen reference. The exact-control test also compares
6/16/32/64-ray presets, larger-count fallback, cutouts, both BVH modes, points,
an additional direction, invalid/near-surface positions and moving geometry.
Cache tests exercise light changes, lattice/reach/seed changes and reset.

These are component measurements, not a game FPS increase. The 2.22× case
depends on rejected-probe reuse; it is not a universal cold-solve speedup.
The GPU feasibility experiments remain separate and do not change the game's
CPU backend. No sample count, bounce limit or rendering quality was reduced.
