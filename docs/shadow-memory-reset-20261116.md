# Shadow memory and Reset recovery, 2026-11-16

The Dalaran session `session-66284-513529906-0` ran source `b5b980794c35`.
Capture 1 completed at renderer frame 8496. A real device Reset followed at
8497; the next frames had no uploaded world mesh. The resident upload payload
had already been discarded, so recovery tried to build another complete local
BVH while retaining the old scene. Available x86 address space was approximately
232–282 MiB after Reset, below the new-scene entry requirement. This explains the
missing Northlight shadows after capture. It does not establish why the client
requested Reset. The supplied capture used 4K/MSAA 2; the standalone pressure
validation explicitly uses 4K/MSAA 8.

## Ownership changes

| Allocation | Current ownership |
|---|---|
| World shadow geometry | Compact DEFAULT vertex/index buffers; resident CPU upload pages are freed after commit. This was already present in the prior installed build. |
| World shadow cutout textures | Exact eight-bit alpha in A8 DEFAULT textures, with A8R8G8B8 fallback. A temporary SYSTEMMEM texture uploads one material at a time and is then released. No permanent MANAGED system copy. |
| Static-object shadow cache | DEFAULT vertex/index buffers and A8 DEFAULT textures. This cache already discarded every resource on Reset, so its previous MANAGED copies provided no recovery benefit. |
| Local GI geometry and RGB texels | Remain in RAM: CPU ray tracing and probe transport read them. Recovery shares the existing immutable BVH instead of allocating another one. |
| Small material reuse data | Remains bounded at 4 MiB; retained bytes support exact content comparison across uploads. Static alpha comparison data remains for collision-safe sharing. |
| Recovery staging | Reconstructed in bounded, file-backed pages from the existing local BVH and far-terrain cache. The file-backed source can replay repeated Reset without rebuilding the local scene. |
| Capture readback | Temporary system staging and bounded reusable GPU resolve; thousands of exported files do not represent thousands of retained runtime allocations. |

DEFAULT removes D3D9's managed system copy. It does not promise that the graphics
driver uses no process address space or host-visible staging. This is an x86
virtual-address-space problem as well as a RAM/VRAM budget problem. The process
reserve and contiguous-allocation checks remain enabled.

Recovery publication now checks mesh-plan identity as well as BVH identity.
Otherwise a restored plan sharing the old BVH could remain unpublished. A
successful upload also clears the rebuild-request backoff for subsequent loss.

## Brazier shadow

Source `6288328728644070691`, point slot 20, was ready and usable with shadow gain
1. Capture pass 19 submitted `LocalLightingPcss` successfully. Its exact exported
shader, constants, depth, normal buffers and cube were replayed against clear
and fully blocked controls. The clear cube produced zero correction. The
captured correction stayed between zero and the fully blocked reference; 351,006
pixels with meaningful lamp energy were fully occluded. The minimum RGB
correction was -0.612793. These are whole-pass counts, not a floor-only ROI.

The captured current-local-light image contains the brazier's dark silhouette.
The final image also contains ambient/directional illumination. Daylight scales
both the added lamp contribution and its shadow subtraction by the same factor
(`1 - 0.7 * sunlit`); shadow strength itself was not reduced. No blanket darkening
or shader colour change was made. This replay verifies the local pass, not all
native light ownership or a complete final-frame reconstruction.

## Validation and test handoff

Private artifacts are under `.local/out/shadow-memory-followup/`. Final reports
and installation hashes belong to the current source tree; intermediate reports
are retained only as investigative evidence.

The tests cover actual 4K/MSAA 8 cold/warm scene uploads, injected upload failure,
repeated Reset, sampled alpha readback and independent page raster comparison.
`actual-worker-reset` additionally runs the real builder/GI threads, requires the
same BVH pointer with a new upload plan, then verifies the restored raster and
another device Reset. Fake-device tests cover static-cache lifetime, sharing,
budget, failure and worker-plan equality; native D3D9/DXVK image tests cover
actual resources.

Manual acceptance: start the installed test build at 4K/MSAA 8, return to the
Dalaran brazier, take a capture, and check that directional and local shadows
return after device recovery. Then move across a streaming boundary and take a
second capture. Standalone tests do not establish game FPS or guarantee that
every client/mod allocation fits in the remaining x86 address space.

## Follow-up: capture still disabled shadows

Session `session-39448-516558609-0` ran `4b2ffd8d07af`. Capture 1
finished at renderer frame 6896 with 18 exported passes; Reset succeeded at
6897. Its stack enters from the client's window handler. The forwarded
`WM_SIZE` had the same 3840x2160 dimensions and was classified
`no-current-present`. This establishes a real client-requested Reset after the
capture, not device-state corruption in the capture export. Its original
trigger is not proven by the stack. The capture took about 5.6 seconds inside
world submission.

The retained-BVH recovery did work: build 355 published after about 1.5 seconds
without another local BVH. Upload then stalled at `pressure scene targets`:
273 MiB available, 295 MiB required to re-create a managed GI visibility atlas.
Before capture the latest steady sample was about 410 MiB; during export it
fell to about 315 MiB. The 256 MiB process reserve was still enforced. Local GI
CPU data was about 300 MiB, including 178 MiB of RGB material texels; its upload
payload was already zero. The static caster cache retained another 94 MiB.
These measurements concern x86 address space, not exhaustion of physical RAM
or VRAM.

The fix retains compatible MANAGED probe textures on every device Reset,
including resident mesh recovery and interruption before any new page exists.
Probe history and publication serial are invalidated; current probe data is
rewritten. Descriptor validation still replaces incompatible layouts. This
reuses existing allocations without lowering admission floors or granting a
new upload plan the old plan's narrower replay allowance.

The optional static-caster reclaim gate now checks drawable GPU geometry,
not merely an extant CPU BVH. A stalled mesh upload can request the same
frame-boundary reclaim as a stalled builder. The streamer remains paused until
the world mesh is drawable, so it cannot immediately refill the released
cache during recovery.

The strengthened `actual-worker-reset` stand initializes all seven probe
textures, exports and reads back an actual 4K/MSAA8 image, resets twice (the
second time before page creation), rebuilds the real Dalaran scene through the
production worker, and constrains target admission to approximately 273 MiB.
It verifies texture identity, static-cache pause/resume, the unchanged reserve,
independent page raster output, and another Reset. The old implementation
reproduced `pressure scene targets` retries and failed retained-texture checks.
Follow-up evidence belongs in `.local/out/capture-reset-followup/`.

The supplied gameplay session itself still used MSAA 2: both the capture
metadata/device Reset and `WTF/Config.wtf` (`gxMultisample "2"`) agree. The
standalone tests explicitly request eight samples. Client settings were not
changed for this investigation.
