# World cache build speed, 27 November 2026

The installer builds the world cache from the player's own client
(`scripts/install_world_cache.py`). On a 16-core computer the four continents
took 24 minutes and the six dungeon and raid groups another 6.4 minutes. This
records why, what changed on branch `perf/world-cache-build-speed` (commits
`86023c08` and `9ce60f9f`) and what is left to try.

All measurements: stock 3.3.5a client (19 archives, enUS), Windows 11,
16 logical processors, 32 GB, 14-15 GB free when the build started. Every new
cache was compared file by file (SHA-256) with one built by the previous code.

## Findings

### Continents

One `world_scene_builder.py` process built each continent, single-threaded.
Northrend alone took 12.8 minutes, so the largest map set the build time while
most cores were idle. Profiling the scene builder:

| Share of scene build time | Work |
|---|---|
| ~45% | Terrain: per-vertex Python for 145 vertices and 256 triangles per MCNK chunk, normals summed one triangle at a time |
| ~24% | Writing FGS2: one `struct.pack` call per vertex and per triangle |
| ~16% | BLP texture decode |

Each model's FGS2 file also embeds its own copies of the textures, so models
that share a texture store it more than once.

### Dungeons and raids

The instance layer ran one process per group (six groups). With the continent
changes it took 4.5 minutes instead of 6.4. Wrath dungeons (16 maps) was the
critical path at 158 s, while classic raids finished in 61 s.

4,270 of the 6,257 model files that the instance build wrote already existed,
byte-identical, in the continent cache. The move into `<output>` keeps the
existing file of any model that a continent tile uses, so all of that export
work (texture decode and FGS2 write) was thrown away. Profile of the Wrath
dungeons process: BLP decode 36%, terrain 21%, FGS2 writing 15%.

Per-map cost varies from under a second to 33 s (HillsbradPast). Every
global-WMO map (one placed WMO, no ADTs) costs at most 5.5 s. Tile count is
only a rough guide: Karazahn has 9 tiles but takes 30 s.

After the scene step, the instance lights builder runs as one process
(about 65 s, about 16 s of it fixed cost) and validation takes about 10 s.

## Work done

### 1. Faster terrain and FGS2 writer (`86023c08`)

- `Builder.terrain()` is unrolled over precomputed MCNK row/column offsets
  and cell quads. It skips holed cells and accumulates normals in flat lists.
  The arithmetic is unchanged; vector length keeps `math.sqrt(sum(...))`
  because Python 3.12's `sum()` is compensated and differs from chained `+`
  in the last bits.
- `write_scene()` writes vertices and triangles with `array('f')`/`array('I')`
  in one call each. It rejects non-finite vertices after the float32
  conversion (which also catches float32 overflow) and out-of-range triangle
  indices.
- `tests/test_scene_builder_bulk_terrain.py` keeps the per-vertex code as a
  reference and requires exact equality on 43 synthetic chunks, including
  random hole masks.

### 2. Continents in parallel parts (`86023c08`)

- `world_scene_builder.py --band K N` builds part K of N contiguous, equal
  tile ranges of each map. The orchestrator runs up to `MAX_MAP_PARTS` (4)
  parts per map when there are cores for them (`map_parts`, `--parts`).
- With `release_exported`, the command-line `InstancedBuilder` replaces the
  mesh of a model whose FGS2 is written by a stub that keeps only what later
  placements need. A part's memory then grows with its decoded textures, not
  its parsed meshes.
- A part's memory budget is its share of the map's, at least
  `PART_MEMORY_SHARE` (0.35). Northrend's densest quarter peaked at 1.23 GB,
  against 3.51 GB before the mesh release and 4.8 GB for the whole map.
- The parts of a map share one progress count: the mean of the parts'
  fractions, so the percentage never goes back or reaches 100% early. Each
  part writes its own build report; `scene_failures` merges them.

Result: the continents went from 24.0 to 8.9 minutes (Northrend in four parts:
213 s). All 19,837 files were identical, except for elapsed-time fields in the
fog and lights manifests.

### 3. Instance maps from one shared queue, reusing continent models (`9ce60f9f`)

- Before the scene build, `link_shared_models` hard-links into
  `<output>.instmp/models` the models that the continent tiles and the kept
  groups' tiles use. Here that was 11,846 links. These are
  exactly the files `apply_instances` never replaces. Rebuilt or removed maps
  are excluded, so a model that only they use is still exported fresh.
- `world_scene_builder.py --keep-models` does not write a model file that
  already exists. The mesh is still parsed and its key, bounds and triangle
  count are computed as before, so the FGS3 tiles are unchanged. Any worker's
  model is reused by the others, so each shared model is exported once.
- The orchestrator starts every scene worker at once (`instance_workers`).
  The count is as many as the memory budget fits (`INSTANCE_MEMORY_GB` each),
  at most `--jobs` and one per map; `--parts` overrides it. A worker admitted
  later would find every map already claimed.
- `world_scene_builder.py --claim DIR`: a worker builds only the maps it
  claims (`O_EXCL` on `DIR/<map>.claim`), largest first by planned tile
  count. Each run starts with a fresh claims folder, so a resumed run claims
  an interrupted map again. Each worker prints the total of all maps with
  `"shared": true`, and the progress shows tiles done of that total.
- Failure logs: `instance_failed_logs` names the workers whose build reports
  (named by process id) hold the failed maps.
- A map's build report now lists every WMO group clamped while building that
  map, not only the ones new to the process. Without this, a group's
  `clamped_wmo_groups` in the manifest depended on which worker built which
  map (seen with one worker: Wrath dungeons recorded 0 instead of 2).

A first version gave each group its own parts. The memory budget admitted
only 7 of 18 processes, so a group's later parts started after its first part
had claimed every map, and did nothing. It still took 238 s to 167 s, but the
shared queue replaced it.

| Instance build, all six groups | Scene step | Total |
|---|---|---|
| Before (one process per group) | 158 s | 238 s |
| Shared queue, default budget (7 workers here) | 64 s | 152 s |
| Shared queue, 1.0 GB budget (11 workers) | 60 s | 152 s |
| Shared queue, one worker (`--parts 1`) | 310 s | 403 s |

All four caches are identical (23,769 files), except for timestamps and step
timings in `instances-manifest.json`, `instances-validation.json` and
`lights/instances-manifest.json`. The group records match the old build
exactly.

### Computers with 8 GB of memory

The cache build must work with 8 GB installed (a full build is refused below
`MIN_TOTAL_GB`, 7.0 GiB reported). This shaped the instance changes:

- Peak working set, one worker building all 74 maps (the case when the
  budget fits only one): **0.80 GB**. That is lower than the old per-group
  process's 1.3 GB, because meshes are released and cleared per map. With 7
  or 11 workers, each peaked at 0.3-0.8 GB.
- `INSTANCE_MEMORY_GB` stays at 1.5 GB (about 1.9× the measured peak) to
  leave room for HD textures. A 1.0 GB budget ran 11 workers here but saved
  nothing measurable (table above): the largest maps and the deduplicated
  model export bound the scene step.
- With one worker the scene work is about half of the old total (310 s
  against about 650 s of process time), because no model is exported twice.
- Continents on a computer with few cores get one part per map, as before.
  When parts do run, a part's budget is a third of the map's or more.

## Tests

- `tests/test_scene_builder_bulk_terrain.py`: terrain and writer exactness,
  writer errors, `band_tiles` coverage, released meshes place identically.
- `tests/test_instance_scene.py`: `--band` parts; `--claim`/`--keep-models`
  build each map once across processes, keep an existing model file, write
  the same tiles and claim the largest map first.
- `tests/test_install_world_cache.py`: part counts and budgets, shared
  progress, instance worker counts and specs, model linking, fake builders
  that honour claims and kept models, failure logs naming the right worker.
- `tests/test_wmo_group_tolerance.py`: a later map's report records a WMO
  group that the process clamped earlier.
- `tests/test_northlight_install.py`: the new part logs and claim files fit
  the Windows path limit.

## Future improvements

In rough order of expected value:

1. **Parallel instance lights.** At about 65 s, one `world_lights_builder.py`
   process is now the longest instance step, longer than the 64 s scene
   step. Splitting its `--maps` across processes and merging the per-map
   outputs and manifest would cut most of that. The 16 s fixed cost per
   process limits how many parts are worth running.
2. **`--keep-models` for continent parts.** The parts of a continent export
   the models they share once per part. Passing `--keep-models` would skip a
   model another part already wrote, and a resumed build would also stop
   re-exporting. The output should be identical; measure first.
3. **BLP decode.** It is 16% of continent and 36% of instance scene time.
   Options: a faster bulk BC1/BC2/BC3 decoder (exact equality against
   `decode_blp` is required), or decoding each texture once per build rather
   than once per process.
4. **Shared textures in the cache format.** FGS2 embeds textures per model.
   Referencing shared texture files would shrink the cache and the write
   time. This is a format change for the renderer and needs a cache revision.
5. **Better map order for the instance queue.** Tile count misorders a few
   maps (Karazahn). Build reports already hold each map's elapsed time;
   claiming in last run's measured order would shorten the tail. The tail is
   already small: the 7 workers finished within 2 s of each other.
6. **Drives without hard links.** On FAT32/exFAT, `link_shared_models` links
   nothing and every model is exported as before. Letting the builder check
   `<output>/models` directly for the shared keys, without staging copies,
   would keep the saving there.
7. **Progress on resume.** A resumed build skips finished tiles without a
   progress event, so its percentage never reaches 100%. Emitting a `tile`
   event with `"skipped": true` would fix the count. This predates these
   changes.
