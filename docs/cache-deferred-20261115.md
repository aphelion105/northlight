# Cache and deferred work, 15 November 2026

The run ending at 00:03:31 uses `ed4f773f`, source identity
`1b14d2c50a0895eb6b6aff09c90a281550c60b789b1d9376e5e1e64ec45920a4`.
Its frozen log and experiments are in `.local/out/cache-deferred-20261115/`.
Settings change during this run. Material comparisons use only its 13 sampled
material-active frames, 381 through 1905; the broader GPU summary uses 24 active
effect frames through 3302. Neither is a controlled before/after FPS comparison.

## Whole-pipeline priorities

| Work | Median time | Scope |
|---|---:|---|
| Material capture | 1.187 ms | CPU, exclusive material scopes |
| Entire capture | 1.720 ms | CPU, includes material capture |
| Deferred material flush | 0.190 ms | CPU, part of material capture |
| Model pose envelopes | 0.177 ms | CPU, part of material capture |
| End-of-frame cleanup | 0.314 ms | CPU, sampled diagnostics included |
| WorldComposite | 0.599 ms | GPU, already includes material detail |
| AO + AOBlur | 0.356 + 0.292 ms | GPU |
| RemovalSmooth | 0.408 ms | GPU |
| GI | 0.185 ms | GPU |
| FogMarch + FogBlur + Fog | 0.181 + 0.080 + 0.124 ms | GPU |
| WorldNormals | 0.095 ms | GPU |

CPU and GPU numbers overlap; do not add them into a frame cost. Shadow maps and
character shadows are not the leading GPU costs in this run. MaterialDetail is
zero because the deployed HDR composite already folds that work. Native world
shadow replacement is active; the log does not show duplicate native maps.

The previous grass change is exercised: median grass snapshot/setup scopes are
0.059/0.012 ms, versus 0.188/0.072 in the previous run. Scene/settings differ, so
this confirms the path and workload, not the size of an FPS gain.

## Cache evidence

Typical material frames queue and flush 143 packets (36 grass, 107 models), with
zero snapshot fallback, live-state fallback, discarded packets or source reads.
They draw 143 resident GPU meshes, upload zero bytes and reuse about 6.3 MB of
geometry. The material GPU cache occupies about 16.9 MB without pressure or
evictions. Shared immutable CPU sources retain about 112 MiB. Raising these
limits would not remove the measured warm work.

Pose metadata has 126 hits and zero misses at the median. Current animated
palettes must still be evaluated. The older pose-result memo is deliberately
disabled after its low hit rate; re-enabling it would add comparisons/copies
without evidence of benefit. Source identities and their immutable geometry
remain shared with shadow consumers.

The replay already skips approximately 9308 identical state writes per sample,
but still checks those individual slots. The accepted change compares contiguous
render/raster state blocks first and falls back to the original per-slot writes
only when a block differs. It also constructs the key descriptor once per flush
instead of once per packet. Grass and model materials both use this code.
Setter order, geometry, constants, target transitions, texture-write barriers
and error handling are unchanged. No cross-frame render-state assumptions were
introduced; the cache still ends with its uninterrupted replay.

`test_material_deferred_blocks.py` compiles the shipped `ed4f773f` implementation
and the current implementation. An ABBA sequence checks exact per-draw state
hashes, 143 submitted draws, setter counts and restored native state for equal
blocks, mixed blocks and changing full palettes. Its fake device excludes GPU
work. Initial medians were approximately 0.020–0.022 to 0.013 ms for equal blocks,
0.021–0.022 to 0.017 ms for mixed blocks, and 0.027 to 0.024 ms for full palettes.
These microseconds apply to the component fixture, not the complete 0.190 ms
game flush or whole-frame FPS. The named `cpu-materials` suite also covers
mixed bindings, raster state, target ownership and allocation reuse.

## GPU experiments and remaining work

AOBlur's five-tap inner loop was tested without reducing its samples. Full
unrolling exceeds the SM3 instruction limit (557 slots); it was rejected at
compilation. A two-pairs-plus-one variant compiles and passes 180 GPU image
comparisons with zero changed bytes/ULPs, but timings are inconsistent and several
cases regress. Neither variant is in production. Raw results are in `ao-unroll/`
and `ao-pairs/run/`. Forever stayed open throughout, as requested.

The AO kernel's paired/four-tap alternatives and RemovalSmooth scheduling
alternatives were already tested in [the earlier investigation](performance-20261114.md).
They did not establish a repeatable win; those experiments must not be mistaken
for missing production optimizations. RemovalSmooth's removal ratio depends on
the centre pixel's legacy lighting, so an exact replacement cannot simply cache
one neighbour ratio for every receiver. Its temporal consumer also needs the raw
lighting for neighbourhood clamping. A merge requires a new shader/resource
contract and GPU evidence, not just deleting a draw.

Frame-boundary snapshot resets and pose-budget resets are already constant-time.
Sampled cleanup includes many diagnostic lines; no profiling or logging was
disabled to manufacture a saving. Remaining priorities are the fused final
composite, AO/filtering and removal smoothing, followed by current-pose bounds
and real driver submission costs. No shading sample count, resolution, material
distance, animation update or quality setting was reduced in this change.
