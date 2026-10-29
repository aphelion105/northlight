# Scene entry under address-space pressure, 2026-10-29

Request 1785 in the captured player log waits before geometry allocation at approximately 307 MiB free virtual address space. Its camera is `(5139.84, -56.93, 380.31)` on Northrend. The 392 MiB entry requirement contains a 256 MiB process reserve and 136 MiB working allowance. The reported 0.08 MiB builder capacity describes the deferred attempt, not the memory needed to build that scene.

A private x86 replay of that location shows why simply changing the entry number is insufficient. The existing chunked pipeline consumes up to 128.69 MiB of sampled process VA across seven nearby regions, including replacements with the preceding BVH retained. The first completed reachable allocation census is 118,220,388 bytes. Large temporary terrain, merged geometry, packed indices and finished CPU pages overlap a duplicated local acceleration/cache representation.

The pressure path now omits optional source/BLAS caches while preserving the same cropped geometry, materials, triangle order and ray results. Disposable terrain and shadow/index staging use private `std::tmpfile` storage with four 64 KiB cache slots per array. Finished pages are written to a per-generation temporary file. No full file is mapped into the client address space. Normal memory admission and normal MANAGED generations keep their existing path.

Pressure entry requires 304 MiB: the unchanged 256 MiB process reserve plus a 48 MiB working allowance. Interior checks preserve the process reserve plus the greater of the request or a 2 MiB guard; the existing 32 MiB contiguous margin remains. A larger scene can still refuse safely and raise its measured entry floor. The change does not promise that every city scene fits 48 MiB.

File-backed pages upload in bounded chunks into DEFAULT vertex/index buffers, avoiding the MANAGED host copy. Each growth still checks fresh process VA and reserves three resource capacities. Generation admission uses the bounded request requirement instead of reserving a second full page payload and another fixed 64 MiB. Texture growth is checked separately. Actual allocation or copy failure cancels the candidate while the preceding complete GPU table stays usable.

`WorldMeshPages::Plan::copy(page, vertices, offset, bytes, target)` validates ranges and returns false on checked I/O failure. Page keys retain the exact original sizes even when their CPU vectors are empty. File-backed payloads and their cutout texels remain owned by the immutable plan after commit. Reset releases DEFAULT buffers before the device reset; the same file-backed payload can recreate them. MANAGED generations retain their prior reset behavior. Supersession, cancellation and final ownership retirement close the private files. Storage failures retry without publishing partial geometry.

## Validation

`tests/validate_streaming_pressure.py` compares the integrated baseline `c03fec873602463265ada22c63990701514b445c` against the actual seven-region CPU pipeline at 434, 395 and 307 MiB, with one old BVH retained after each publication. The 307 MiB case uses the pressure path. Every plan/page/ray hash and every triangle, page and batch count matches. The test copies every persisted page back through the production copy helper, recomputes its content key and validates sealing.

| Measurement | Existing chunked path | Pressure staging |
| --- | ---: | ---: |
| Maximum sampled VA consumption across seven regions | 128.69 MiB | 41.54 MiB |
| First completed reachable CPU census | 112.74 MiB | 30.97 MiB |
| Minimum free VA in the 307 MiB replay | entry refused | 265.46 MiB |
| First region local/plan triangles | 287,935 / 1,007,742 | identical |
| First region pages / batches | 82 / 3,666 | identical |

Persisted page data is 24.85–26.98 MiB per tested region. Reading and checking the complete payload takes 17.30–21.81 ms in this fixture. The fixture intentionally performs a full VirtualQuery walk on each admission callback; its whole-build timings include that instrumentation and do not estimate client frame rate. The pressure path repeats cold source work and trades extra file I/O for bounded VA; the normal cache path remains available above pressure.

`tests/test_temporary_geometry.py` passes O0/O2 with 104 injected I/O failure sites and five allocation failures. All failed candidates leave the previous result unchanged and close their files. It also checks short-copy failures, invalid ranges and overflow. Native ASan/UBSan passes. Existing shared-scene tests pass O0/O2 with 600 allocation sites, held generations and concurrent immutable leases. Existing exact paging/upload-cursor ASan/UBSan tests, memory inventories, entry/overflow/generation boundaries and diagnostics gates pass. The complete renderer builds locally.

`tests/validate_world_cold_upload_gpu.py` is an opt-in standalone graphics test. It covers 307/395/434 MiB, a retained complete generation after injected index-buffer failure, DEFAULT pool selection, GPU raster equality, actual device Reset and re-upload from the same payload. It is compiled but not executed while the player game is open. Its 307 MiB case reports a separate simulated 32 MiB final CPU reservation; it does not substitute for the real scene CPU replay above. No installed DLL, game settings or game process is changed by this work.

All process minima are allocation-boundary/stage samples, not a continuous measurement of every game/driver allocation. Driver VA behavior, complete rendered-image equality and frame pacing remain subject to the guarded graphics test and later client validation.
