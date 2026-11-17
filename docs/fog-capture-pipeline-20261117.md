# Fog pressure path and geometry preflight, 2026-11-17

The latest client log ends at 19:17:11 local time and identifies renderer source
`e39f2968815d10ce1875667142d6c6571c4e764d6f803972e007d83a17b9ddd0`.
No newer gameplay or complete fog capture accompanies it. The frozen input and
component evidence are under `.local/out/fog-capture-20261117/`.

## Fog: pressure selected the expensive algorithm

Before renderer frame 1190, the log reports the normal 240×135×64 fog volume.
At 357 MiB of free process address space the CPU/cache pressure policy activates.
The previous screen executor then reset the volume every pressured frame and
forbade its recreation. Imported noisy fog fell back to the half-resolution
48-interval raymarch; its sampled cost at frame 2794 was 3.847 ms.

The volume's three DEFAULT RGBA16F render targets do not request a CPU shadow
copy. They now survive CPU/cache pressure. Cold allocation under pressure uses
the same existing DEFAULT-target memory admission as other screen targets;
unknown samples or insufficient reserve still select the fallback. Capability,
shader, allocation and device-reset failure handling remain in place. Resize
and Reset release the atlases. No quality setting, noise sample count, radial
slice count, shadow resolution or shader calculation was reduced.

The component comparison at 3840×2160 uses the actor-shadow, noisy,
non-transition variant selected in the log, including its tiled fallback.
It runs both algorithms with the same inputs in one executable:

| Backend | Raymarch | Volume update | Ratio | Cached resolve |
| --- | ---: | ---: | ---: | ---: |
| Native D3D9 | 1.950 ms | 0.200 ms | 9.77× | 0.019 ms |
| Packaged DXVK | 1.987 ms | 0.211 ms | 9.43× | 0.018 ms |

The three atlases occupy 49,766,400 logical GPU bytes. Native D3D9 measured zero
additional process address-space consumption at creation; DXVK measured zero
in the initial comparison and 16.875 MiB in the release repeat. Driver allocation
overhead varies, so this does not establish zero process-memory cost on every
driver or run. The existing memory reserve and allocation-failure fallback
are still required.

`fog-4k-release/validation.json` records source hashes and 795 GPU cases per
backend. The volume/fallback comparison retains its existing limits: RMS below
0.008 and maximum error below 0.04. They are existing different algorithms, not
bit-identical images. The production WorldRenderer contract separately requires
byte-identical output when a settled volume enters pressure; it also covers
moving noise, cold pressure allocation, partial OOM, unsupported caps, failure
fallback and MSAA 0/2. `test_fog_volume_admission.py` exercises 10,000 resident
pressure frames without allocation or memory resampling and 42 cold cases.

The additional native D3D9 executor run exposed incomplete initial state capture:
a newly created ALL state block restored high pixel constants as zeros until
its first explicit Capture. DXVK did not reproduce this behavior. All new
pooled, overflow and standalone state blocks now Capture explicitly before any
effect work. Pooled steady-state capture counts are unchanged. The native mock
models incomplete initialization and capture failure; the full GPU contract
checks the actual c154–c156 sentinel across success, fallback and recovery.

## Capture: stop optional work without a consumer

The optional coarse material preflight previously read declaration, source and
pose state for skinned programs that support neither a coarse envelope nor the
stock SingleBone far-range proof. It cannot produce a result for these programs.
They now return before these reads. Mandatory capture keeps its original path;
SingleBone range proofs and weighted envelopes retain their previous behavior.
`MATERIAL_POSE_COARSE unsupportedSkipped` counts the omitted attempts.

The matched ABBA fixture invokes optional preflight followed by mandatory
prepare, using the actual old method and current method in one executable.
For unsupported programs it measured about 1.09–1.15× improvement, with equal
prepared data and retained-source digests. SingleBone near, middle and far
controls show no material improvement. The fixture does not establish the
frequency of these programs in gameplay or a multiplier for the entire capture.
Broader shortcuts that regressed the SingleBone controls were rejected.

## Acceptance limits

These are component timings and ownership/image contracts. The old log's
3.847 ms cannot be divided by the synthetic 4K ratio to claim a new game-frame
time. A new matched gameplay run must confirm `FOG_VOLUME` remains active after
pressure, measure `FogMarch` and total frame time, and inspect the new capture
counter. Existing staged effect toggles are useful for that comparison. CPU and
GPU durations must not be added to infer FPS.
