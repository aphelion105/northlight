# Local shadow pixelation

The 12.83-second `qa_report/2026-11-15_20-22-34.mp4` shows a character shadow
changing from a soft outline to coarse square steps on a nearby Telredor wall.
The video does not contain a renderer build ID or captured frame inputs. It
establishes the visible symptom, not an exact replay of the affected renderer.

The deferred local-light filter had a reproducible matching defect. Its contact
PCF averaged four POINT depth comparisons equally. Receiver motion across cube
texels therefore changed visibility in quarter steps, including half steps when
two samples crossed together. Wider PCSS used sixteen equally weighted POINT
comparisons and abruptly replaced the contact footprint when its radius grew.

The contact path now bilinearly weights four depth comparisons. Depths remain
point-sampled: interpolating depth before comparison would introduce false
occlusion. Receiver-plane depth correction follows the sampled texel, including
neighbouring cube faces and the three-face corner. Soft shadows expand nine
interpolated footprints from zero additional support, converging to the contact
filter instead of switching to a different footprint at its minimum radius.

Map resolution, caster capture, bias, the emitter-size model, and directional
and native-material shadow paths are unchanged. Hard PCF still reads four cube
texels. Soft contact filtering uses the existing sixteen blocker-search reads
plus four comparisons; a wider penumbra uses sixteen plus thirty-six, compared
with the former four plus sixteen plus sixteen. This is a bounded extra cost
for wider local penumbras; no gameplay FPS improvement is claimed. The compiled
production PCSS shader remains within Shader Model 3's 512-slot limit.

`test_local_shadow_continuity_gpu.py` tests subtexel motion on six faces, both
axes, face centres/edges/corners and 128/256/512 cube sizes. Contact results
match an independent linear-coverage oracle. The previous filter is retained
as a negative control. Broader existing caster/receiver tests cover actual
rasterized planes, blockers, grazing angles and translated world coordinates.
These image contracts run on system D3D9, DXVK 2 and DXVK 3; they do not recreate
the video scene or establish macOS/Linux gameplay behaviour.
