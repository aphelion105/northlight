# HDR rain over the sky (r75)

Rain without depth writes must enter the owned linear HDR sky as well as the
native scene. Otherwise the final clear-depth sky pixels discard its native
colour, while geometry retains the rain through the ordinary scene decode.

The foreground admission previously required the model viewport band
`0..0.94` and enabled depth testing. A recognized weather draw using `0..1`
therefore missed that capture. The native draw wrapper now retains its identity
through the original native draw, including when supplemental `Weather=0`,
allowing these weather states without changing
admission for other transparent draws, UI or unrelated targets.

Fixed-pixel rain with VS 1.1/2.0 can also use the existing foreground MRT path
without the optional rain mask. Its narrowly checked texture-times-diffuse
source uses PS 2.0, retains the original native output and writes linear colour
to the existing sky target in the same draw. Unsupported transfers retain the
compatibility replay. Its simple legacy-VS transfer reuses the existing PS 2.0
shader. No additional image or full-screen pass is allocated for the MRT path.

## Validation

- The previous production admission reproduces missing sky rain with viewport
  `0..1` and depth testing enabled: red stays `2.0` instead of `1.02903`.
  This failure uses current transfer helpers, isolating the admission bug.
- 2,016 rain cases pass on Native D3D9 and DXVK 2/3, including DXVK MSAA
  0/2/8, both vertex interfaces, optional mask, full-depth viewport, disabled
  depth testing, occlusion, alpha test, fog and missing-MRT fallback.
  Native RGBA bytes remain exact; sky results match the linear blend within
  `0.003`, including the existing RGBA8 fog-layer quantization.
- MRT cases submit native geometry once and preserve occlusion query samples.
  Failure/exception checks restore the scoped rain identity. There are 140
  rejected fixed-pixel states and 112 general admission controls across the
  seven backend/MSAA runs, including unrecognized full-depth draws.
- Existing smoke, native foreground, spell-family and sky wrapper cases pass.
  Three-backend replay and additive UV/state/side-effect oracles, HDR patch
  ownership, colour-cache lifetime, UI boundary and weather dispatch pass.
- All 30 existing HDR shader entries are byte-identical. The new source uses
  three SM2 slots, one temporary and one sampler. Production shader freshness,
  Zig compiler preflight, x86 exports and installer verification pass.

The attached client frame has not been replayed; the regression proves the
offending render-state path rather than an in-client visual acceptance.
Runs are independent correctness checks with a running game allowed, without
FPS or isolated GPU performance claims. The installed client is unchanged.

## Delivery

Package `0.3.217-test.20261109.9` includes the previous r73/r74 work.
Source identity: `d0f48b9a7537caea8bc18a60f41cf7a8f4b8a427c3defe9e889b9c7a72ef7672`.
DLL SHA-256: `fc6106641c3fa878dd2a97fba5bf4a4e86c1614784085bf397c28a054b0957ee`.
The adjacent validation JSON records the test receipts and package hashes.
