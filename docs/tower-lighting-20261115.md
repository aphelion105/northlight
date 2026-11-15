# Tower lighting, 15 November 2026

The supplied `qa_report/northlight-renderer (5).log` and `(6).log` correspond
to the torch and entrance captures from another installation, `D:\Wow_3.3.5a`.
Both use source identity `02cc90bec4ac`. The two downloaded diagnostic archives
are retained privately under `.local/out/tower-lighting-20261115/`.

## Missing directional shadows

Both logs report `northlightRequested=1 verified=0 owned=0`, and the first
reports `WORLD_LIGHTING variant=unshadowed shadows=0 geometry=1 gi=1 probes=1`.
The exported near/far shadow maps explicitly say `skipped` and `zero_filled`;
their black pixels are not valid occlusion evidence.

The startup snapshot diagnostic reports an image-code envelope ending at
`0x00e100b3`, with camera, map, sky and weather globals all incorrectly inside
that envelope. The shadow CVar, quality and dirty globals are inside it too.
Recording therefore bypassed these mutable values. The strict shadow-provider
reader correctly rejected the missing frame records, disabling Northlight's
directional maps. Other readers fell back to live memory across queued frames,
which also broke camera/light coherence.

`CodeRange` now retains each readonly code section separately. An appended
code section cannot turn the intervening data into code. Signature reads remain
live, writable executable sections remain snapshot data, and a read crossing
a section boundary cannot bypass capture. Native instruction proofs and the
producer's acquisition/restore transaction are unchanged. The provider log now
separates `codeVerified` from `frameReadable` to distinguish these failures.

The snapshot test exercises a disjoint section layout with that same envelope,
clobbers live camera/light/shadow memory after recording, and verifies the
captured native-shadow state without live reads. An isolated old-envelope
control must fail. Actual compatibility of the foreign executable's native
shadow instruction hashes still requires a new client log.

## Residual model light

The ordinary composite and removal filter capped darkening at 55% with the
captured shadow strength of 125, including negative GI. Prelit WMO could remove
almost all authored light. Consequently, a dark solved room retained an
artificial cool component on ordinary models and exterior-lit WMO groups.
The captured character also uses different native ambient/direct colors from
the world's baseline; restoring directional visibility is essential, rather
than changing that character's material or suppressing its local lights.

The shared removal limit now allows the configured GI replacement amount,
up to the entire transported surface term. The GI producer still bounds its
negative correction by native ambient. Fog remains outside this removal, and
current local lamp radiance is added afterward. When GI is disabled, the host
uploads zero GI strength so the existing direct-shadow limit remains active.
The same rule is used in the standalone and merged temporal filters.

The GPU regression covers six GI strengths, three shadow strengths, SDR/HDR,
local lights and all three smoothing variants: 126 dark-room cases per backend.
It checks ambient-energy removal against an independent expected value, not
an emulation of the shader clamp. Existing sky/unlit/prelit and merged/separate
GI oracles remain in that test. These are device image contracts, not a full
replay of the remote client or a measurement of game FPS.
