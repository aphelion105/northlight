# Coarse GI coverage and directional receiver facing

The input log loaded source 78358273. Session session-25240-410069109-0
contains three captures: 1/3 are shadow diagnostics with explicitly skipped
normal/depth/map exports; capture 2 has usable directional inputs. Replaying
that receiver query reproduces the feathered light at the shaded cliff edges.
The native sun angular response differs from the direction used for shadow
maps. A PCF footprint can see past the edge of a face turned away from the
shadow source, and native angular lighting keeps that leaked light visible.
The receiver now suppresses visibility on the rear hemisphere with a narrow
continuous grazing transition. Current and historical cascades use their own
source directions. Missing geometry still leaves directional lighting neutral.
This isolated query is not a whole-scene replay.

GI now has two world-aligned grids, with spacing 8 and 32. The near grid keeps
its full rays, moving-actor correction, order and publication cadence. Once the
near request publishes, the worker fills static coarse probes with max(8,
GIRays/4) rays. Each new request preempts coarse work between probes. Coarse
results publish at most every 100 ms, plus completion/cancellation, and reuse
exact world keys until the lighting/geometry generation changes. Same-map
replacement retains displayed results until replacement; map changes discard
them. Published storage is immutable.

The second grid uses existing loaded BVH geometry; it does not load distant
regions. Probes require a 64-unit margin within the loaded horizontal crop and
320-unit vertical crop. With GIChunkRadius=1 this limits their centres to 224
units from the geometry centre; interpolation also needs neighbouring probes.
This is bounded additional coverage, not GI across the entire visible world.
Its 9x9 visibility maps trace 64 units instead of 16, covering the coarse cell
diagonal. The reduced transport sample count and wider cells trade detail for
coverage. Moving actors remain a near-grid effect.

GPU textures pack independent near/coarse layers. Coverage blends the coarse
result only into missing/arriving near coverage. A resident blocked near cell
therefore cannot borrow unoccluded coarse radiance. Wrapped keys, map changes,
activation fades and same-key SH history remain independent per level. With
resident probes the standalone GI pass handles both grids; the merged
unshadowed shader is retained for analytic fallback without probe reads.
Allocations use the existing memory admission/retry path. Resource inventory
and immutable snapshot accounting include the extra layer.

Validation includes an independent analytical GPU cascade blend, production
upload/visibility packing, blocked near/far cells, distant receiver keys,
activation and stale-key rejection on system D3D9, DXVK2 and DXVK3. Native tests
exercise preemption, retained immutable publications, map reset, geometry
bounds and visibility past 16 units. The existing screen-resource fixture
checks actual WorldRenderer allocation, both SH layers, visibility layers and
retention across pressure/reset. No timings or game FPS are implied. Final
acceptance reports and installed hashes belong under
.local/out/gi-cascades-20261115/; live gameplay validation is separate.
