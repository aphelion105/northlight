# r64 — base lighting composition with AO

The latest frozen game log is `out/perf-20261109-r64/game.log`, SHA-256
`6f744f421651e0e7ee0215a58418d19133bad318466e08af945e188ef850ac92`.
It confirms the r63 source/build identity. There are no recorded settings
changes, so its frame times are not an AO-on/off comparison or an r62/r63
performance acceptance test.

On the three sampled geometry-ready frames (3048, 3175, 3302), independent
model-normal replay costs 1.424/1.195/1.746 ms GPU; effects cost
3.849/3.300/3.960 ms. RemovalSmooth costs 0.351/0.358/0.426 ms, composition
0.516/0.434/0.471 ms. Sampled CPU capture is about 4.1–4.4 ms. These scopes
overlap and GPU elapsed time includes submission gaps. The normal receiver
target is full resolution; frame 3175 queues 784 logical draws and submits
502 after merging. This remains a large common-path optimization target.

The largest EVENT wait is startup frame 2 (168.040 ms, 159.904 ms before
replay). The largest later wait is 62.410 ms at frame 2937, immediately after
geometry becomes ready and shadow caches rerender at frame 2936. That
correlation identifies the transition to investigate; it does not establish
which CPU/GPU operation exclusively caused the pause. EVENT policy is unchanged.

## Selected change

The existing `WorldBaseCompositeHdr` now consumes real AO instead of requiring
an all-one-alpha proof. It retains the same color/depth-guided light and AO
upsampling, native fog transport, weather haze, rain and coverage behavior.
With actual volume fog and current-local-light buffers absent, it omits their
unused work and skips the half-grid loop on non-relit pixels. No new render
pass, shader variant, target, backend policy or quality setting is added.
The existing backend admission and ordinary-compositor fallback remain.

Its compiled program is 418 slots/16 temporary registers, versus 493/22 for
the ordinary HDR compositor. A stricter candidate retains neutral-fog
arithmetic and uses 438/17. Both candidates share the same host selection.

Two interleaved ABCCBA blocks use one actual WorldRenderer executable, an
identical captured 3840×2160 scene and authored non-neutral AO. Stage medians:

| Backend, AO active | Ordinary, ms | Selected fast, ms | Strict, ms |
| --- | ---: | ---: | ---: |
| DXVK 2.7.1 | 0.230656 | 0.167936 | 0.186880 |
| DXVK 3.1.1 | 0.210944 | 0.160768 | 0.173056 |

The fast compositor saves 27.2%/23.8% in this isolated stage. Strict costs
11.3%/7.6% more than fast (18.944/12.288 microseconds). These are not whole-game
FPS measurements. The user selected performance after seeing the difference
comparison and explicitly requested no additional validation runs.

For the displayed captured frame without the artificial AO stress pattern,
the first fast prototype changes 1,291,387 floating-point pixels, with maximum
linear RGB difference 0.00048828125 and unchanged alpha. The same linear-to-sRGB
conversion produces **identical 8-bit previews**. On the separate AO stress
pattern, 18,095 of 8,294,400 preview pixels change, by at most one 8-bit code.
This is a bounded observation on these inputs, not a universal HDR-display or
temporal guarantee. The selected compositor intentionally does not claim
bit-exact HDR equivalence. Its final-color rounding is not fed into lighting
history. Evidence and previews are under `out/perf-20261109-r64/`.

## Other investigations

RemovalSmooth variants preserve the tested pixels, but gains on authored
edge cases did not consistently carry over to the saved gameplay inputs;
several cases regressed. They remain isolated prototypes and are not selected.
The GPU oracle now accepts explicit backends, captured inputs and uniform
shadow controls. The base-pipeline stand supports frozen AO and shader controls
for future comparisons; these are test-only inputs.

After the user selected fast, additional GPU comparisons and acceptance runs
were stopped. Embedded world and dependent transition shaders were rebuilt
with the existing compiler/SM3 gates. No new DLL or installer is claimed in
this iteration; the previously delivered DLL remains r63.
