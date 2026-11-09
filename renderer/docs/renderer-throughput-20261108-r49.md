# Material geometry under address-space pressure, r49

## Evidence from the r48 gameplay run

The frozen `out/perf-20261108-r49/game.log` is 1,165,107 bytes, SHA-256 `07f155c779ce832611697cbaf9b8e3a283767f023efec20a69b99bc09996efce`. It identifies source `d725567c207d63077003e7213c3976b93bcc588dd1f1f4c72f20ab6a31e6be47` and DXVK 3.1.1. DXVK 1/2 gameplay performance is not inferred from this run.

With effects enabled, frame intervals near frames 481–639 average 28.339–31.704 ms; the following effects-off segment averages 15.859 ms. Camera movement and settings change during the run, so this is evidence of the reported cost rather than a controlled cross-build benchmark.

At sampled frame 635, CPU extension work totals 12.468 ms (7.192 ms capture, 3.704 ms effects, 1.048 ms gate, 0.524 ms cleanup). GPU frame time is 27.680 ms, including 20.023 ms GameScene, 1.678 ms final ModelNormals, and 4.567 ms Effects. Earlier ModelNormals flushes are inside GameScene. Material GPU micro-interval coverage is incomplete and must not be extrapolated to a whole-frame breakdown.

The zero-budget problem is explicit in the cache reports:

- Frame 508 reports one r48 spare-quota transfer. The retention path is active.
- After the critical pressure transition, material-owned residency is capped at zero, while shadows retain 32 MiB. Materials can borrow shadow residents but cannot retain their own working set.
- ModelNormals frame 762 sends 19,166,316 bytes through UP fallback. Frames 889–1397 still send roughly 7.6–9.5 MB per sampled frame. After further settings changes, frames 1524 and 1778 have no residents and send about 30 MB each.
- The process already has 4,095 MiB of virtual address space and approximately 13 GiB of free physical memory. The problem is available/fragmented 32-bit address space, not exhausted physical RAM.
- Critical trimming at frame 758 takes 26.270 ms overall, including 12.461 ms snapshot clearing and 4.531 ms replay-pool clearing. The nearby EVENT at game frame 760 takes 51.9583 ms, of which 45.0924 ms precedes replay and 6.8555 ms is in replay. This change does not claim that the trim or every EVENT delay is eliminated.

## Changes

### Aggregate residency allowance

The production pressure transition configures a 32 MiB aggregate `NativeStore` allowance for materials and shadows together. Each consumer may use space in that allowance; the combined charged metadata, pending reservations and GPU allocations cannot exceed it. The previous pressure policy allowed 0 MiB for materials plus 32 MiB for shadows. The new policy uses the same aggregate allowance and avoids disabling material residency.

Local owner limits still apply. Aggregate pressure evicts the oldest eligible entry across owners. Accounting-only transfers are attempted only when they can relieve owner pressure without violating the aggregate limit. Metadata/ownership transfers do not count existing data as a second allocation. If owner pressure gives way to aggregate pressure during a scan, the scan revisits previously skipped owners. Current-frame protection, entry count, creation/upload limits, explicit critical clears and CPU pool limits remain enforced.

The existing `GPU_SCENE` lines now report aggregate charged bytes and the effective aggregate limit. Standalone material users retain the former critical-zero policy until the shared aggregate cap is explicitly configured.

### Bounded streaming of cache misses

ModelNormals misses now use the existing fence-checked append-ring policy. The private buffers have a logical capacity of 3 MiB for vertices and 1 MiB for indices, allocated lazily behind a virtual-memory guard. They are released on reset/critical trimming. Driver rename/backing memory is not measured by these logical capacities.

Only supported, validated single-stream indexed triangle lists use this path. Geometry is uploaded in bounded chunks, index bases are rebased without changing effective vertex addresses, and compatible adjacent packets use the existing merged-draw path. The existing pointer-identity layout reuses each shared immutable source once per chunk even when pose or raster state differs. Repeated identical opaque calls can collapse only before another geometry range has joined their run, with identical buffers, ranges, shaders, constants, textures and raster state.

Ring spans are never overwritten before their frame fence completes. A pending span forces DISCARD on wrap; completed spans use non-flushing queries and NOOVERWRITE. One lazily-created EVENT is issued per frame for these rings, rather than one query per packet/chunk. Query creation failure keeps spans non-reusable until DISCARD. Unsupported geometry, low virtual memory, allocation failure or lock/unlock failure retains the original UP fallback. Resident cache admission remains separately bounded; streamed draws are not reported as cache hits.

`MODEL_NORMALS` reports streamed draws, batches, uploaded/saved bytes, discards, failures and logical capacity. This makes the next gameplay run able to distinguish cache reuse from cheaper misses.

## Validation

Receipts and measurements are stored in `out/perf-20261108-r49`. The GPU stream fixture runs with a zero-byte resident cache, compares original UP output to streamed output bit for bit, and covers unique chunks, repeated sources, changing raster/pose state, VB/IB allocation failure, EVENT creation failure and wrap/discard. It is exercised with the eight stock VS2/VS3 rigid/skinning programs on each DXVK backend.

The initial DXVK 1 rigid-program test changes 128 distinct chunk submissions to two draws. Repeated full-model geometry is uploaded once (34,080 bytes instead of 4,362,240 bytes); compatible repeated calls become one draw. Different raster/pose states retain their draw boundaries while sharing the upload. These are component results, not measured gameplay FPS gains.

The complete cache, pressure transition, ModelNormals, canonical MRT/MSAA and local-shadow regression results are recorded after final validation. No AO settings, effect strengths, precision or render resolutions are reduced.


Completed regression checks: 26 cache controls in O2 and ASan/UBSan, the full canonical CPU suite, pressure transition/recovery, production world binding, eight cache/bulk upload variants, 24 stream program/backend cases (three geometry modes and VB/IB/EVENT creation failures), 24 standard ModelNormals cases, nine canonical MRT/MSAA cases, nine constrained aggregate-budget MRT/MSAA cases, 18 retained-geometry pixel comparisons, and 2,034 exact plus 8,136 recovery local-shadow faces per backend. Assertions remain enabled.

Median component CPU submission across the eight stock programs (milliseconds; three measured frames per program after warmup):

| Backend | 128 distinct chunks: UP / streamed | Repeated source, differing raster/pose: UP / streamed |
| --- | ---: | ---: |
| DXVK 1.10.3 | 0.3001 / 0.3064 | 0.3673 / 0.1114 |
| DXVK 2.7.1 | 0.3649 / 0.3059 | 0.3643 / 0.1152 |
| DXVK 3.1.1 | 0.3544 / 0.2638 | 0.3616 / 0.0959 |

The DXVK1 distinct-chunk CPU result is effectively unchanged/slightly slower. Completion timings include per-frame surface allocation/readback and are noisy; some DXVK2 repeated-source cases are slower in that metric despite lower CPU submission. These direct-backend component tests do not prove GPU timing or gameplay parity, and do not apply the proxy's process-level backend defaults. Full raw measurements, rather than an unconditional speedup claim, are retained in `stream-measurements.json`.

Final production DLL: `out/dll-20261108-r49/d3d9.dll` (10610176 bytes), SHA-256 `d5a3f9b1986ff678354a845edff7db1ef2edf1475a39861f146a5bb401e8fa12`, source identity `3fecebdb2e00c689f2ba9a85b978b60cc1705a1e185096da0a1ba6255750ae2b`. The final proxy passes command-stream startup and 64 real EVENT completions on each DXVK backend (192 total), with the existing real-completion/zero-queue-fence policy. Compiler preflight, shader freshness, PE32 exports and 779 production source hashes pass. The installed client is unchanged; no installer was built.
