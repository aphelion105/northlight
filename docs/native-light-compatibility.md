# Native light compatibility

`[Renderer] NativeLightStay=1` in `northlight-renderer.ini` is enabled by
default. The settings window exposes **Keep off-screen lights** on its Renderer
page. It is a startup option: saving a change requires restarting the game.
The switch controls Northlight's installation only; it never removes another
mod's patch. Process exit discards Northlight's change.

For x86 build 12340, `native_light_stay.h` validates all 400 bytes of
`QueryLights` at VA `0x81e400`, allowing only the original or already patched
opcode at `0x81e548`. The original `JE` becomes `JMP`, retaining the native
target and instruction length. An existing patch is a no-op. Unknown code or
an unreadable image is skipped. Failed publication restores the saved opcode
and page protection before paused threads resume. There are no file writes or
callbacks into the DLL. Imported replay hosts skip native installation.
The glue selector accepts either validated QueryLights variant.

The log reports `NATIVE light stay status=active`, `already-active`, `disabled`
or the precise validation/publication failure. This fixes the native draw-stamp
gate; it does not remove the native four-light limit, add shadows or prove that
every camera-dependent lighting change has the same cause.

## Examined x64 client

The local `WowClient64.exe` examined on 2026-11-13 has SHA256
`eb9e7cc49e740e2bd1b370f247348b21483238acc0f5a88131fc267fe3b02586`.
Its corresponding light query at VA `0x140196a00` traverses both light lists
and feeds candidates to `0x140172540` without the x86 stale-draw-stamp
comparison or `SetEnabled(0)` call. The queried path already has the equivalent
LightStay behavior. This is static evidence for that executable, not a live
gameplay acceptance. The binary also embeds LuxVelum and reads its LightStay
setting; that string alone does not establish how the light query behaves.

## Eight native lights

St0ny's More Lights patch has separate CPU and shader parts. The CPU part
retains the original four slots and collects rejected/displaced candidates into
an additional ring. Model/WMO uploads use VS c236..246; terrain instead uses
c236..250 in an interleaved world-minus-camera layout. The shader part adds
four model/WMO lights or five terrain lights. Both layouts must be uploaded for
their own draws to avoid interpreting stale registers as a different layout.

`[Renderer] NativeMoreLights=1` is enabled by default and requires restarting
the game after a change. Its six validated x86 selection/transform/upload hooks
use process allocations only. Unknown executable layouts are skipped; a known
existing patch is validated and reused. Native thunks do not call the DLL.
The original EXE and MPQs are never written. Fixed-function and OpenGL hooks
from the upstream patch are not installed.

Supported SM2/SM3 programs receive private vertex shader variants; public shader
identity and bytecode remain original for the renderer's material/geometry
contracts. Already expanded programs are reused. Unproven layouts, exhausted
registers and shader instruction limits retain the original program. SM1 and
unproven private material variants retain their native fallback.

The Renderer page has separate live sliders, both defaulting to eight:
`NativeModelLightLimit=4..8` for model/WMO and `NativeTerrainLightLimit=3..8`
for terrain. Partial limits zero additional RGB slots for the actual submission
and restore only the changed register span afterward. If an unexpanded source
already computes the requested result (minimum limit, no extra RGB, or full
ownership handoff), the original shader draws directly. This removes both extra
vertex arithmetic and private state changes without changing the light result.
Existing externally expanded shaders retain scoped zeroing at lower limits.

Original proven variants have a separate 512-entry cache from the 512-entry
private derived cache. Lightweight original capability metadata survives eviction
so a later draw can rebuild rather than lose eight-light support. Unsupported
registrations do not retain COM objects or evict ready original variants.
Already-eight draws at the full limit without ownership do
not fetch their extra constants. RGB-only decisions borrow known mirror state
under the existing gate; unknown state keeps an authoritative device read.
Native shadow-only bindings do not activate an unused RGB handoff.
State restoration retries a transient failure once, covers failed setters that
already changed the backend, and runs on exceptions. Persistent restoration
failure blocks further draws through this manager until a successful device
Reset establishes clean state.

These are native per-receiver limits, not the scene-wide Northlight
`LocalLightLimit` (0..128, default 64). Eligible shadowed sources are a subset
capped by `min(LocalLightLimit, PointShadowLimit)`; distance, eligibility and
resource availability also apply. GI consumes its prepared scene-light list
independently. Native slots do not create additional GI or shadow sources.
For proven opaque receivers, exact selected-source ownership removes the native
RGB fraction supplied by Northlight, including the additional register banks.
Unmatched or ambiguous sources retain their native contribution.

`test_native_more_lights_gpu.py` validates real-client variants on D3D9 and DXVK,
compares expanded output with sums of original draws, and checks live limits,
ownership removal, state restoration and injected setter failures. The native
selector test executes the actual client AddLight on a local mapped copy and
checks closest-eight selection, idempotence and publication rollback. These
component tests do not establish live game FPS or complete material coverage.

`scripts/benchmark_native_more_lights.py --baseline-header <saved-header>` runs
paired ABBA measurements on both backends with quad and 1536-vertex receivers,
known/unknown mirrors, six light cases, D3D call counts, raw CPU/GPU samples and
cache-pressure controls. Its image oracles must pass and emitted programs for
the entire accepted client corpus must have the same digest in both builds.

Sources:

- [LightStay and Add-MoreLights implementation](https://github.com/Raz0r1337/St0nys-AIO-WoW-EXE-Patcher/blob/st0ny-main/apply_patches.ps1)
- [Model and terrain shader formats](https://github.com/Raz0r1337/WoW-lights-patch-shader)
