# Compact diagnostic captures

New `northlight-diagnostics/session-*` folders store each distinct binary payload
once in `.capture-blobs/<sha256>`. The ordinary `.fgr`, `.bin`, `.f32` and atlas
`.bc` names contain short `NLCREF1` references. Journals and metadata remain plain
text. ZIP the **whole session folder**, including `.capture-blobs`.

This is lossless deduplication. Image dimensions, formats, float precision,
shader bytes, constant banks, draw order and before/after evidence are unchanged.
The FGR header participates in the hash; identical pixels with different formats
cannot share a blob. D3D pitch padding is excluded as in the previous writer.
Empty products still restore to their full zero-filled images.

The writer hashes borrowed rows, writes only new payloads, and publishes each
reference after its blob is closed. It retains no pixel copy or per-frame cache.
All calls remain behind explicit diagnostic capture. GPU readbacks are unchanged;
hashing has a capture-time CPU cost, not a normal-frame GPU pass. An IO failure
still marks the capture journal incomplete. Folders without `.capture-blobs`
retain the old raw-file output, including independent legacy fixtures.

## Existing captures and analysis tools

From the repository root, compact either a legacy session folder or a ZIP:

```sh
python scripts/capture_files.py pack old-session-or.zip compact.zip
python scripts/capture_files.py unpack compact.zip new-analysis-directory
```

Release packages include the tool at `app/scripts/capture_files.py`. From the
unpacked package, use its bundled Python without installing another runtime:

```text
Windows: runtime\python.exe -I -B -X utf8 app\scripts\capture_files.py unpack capture.zip analysis
macOS/Linux: runtime/bin/python3 -I -B -X utf8 app/scripts/capture_files.py unpack capture.zip analysis
```

`unpack` also accepts a new session folder directly. It validates every referenced
blob's size and SHA-256 before publishing the new output directory. Existing
analysis and C++ replay tools then consume ordinary files with their original
names and bytes. Neither command changes the source; the output must be new and
outside it. Restored identical files share hard links where supported and should
be treated as immutable analysis inputs. A filesystem without hard links receives
ordinary copies. Python readers can use `capture_files.read_bytes(path)` directly.

Archive paths, symbolic links, duplicate case-insensitive names, invalid
references, missing/truncated/corrupt blobs and excessive expanded sizes are
rejected. Partial extraction is staged outside the final output directory.

## Measured size, 2026-11-16

Both supplied archives were compacted and restored. Every restored file was
compared with the original archive using size and SHA-256; all 9,395 files match.

| Capture | Original ZIP bytes | Compact ZIP bytes | Reduction |
|---|---:|---:|---:|
| Wintergarde inn | 476,259,930 | 104,344,360 | 78.1% |
| Tower | 184,026,887 | 67,634,336 | 63.2% |

The inn retains 335,706,596 unique binary bytes instead of repeatedly writing
6,042 duplicate payloads. The tower retains 282,668,276 unique binary bytes and
removes 2,224 duplicate payloads. These numbers describe capture storage, not VRAM
or runtime frame time. Private roundtrip evidence is under
`.local/out/compact-captures-20261116/real-capture-roundtrip.json`.

## Pipeline buffer audit

The current lighting pipeline already shares several resources. Source inspection
of `world_screen_resources.inl`, `world_renderer.h`, `hdr_frame.h` and
`resource_memory_profile.h` establishes the following lifetimes:

| Names that can look duplicated | Actual ownership |
|---|---|
| Texture and its `*Surface` | `target()` creates one texture and obtains its level-0 surface view. These are two COM interfaces to one allocation. The memory inventory counts the texture once. |
| Raw world light and current local light | After temporal lighting consumes raw `light`, the local-light pass clears and reuses `lightSurface`. There is no separate current-local-light allocation. |
| Removal smoothing and `fogBlurred` | Smoothing uses the fog scratch target before the fog stages need it. |
| Local lamp fog and temporal lighting | Lamp fog reuses the old `temporalLight[temporalIndex]` side after history consumption and index rotation. Current lighting remains on the other side. |
| Two temporal light/depth targets | One is sampled as history while the other receives this frame. They cannot simply be collapsed into the same sampled/rendered subresource. |
| Fog, fog scratch and fog history | Temporal resolution and the horizontal/vertical blur have overlapping input/output lifetimes. Texture/surface pairs rotate together. Equal zero contents in one capture do not establish interchangeable lifetimes. |
| Thousands of native before/after thumbnails | One reusable `traceTarget_` and one bounded native resolve produce many diagnostic files. System-memory staging is temporary per readback and released afterward; it does not accumulate once per file. |

At 2560x1440, a half-resolution FP16 RGBA target is 7.03125 MiB, and a
full-resolution one is 28.125 MiB, before driver overhead. The hundreds of MB
removed from a capture do not imply that much GPU memory was duplicated.

Two areas still warrant measured lifetime work: the HDR composed-scene copy into
the sampleable linear target (`Frame::finish`), and targets retained warm after
effects are switched off. The former must preserve MSAA/late draws/UI and avoid
sampling a current output; the latter trades retained memory for avoiding
allocation churn. No redundant steady-state lighting allocation was established
in this audit, and no GPU buffers were merged by content equality. The supplied
captures do not contain a complete live resource-allocation trace, so this is
not a claim that the entire renderer has no further memory savings.
