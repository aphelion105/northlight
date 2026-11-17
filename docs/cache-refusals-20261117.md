# Exact material refusal cache, 17 November 2026

Reviewed after `70ca97a2`, following the [pipeline cache work](pipeline-buffers-20261117.md).
The available game logs predate that build. Their last `AUTONORMAL` sample has
15,413 cumulative attempt deferrals and 3,555 refusal hits. Those counters do
not identify collisions or establish current FPS; the fixture below isolates
one concrete cause of redundant failed work and admission starvation.

## Fixed

`NorthlightAutoNormalCache::resolve` had a sixteen-slot direct mapping from
low texture-pointer bits and model mode. Each cold attempt retained its source,
revision and recipe mode in one slot. Two different textures could replace each
other's refusal, spending the eight-attempt frame allowance on repeated failures.
The remaining allowance then rejected an unrelated valid material.

The existing frame allowance means only eight attempted keys can exist. Keep
all of them in the existing fixed array, search those exact keys and append each
new attempt. Source/model/revision equality, strong COM references, eight cold
attempts and two successful generations per frame remain the admission rules.
Frame end, Reset and recipe changes release the references and permit retries.
Revision changes may retry immediately within the remaining frame allowance.
Resident successful-map hits still return before the refusal lookup.

No new buffer, payload copy, heap allocation, cache capacity or GPU resource is
introduced. The maximum number of retained attempted-key references remains
eight, including multiple revisions/recipes of one source. The existing array
and class memory layout are unchanged.

## Controlled result

The fixture selects real allocated texture addresses with equal old slot indices.
It runs the actual production resolver with a fake D3D device, both O2 and O0,
and compares the frozen `70ca97a2` header with current code.

| Work after 1,000 alternating failed-source requests and one valid source | Before | Current |
|---|---:|---:|
| Failed-source descriptor reads | 8 | 2 |
| Refusal hits | 496 | 998 |
| Total cold attempts, including the valid source | 8 | 3 |
| Deferred requests, including the valid source | 497 | 0 |
| Valid material admitted | No | Yes |

The no-collision control has the same descriptors, attempts and admission before
and after. This is a count of avoided failed work, not a 4x CPU/FPS measurement.
The new map follows the existing shader and visual settings; restoring its
admission can add intended rendering work compared with the starved case.

`test_material_refusal_cache.py` also checks the full key, revised sources,
model/world distinction, the ninth distinct key, eight retained leases, caller
release, frame/config/Reset cleanup, failed-draw retry and allocation-free hits.
The existing normal-index oracle continues to cover exact map/cache state,
eviction, failure recovery and deliberate hash collisions. GPU cache tests cover
actual map pixels, leases, upload reuse and persistence.

Other reviewed caches already share geometry and have bounded ownership. Pose
metadata eviction and FGS2 path lookup remain candidates for future matched
profiles; their retained data and revision checks are still required. This fix
does not infer that every earlier attempt deferral was caused by a collision.
