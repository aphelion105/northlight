# Model normals and point shadows in a crowded Northrend scene

Two stationary-camera runs at 3840×2160 with 2× MSAA used the same position and direction. Each run toggled Northlight effects on, off and on again with Ctrl+Shift+F10. The off interval kept the D3D9 proxy and HDR loaded.

| Build source | On, FPS | Off, FPS | Back on, FPS | On frame, ms | Off frame, ms | Difference, ms |
|---|---:|---:|---:|---:|---:|---:|
| `519eddc4` | 35.15 | 87.14 | 35.52 | 28.30 | 11.48 | 16.83 |
| `d680bb48` | 34.48 | 79.45 | 39.14 | 27.28 | 12.59 | 14.69 |

Both runs used the same comparison horizon of 10.547 seconds per state. Only complete, focused 120-frame windows were included, with two seconds of settling after each switch. The two on intervals have equal weight in the frame-time difference. The new run contributed 240, 720 and 360 frames. The on frame improved by 1.03 ms and the off frame took 1.11 ms longer. The crowd, lights and GPU load varied. The installed builds also differed in lighting changes, and a background worker test finished early in the new recording.

## Normal capture

Commit `3baaa913` gives each queued normal packet a reference to a stable float bank. Existing epoch and byte comparisons determine when packets can share that bank. A batch retains every published bank until all its draws finish. Active and spare bank storage count toward the existing 8 MiB limit.

The old queue flushed twice in all four profiled on frames. The new queue flushed once in all five, holding 1,324–1,449 packets in 697–731 banks. It still submitted 958–1,020 normal draws per sampled frame. CPU capture medians for the two on intervals went from 7.20 and 7.41 ms to 6.60 and 5.87 ms.

On a controlled 1,500-packet stock-model fixture, the old queue peaked at 1,306 inline banks and the new queue at 600 owned banks. CPU capture through final submission took 3.786 and 2.784 ms respectively on DXVK. The readback images were byte-identical. The `--constants-only` mode of `tests/test_model_normals_gpu.py` exercises the capacity and image checks, including changes to poses and device state.

## Point shadows

The instance draw binds its own shader and vertex declaration. After each packet passes the existing skinning and coverage checks, the grouping key uses the actual index and vertex addresses and the bone palette window. Native material variants can therefore share a draw. Cutouts keep their ordering barriers.

The 100-model, 16-layout fixture in `tests/test_point_palette_gpu.py` used 64 submissions for one cube with the old grouping and 44 with the new grouping. Measured across ten lights including GPU completion, the elapsed time went from 0.326 to 0.260 ms. Six-face depth agreed with individual model replay within the existing 0.000002 tolerance on native D3D9 and DXVK.

In the game only one of five profiled on frames used instancing, combining six packets in three draws. Actor replay still issued 2,021–7,732 draws for point shadows. Across static and animated casters, the cubes received 1.92–6.71 million triangles per sampled frame. Median GPU times were 1.60 ms for model normals, 1.26 ms for point shadows, 0.76 ms for final world composition and 0.50 ms for the fog march. Point shadows reached 3.14 ms in the highest-work sample. Processing ordinary caster geometry across lights and faces remains the larger opportunity in this scene.
