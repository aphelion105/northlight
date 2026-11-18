# Northlight 0.3.218.14

World and character shadows share the directional refresh budget. Nearby characters use the near cascade. Point-light shadows batch compatible caster ranges and reject cube faces using the captured caster bounds. Normal capture can reuse resident geometry, verified section poses and tracked draw state. GI can begin lighting the world while static mesh uploads continue.

The static directional shadow renderer now shares identical ordered placement transforms across model sections. Each section keeps its own draw, bounds, material and depth shader. In the two-pass regression scene with 700 placements and 41 sections, the instance stream uploads 67,200 bytes, down from 2,755,200. Both paths submit the same 57,400 transforms in the same order.

In an isolated DXVK point-shadow benchmark, grouping repeated geometry reduced 1,536 draws to 48 and GPU time from 0.310 to 0.099 ms. A separate animated-palette case went from 447 draws to 120 and from 0.281 to 0.172 ms. These measurements cover the individual workloads. The available Northrend gameplay capture predates the final shadow changes.

## Installation

Close the client and run the installer for your platform. For a manual renderer update, install both `d3d9.dll` and `northlight-assets.bin`. The archives include the converted atmosphere table. Download checksums are in `SHA256SUMS.txt`.
