# Skip empty fog rendering (2026-11-01)

The production renderer skips fog marching, temporal filtering and both blur
draws when the regional field is empty or conservatively outside every view
ray's reach. Unknown bounds retain rendering. Local lamps remain independent.
The skipped frame clears raw fog and temporal history to exact `(0,0,0,1)` and
keeps that history valid, so entering fog again matches the existing path.

This independent port is based on `f6f55b80`. All shader sources, compiled
payloads, samplers and GPU constant contracts remain unchanged. Cloud-tail
clipping is only in the combined [#98](https://github.com/lepola/northlight/pull/98)
implementation: a trial against this branch's older cloud kernel failed exact
RGB comparison and was excluded.

The Windows x86 release DLL builds. The extended production-renderer fixture
passes on DXVK 2.7.1 and 3.1.1 with MSAA0 and MSAA2 (four runs). It compares
known and unknown height bounds above and below the field, empty fields,
neutral FP16 history and re-entry bit for bit. Existing independent publication,
no-BVH rendering, disabled direct shafts, Reset recovery and D3D state checks
also pass. No game FPS improvement is claimed.

Reproduce with `python tests/test_regional_fog_recovery_gpu.py`;
`NORTHLIGHT_TEST_D3D9` selects a backend DLL. Generated binaries and local logs
are not committed. See [#109](https://github.com/lepola/northlight/pull/109) for
the independent fog recovery/preparation review and #98 for combined validation.
