# r78: retain native normal capture with sRGB writes

Baseline: `dc2831c14d97f9dd738112a8caa23bfd0ddcc1b5` (r77).
Production source identity:
`9585bcc01dc54991946ec5e32754f6863390428b1bcc4cc2535bfc6baefc30c6`.
The latest available client and QA logs still identify r73; no later
gameplay measurement is available. The [r77 report](renderer-throughput-20261110-r77.md)
records their CPU capture costs and combined `blend-srgb` fallback.

## Change

Query the existing FP16 normal texture's format support once at device
construction. If sRGB writes are explicitly unsupported, D3D9 leaves that
format linear even when the native colour target uses sRGB writes
([Microsoft contract](https://learn.microsoft.com/en-us/windows/win32/direct3d9/gamma)).
That native state no longer cancels the entire normal transaction. The
existing MRT output can finish without the separate normal replay.
Post-pixel blending support is also checked for the actual FP16 format,
as required by the [MRT contract](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multiple-render-targets).
Failed/unknown queries retain replay, rather than assuming support.

Native colour conversion and normal calculations remain independent.
No target, mask, shader variant, frame query, quality setting or normal
encoding is added. The HDR sky foreground path keeps its own eligibility
and colour transform; this change does not relax its sRGB restriction.

## Validation

Native D3D9 and DXVK 2/3 each pass 7,038 GPU cases: 21,114 total. Native
RGBA and valid normal/depth lanes remain exact; invalid zero-XYZ pixels
ignore W. The matrix includes sRGB receivers, sRGB blended depth writers,
materials off/POM/displacement, triple local lighting, HDR on/off, alpha
tests, target/viewport/depth changes, MSAA, shader faults and fill failure.
Failed format queries explicitly retain the `srgb-format` fallback.
Independent consumer/cancellation checks also pass.
The Zig 0.15.2 production build and Windows package
`0.3.217-test.20261110.3` pass freshness, payload and export verification.

Three alternating before/after pairs on RTX 4090 / DXVK 3.1.1 measure 256
resident draws at 3840x2160 with native sRGB writes enabled. Milliseconds
cover capture, native submission and replay or commit only:

| Receiver / vertices per draw | HDR | GPU before → after | CPU before → after |
| --- | --- | --- | --- |
| Plain / 544 | off | 0.129024 → 0.073728 | 0.2817 → 0.3158 |
| Plain / 544 | on | 0.139264 → 0.086016 | 0.2778 → 0.3134 |
| Plain / 2176 | off | 0.409600 → 0.209920 | 0.2829 → 0.3167 |
| Plain / 2176 | on | 0.420864 → 0.223232 | 0.2840 → 0.3121 |
| Three local lights / 2176 | off | 0.558080 → 0.321536 | 0.5103 → 0.4789 |
| Three local lights / 2176 | on | 0.561152 → 0.330752 | 0.5035 → 0.4780 |

GPU intervals may contain CPU command starvation. Plain-case CPU time
increases by 0.028–0.036 ms; local-light CPU time improves by about 5–6%.
This is a subset benchmark, not whole-game FPS or AMD performance.

Two point-face clipping loop rewrites preserved all 174,335 reference
masks but were slightly slower in paired measurements. Both were reverted;
the existing culler and its conservative coverage remain unchanged.
Detailed receipts, raw logs, benchmark controls and binaries remain in
ignored `out/optimization-20261110-r78/`.
