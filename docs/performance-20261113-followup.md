# Performance after the shadow fix, 13 November 2026

The user confirmed that the shadows look correct after restarting. The fresh log
identifies runtime source `de52e3629c366fd24f9fb73f50e4717247fdfaf6afbd8487c1723675fd9493f7`.
There are no reported shadow-transition failures. The session ends with normal
device destruction; this is not evidence of a new renderer crash.

The preserved log and machine-readable analysis are under
`.local/out/test-output/release-20261113/`. Log SHA-256:
`56faa1c8556e06ed32a08062ba55b38bcbe3409e1447de06a3bf0417763f73a5`.
These are moving gameplay samples with diagnostics enabled, not a matched
before/after benchmark. Do not attribute FPS changes to the constant restore.

For focused gameplay after frame 1000, 53 frame-interval windows of at least 100
samples have a median mean interval of 12.886 ms. The median of their p95 values
is 15.386 ms, but the worst window's p95 is 27.932 ms. The final two complete
windows average 67.27 and 64.83 FPS. Those final windows do not describe the
entire session.

In 56 sampled GPU profiles the reported total has median 3.283 ms, p95 5.035 ms
and maximum 5.798 ms. AO is reported in only 28 of those samples: its median is
0.8135 ms, plus 0.1995 ms median AO blur. Other reported medians are removal
smoothing 0.4215 ms, world composite 0.4705 ms and fog march 0.375 ms. Effects
are not all active in every sample; do not add these independent medians into a
frame total or compare them directly with delayed `gpuEffectsMs` observations.

The material CPU profile has median 0.4359 ms but p95 4.1774 ms and maximum
4.3147 ms. Geometry-source records still show cache hits and zero additional
reads, so this does not demonstrate that the earlier material caching was lost.
Pose preparation/envelopes and deferred work remain useful targets for matched
scene profiling. AO is a substantial recurring GPU cost when enabled. Slow
frames also contain scene submission/queue-idle intervals, which cannot be
assigned to shadow rendering from this log alone.

The release preserves the confirmed shadow correction and existing optimizations.
No new FPS improvement is claimed from packaging or from moving resources out
of the DLL. Further performance work needs matched scenes/settings and image
checks before changing capture cadence, AO quality or material behavior.
