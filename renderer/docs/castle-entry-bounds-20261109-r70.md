# Rigid source bounds across position programs

The user deferred the specific castle-entry video issue. The full YouTube video
was unavailable: both the official download paths and the exact browser tab had
no buffered video data. This change is a general replay-bounds optimization; it
does not establish or fix the cause of that particular video.

An independent Westguard-area segment in the tester's earlier capture motivates
reducing cold rigid work. At renderer frame 9652 it reports 766 bounds, 580 valid,
186 unbounded rigid sources, 10,044 unbounded rigid face packets, 6,312 replay
draws and 4,835,772 caster triangles. Point-shadow CPU was 11.632 ms and its GPU
interval 11.976 ms, with no cold allocation or shader attempts. The capture
predates the video; these are independent regional observations, not synchronized
video telemetry. They do not identify how many of those 186 sources can reuse a
peer envelope.

## Change

`EnvelopeCache` already retained conservative immutable source envelopes and
evaluated each current position program/constants/camera separately. Different
program keys could nevertheless scan the same rigid POSITION0 input again.
The existing cache now copies the eight local input-limit floats from a ready
rigid peer when the complete source contract or live owner, vertex/index counts,
used declaration element and stride match. No geometry or world-space evaluated
bounds are shared. Current program evaluation, conservative error enclosure,
cache caps and fail-open behavior remain in place.

Peer lookup is attempted once per new entry, visits at most eight candidates,
charges the visits and eight-float copy separately, and skips low operation
budgets. An incompatible bucket therefore cannot repeatedly starve construction.
The existing sampled `WORLD replay bounds` row adds `rigidPeerHits`; the next real
capture can measure applicability directly.

## Verification

| Check | Original | Current |
| --- | ---: | ---: |
| 117 stock WMO VS3 programs, valid results | 117 | 117 |
| Distinct retained position keys | 2 | 2 |
| Source scans / built vertices on one 96-vertex source | 2 / 192 | 1 / 96 |
| Bounds hash across all 117 results | `299d2b92846b8b3b` | `299d2b92846b8b3b` |
| Second program ready after one-vertex cold budget | fails, exit 13 | passes |

All vertices are enclosed in the stock-program experiment. The retained CPU
regression checks changed current constants/camera, full identity and deliberate
hash collision, reconstructed packets, owner control-block mismatch at the same
address, declaration offset, stride, vertex count, zero admission/operation
budgets, and unchanged geometry bytes/ownership. With 32 incompatible peers and
one vertex/16 operations per frame, all 96 vertices still complete in 96 frames.

The GPU oracle uses stock `nd_human_barracks.wmo` geometry: 34,004 opaque triangles
(no nonopaque triangles were excluded), nine authored light positions, the
production local-shadow shaders and production conservative face clipping. It
compares unknown-bounds fail-open submission with the newly ready peer bounds.
Native D3D9 and DXVK 3 both pass:

- All 54 face maps are bit-identical; 7,250 covered pixels prevent a blank-output
  comparison from passing.
- Draw submissions fall from 54 to 12; submitted triangle exposure falls from
  1,836,216 to 408,048 for this oracle.
- WoW was running concurrently (PID 19136). `timing_valid=false`: these are
  correctness/work-count results, not elapsed GPU timings or game-FPS results.

Retained checks: `tests/test_rigid_peer_bounds.py` and
`tests/test_rigid_peer_point_gpu.py`. The GPU receipt records source/backend
hashes and checks that relevant source inputs remain unchanged. Client geometry
and shader programs are read locally; no game-derived binary fixtures are added
to Git. Private experiment receipts are under
`out/castle-entry-20261109-followup/`, including `point-gpu-final/validation.json`,
`wmo-peer-original.log`, `wmo-peer-current.log` and `rigid-peer-original.log`.
