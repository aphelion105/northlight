# Material performance investigation — 22 October 2026

The latest user restart did not demonstrate an in-game GPU displacement draw: `heightGPU supported=1 draws=0`. Its steady full-material windows averaged 19.182 ms (52.13 FPS), compared with 16.015 ms (62.44 FPS) in selected earlier restart windows. Camera, scene and lighting differed, so this is not a controlled patch regression measurement. The later 100–107 FPS intervals followed disabling material effects and cannot establish full-quality performance.

A new baseline was recorded in Crystalsong Forest with terrain/wall/grass POM, model displacement, cavity and parallax shadows enabled, at 3840×2160, MSAA 2x and HDR. The previously installed combined follow-up build averaged 18.120 ms (55.19 FPS) over 103 focused 120-frame windows. Transition and background intervals were excluded. A 120 FPS frame budget is 8.333 ms. **The new candidate has not yet been benchmarked in the game; 120 FPS is not achieved or claimed.**

## Five priorities

1. **Current-pose bounds:** hundreds of thousands of displacement pose-budget refusals remain after snapshot-budget isolation. The candidate reuses the bounded replay envelope cache to prove distant models outside displacement range before interpreting every vertex. Near, cold, unsupported and budget-limited models retain their original preparation and fade. Current constants are reevaluated for every proof.
2. **Repeated POM:** native diffuse shading and normal/occlusion capture repeat the raymarch. Sharing results needs an MSAA-, alpha-, layer- and depth-safe design. That merger is not implemented in this candidate.
3. **Rigid range inputs:** optional range snapshots now read POSITION0 only. Unrelated UV/color streams no longer consume the range budget or invalidate its immutable snapshots. Full displacement capture still requires the full layout. Existing read budgets and snapshot payload caps are unchanged.
4. **Replay overhead:** distant proofs avoid normal-map acquisition, capture shaders and target/state changes. The candidate retains alpha/texkill-only model capture and indexed normal-map lookup. Further batching needs an image-verified implementation.
5. **Synchronization:** retain PR #103's adaptive EVENT-query waits and measure their rates after material work changes. Aggregate waits overlap GPU dependencies; they are not independent removable CPU costs.

## Validation

- Production Windows x86 renderer build passes.
- State/budget regression passes: relative palette addressing, current poses, immutable source replacement, near overlap, unsupported programs, Reset, separate reads and position-only layouts.
- Six CPU/GPU displacement image/depth fixtures pass on DXVK with MSAA 2x; SM2 and layered fallbacks remain covered.
- All 40 model shader/skin/combiner combinations pass, including animated geometry, source bindings and vertex constants.
- All 480 model images match the previous combined follow-up fixtures bit for bit.
- Far-model capture is skipped while native drawing remains; returning near resumes displacement.
- Native D3D9 and DXVK buffer-generation checks pass: over 32 color-stream updates, POSITION0-only range admission keeps 31 tracked hits and reads 168 bytes, versus zero hits and 7040 bytes with the full layout. Position writes invalidate the proof, nonfinite positions fail open, and full material capture sees every color update. These are fixture byte counts, not game timing estimates. Run `tests/validate_displacement_gpu_height.py --backend <d3d9.dll> --range-streams-only`.

These are correctness tests, not a game FPS benchmark. Final acceptance needs a matching focused, full-quality game run, a positive GPU-height draw delta for geometry within camera range, and a near/far image check. In-game validation is pending an interactive login. See [material-displacement.md](material-displacement.md) for rendering contracts, fallbacks and diagnostic counters. Keep the integration PR in draft while this work remains incomplete.
