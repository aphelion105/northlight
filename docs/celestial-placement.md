# Celestial placement and size

The **Sun & moon → Celestial placement & size** selector saves
`[Quality] CelestialStyle=Default` or `Forever` in `northlight-quality.ini`.
Default follows the client's visible billboard positions and animated sizes.
Forever uses the locally recovered daily orbit and enlargement tables, with
Northlight's solar calibration and the client's unreduced lunar base:

| Body | Base angular radius | Imported enlargement |
|---|---|---|
| Sun | `atan(0.506736 / 24)` | 1–2 × linear size |
| Moon | `atan(1.75 / 24)` | 1–1.5 × linear size |

The original lunar calibration multiplied the base by `0.46176318`, making
the disc about half as wide. Forever placement now omits that historical
reduction. Native base-size and animated-size fields do not multiply it again.
The earlier orbit's horizon constants and Outland planet scale remain separate.

The absolute diameter is a Northlight artistic choice, not a recovered Forever
constant. The provenance and limitations of
the imported orbit/scale tables are recorded in the
[import investigation](../renderer/docs/expansion-atmosphere-and-celestial-20261112.md).
Discs, halos, lighting and fog use the same resolved size and position. HDR
lunar surface radiance uses a linear gain of 8 instead of 2 before atmospheric
extinction, retaining texture contrast and energy above the bloom threshold.
The early, late and native-sky residual paths share this gain. World lighting,
fog intensity and global exposure are independent.

`test_expansion_atmosphere.py` checks equality across Azeroth, Kalimdor and
Northrend, and both bodies at four time keys with nine
different combinations of native size/base size, repeated resolution without
compounding, and preservation of Default mode. Existing native celestial and
orbit tests cover the unchanged default and the shared horizon calculations.

## Expansion celestial bodies

Northrend uses the same calibrated Forever sun/moon angular radii as Azeroth,
including the imported daily size curves. No native skybox size is multiplied
on top. Early sky drawing and late world/light resolution receive the current map.

Outland (`Expansion01`) keeps its authored planetary textures, positions, rotation,
relative sizes and rings. The ordinary Azeroth lunar disc/halo is disabled there in both placement modes.
The existing night-light budget is retained; skybox planets keep their native
material emission without inventing additional directional lights.

In **Forever**, reviewed planet meshes scale by `0.46176318` about their original
centers, reusing Northlight's existing lunar linear-size calibration. Both ring
halves scale about their parent planet's center. These are extrapolated Outland
sizes, not recovered Forever continent data. Planets do not follow the lunar
orbit or lunar size animation. **Default** submits the original planet geometry.

The material catalog records exact planet input geometry from all available
LightSkybox models/skins. Runtime accepts only matching 4/8-vertex indexed meshes
in the main Outland sky band, with a single stream and shared bone weights. It
copies at most eight vertices and twelve indices, changes position only, and
uses the same submission for the native, transmission and HDR passes. Unknown
geometry/read failures keep the original draw. The client's buffers/assets are
never modified; UP submission restores stream 0 and the index binding.

Checks: `test_sky_planets.py` covers catalog geometry, ring anchors, unchanged
attributes and map/style transitions; `test_sky_planets_gpu.py` compares actual
production submissions with independently scaled references and verifies native
bindings/buffer contents. `test_sky_material_catalog.py` rebuilds the catalog
against the configured client.

Native emission audit: the six reviewed planetary skybox models contain zero
M2 light records. Their planet materials are unlit (flag bit 1), usually alpha
blended; Netherstorm's ring uses additive blend mode 4. Scaling retains those
materials, shader constants and blend modes, including HDR emission. It changes
neither native light records nor environmental lighting data. The GPU oracle
also checks additive blending and radiance above 1 on a float target.
