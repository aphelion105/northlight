# Native point-light resolution

Status: implemented; automated validation and live visual acceptance in progress. This document does not claim
that an untested client or a continuous camera sweep has passed acceptance.

## Contract

MPQ archives are read-only inputs. Each installation builds its own Northlight
cache from the winning archive chain, including replacements. A filename is not
a lighting preset. Cache build inputs include the archive fingerprint and producer
code provenance; scene/light compatibility revisions advance to 2. Evidence records include hashes of the actual model, skin and textures.
Changing a pack requires cache regeneration through the installer.

A light has two distinct values:

* The native observation contains identity, original position, colour and native
  attenuation. It is immutable evidence for matching the client's draw slots.
* The resolved emitter contains the position used by renderer direct lighting,
  fog and all six point-shadow faces. It retains native identity, colour and
  energy. Camera orientation and native slot number cannot change this value.

The producer transforms a model-local correction with the actual instance basis,
including rotation and scale, and copies values into the frame snapshot. Render
workers never follow game pointers. No change to the FGL light disk ABI is needed.
Snapshot archives write v4 with the resolved offset; v1/v2/v3 remain readable
with zero offsets. The command stream carries the expanded same-build value type.

## Placement policy

Reuse the existing bounded authored flame centroid where it is proven. Otherwise,
a geometry-derived correction is permitted only for an unambiguous native point
near a proven fixed fire-particle anchor and a supported static opaque fixture.
Read actual SKIN topology and material flags. Reject animated body geometry,
cutout bodies, unsupported batches and ambiguous source associations. A closed
upward path rejects relocation.

Measure visibility of a fixed model-space ground annulus below the fixture.
If the original point is substantially enclosed downward, choose the smallest
bounded upward step that exposes the surrounding annulus. Keep the centre
occluded by the original body; never delete the bowl or mark it emissive. Limit
travel by model extent, source range and the observed flame region. This is an
explicit rendering policy, not a claim to reconstruct physical radiance from an
arbitrary model. Unsupported assets keep their original emitter.

NLF1 v4 adds bounded source-indexed model-local corrections to the existing
content-matched fixture catalogue. v2/v3 remain readable with zero corrections.
Malformed/ambiguous profiles reject the catalogue rather than partially applying
it. The same resolver feeds placed and server-spawned fixtures.

## Ownership

Ownership must match the raw observation, even when the resolved emitter moved.
An exact source binding supplies the resolved position to the gain proof; merely
being nearby cannot grant ownership. Native bank decoding and matching always
use raw coordinates. RGB removal retains the selected source's fade gain and is
scoped to the actual native draw submission with restoration on exit.

Placed attachment bindings must also resolve through their native identity;
matching their displaced FGL coordinates against the raw native position fails.
The renderer-owned direct source remains available when the native receiver has
no slot for it. Shadow availability cannot select a different emitter position.
Unsupported receiver programs remain a separate coverage limitation and must be
reported; moving the source does not prove their lighting stable.

## Validation and acceptance

Synthetic geometry tests cover an open bowl, closed lid, unsupported material,
scaling, changed geometry at the same path, ambiguous flames and bounded refusal.
Native tests cover transformed corrections, raw-slot matching, displaced direct
ownership, malformed catalogues and old versions. GPU tests compare a receiver
with and without its native slot while the resolved emitter is displaced.

Build the actual client catalogue and report which fixtures qualify and why.
Run source and architecture suites, affected asset/native/GPU tests and the
production build. Then verify actual game captures from both problematic views:
raw/resolved identities and positions, native ownership, complete six-face cubes,
direct/fog agreement and final-image illumination. A component check or a pair
of stable source records alone does not establish final visual stability.
