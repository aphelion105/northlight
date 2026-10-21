# Independent and combined testing

The feature PRs each contain a complete buildable checkout. Shared prerequisites are included in dependent branches so testing does not require merging unrelated feature branches. Review and merge the focused PRs first; this branch is an assembled integration snapshot for combined testing, including the previously published foliage/emitter work. It is not the review unit for an individual feature.

| Scope | PR | Included prerequisites |
| --- | --- | --- |
| Build/test tools and explicit compiler provenance | [85](https://github.com/lepola/northlight/pull/85) | main |
| Optional DXVK 3 engine-name workaround | [86](https://github.com/lepola/northlight/pull/86) | 85 |
| World capture and UI boundaries | [87](https://github.com/lepola/northlight/pull/87) | 85 |
| SSAO/HDAO/HBAO+/GTAO techniques | [88](https://github.com/lepola/northlight/pull/88) | 85 |
| General render performance | [89](https://github.com/lepola/northlight/pull/89) | 85 |
| Celestial clouds, moon and halo composition | [90](https://github.com/lepola/northlight/pull/90) | 85 |
| Settings menu and transactional live controls | [91](https://github.com/lepola/northlight/pull/91) | 85 |
| Local-source shadows and selection budgets | [92](https://github.com/lepola/northlight/pull/92) | 85 |
| Water, SSR and weather shelter | [93](https://github.com/lepola/northlight/pull/93) | 85, 87 |
| Experimental HDR presentation | [94](https://github.com/lepola/northlight/pull/94) | 85, 87 |
| Automatic normals, POM, self-shadowing and cavity | [95](https://github.com/lepola/northlight/pull/95) | 85, 87 |
| UI atlas diagnostic components | [96](https://github.com/lepola/northlight/pull/96) | 85 |
| Signed height, displacement and animated models | [97](https://github.com/lepola/northlight/pull/97) | 85, 87, 95 |

Existing related PRs cover [foliage shadows](https://github.com/lepola/northlight/pull/76), [world emitter catalogs](https://github.com/lepola/northlight/pull/82), [equipment/particle lights and installer migration](https://github.com/lepola/northlight/pull/83), and [native detailed-shadow installation policy](https://github.com/lepola/northlight/pull/84). This snapshot includes the local foliage/catalog/equipment implementations and the catalog's per-source shadow metadata. The native installation policy remains a separate review.

The combined tree connects menu AO preview, material/height controls, HDR status, water and local-source lighting. Its materials source was frozen at commit 695c23b; no live checkout was edited. New atlas diagnostics remain opt-in components without a game hotkey. HDR remains off by default.

Configure northlight.local.ini and follow tests/README.md. Run scripts/check_layout.py, scripts/affected_tests.py --why and the selected plain test scripts. Full runs belong to shared-tooling, release or main validation. Windows native Zig adaptations used for local host checks do not provide ASan/UBSan/TSan certification. Missing host requirements and manual in-game checks must remain explicit.

Generated HLSL headers, bytecode, assembly and manifests must agree. Generator commands use the pinned compiler by default; --compatible-compiler explicitly allows the separately verified Microsoft SDK binary and records its actual hash. Client-extracted fixtures and test-generated shaders, captures, DLLs and logs belong in ignored output. Committed GPU baselines are Northlight's own shader code.

The integrated performance fixture uses the combined shader interface with only interval skipping and flattened taps reverted. Rebuild it with tests/regenerate_integrated_perf_baseline.py after shader changes; tests/test_five_path_perf.py verifies its source hash and paired output. The isolated performance PR retains its upstream baseline. GPU timestamps and API-call counts are fixture measurements; representative in-game visuals, LOD transitions, overlays and FPS remain manual review work.
