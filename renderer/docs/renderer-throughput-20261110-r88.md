# r88: time cold range scans, keep warm rejection cheap

Base: r87, `cf3e5dc7a06fd8fad93175792aba9101f90b289b`.
Measured/production source identity: `f21012e9cec082fb71d9329ddc20037c2257d5bd9b0cef9eb1a8afa0093198a8`.

## CPU change

ModelNormals previously timed every far range proof, including immutable
bounds already present in its bounded cache. Two steady-clock calls were
more expensive than the warm proof itself. They also consumed the 0.25 ms
allowance, eventually making already-known far meshes fall back to capture.

The existing time and 2,048-vertex budgets now bound cold scanning/preparation.
A cold miss checks that allowance before reading projection/pose constants.
A warm hit still checks the current pose, canonical projection, finite lanes,
overflow and source ownership, using the exact previous interval arithmetic.
It no longer reads the clock or spends cold-scan allowance. Mixed-bone and
unsupported cases keep their existing fail-open behavior.

No new cache, state flag, allocation, shader, pass or quality setting is added.
The cache remains 57,392 bytes. Near geometry does not spend scan allowance;
reset and source-owner ABA validation remain intact.

## Measurements

One executable contains the old range cache and the current cache, each
inside the same current ModelNormals implementation. All other source,
state-mirror, geometry-pool and GPU-store code is shared. Six measured ABBA
blocks per side follow four warm-up blocks; each block contains 32 frames.
The fixture submits 133 near plus 310 far draws of a stock streetlamp subset,
at 4K/MSAA2 through the production state mirror. No AutoBump producer or
pose/source handoff is supplied. Replay GPU timestamps exclude preceding
native draws and can include command-supply gaps. Per-draw timing adds some
CPU overhead equally on both sides; internal phase probes are off.

| Component | Before, ms | Current, ms |
| --- | ---: | ---: |
| captureMs | 0.205850 | 0.175675 |
| flushCpuMs | 0.040475 | 0.038400 |
| nativeSubmitMs | 0.051700 | 0.052350 |
| totalCpuMs | 0.348350 | 0.314175 |
| replayGpuMs | 0.033280 | 0.033792 |

Capture submission CPU decreases 14.7%; the fixture's CPU interval,
which also includes native submission and constant updates, decreases
9.8%. GPU work remains the same; the timestamp difference is below
one microsecond. Warm geometry reads stay at zero bytes; 310 draws are
culled and 133 remain queued/resident.

The isolated CPU cache benchmark decreases from 88.18 to
31.40 ns per call on a 133-near/310-far workload, using six samples
per side after four warm-up ABBA blocks. This is a leaf cost, not a game
frame or a claim of the same percentage for the whole renderer.

## Validation

O0 and O2 each compare 200,000 pose/projection cases against the frozen
range implementation. A generated clock-instrumented copy of the actual
header verifies independently exhausted time/vertex budgets, no clock reads
on warm hits, no pose reads on a budget-refused cold miss, current-pose and
failed-read rejection, cache reset and a recycled mesh address with a new
shared owner. The production header contains no test clock or new test API.

The stock-client range proof suite passes both shader families: 6,400 vertex
checks each, plus uniform/single-bone, mixed-bone, negative projection,
finite/overflow, source-owner ABA and oversized-source cases.

The same real-capture executable passes 512 frames on each of native D3D9,
DXVK 2 and DXVK 3. Cold and block-boundary normal-buffer readbacks (19 per
backend) are byte-exact against the before-path image on that backend. All 1,536 frames
complete without capture failures. The native/DXVK 2 runs are correctness
controls; headline timings use DXVK 3 on the RTX 4090.

This is a controlled capture component. It excludes game simulation,
WorldRenderer/other effects, streaming and presentation. It does not prove
the requested total 2-3 ms gameplay budget. GPU timings and CPU builds were
serialized. Client files were not installed or modified.

## Reproduction

```powershell
python tests/benchmark_model_range_budget.py --reference-header $r87RangeHeader --output $newCpuOutput
python tests/profile_base_capture.py --backend $dxvk3 --range-reference $r87RangeHeader --output $newCaptureOutput
python tests/test_model_normal_range.py
```

The reference header is `src/proxy/model_normal_range.h` from the base commit.
Receipts are ignored under `out/optimization-20261110-r88/`: `range-verified/`,
`capture-verified/`, `backends-final.json`, `client-range-final.log`, source/
shader/artifact verification, package verification and PR verification.

## Production artifacts

`0.3.217-test.20261110.14`, built `2026-11-10 08:53:05 UTC`. Zig 0.15.2 compiler
preflight, shader freshness, x86 PE/17 exports, exact installer payload and
release-package verification pass. Source identity matches both measured
stand and DLL. All 191 precompiled shader binaries remain unchanged.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `releases/Northlight-0.3.217-test.20261110.14/d3d9.dll` | 10,853,376 | `d77ab0a8dab5991f7eb996e3d3b35cacd8256c1be68d6d8b6ac941f284efe22d` |
| `releases/Northlight-0.3.217-test.20261110.14-Windows.zip` | 20,820,986 | `07f8f4d01861b918b63fe458e0698eecac40b4800268000d0570ac372af3de2e` |
