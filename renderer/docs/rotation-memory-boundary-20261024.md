# Rotation stalls: coalescing required world-cache retirement

Private follow-up to [angle corrections](rotation-angle-corrections-20261024.md).
The game log is unchanged at October 24, 03:28:59, SHA-256
`b71ba562267c13188f24219df83bd71bdf7273e9bd914210833227343d886443`.
It measures the earlier installed renderer, not the proposed boundary change.
The game and optimizer are not launched or modified. Nothing is published.

## What still costs time

The largest sampled frame includes a 249.4252 ms interval with no producer
commands available to replay. Partial Lua/client samples corroborate work on
the game thread; they do not identify a script or attribute the entire gap.
That separate problem requires the newer producer CPU telemetry and genuine
input/stack evidence. Cache enlargement is not a demonstrated cure.

There is also directly attributed Northlight work worth removing:

| Stage | Existing runtime evidence | Scope of this change |
| --- | --- | --- |
| Memory relief | Frame 4284: 39.025 ms total, including 5.770 ms world-cap changes, 21.176 ms material-cap changes and 12.079 ms subsequent trimming. Frame 5671 totals 35.651 ms. | World cap enforcement still precedes a mandatory full world-cache clear. Avoid that redundant LRU traversal. Material clear-before-cap behavior was already corrected separately. |
| Terrain capture | Frame 18415: 8.349 ms, 571 misses and 573 byte-budget evictions. | No cap increase or geometry change. A genuine working-set comparison still needs native keys, updates and revisit order. |
| Material CPU preparation | The final 97-sample configuration reports approximately 3.55 ms exclusive sampled CPU, mainly pose/envelope/setup work. | No stale-pose substitution, reduced range or quality change. |
| GPU critical path | The final configuration has a 10.781 ms median GPU frame, above the 8.333 ms budget for 120 FPS. | No claim that this CPU boundary repair achieves the overall target. |
| Retained material atlases | MHEIGHT reports only twelve full uploads by approximately frame 1200, unchanged through the end. | No evidence supports a new lazy atlas cache as a remedy for this run. Rectangle/copy costs remain separately unmeasured. |

The five-topic audits are retained under the private diagnostic directory:
`lighting/off-mode-atlas-cost-audit-c568dea.md`,
`lighting/northlight-cpu-lua-boundary-audit.md`,
`native-chain/optimizer-native-gap-followup.md`, and the coordinator's
world/pressure review. Source behavior and historical stage times identify
the candidate; they do not establish its achievable timing reduction.

## Boundary contract

Only a boundary that already requires a full memory trim may clear the world
caches early. It must execute after completed-frame cleanup and before
lowering their limits. Its trim result and elapsed time are carried to the
remaining renderer trim, which must not clear the world a second time.

Material pressure must still precede material trimming: displacement's trim
uses its current pressure flag. Moving the entire renderer trim before all
policy updates would use the old flag and could reintroduce an unnecessary
OS memory query and different budget maintenance. Replay-copy and stream
policy updates, completion acknowledgement and the after-trim sample retain
their required ordering. Cap-only and recovery boundaries keep their existing
behavior. No draw, source admission or resource upload is introduced between
world retirement and cap enforcement.

The world still quiesces preparation before clearing state/caches. GPU COM
retirement remains on its owner thread, active and staged mesh pages remain
excluded, and escaped immutable snapshots retain valid payloads and live-byte
charges. Lower entry/byte limits, reserve thresholds, cooldown, hysteresis,
terrain pressure distinctions, actor geometry and MSAA remain unchanged.
Reported freed bytes remain estimates; clearing a pool does not imply that
an escaped owner's payload has been physically released.

## Stand fidelity evidence

The two preserved authored transport binaries now both pass their unchanged
six-frame Reset source sequence in matched fresh environments, including
the older binary that previously produced a black image. Consequently the
new optional pre-read branch is not established as the cause of success.
Earlier failures remain preserved. Evidence:
`native-chain/reset-followup/frozen-baseline-pair-1/validation.json`.

The exact original AA0 smoke executable, without recompilation, extra reads,
events, resizing or diagnostic arguments, also passes its six strict source
RGB/coverage/query frames through the pinned wrapper/backend. Evidence:
`native-chain/reset-followup/original-frozen-source-aa0-1/validation.json`.
This qualifies that specific source execution, not the original complete
capture/retrace chain matrix, raw-depth parity or same-scene performance.

No genuine game draw/resource trace or current animated actor/equipment
journal was found in the available corpus. The actual MPQ terrain/static
geometry and recorded camera observations are useful stage inputs, but do
not reconstruct native Lua/client work, dynamic visibility, skin palettes,
all original material bindings or depth/MSAA/HDR transitions. Authored
transport throughput is never reported as game FPS. The uncapped original
scene's static/rotation/native-only comparison and stable 120 FPS remain open.

## Implementation acceptance

Production changes and focused measurements are pending the source freeze.
Only affected boundary/cache checks will run. No full test suite is planned
for this slice. Adoption requires exact final limits/ownership/output and
meaningful old-order rejection, with complete component timing controls
reported separately from game FPS.
