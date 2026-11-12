# r60: remove repeated admission and material-cache work

The latest client log remains the r57 run: 680,540 bytes, SHA-256 `435e1213014ffdac3b5d7fdcf8a0c1abc5baaf10e867f920c4b7e069cb6a3ab6`. No new gameplay timings or FPS results are available. This iteration changes four shared CPU headers relative to r59; it adds no backend policy, cache, resource budget or shader variant.

## Removed work

1. Both ModelNormals and deferred material capture visited every packet again after `commitPending()`, including warm flushes with no newly committed batch. The second pass now runs only when the existing commit flag reports new resident pages. A separate-buffer upload returns its lease during the first pass, so it needs no retry pass. The flag also covers batches committed earlier by a full admission group during the same admission phase.
2. During that retry pass, `resident(mesh)` performed the cache lookup and `bind(mesh)` immediately repeated it. The existing `residentOnly=true` bind performs one lookup and cannot create or admit a missing resource. Failed admissions retain the existing UP fallback. Ready packets retain strong buffer references across trims.
3. The persistent material cache removed stale read records in one scan, then searched the same vector again. One scan now both removes stale source revisions and selects the first matching record. The selected `Read` stays at its stable heap address while its owning `unique_ptr` moves. Ticket state is still acquired on every request, so a worker completion is visible in the same frame.
4. `textureProfile()` retried the uploaded-pixel cache from `read()` immediately after its own lookup failed. It now uses the existing `checkUploaded=false` argument for that fallback. The fallback still reads current mip data and calculates the same statistics.

The r57 ModelNormals snapshot at frame 1524 has 1,138 queued packets and no new geometry uploads. The old retry loop still visited those packet records. This observation explains the avoidable traversal; it is not a measurement of r60 time saved. No whole-frame or EVENT-tail improvement is claimed.

## Validation

Evidence: `out/perf-20261109-r60`.

- Production material flushes on indexed meshes produce 72 identical normal/occlusion outputs per backend against frozen r59, for 288 bit-exact comparisons across native D3D9 and DXVK 1.10.3, 2.7.1 and 3.1.1. The fixture runs cold and warm frames, MSAA 0/4/8, mixed near/far writes, viewport changes and independent-write-mask fallback. Indexed geometry now exercises the pending-batch commit path directly.
- The full existing ModelNormals oracle passes on all four backends: immediate/deferred pixel parity, cold/warm residency, source mutations, tracked handoffs, stale-source rejection, reset/trim, constant and shader sharing, merge-order controls, alpha coverage and restoration failures. Its existing component timings do not compare r59 with r60 and are not used as evidence of a speedup here.
- Persistent-read matching passes 80 cases per backend, including all five ticket states, stale records after a retained match, stable record identity, canonical model recipes and same-frame Pending-to-Hit completion. Existing uploads compare 72 actual maps per backend with busy/failure/retry handling and unchanged pixels.
- CPU material regressions pass at O2/O0; displacement state/pose/declaration and actual world-cache wiring regressions also pass.

The old CPU material fixture initially crashed with both current and frozen r59 renderers. Its fake device forwarded `GetDeviceCaps` to a null real device. The fixture now implements that method and generates its missing client-program header itself. Buffered output was disabled to retain useful diagnostics on future failures. The structural wiring assertion was updated for the conditional commit form.

Standalone test shaders use the installed compatible compiler, with provenance in `gpu-validation.json`. Production shader sources and freshness gates are unchanged.

## Scope and delivery

The installed game DLL and log are unchanged. These source changes are newer than the delivered r59 DLL and are not included in it. This iteration does not build a production DLL or installer. Remaining repeated state reads that prove the current declaration/pose were retained; removing them without an equivalent validity proof would risk stale geometry or lighting.
