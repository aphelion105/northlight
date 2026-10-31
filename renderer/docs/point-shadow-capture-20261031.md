# Complete point-shadow capture updates

The reported `Northlight-0.3.216-test.20261030.9-Windows.zip` records combined source `b9077f0d`. Its point-shadow path and independent PR #92 could overwrite a cube without current model replays and mark that static-only result ready. A stale face cycle switching back to six faces per frame is one reproducible case. New, changed or invalidated sources could also publish incomplete cubes on a skipped-capture frame.

Return before cube writes when capture is skipped. Request capture for the next frame; reuse an already usable cube only when map, source identity, position, range, attenuation start and flags remain compatible. Otherwise leave the source unavailable until a fresh capture completes. No stale pose storage, additional GPU resources, shader changes or capture-budget changes are introduced.

`tests/test_point_terrain_coverage_gpu.py` executes the production renderer and shader patcher on a hidden device. An authored replay caster must match static rasterization exactly at six light distances, 0.11 through 4 world units. Fifty-four transitions cover refresh-interval reuse, complete-cube reuse, stale schedules, identity/position/range/attenuation/flag changes, invalidation and fresh-capture recovery. The earlier four terrain camera-membership checks remain.

Both independent PR #92 and the corrected test.9 integration pass on native D3D9 and DXVK 3. Compiling the same regression with the unmodified `b9077f0d` point-rendering include fails on both: caster center depth changes from `0.091673024` to empty `1.000000000`. The negative control retains all other fixture and renderer inputs.

Focused scheduler, clipping, shader-artifact and capture checks pass, including optimized and ASan/UBSan runs. Both Windows x86 DLLs build independently. This proves the capture-gap regression with authored geometry; the complete reported gameplay scenes were not replayed. No game was launched, package installed, settings changed or client cache modified.
