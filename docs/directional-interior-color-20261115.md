# Directional shadows and interior colour

The supplied Wintergarde Keep screenshots show much darker, redder interiors
with the Northlight directional provider than with Game. They were made in
another installation. Its original programs, frame inputs and log are not
available locally, so these images do not establish which runtime path failed.

One reproducible defect was the mismatch between world capture and receiver
classification. World capture accepts a rigid coordinate proof after a shader's
identity changes. Prelit classification previously required a catalogued WMO
hash, even when the independent RGB slice still proved vertex-colour lighting
without normals or global-light constants. Such a receiver incorrectly reached
the ordinary compositor, which subtracts occluded native directional light.
Subtracting cool daylight from baked RGB can leave a dark red result.

Prelit registration now shares world admission and then runs the existing RGB
proof. Coordinate admission alone does not grant prelit behaviour. Known self-lit
M2 shaders keep their existing classification. No shadow-map, light-colour,
exposure, GI-strength or user-profile settings are changed.

The client regression audits available stock WMO programs and changes their
identity with a bytecode comment. It preserves prelit classification, rejects
outdoor RGB dependencies, and rejects altered model-view coordinates.
The GPU regression uses actual `Device::CreateVertexShader` registration and
the production `WorldRenderer` SDR/HDR compositor. On the same neutral authored
surface, ordinary admission under a fully removed cool directional term is the
negative control; prelit admission retains the original colour at both direct
lighting endpoints. The fixture does not replay the screenshot scene or measure
game performance.

`NAMEMASK` diagnostics now include `prelit` and `unlit` draw counts. A fresh log
and matched provider captures from the affected installation are still needed
to establish whether this defect explains that installation's screenshots.
