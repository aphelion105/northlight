# Shared GPU cache retention, r48

The r46 client log remains the latest available gameplay run. It reports 25,412,724 material-owned GPU bytes out of 32 MiB at material frame 635. A separate shadow ownership sample at world frame 190 reports 5,769,204 bytes out of 64 MiB. These samples are not treated as a simultaneous free-space measurement. r47 increases grouped cold admissions, so material byte pressure can arrive sooner. Source inspection exposed a retention gap: independent owner quotas discarded shared geometry even when the other consumer had room. The counterfactual below demonstrates that gap directly.

## Change

`NativeStore::room` now tries the existing ownership-preservation path before evicting an eligible resident under ordinary byte pressure. The destination must already have unused retention capacity for the entire allocation and its metadata. A separate multi-stream entry transfers individually; an immutable batch transfers as a unit when any member has been used by the peer. Only accounting and owner links change: VB/IB pointers, placements, indices, vertex contents and draw selection stay intact.

The spare-only path never evicts destination entries, allocates a GPU buffer or copies geometry. Explicit trim/clear retains its existing one-way policy, so recursive transfers cannot occur. Global entry-count pressure still erases an entry. Current-frame protection, 8,192 entries, 32/64 MiB owner limits, four MiB upload allowance and the creation-time allowance remain in force. More data can remain resident within the existing combined budget; this is not a claim that actual retained memory is unchanged.

`GPU_SCENE` diagnostics add `spareLoans` and `spareLoanedBytes` for each source consumer. These are frame counters; bytes include transferred metadata and buffer allocation, rather than just geometry payload.

## Deterministic comparison

`tests/test_gpu_cache_spare.py` exercises both consumer directions, grouped/single-entry uploads, exact-fit/one-byte-short capacity, full/zero destination quota, unshared geometry, protected probation at the global entry cap, held buffer references, and destination eviction protection. Its optional `--reference` build loads the frozen r47 `NativeStore` alongside the current implementation with identical input, quotas and device model.

| Camera-return fixture, per consumer direction | r47 | r48 |
| --- | ---: | ---: |
| 16 grouped meshes still resident | 0 | 16 |
| Return-frame cache hits | 0 | 16 |
| Geometry sent through fallback | 159,744 bytes | 0 |
| Geometry reuploaded next frame | 159,744 bytes | 0 |
| New buffers on return | 2 | 0 |
| One multi-stream mesh still resident | 0 | 1 |
| Multi-stream fallback/reupload payload, each | 11,520 bytes | 0 |
| Multi-stream new buffers on return | 3 | 0 |

The grouped transfer accounts for 164,096 bytes, including metadata. The multi-stream transfer accounts for 11,792 bytes. Optimized and ASan/UBSan builds use enabled assertions and independently check effective index addresses and vertex bytes. Held draw bindings remain valid after eviction in both versions.

## Validation and limits

Full receipts are in `out/perf-20261108-r48`. Real-GPU checks compare retained geometry against the original UP geometry in both consumer directions, both MRT outputs and MSAA 0/4/8 on DXVK 1.10.3, 2.7.1 and 3.1.1. The existing canonical scene, ModelNormals and local-shadow visibility/recovery oracles are also rerun.

This removes a demonstrated reupload path. There is no r48 gameplay FPS or frame-time result yet. The r46 log's 22.765 ms memory boundary and 48.5659 ms EVENT wait are not claimed eliminated by this change. Windows DXVK correctness does not establish macOS performance parity.

Completed validation: 22 cache boundary controls in optimized and ASan/UBSan builds, four frozen-r47 comparisons, nine canonical MRT/MSAA cases, 18 retained-geometry pixel comparisons, 24 ModelNormals cases, and 2,034 exact / 8,136 recovery point-shadow faces per backend. World binding and eight cache/bulk upload variants also pass.

Production DLL: `out/dll-20261108-r48/d3d9.dll` (10,605,056 bytes), SHA-256 `076b1fee7750b62d73dc3ef815a981668db977e13d47d997d277bbd3a8012264`, source identity `d725567c207d63077003e7213c3976b93bcc588dd1f1f4c72f20ab6a31e6be47`. The final DLL passes isolated command-stream startup and 64 real EVENT completions on each DXVK backend (192 total). Compiler preflight, shader freshness, PE32 exports and the production source hash set pass. No installer or installed-client update was performed.
