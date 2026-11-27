// The settings window's contents. See settings_panel.h; the draft rules are in settings_draft.h.
#include "settings_panel.h"
#include <cctype>
#include <deque>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_internal.h"
#include "settings_service.h"
#include "diagnostic_capture_request.h"
#include "qa_scene_capture.h"
#include "settings_draft.h"
#include "memory_profile_snapshot.h"
#include "performance_hardware.h"

#ifndef NORTHLIGHT_SOURCE_ID
#define NORTHLIGHT_SOURCE_ID "unrecorded"
#endif
#ifndef NORTHLIGHT_BUILD_UTC
#define NORTHLIGHT_BUILD_UTC "unrecorded"
#endif

namespace NorthlightOverlay {
namespace {
using NorthlightSettings::Field;
namespace D=NorthlightSettingsDraft;

namespace Color {
const ImVec4 Accent{0.40f,0.70f,1.00f,1.f},Edited{1.00f,0.74f,0.32f,1.f},Muted{0.60f,0.63f,0.68f,1.f},Good{0.48f,0.84f,0.54f,1.f},
    Bad{1.00f,0.48f,0.43f,1.f},Restart{1.00f,0.60f,0.26f,1.f},Heading{0.94f,0.81f,0.52f,1.f};
}

namespace W {
void hint(const char* text){
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(Color::Muted,"%s",text);
    ImGui::PopTextWrapPos();
}

bool beginCard(const char* id){
    return ImGui::BeginChild(id,ImVec2(0,0),ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysUseWindowPadding,
                             ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
}

void endCard(){ImGui::EndChild();}

void cardTitle(const char* title){
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(Color::Heading,"%s",title);
}
}

// ---- Words (from the retired Lua menu) ----
const std::map<std::string,const char*> Labels={
    {"Palette","Whole-world appearance"},{"day_vibrance","Vibrance"},{"dusk_vibrance","Vibrance"},{"night_vibrance","Vibrance"},{"tone_curve_strength","Tone curve strength"},{"day_shadow_level","Shadows (25%)"},{"day_midtone_level","Midtones (50%)"},{"day_highlight_level","Highlights (75%)"},
    {"night_shadow_level","Shadows (25%)"},{"night_midtone_level","Midtones (50%)"},{"night_highlight_level","Highlights (75%)"},{"day_fog_tint","Fog colour"},{"day_fog_tint_strength","Fog tint strength"},{"day_fog_brightness","Fog brightness"},{"dusk_fog_tint","Fog colour"},{"dusk_fog_tint_strength","Fog tint strength"},{"dusk_fog_brightness","Fog brightness"},{"night_fog_tint","Fog colour"},{"night_fog_tint_strength","Fog tint strength"},{"night_fog_brightness","Fog brightness"},{"shadow_contrast","Shadow contrast"},
    {"WaveStrength","Wave strength"},{"WaveSpeed","Wave speed"},{"WaveCrestShape","Crest shape"},
    {"WaveCrestSharpness","Crest sharpness"},{"RippleStrength","Ripple strength"},
    {"FoamStrength","Foam strength"},{"OpenWaterCrestFoam","Foam on open-water crests"},
    {"ReflectionScenery","Reflections"},{"ReflectionResolution","Reflection resolution"},{"ReflectionDistance","Reflection distance"},{"ReflectionSceneryDetails","Nearby reflected scenery"},{"FineHighlights","Fine water and slime highlights"},{"Clarity","Water clarity"},{"SurfaceBlur","Depth blur"},{"Reflection","Reflection strength"},{"NearReflection","Near reflection strength"},
    {"Underwater","Underwater effects"},{"UnderwaterVisibility","Visibility (yards)"},{"UnderwaterScenery","Underwater scenery"},
    {"Slime","Slime surface"},{"Lava","Magma and fel magma surfaces"},
    {"CelestialStyle","Sky style"},
    {"CelestialEffects","Enhanced sun, moon & sky"},{"Godrays","Sun & moon godrays"},
    {"TreeCanopy","Canopy directional lighting"},{"TreeCrown","Crown backlighting"},
    {"NativeLightStay","Keep off-screen lights"},
    {"NativeMoreLights","Expand native light slots"},{"NativeModelLightLimit","Lights per model / building"},{"NativeTerrainLightLimit","Lights per terrain chunk"},
    {"RainRivulets","Merge drops into rivulets"},{"SnowFrost","Frost at screen edges"},{"SnowFrostStrength","Frost strength (%)"},
    {"WeatherWindDirection","Wind heading (world degrees)"},{"WeatherWindStrength","Wind strength (%)"},{"WeatherCameraMotion","React to camera turns"},
    {"SandLens","Sand on lens"},{"SandLensStrength","Sand strength (%)"},{"SandLensSize","Sand grain size (%)"},
    {"RainDropSize","Rain particle size (%)"},{"SnowFlakeSize","Snow particle size (%)"},{"RainLensSize","Camera droplet size (%)"},{"SnowLensSize","Camera snow clump size (%)"},
    {"AutoBump","Material detail"},{"AutoBumpStrength","Normal map strength (%)"},{"AutoBumpDistance","Normal map distance (yards)"},
    {"AutoBumpTerrain","Ground and roads"},{"AutoBumpWalls","Walls and floors"},{"AutoBumpGrass","Grass tufts"},
    {"AutoParallaxDepth","POM depth (%)"},{"AutoParallaxDistance","POM distance (yards)"},{"AutoParallaxSteps","POM trace steps"},
    {"AutoParallaxShadows","POM self-shadowing"},{"AutoParallaxShadowStrength","Self-shadow strength (%)"},{"AutoParallaxShadowSteps","Self-shadow trace steps"},
    {"AutoCavity","Cavity shading"},{"AutoCavityStrength","Cavity strength (%)"},
    {"AutoHeightPolarity","Height interpretation"},{"AutoHeightDetails","Colored height detail (%)"},{"AutoNormalFilter","Material normal filter"},
    {"Preset","Quality preset"},{"Effects","All Northlight effects"},{"WorldShadows","World shadows"},{"WorldFog","Atmospheric fog"},{"LocalLights","Local light illumination"},{"GI","Global illumination"},{"GIStrength","GI influence (120 = full)"},{"GIBrightness","GI brightness adjustment (%)"},{"GIPrelitMix","GI / baked lighting blend (%)"},{"AuroraStrength","Northrend aurora (%)"},
    {"ShadowStrength","Shadow strength (%)"},{"ShadowSoftness","Penumbra size (%)"},{"ShadowContactRadius","Contact filter radius (0.01 texel)"},
    {"SoftShadows","Contact-hardening shadows"},{"ActorShadows","Character shadows"},{"ActorShadowDetail","Detailed character shadows"},{"GrassShadows","Grass shadows"},{"ActorShadowRadius","Character shadow distance"},
    {"ContactAO","Ambient occlusion"},{"AOMethod","AO method"},{"AOQuality","AO quality"},{"AutoBumpModels","M2 models (all types)"},{"AutoMaterialOcclusionDistance","Cavity and self-shadow distance (yards)"},{"AutoModelDisplacementDistance","Model geometry distance (yards)"},{"AutoModelDisplacementDetail","Model subdivision"},{"AutoModelDisplacementStrength","Model height (%)"},{"AutoDisplacementDistance","Terrain and object geometry distance (yards)"},{"AutoDisplacementStrength","Terrain and object height (%)"},{"AutoDisplacementDetail","Terrain and object subdivision"},{"AutoHeightBalance","Height balance (%)"},{"AOStrength","AO strength (%)"},{"AORadius","AO radius (0.01 yd)"},
    {"Weather","Enhanced weather"},{"WeatherMemory","Read native weather state"},{"RainFog","Weather fog strength"},{"RainLens","Rain on camera"},{"RainLensStrength","Rain on camera strength (%)"},{"SnowLens","Snow on camera"},{"SnowLensStrength","Snow on camera strength (%)"},{"FogClouds","Drifting fog banks"},{"FogCloudDensity","Fog density (%)"},
    {"HorizonHaze","Horizon haze (%)"},{"HorizonHazeStart","Haze start (%)"},{"HorizonHazeBand","Haze height (degrees)"},{"HorizonHazeTerrain","Haze over terrain"},
    {"Backend","Graphics backend"},{"ExperimentalHDR","Enable HDR"},{"HDRPaperWhite","White point (nits)"},{"HDRUIWhite","UI brightness (nits)"},{"HDRMaxNits","Manual peak (nits)"},{"HDRContrast","Contrast"},{"HDRExposure","Exposure / compensation (EV)"},
    {"HDRExposureMode","Exposure mode"},{"HDRPeakMode","Peak mode"},{"HDRWideBloom","Wide soft bloom"},{"HDRBloomStrength","Bloom strength"},{"HDRLegacyExpansion","Legacy highlight expansion (EV)"},
    {"HDRAutoMinEV","Auto exposure minimum (EV)"},{"HDRAutoMaxEV","Auto exposure maximum (EV)"},{"HDRLightAdaptation","Bright adaptation T90 (s)"},{"HDRDarkAdaptation","Dark adaptation T90 (s)"},
    {"NightBrightness","Night brightness (%)"},{"EquipmentLights","Equipment and effect lights"},
    {"GIPreserveRigidRoomLight","Preserve baked room light on static props"},
    {"ShadowRefreshCpuMs","Shadow map refresh target (ms)"},
    {"CaptureBudgetMiB","Capture memory (MiB)"},{"ActorShadowBudgetMiB","Character shadow memory (MiB)"},{"MinSkinnedTriangles","Minimum character triangles"},{"LocalLightLimit","Total local lights (0 = off)"},{"GpuBudgetMs","GPU effect budget (ms)"},{"ActorShadowCpuMs","Shadow CPU budget (ms)"},
    {"CommandStream","Separate render thread"},{"StreamFramesAhead","Buffered frames"},{"StreamFrameSkip","Skip delayed frames"},{"StreamOcclusionLocal","Local occlusion results"},{"ReplayJobs","Parallel effect preparation"},{"FrameDrawGates","Skip inactive draw work"},
    {"PointShadowLimit","Of these, shadowed lights"},{"PointShadowDistance","Shadow distance (yd)"},{"PointShadows","Local light shadows"},{"PointShadowRefreshMs","Shadow update interval (ms)"},{"PointShadowFacesPerFrame","Faces updated per light"},{"PointShadowAdaptiveRefresh","Adaptive lamp shadow updates"},{"BlobShadows","Round ground shadows"},{"ShadowProvider","Directional shadow renderer"},{"DisableOriginalTerrainShadows","Disable original terrain shadows"},{"BlobShadowStrength","Ground shadow strength (%)"},
    {"FarShadowInterval","Distant shadow interval (frames)"},{"NearShadowInterval","Near environment shadow interval (frames)"},{"StaticCacheSlices","Static cache update slices"},{"ShadowDirectionSteps","Shadow direction precision"},{"ShadowPivotCorrection","Correct shadow pivot"},
    {"GIDistance","GI distance (yards)"},{"GIChunkRadius","GI geometry chunk radius"},{"GIProbeAhead","GI look-ahead (yards)"},{"GIRays","GI rays per probe"},{"GIBounces","GI light bounces"},{"GIThreads","GI solver threads"},{"GIFastBVH","Fast geometry tracing"},{"GIProbeMoveStep","GI update movement step"},{"GIDynamicProbes","Dynamic GI probes"},
    {"SpellLayer","Separate spell effects"},{"SpellLayerClasses","Spell blend types (mask)"},{"SpellFog","Fog on spell effects"},{"NameMask","Protect unit names"},{"DepthFuncAware","Respect draw depth tests"},{"terrain_radius","Terrain shadow reach (yards)"},
    {"Diagnostics","Diagnostic logging"},{"RenderProfile","Render profiling"},{"DiagReplayProbe","Shadow replay probe"},{"ShadowFateDiagnostics","Shadow lifetime diagnostics"},{"SpellTrace","Spell trace frames"},{"NameMaskDebug","Show name protection mask"},{"MemMap","Address space diagnostics"},
    {"local_surface_specular","Lamplight highlights"},
};
// Shorter row labels where the group title already gives the context.
const std::map<std::string,const char*> ShortLabels={
    {"CelestialStyle","Sky style"},{"ReflectionDistance","Reflection distance"},{"ReflectionResolution","Reflection resolution"},
    {"ReflectionScenery","Reflected scenery"},{"ReflectionSceneryDetails","Reflected scenery details"},
    {"FineHighlights","Fine highlights"},{"SurfaceBlur","Reflection blur"},
    {"GIStrength","GI influence"},{"GIBrightness","Brightness adjustment (%)"},{"GIPrelitMix","Baked lighting blend (%)"},{"AuroraStrength","Northrend aurora (%)"},{"GIDistance","GI distance (yd)"},{"GIBounces","Light bounces"},{"AOStrength","Strength (%)"},{"AORadius","Radius (0.01 yd)"},
    {"ActorShadowRadius","Distance (yd)"},{"ActorShadowDetail","Detailed shadows"},{"EquipmentLights","Equipment lights"},{"FogCloudDensity","Density (%)"},
    {"HDRMaxNits","Manual peak (nits)"},{"AutoBumpDistance","Relief distance (yd)"},
};
const std::map<std::string,const char*> CelestialLabels={
    {"disc","Disc tint"},{"disc_gain","Disc brightness"},{"light","Light tint"},{"light_mix","Light tint mix"},{"light_strength","Light strength"},{"fog_gain","Fog illumination"},
    {"fog_height_scale","Fog height"},{"fog_density","Air density"},{"horizon_haze","Horizon haze"},{"glow_strength","Glow strength"},{"glow","Glow tint"},{"glow_mix","Glow tint mix"},
    {"surface_specular","Sunlight highlights"},{"wet_stone_specular","Wet stone highlights"},{"terrain_ice","Ice response"},
    {"terrain_snow","Snow response"},{"terrain_specular","Moonlight on terrain"},{"foliage_transmission","Leaf transmission"},
};
const std::map<std::string,std::vector<const char*>> Help={
    {"CelestialEffects",{"Replaces the sun, moon, glare and sky background independently of AO, shadows and other effects. Off preserves the native sky and skips Northlight sky capture and composition. HDR output remains separate."}},
    {"TreeCanopy",{"Angle-dependent highlights and transmission on tree leaves. Requires the Forever sky style."}},
    {"TreeCrown",{"Broad transmission when the sun or moon is behind a tree crown. Requires the Forever sky style."}},
    {"Godrays",{"Directional sunlight and moonlight scattered through atmospheric fog. Off skips directional fog shading, shadow sampling and extra source passes while retaining fog density, ambient scattering and local lights. Requires Atmospheric fog."}},
    {"CelestialStyle",{"Default uses the game's sky colors and celestial placement. Forever uses imported colors in Eastern Kingdoms and Kalimdor and approximated regional sky colors in Northrend and Outland. Northrend shares the Forever sun and moon curves. Outland keeps its native sun visibility and skybox planets."}},
    {"RainRivulets",{"Falling drops and their wet trails collect nearby beads into longer curved streams, conserving water. Off keeps drops independent."}},
    {"SnowFrost",{"Crystalline ice grows from the screen edges with accumulated snow and storm intensity. Nearby fire melts it. Independent of Snow on camera; requires Enhanced weather."}},
    {"SandLens",{"Dust grains and sparse deposits on the camera lens. They stay in glass coordinates, ignore scene geometry and receive optional inertial forces during camera turns. Shelter hides the effect."}},
    {"RainDropSize",{"Size of Northlight's supplemental rain particles; 100% keeps the original look. Native game particles are unchanged."}},
    {"SnowFlakeSize",{"Size of Northlight's supplemental snowflakes; 100% keeps the original look. Native game particles are unchanged."}},
    {"RainLensSize",{"Scales the camera droplets, including their collision and merging radius."}},
    {"SnowLensSize",{"Scales the camera snow masses, including their collision and merging radius."}},
    {"Effects",{"Stops scene effects, capture and background world/GI work together. Keeps your individual settings for reactivation. Uses the same bypass as Ctrl+Shift+F10. HDR output and this menu remain available."}},
    {"WorldShadows",{"Enables Northlight world and character shadows. Ctrl+Shift+F9 remains an independent temporary switch."}},
    {"WorldFog",{"Enables Northlight regional fog, atmospheric scattering, fog banks and horizon haze. Native game fog is preserved. Ctrl+Shift+F7 remains independent."}},
    {"LocalLights",{"Enables direct lighting and fog scattering from lamps, torches and equipment. Off skips their capture and selection without changing your saved light limit."}},
    {"AutoBump",{"Generates and caches normal maps from installed textures. Directional detail is applied at full screen resolution. MPQs remain unchanged. Supports MSAA."}},
    {"AutoBumpStrength",{"Higher values make details more pronounced and can exaggerate painted shadows. Start at 75%."}},
    {"AutoBumpTerrain",{"Includes grass, dirt and stone painted on the ground.","Off: original material. Normal maps: detailed light response. Normal maps + POM: shifts the original diffuse lookup to show depth at an angle. Displacement + normals: changes nearby geometry and keeps surface normal detail."}},
    {"AutoBumpWalls",{"Off: original material. Normal maps: detailed light response. Normal maps + POM: shifts the original diffuse lookup to show depth at an angle. Displacement + normals: changes nearby geometry and keeps surface normal detail."}},
    {"AutoBumpGrass",{"Applies to separate native grass tufts and respects their fade."}},
    {"AutoParallaxSteps",{"More steps improve grazing angles and cost more GPU time. POM fades out at its distance limit; grass tufts use normals only."}},
    {"AutoParallaxShadows",{"Relief blocks the dominant sunlight or moonlight. Requires Normal maps + POM for the material. Cavity and self-shadowing share the near material shading distance. More shadow steps cost more GPU time. Local lights retain their existing shadows."}},
    {"AutoCavity",{"Reduces ambient light inside small cracks inferred from the texture. Works with normal maps and POM; fog and direct light are preserved. Painted shadows can become too dark at high strength."}},
    {"PointShadows",{"Torches, candles, lamps and compact fires cast shadows. Diffuse glows and short-lived effects do not.","Only nearby eligible sources allocate maps (about 3.5 MiB each). More sources increase rendering cost."}},
    {"ShadowStrength",{"Controls sun and moon shadow contrast. 100% keeps physical visibility; higher values darken partial coverage and allow deeper shadow removal. GI and local lights retain their illumination."}},
    {"ShadowSoftness",{"Scales contact-hardening penumbrae for directional and character shadows. Zero keeps the contact filter. Distant cascades use a compact fixed filter."}},
    {"ShadowContactRadius",{"Minimum directional filter radius in hundredths of a shadow texel. Lower values preserve leaves and thin branches; higher values reduce shimmer."}},
    {"SoftShadows",{"Softens sun, moon and local-light shadow edges with distance from the caster. Covers captured characters, grass and world geometry. Off keeps ordinary filtered shadows and their existing range."}},
    {"BlobShadows",{"Shows the game's round shadow directly below characters, creatures and mounts. Off removes only this decal; detailed sun, moon and local light shadows remain enabled. Takes effect immediately while Northlight effects are on."}},
    {"BlobShadowStrength",{"Adjusts the native round shadow when detailed character shadows are composited. Turning Round ground shadows off hides it independently and preserves this strength for later."}},
    {"DisableOriginalTerrainShadows",{"In Northlight shadow mode, removes the original terrain shadow layer, including shadows baked into the map that remain at the game's lowest Shadow Quality. Works while Northlight scene effects are active, independently of World shadows. Applies immediately without restarting the game. Northlight sun, moon, character and local-light shadows keep their own controls. Baked lighting or darkness painted into textures and vertex colors remains. Turning all Northlight effects off restores the original game shading."}},
    {"RainLens",{"Moving refractive droplets; Merge drops into rivulets lets them collect water along their wet paths. Heavier rain increases their speed. Requires Enhanced weather; the effect disappears under shelter."}},
    {"SnowLens",{"Crystalline snowflakes adhere and collect into clumps. Nearby classified flames melt them into moving droplets; walls block this heat. Requires Enhanced weather; the effect disappears under shelter."}},
    {"PointShadowLimit",{"Of the total local lights, how many eligible lamps may cast shadows. The smaller of this value and Total local lights is used; this never adds more lights. Higher limits increase shadow rendering and map memory. Extra static maps can be retained in unused slots of this same budget."}},
    {"PointShadowAdaptiveRefresh",{"Alternate whole-cube updates for small lamps when the light stays still or moves slightly. Shadows can briefly lag."}},
    {"PointShadowDistance",{"How far from the camera local lights can cast shadows: 8 to 192 yards; 48 by default. Measured to the edge of each light's influence, so large lights can cast shadows from farther away.",
        "Higher distances can increase rendering cost; Of these, shadowed lights still limits how many lights cast shadows."}},
    {"HDRContrast",{"1.00 is neutral. Adjusts world contrast around middle grey; UI brightness is unchanged."}},
    {"HDRExposure",{"0 EV is neutral. +1 EV doubles world light; -1 EV halves it. UI brightness is unchanged."}},
    {"HDRMaxNits",{"Limits world HDR output. UI brightness remains independent; this does not raise SDR highlights to the chosen peak."}},
    {"ExperimentalHDR",{"Requires DXVK 2 or 3, the separate render thread, windowed mode and HDR enabled for the current Windows display. MSAA is supported."}},
    {"HDRLegacyExpansion",{"Up to +2 EV lifts legacy highlights and native aurora emission. At 0, auroras keep their base HDR emission and other legacy details stay unexpanded. UI and Northlight's added twilight emission are excluded. Clipped SDR detail cannot be restored."}},
    {"HDRExposureMode",{"Auto meters the world before bloom and UI. Exposure becomes compensation. Manual uses only the selected EV."}},
    {"HDRPeakMode",{"Auto follows Windows calibration / display metadata, with a 1000 nit fallback. Manual uses your saved per-display peak."}},
    {"HDRWideBloom",{"Adds a wider, softer glow around bright world light. Uses more GPU arithmetic and extra memory. Falls back to compact bloom if resources are unavailable. UI brightness is unchanged."}},
    {"HDRBloomStrength",{"Bloom reads unclipped HDR radiance. Setting 0 removes its halo while preserving the source's brightness."}},
    {"Backend",{"Which Direct3D 9 implementation the renderer runs on. Takes effect after a game restart."}},
    {"NativeLightStay",{"Keeps nearby torch and lamp lighting when their models leave the view. Takes effect after a game restart."}},
    {"NativeMoreLights",{"Raises supported native shaders to eight point-light slots. Takes effect after a game restart."}},
    {"NativeModelLightLimit",{"Maximum native point lights affecting each model or building. Applies live. Separate from Northlight's total light and shadow budgets."}},
    {"NativeTerrainLightLimit",{"Maximum native point lights affecting each terrain chunk. Applies live. More slots improve coverage near groups of lamps. The minimum limit can use the original shader; partial limits retain eight-light arithmetic."}},
    {"terrain_radius",{"How far terrain casts shadows in this region. Larger values cost more GPU time."}},
    {"GI",{"Adds indirect sky light, bounced colors and occlusion from surrounding geometry. Turning it off stops the background lighting solver and leaves the game's ambient lighting."}},
    {"GIStrength",{"Influence of measured sky light, bounced color and occlusion on terrain and objects. 120 fully replaces native ambient; 60 restores the old half blend; up to 240 strengthens the contrast. 0 suspends GI work."}},
    {"GIBrightness",{"Adjusts measured indirect light: negative values darken, positive values brighten. -100 removes it, 0 keeps it unchanged, +100 doubles it. Missing probes keep native lighting; direct sun and lamps are unchanged."}},
    {"GIPrelitMix",{"Blend GI with the game's authored building lighting. 0% preserves it; 100% applies resident-probe occlusion and bounce. Missing probes preserve the original. Direct sunlight, moonlight and lamps are unchanged."}},
    {"GIPreserveRigidRoomLight",{"Keep the game's authored room lighting on rigid props such as paintings and shelves. Turn off to let GI probes darken that light according to the baked lighting blend."}},
    {"AuroraStrength",{"Scales all native Northrend aurora emission and its light: 0 turns it off, 100 is normal, 200 doubles it. Extra green/purple emission strengthens it between sunset and moonrise. Surface illumination requires GI and resident probes; roofs and walls occlude it. Independent of enhanced rain/snow and fog."}},
    {"GIRays",{"Samples the surroundings in more directions for each lighting probe. More rays improve indirect lighting stability and detail, but increase background CPU work."}},
    {"GIBounces",{"How many times indirect light can reflect between surfaces. More bounces carry light farther into enclosed spaces and cost more background CPU time."}},
    {"GIProbeMoveStep",{"Camera movement in yards before the lighting grid moves. Smaller steps follow movement sooner but trigger more frequent background updates."}},
    {"GIDynamicProbes",{"Lets captured characters and other animated geometry affect indirect lighting. Turning it off reduces capture and lighting work; direct character shadows have a separate switch."}},
    {"GIThreads",{"CPU threads available to the background lighting solver. More threads can update lighting sooner, but compete with the game for CPU time. The solver also limits itself to available cores."}},
    {"GIFastBVH",{"Uses the faster geometry traversal path for indirect lighting rays. Leave enabled for normal play; disable to compare against the older tracing path when troubleshooting."}},
    {"GIChunkRadius",{"Geometry tile rings loaded for GI: 1 = 3 x 3 tiles (9), 2 = 5 x 5 (25), 3 = 7 x 7 (49). More tiles let distant probes trace real walls and blockers, at a higher background CPU and memory cost. Save and restart to apply."}},
    {"GIDistance",{"Size of the area covered by indirect lighting around the camera. A larger distance covers more scenery, but needs more memory and background CPU work and takes longer to populate."}},
    {"GIProbeAhead",{"Shifts the indirect lighting grid toward the camera's horizontal viewing direction. Helps prepare lighting ahead of you without enlarging the grid. 0 keeps it centered on the camera."}},
    {"NightBrightness",{"Restores some of the game's ambient light during outdoor nights. 0 keeps Northlight's darker night; 100 restores the native ambient level. Does not brighten daytime, interiors, the sky or water."}},
    {"ContactAO",{"Adds contact shading around corners, creases and overlapping surfaces using the visible scene depth. Turn off if this shading is too heavy or to save its GPU work."}},
    {"AOMethod",{"Chooses how contact shading is calculated. Methods differ in fine detail, halos and GPU cost. Compare them in the same scene; use Strength and Radius to adjust the chosen method's appearance."}},
    {"AOQuality",{"Controls the detail and sampling cost of ambient occlusion. Lower settings reduce GPU work but can lose fine contacts or appear noisier. The difference depends on the AO method."}},
    {"AOStrength",{"Darkness of ambient occlusion: 100 is the normal strength, 0 hides it. Lower this if creases look too dark. Disable Ambient occlusion to skip the effect's processing."}},
    {"AORadius",{"Size of the neighborhood used for contact shading, in hundredths of a yard: 200 means 2 yards. Larger values produce broader shading and can create halos around objects."}},
    {"LocalLightLimit",{"Total budget for nearby lights illuminating the scene and fog, including shadowed lights and equipment. Shadow settings cannot increase this value. Higher values increase rendering work. Outgoing lights can briefly remain while fading out. 0 disables local illumination and its capture."}},
    {"EquipmentLights",{"Adds local lighting from supported equipped items, mounts and effects. Uses their light records and supported particle emitters; a glowing texture alone does not guarantee a light. Shares the local light limit."}},
    {"AutoBumpDistance",{"How far generated surface normals remain visible. Larger distances add detail to more surfaces and increase the number of pixels that need material shading."}},
    {"AutoNormalFilter",{"Selects how normals are generated from diffuse textures on terrain, walls, grass and models. Both choices work with the selected material modes."}},
    {"AutoBumpModels",{"Applies material detail to model objects, including characters, creatures and props. Normal maps change lighting; POM adds apparent texture depth; displacement changes nearby geometry. Higher modes can exaggerate painted texture details."}},
    {"AutoParallaxDepth",{"Apparent depth of texture relief when using Normal maps + POM. 0 looks flat; high values can stretch or distort textures at shallow viewing angles. Does not change the object's silhouette."}},
    {"AutoParallaxDistance",{"How far POM texture relief remains visible before fading out. Larger distances shade more pixels with POM and cost more GPU time. Normal map distance is controlled separately."}},
    {"AutoDisplacementStrength",{"Height of generated relief on terrain and object geometry in displacement mode. Larger values make silhouettes more pronounced, but can distort surfaces whose textures contain painted shadows."}},
    {"AutoDisplacementDistance",{"Distance within which terrain and object geometry receives displacement. Larger distances process more geometry and increase CPU, GPU and memory costs."}},
    {"AutoDisplacementDetail",{"Subdivides terrain and object surfaces to resolve smaller displaced details. Higher levels add many more triangles and can substantially increase geometry work and memory use."}},
    {"AutoModelDisplacementStrength",{"Height of generated relief on model objects in displacement mode. Lower values keep characters and props closer to their original shape; high values can distort thin or painted details."}},
    {"AutoModelDisplacementDistance",{"Distance within which model geometry receives displacement. A larger distance includes more characters and props, increasing geometry work and memory use."}},
    {"AutoModelDisplacementDetail",{"Subdivides model geometry for finer displacement. Higher levels add more triangles, increasing CPU, GPU and memory costs, especially around many characters."}},
    {"AutoHeightPolarity",{"How texture brightness becomes height: 0 uses automatic interpretation, 1 raises bright areas, 2 raises dark areas. Try the opposite polarity if relief looks inverted; this also changes the polarity of model relief."}},
    {"AutoHeightDetails",{"Contribution of colored texture accents to automatic height interpretation. 0 uses brightness alone; higher values include more accent detail. Only applies with Height interpretation set to 0; does not change model displacement."}},
    {"AutoHeightBalance",{"Offsets the generated height profile: 50 is neutral, higher values raise it and lower values recess it. Does not change model displacement."}},
    {"AutoParallaxShadowStrength",{"Darkness of shadows cast by POM relief onto itself. Lower values soften the effect if painted textures look too dark. Requires POM self-shadowing and a material using POM."}},
    {"AutoParallaxShadowSteps",{"Samples used to trace sunlight or moonlight across texture relief. More steps improve self-shadow detail and reduce missed blockers, at a higher GPU cost."}},
    {"AutoCavityStrength",{"Amount of ambient darkening inside texture cracks. Higher values emphasize small recesses, but can double-darken details already painted into the texture. 0 hides cavity shading."}},
    {"AutoMaterialOcclusionDistance",{"Shared range for cavity shading and POM self-shadowing. Larger distances show these details farther away but increase GPU work. Normal maps and POM relief have their own distance settings."}},
    {"ActorShadows",{"Captures characters and other animated objects for detailed shadows, including your own character. Turning it off reduces capture and shadow rendering work; static world shadows remain controlled separately."}},
    {"ActorShadowDetail",{"Adds a dedicated nearby shadow map for finer character detail. Costs extra GPU time and memory. Turning it off keeps the ordinary character shadows."}},
    {"GrassShadows",{"Lets nearby native grass tufts cast shadows. Adds geometry capture and shadow rendering work, most noticeable in dense grass. Requires character shadow capture."}},
    {"ActorShadowRadius",{"Distance from the player focus within which animated objects are selected for shadows. Lower values reduce crowd-related work. 0 removes this distance cap; capture and memory budgets still apply."}},
    {"ActorShadowBudgetMiB",{"Memory budget for selecting character shadow geometry, prioritizing nearby objects. Lower values can omit distant or less important casters. 0 removes this selection cap; the overall capture budget still applies."}},
    {"CaptureBudgetMiB",{"Per-frame limit for captured animated geometry. Lower values reduce capture memory but can omit geometry used by effects. This is not a limit on the game's total RAM usage."}},
    {"MinSkinnedTriangles",{"Ignores animated draws smaller than this triangle count when capturing geometry. Higher values reduce work but can omit small weapons or attachments from effects. 0 keeps small draws eligible."}},
    {"FarShadowInterval",{"Frames between updates of distant world shadows. Higher values save GPU work but make changes in distant shadows take longer to appear."}},
    {"NearShadowInterval",{"Frames between updates of nearby environment shadows. Character shadows still refresh each frame, and camera changes can force an environment refresh. Values above 1 reuse cached results and need extra scratch memory."}},
    {"ShadowDirectionSteps",{"Precision of cached sun and moon directions. Higher values follow the moving light more smoothly but refresh cached shadows more often. Lower values can make shadow direction changes more noticeable."}},
    {"StaticCacheSlices",{"Spreads static shadow cache updates across this many frames. More slices reduce work per update frame but take longer to finish a changed cache. 1 updates it together."}},
    {"ShadowPivotCorrection",{"Keeps nearby shadows aligned when camera zoom or collision moves the view. Leave enabled for normal play; disable only to compare the older shadow positioning."}},
    {"ShadowProvider",{"Northlight uses its own directional cascades and suspends the game's shadow-map preparation and rendering while they are available. Game uses the client's directional shadows and skips Northlight directional passes. AO, GI and local-light shadows keep their own controls. The game's saved Shadow Quality is restored on switching, reset, or leaving the world."}},
    {"ShadowRefreshCpuMs",{"CPU time target shared by directional and lamp shadow maps. When exceeded, a stationary lamp can keep its previous complete shadow for one extra update. 0 disables this scheduling."}},
    {"PointShadowRefreshMs",{"Minimum interval between ordinary local-light shadow updates. Higher values save rendering work but can make moving shadows lag. 0 allows updates every frame; moved or replaced lights can force a refresh."}},
    {"PointShadowFacesPerFrame",{"Directions updated per local-light shadow cube, out of six. Fewer faces spread the work over more frames but can briefly show different shadow ages across directions. Moved or replaced lights require a full refresh."}},
    {"Weather",{"Enhances the game's current rain, snow and sand weather with Northlight effects. Does not force a storm. Turning it off leaves native game weather and disables the related Northlight enhancements."}},
    {"WeatherMemory",{"Reads the game's weather state directly for more reliable intensity and shelter transitions. Off uses texture-based detection as a fallback. Leave enabled unless troubleshooting weather detection."}},
    {"RainFog",{"Strength of the weather-driven atmosphere and lighting response, including fog and storm dimming. 1 is the tuned amount; 0 removes this response and 2 intensifies it. Lens effects have separate controls."}},
    {"FogClouds",{"Adds drifting, low fog banks where the region and weather support them. Uses an additional GPU effect; turn off to reduce its rendering cost."}},
    {"FogCloudDensity",{"Density of drifting fog banks: 100 is normal, 0 makes them invisible. Higher values hide more scenery. Use the Drifting fog banks switch to disable the effect."}},
    {"RainLensStrength",{"Visibility of rain droplets on the camera: 100 is normal, 0 hides them. Actual rain and shelter still determine when droplets appear."}},
    {"SnowLensStrength",{"Visibility of snow clumps on the camera: 100 is normal, 0 hides them. Does not change native world snow particles or the separate edge frost strength."}},
    {"SnowFrostStrength",{"Visibility of accumulated frost around the screen edges: 100 is normal, 0 hides it. Higher values make the edge frost more prominent during snow."}},
    {"SandLensStrength",{"Visibility of sand deposits on the camera: 100 is normal, 0 hides them. Sand weather and shelter still control when the effect appears."}},
    {"SandLensSize",{"Size of sand grains on the camera: 100 keeps the normal size. Larger values make individual grains coarser without changing the weather intensity."}},
    {"WeatherWindDirection",{"Wind heading in world coordinates, measured in degrees. Changes the direction of supported weather motion; turning the camera does not rotate the wind itself."}},
    {"WeatherWindStrength",{"Strength of wind applied to supported weather motion: 100 is normal, 0 removes this wind force. Does not turn off rain, snow or their gravity."}},
    {"WeatherCameraMotion",{"Makes lens droplets, snow and sand react to camera turns. Off removes the camera-induced drift while keeping their ordinary weather motion."}},
    {"HorizonHaze",{"Blends distant terrain and the low sky toward the region's fog color. Higher values make the horizon softer; 0 disables this haze. Requires Atmospheric fog."}},
    {"HorizonHazeStart",{"Distance where terrain haze begins, as a percentage of the region's fog end distance. Higher values keep more of the landscape clear before the haze starts."}},
    {"HorizonHazeBand",{"Height of the sky haze band above the horizon, in degrees. Higher values extend the haze farther up the sky."}},
    {"HorizonHazeTerrain",{"Applies horizon haze to distant terrain as well as the sky. Turn off to keep the landscape clearer while retaining the sky's horizon band."}},
    {"CommandStream",{"Records rendering commands on the game thread and executes them on a separate thread. Can help when CPU work limits performance, but needs spare CPU capacity. Required for HDR output."}},
    {"StreamFramesAhead",{"Number of buffered frames allowed ahead of rendering. More buffering can smooth uneven CPU work, but adds input latency and queue memory. Does not set an FPS limit."}},
    {"StreamFrameSkip",{"Drops outdated draws when the render thread falls behind, while retaining state changes and uploads. Can reduce accumulated delay, but skipped frames can make motion less smooth."}},
    {"StreamOcclusionLocal",{"Uses the latest completed visibility result instead of waiting for a fresh GPU query. Reduces stalls, but some visibility-dependent effects can react later. Disable when troubleshooting those effects."}},
    {"ReplayJobs",{"Prepares effect work on helper CPU threads. Can help with CPU-heavy scenes when spare cores are available; the intended image is unchanged."}},
    {"FrameDrawGates",{"Skips per-draw checks for effects that are inactive in the current frame. Reduces CPU work without changing enabled effects. Leave on unless troubleshooting."}},
    {"GpuBudgetMs",{"Target GPU time for adaptive effect quality, including fog. A lower budget reduces supported effect detail under load. 0 disables this adaptation. This is not an FPS limiter or a guaranteed cap on total frame time."}},
    {"ActorShadowCpuMs",{"Target CPU time for character shadow work. When exceeded, the adaptive budget reduces the character shadow radius. 0 disables adaptation. Does not cap the game's total CPU time."}},
    {"SpellLayer",{"Preserves supported particles, ribbons and projected spell effects when Northlight shades the world. Keeps casts from being darkened by world lighting. Requires extra draw tracking; disable only to compare or troubleshoot."}},
    {"SpellLayerClasses",{"Which blend types receive spell protection: 1 additive, 2 alpha, 4 multiply, 8 other. Add values to combine them; 15 includes all. Keep 15 for normal play; narrower masks are for troubleshooting."}},
    {"SpellFog",{"Applies Northlight distance fog to supported spell shaders so effects blend with the scene. Coverage depends on the spell shader. Off leaves their native fog behavior."}},
    {"NameMask",{"Protects floating unit names from Northlight world lighting and fog. Does not control HUD brightness. Leave enabled to keep names readable while scene effects are active."}},
    {"DepthFuncAware",{"Respects draw depth-test behavior when deciding which draws receive scene effects. Helps preserve overlays and effects that intentionally ignore depth. Leave enabled unless troubleshooting draw classification."}},
    {"Diagnostics",{"Writes periodic renderer diagnostics to the log for troubleshooting. Adds logging overhead; normally leave off unless collecting a report."}},
    {"RenderProfile",{"Collects per-pass CPU and GPU timings for the diagnostic log. Requires Diagnostic logging and adds measurement overhead. Enable while investigating performance, then turn off."}},
    {"DiagReplayProbe",{"Repeats nearby character shadow draws on a hidden target to measure their cost. Does not change the scene, but adds GPU work. Use only for a requested diagnostic run."}},
    {"ShadowFateDiagnostics",{"Logs why captured shadow geometry is kept or discarded during budget selection. Useful for missing character shadows; leave off for normal play to avoid extra diagnostic work."}},
    {"SpellTrace",{"Number of frames of detailed spell-draw logging to collect. 0 disables the trace. Use a short trace while reproducing a spell issue; verbose logging adds CPU and disk work."}},
    {"NameMaskDebug",{"Displays the protected unit-name area in magenta. Diagnostic visualization only; turn off for normal play."}},
    {"MemMap",{"Logs snapshots of the game's virtual address space to investigate allocation failures. Adds diagnostic overhead."}},
    {"HDRPaperWhite",{"Brightness of ordinary white world surfaces, in nits. Raise it for a brighter HDR baseline; peak highlights and UI brightness have separate controls. Saved for the current display."}},
    {"HDRUIWhite",{"Brightness of HUD elements and text in HDR, in nits. Independent of world exposure and contrast. Saved for the current display."}},
    {"HDRAutoMinEV",{"Lowest exposure the automatic meter may choose before compensation. A lower value lets bright scenes darken further. Only used in Auto exposure mode."}},
    {"HDRAutoMaxEV",{"Highest exposure the automatic meter may choose before compensation. A higher value lets dark scenes brighten further. Only used in Auto exposure mode."}},
    {"HDRLightAdaptation",{"Seconds to complete 90% of an exposure change when entering brighter scenery. Lower values react faster but can make brightness changes more noticeable. Only used in Auto exposure mode."}},
    {"HDRDarkAdaptation",{"Seconds to complete 90% of an exposure change when entering darker scenery. Lower values reveal dark areas sooner; higher values give a slower eye-adaptation effect. Only used in Auto exposure mode."}},
    // Sun and moon share these controls; their saved values remain independent.
    {"disc",{"Color of the visible sun or moon disc. Changes its appearance in the sky; surface lighting has a separate tint."}},
    {"disc_gain",{"Brightness multiplier for the visible disc: 1 is unscaled, 0 hides it. Does not change the strength of light falling on surfaces."}},
    {"light",{"Tint for sunlight or moonlight on surfaces. Light tint mix controls how much of this color replaces the region's authored hue while preserving its brightness."}},
    {"light_mix",{"Blends surface light color from the region's authored hue at 0 to the chosen Light tint at 1. Use Light strength to change brightness."}},
    {"light_strength",{"Strength multiplier for sunlight or moonlight on surfaces: 1 is normal. Lower values weaken this lighting; higher values intensify it. Disc brightness and fog illumination are separate."}},
    {"fog_gain",{"Amount of sunlight or moonlight scattered through atmospheric fog. Higher values make illuminated air more prominent without increasing surface light or disc brightness."}},
    {"fog_height_scale",{"Vertical reach of this body's fog illumination. Higher values extend the illuminated atmosphere farther upward. Requires Atmospheric fog."}},
    {"fog_density",{"Density of atmospheric air: higher values obscure distant scenery and strengthen scattered light together. Blends between the sun and moon profiles over the day. Ground fog and rain keep their own density."}},
    {"horizon_haze",{"Multiplier for horizon haze under this sun or moon profile: 1 uses the global haze strength, 0 removes this contribution. Requires Atmospheric fog and a nonzero global Horizon haze setting."}},
    {"glow_strength",{"Strength of the atmospheric glow around the sun or moon direction. 0 removes this glow. Surface lighting and disc brightness remain separately controlled."}},
    {"glow",{"Custom tint for the atmospheric glow. Glow tint mix controls how much of this color replaces the light's hue."}},
    {"glow_mix",{"Blends glow color from the light's tint at 0 to the chosen Glow tint at 1, preserving brightness. Use Glow strength to change its intensity."}},
};

struct Group { const char* id; const char* title; const char* master; const char* fields; const char* advanced; };
struct Section { const char* id; const char* title; const char* keys; std::vector<Group> groups; bool open; };
const std::vector<Section> QualitySections={
    {"lighting","Lighting & AO","GI GIStrength GIBrightness GIPrelitMix GIPreserveRigidRoomLight GIRays GIBounces GIProbeMoveStep GIDynamicProbes GIThreads GIFastBVH GIDistance GIChunkRadius GIProbeAhead NightBrightness ContactAO AOMethod AOQuality AOStrength AORadius LocalLights LocalLightLimit EquipmentLights PointShadows PointShadowLimit PointShadowDistance PointShadowRefreshMs PointShadowFacesPerFrame PointShadowAdaptiveRefresh",{
        {"gi","Global illumination","GI","GIStrength GIBrightness GIPrelitMix GIPreserveRigidRoomLight GIDistance GIChunkRadius GIBounces","GIRays GIProbeMoveStep GIDynamicProbes GIThreads GIFastBVH GIProbeAhead"},
        {"ao","Ambient occlusion","ContactAO","AOMethod AOQuality AOStrength AORadius",""},
        {"ambient","Environment",nullptr,"NightBrightness",""},
        {"local","Local illumination","LocalLights","LocalLightLimit PointShadows PointShadowLimit EquipmentLights","PointShadowDistance PointShadowRefreshMs PointShadowFacesPerFrame PointShadowAdaptiveRefresh"}},true},
    {"materials","Materials & parallax","AutoBump AutoBumpTerrain AutoBumpWalls AutoBumpGrass AutoBumpModels AutoBumpModelMinPixels AutoNormalFilter AutoBumpStrength AutoBumpDistance AutoParallaxDepth AutoParallaxDistance AutoParallaxSteps AutoParallaxShadows AutoParallaxShadowStrength AutoParallaxShadowSteps AutoCavity AutoCavityStrength AutoMaterialOcclusionDistance AutoDisplacementStrength AutoDisplacementDistance AutoDisplacementDetail AutoHeightPolarity AutoHeightDetails AutoHeightBalance AutoModelDisplacementStrength AutoModelDisplacementDistance AutoModelDisplacementDetail",{
        {"bump","Material modes","AutoBump","AutoBumpTerrain AutoBumpWalls AutoBumpGrass AutoBumpModels AutoNormalFilter AutoBumpStrength AutoBumpDistance","AutoBumpModelMinPixels"},
        {"height","Height shaping",nullptr,"AutoHeightPolarity AutoHeightDetails AutoHeightBalance",""},
        {"geometry","Terrain and object displacement",nullptr,"AutoDisplacementStrength AutoDisplacementDistance AutoDisplacementDetail",""},
        {"models","Model displacement",nullptr,"AutoModelDisplacementStrength AutoModelDisplacementDistance AutoModelDisplacementDetail",""},
        {"occlusionrange","Near material shading",nullptr,"AutoMaterialOcclusionDistance",""},
        {"parallax","POM",nullptr,"AutoParallaxDepth AutoParallaxDistance AutoParallaxSteps",""},
        {"pomshadow","POM self-shadowing","AutoParallaxShadows","AutoParallaxShadowStrength AutoParallaxShadowSteps",""},
        {"cavity","Cavity shading","AutoCavity","AutoCavityStrength",""}},false},
    {"shadows","Shadows","WorldShadows ShadowProvider SoftShadows ShadowStrength ShadowSoftness ShadowContactRadius ActorShadows ActorShadowDetail GrassShadows ActorShadowRadius BlobShadows BlobShadowStrength DisableOriginalTerrainShadows FarShadowInterval NearShadowInterval ShadowRefreshCpuMs ShadowDirectionSteps StaticCacheSlices ShadowPivotCorrection",{
        {"filter","World shadows","WorldShadows","SoftShadows ShadowStrength ShadowSoftness ShadowContactRadius",""},
        {"provider","Directional shadows",nullptr,"ShadowProvider",""},
        {"actors","Character shadows","ActorShadows","ActorShadowRadius ActorShadowDetail GrassShadows","ActorShadowBudgetMiB ActorShadowCpuMs MinSkinnedTriangles CaptureBudgetMiB"},
        {"blobs","Round ground shadows","BlobShadows","BlobShadowStrength",""},
        {"nativeTerrain","Disable original terrain shadows","DisableOriginalTerrainShadows","",""},
        {"cache","Shadow updates",nullptr,"","FarShadowInterval NearShadowInterval ShadowRefreshCpuMs ShadowDirectionSteps StaticCacheSlices ShadowPivotCorrection"}},false},
    {"atmosphere","Atmosphere & weather","WorldFog Godrays Weather AuroraStrength WeatherMemory RainFog RainLens RainLensStrength RainRivulets SnowLens SnowLensStrength SnowFrost SnowFrostStrength SandLens SandLensStrength SandLensSize WeatherWindDirection WeatherWindStrength WeatherCameraMotion FogClouds FogCloudDensity HorizonHaze HorizonHazeStart HorizonHazeBand HorizonHazeTerrain",{
        {"atmosphere","Atmospheric fog","WorldFog","Godrays",""},
        {"fog","Fog banks","FogClouds","FogCloudDensity",""},
        {"weather","Weather","Weather","AuroraStrength RainFog","WeatherMemory RainDropSize SnowFlakeSize"},
        {"rainlens","Rain on camera","RainLens","RainLensStrength RainLensSize RainRivulets",""},
        {"snowlens","Snow on camera","SnowLens","SnowLensStrength SnowLensSize",""},
        {"snowfrost","Frost at screen edges","SnowFrost","SnowFrostStrength",""},
        {"sandlens","Sand on lens","SandLens","SandLensStrength SandLensSize",""},
        {"wind","Wind & camera",nullptr,"WeatherWindDirection WeatherWindStrength WeatherCameraMotion",""},
        {"haze","Horizon haze",nullptr,"HorizonHaze HorizonHazeStart HorizonHazeTerrain","HorizonHazeBand"}},false},
    {"spells","Spells & names","SpellLayer SpellLayerClasses SpellFog NameMask DepthFuncAware",{
        {"spells","Spell effects","SpellLayer","SpellFog","SpellLayerClasses DepthFuncAware"},
        {"names","Unit name protection","NameMask","",""}},false},
    {"rendering","Rendering","CommandStream StreamFramesAhead StreamFrameSkip StreamOcclusionLocal ReplayJobs FrameDrawGates GpuBudgetMs",{
        {"execution","Rendering",nullptr,"","CommandStream StreamFramesAhead ReplayJobs FrameDrawGates StreamFrameSkip StreamOcclusionLocal GpuBudgetMs"}},false},
    {"diagnostics","Diagnostics","Diagnostics RenderProfile DiagReplayProbe ShadowFateDiagnostics SpellTrace NameMaskDebug MemMap",{
        {"logging","Diagnostics","Diagnostics","RenderProfile","DiagReplayProbe ShadowFateDiagnostics"},
        {"debug","Additional diagnostics",nullptr,"SpellTrace NameMaskDebug MemMap",""}},false},
};
const std::vector<Group> HdrGroups={
    {"hdr","HDR output","ExperimentalHDR","HDRPaperWhite HDRUIWhite HDRPeakMode HDRMaxNits",""},
    {"hdr-look","World appearance","","HDRExposureMode HDRExposure HDRContrast HDRBloomStrength HDRWideBloom HDRLegacyExpansion","HDRAutoMinEV HDRAutoMaxEV HDRLightAdaptation HDRDarkAdaptation"}};
const std::vector<Group> CelestialGroups={
    {"sun","Sun",nullptr,"sun_disc sun_disc_gain sun_light sun_light_strength","sun_light_mix sun_fog_gain sun_fog_height_scale sun_fog_density sun_horizon_haze sun_glow_strength sun_glow sun_glow_mix"},
    {"moon","Moon",nullptr,"moon_disc moon_disc_gain moon_light moon_light_strength","moon_light_mix moon_fog_gain moon_fog_height_scale moon_fog_density moon_horizon_haze moon_glow_strength moon_glow moon_glow_mix"},
    {"material-response","Material lighting",nullptr,"sun_surface_specular sun_wet_stone_specular moon_terrain_specular local_surface_specular",
        "sun_terrain_ice sun_terrain_snow sun_foliage_transmission moon_foliage_transmission"},
};
std::vector<std::string> words(const char* text);
// The old menu's preset page.
struct Preset { const char* key; const char* text; };
const Preset Presets[3]={{"Quality","Maximum effect detail."},{"Balanced","Quality with a moderate effect budget."},{"Performance","Less work spent on effects."}};
const char* const QuickKeys="Effects GI WorldShadows WorldFog LocalLights ContactAO AutoBump Weather CelestialEffects Godrays ActorShadows FogClouds";
const char* const CelestialQualityKeys[]={"CelestialEffects","CelestialStyle","TreeCanopy","TreeCrown"};
// Sidebar pages, in order. quality: the QualitySections id a Quality page draws.
inline bool fogKey(const std::string& key,const std::string& name){return key=="day_"+name||key=="dusk_"+name||key=="night_"+name;}
constexpr std::array<NorthlightTone::HueBand,NorthlightTone::HueBandCount> HueBands={NorthlightTone::HueBand::Red,NorthlightTone::HueBand::Orange,NorthlightTone::HueBand::Yellow,
    NorthlightTone::HueBand::Green,NorthlightTone::HueBand::Cyan,NorthlightTone::HueBand::Blue,NorthlightTone::HueBand::Magenta};
constexpr std::array<NorthlightTone::SelectiveProperty,3> SelectiveProperties={NorthlightTone::SelectiveProperty::HueShift,NorthlightTone::SelectiveProperty::Saturation,NorthlightTone::SelectiveProperty::Lightness};
constexpr std::array<NorthlightTone::TimeStop,3> GradeTimes={NorthlightTone::TimeStop::Day,NorthlightTone::TimeStop::Dusk,NorthlightTone::TimeStop::Night};
constexpr std::array<const char*,3> TimeTitles={"Day","Dusk","Night"};

enum class Page { Overview, Lighting, Materials, Shadows, Atmosphere, Water, Regional, Grading, Celestial, ShadowRange, Spells, Renderer, Performance, Diagnostics, Count };
struct PageInfo { Page page; const char* title; const char* quality; };
const PageInfo Pages[]={
    {Page::Overview,"Overview",nullptr},{Page::Lighting,"Lighting & AO","lighting"},{Page::Materials,"Materials & parallax","materials"},
    {Page::Shadows,"Shadows","shadows"},{Page::Atmosphere,"Atmosphere & weather","atmosphere"},{Page::Water,"Water",nullptr},
    {Page::Regional,"Regional appearance",nullptr},{Page::Grading,"Colour grading",nullptr},{Page::Celestial,"Sun & moon",nullptr},
    {Page::ShadowRange,"Shadow range",nullptr},{Page::Spells,"Spells & names","spells"},{Page::Renderer,"Renderer & HDR","rendering"},
    {Page::Performance,"Performance",nullptr},{Page::Diagnostics,"Diagnostics","diagnostics"},
};
static_assert(sizeof(Pages)/sizeof(Pages[0])==std::size_t(Page::Count),"one entry per page");
constexpr std::array<const char*,std::size_t(Page::Count)> PageLabels={
    "Overview","Lighting & AO","Materials","Shadows","Atmosphere & weather","Water","Fog & contrast",
    "Colour grading","Sun & moon","Shadow range","Spells & names","Renderer & HDR","Performance","Diagnostics"};
struct MenuSection { const char* title; std::array<Page,4> pages; unsigned count; };
constexpr MenuSection MenuSections[]={
    {"Overview",{Page::Overview},1},
    {"Lighting & shadows",{Page::Lighting,Page::Shadows,Page::ShadowRange},3},
    {"Materials",{Page::Materials},1},
    {"World & weather",{Page::Atmosphere,Page::Regional,Page::Grading,Page::Celestial},4},
    {"Water",{Page::Water},1},
    {"Spells & names",{Page::Spells},1},
    {"System",{Page::Renderer,Page::Performance,Page::Diagnostics},3},
};
constexpr unsigned menuSection(Page page){
    for(unsigned i=0;i<std::size(MenuSections);++i)for(unsigned j=0;j<MenuSections[i].count;++j)if(MenuSections[i].pages[j]==page)return i;
    return 0;
}
static_assert([]{
    std::array<unsigned,std::size_t(Page::Count)> occurrences{};
    for(const auto& section:MenuSections)for(unsigned i=0;i<section.count;++i)++occurrences[unsigned(section.pages[i])];
    for(unsigned count:occurrences)if(count!=1)return false;
    return true;
}(),"each settings page needs exactly one menu section");
const Section* qualitySection(const char* id){if(id)for(const auto& s:QualitySections)if(std::string(s.id)==id)return &s;return nullptr;}
// Every key a Quality section names (its key list and its groups).
std::vector<std::string> sectionKeys(const Section& s){
    std::vector<std::string> out=words(s.keys);
    for(const auto& g:s.groups)for(const char* list:{g.master,g.fields,g.advanced})for(auto& k:words(list))if(std::find(out.begin(),out.end(),k)==out.end())out.push_back(k);
    return out;
}

const std::map<std::string,const char*> MapLabels={
    {"AhnQiraj", "Ruins of Ahn'Qiraj"},
    {"AhnQirajTemple", "Ahn'Qiraj Temple"},
    {"ArgentTournamentDungeon", "Trial of the Champion"},
    {"ArgentTournamentRaid", "Trial of the Crusader"},
    {"AuchindounDemon", "Auchindoun: Sethekk Halls"},
    {"AuchindounDraenei", "Auchindoun: Auchenai Crypts"},
    {"AuchindounEthereal", "Auchindoun: Mana-Tombs"},
    {"AuchindounShadow", "Auchindoun: Shadow Labyrinth"},
    {"Azeroth", "Eastern Kingdoms"},
    {"Azjol_LowerCity", "Ahn'kahet: The Old Kingdom"},
    {"Azjol_Uppercity", "Azjol-Nerub"},
    {"Blackfathom", "Blackfathom Deeps"},
    {"BlackrockDepths", "Blackrock Depths"},
    {"BlackRockSpire", "Blackrock Spire"},
    {"BlackTemple", "Black Temple"},
    {"BlackwingLair", "Blackwing Lair"},
    {"bladesedgearena", "Blade's Edge Arena"},
    {"CavernsOfTime", "Opening of the Dark Portal"},
    {"ChamberOfAspectsBlack", "The Obsidian Sanctum"},
    {"ChamberofAspectsRed", "The Ruby Sanctum"},
    {"CoilfangDraenei", "Coilfang: The Slave Pens"},
    {"CoilfangMarsh", "Coilfang: The Underbog"},
    {"CoilfangPumping", "Coilfang: The Steamvault"},
    {"CoilfangRaid", "Coilfang: Serpentshrine Cavern"},
    {"DalaranArena", "Dalaran Sewers"},
    {"DalaranPrison", "Violet Hold"},
    {"DeadminesInstance", "Deadmines"},
    {"DeathKnightStart", "Ebon Hold"},
    {"DeeprunTram", "Deeprun Tram"},
    {"DireMaul", "Dire Maul"},
    {"DrakTheronKeep", "Drak'Tharon Keep"},
    {"Expansion01", "Outland"},
    {"GnomeragonInstance", "Gnomeregan"},
    {"GruulsLair", "Gruul's Lair"},
    {"GunDrak", "Gundrak"},
    {"HallsOfReflection", "Halls of Reflection"},
    {"HellfireDemon", "Hellfire Citadel: The Blood Furnace"},
    {"HellfireMilitary", "Hellfire Citadel: The Shattered Halls"},
    {"HellfireRaid", "Magtheridon's Lair"},
    {"HellfireRampart", "Hellfire Citadel: Ramparts"},
    {"HillsbradPast", "The Escape From Durnholde"},
    {"HyjalPast", "The Battle for Mount Hyjal"},
    {"IcecrownCitadel", "Icecrown Citadel"},
    {"IcecrownCitadel5Man", "The Forge of Souls"},
    {"IsleofConquest", "Isle of Conquest"},
    {"Kalimdor", "Kalimdor"},
    {"Karazahn", "Karazhan"},
    {"Mauradon", "Maraudon"},
    {"MoltenCore", "Molten Core"},
    {"MonasteryInstances", "Scarlet Monastery"},
    {"NetherstormBG", "Eye of the Storm"},
    {"Nexus70", "The Nexus"},
    {"Nexus80", "The Oculus"},
    {"NexusRaid", "The Eye of Eternity"},
    {"Northrend", "Northrend"},
    {"NorthrendBG", "Strand of the Ancients"},
    {"OnyxiaLairInstance", "Onyxia's Lair"},
    {"OrgrimmarArena", "The Ring of Valor"},
    {"OrgrimmarInstance", "Ragefire Chasm"},
    {"PVPLordaeron", "Ruins of Lordaeron"},
    {"PVPZone01", "Alterac Valley"},
    {"PVPZone03", "Warsong Gulch"},
    {"PVPZone04", "Arathi Basin"},
    {"PVPZone05", "Nagrand Arena"},
    {"QuarryofTears", "Pit of Saron"},
    {"RazorfenDowns", "Razorfen Downs"},
    {"RazorfenKraulInstance", "Razorfen Kraul"},
    {"SchoolofNecromancy", "Scholomance"},
    {"Shadowfang", "Shadowfang Keep"},
    {"StormwindJail", "Stormwind Stockade"},
    {"Stratholme", "Stratholme"},
    {"Stratholme Raid", "Naxxramas"},
    {"StratholmeCOT", "The Culling of Stratholme"},
    {"SunkenTemple", "Sunken Temple"},
    {"Sunwell5ManFix", "Magister's Terrace"},
    {"SunwellPlateau", "The Sunwell"},
    {"TanarisInstance", "Zul'Farrak"},
    {"TempestKeepArcane", "Tempest Keep: The Arcatraz"},
    {"TempestKeepAtrium", "Tempest Keep: The Botanica"},
    {"TempestKeepFactory", "Tempest Keep: The Mechanar"},
    {"TempestKeepRaid", "Tempest Keep"},
    {"Uldaman", "Uldaman"},
    {"Ulduar70", "Halls of Stone"},
    {"Ulduar80", "Halls of Lightning"},
    {"UlduarRaid", "Ulduar"},
    {"UtgardePinnacle", "Utgarde Pinnacle"},
    {"Valgarde70", "Utgarde Keep"},
    {"WailingCaverns", "Wailing Caverns"},
    {"WintergraspRaid", "Vault of Archavon"},
    {"Zul'gurub", "Zul'Gurub"},
    {"ZulAman", "Zul'Aman"},
};

const char* find(const std::map<std::string,const char*>& m,const std::string& key){auto i=m.find(key);return i==m.end()?nullptr:i->second;}
std::string celestialName(const std::string& key,std::string* body){
    for(const char* prefix:{"sun_","moon_"}){const std::string p=prefix;
        if(key.compare(0,p.size(),p)==0){if(body)*body=p=="sun_"?"Sun":"Moon";const char* name=find(CelestialLabels,key.substr(p.size()));return name?name:key.substr(p.size());}}
    return {};
}
bool mapDefaultSection(const std::string& section){
    return section.size()>8&&section.compare(section.size()-8,8,":default")==0;
}
std::string mapDirectory(const std::string& section){
    return mapDefaultSection(section)?section.substr(0,section.size()-8):std::string();
}
std::string mapGroup(const std::string& map){
    if(map=="Azeroth")return "Eastern Kingdoms";
    if(map=="Kalimdor")return "Kalimdor";
    if(map=="Expansion01")return "Outland";
    if(map=="Northrend")return "Northrend";
    return {};
}
// Expansion01 also contains these verified Azeroth locations; unlisted area roots stay grouped as Outland.
const std::map<std::string,const char*> ExpansionAreaGroups={
    {"3430","Eastern Kingdoms"},{"3433","Eastern Kingdoms"},{"3455","Eastern Kingdoms"},
    {"3487","Eastern Kingdoms"},{"4080","Eastern Kingdoms"},
    {"3479","Kalimdor"},{"3524","Kalimdor"},{"3525","Kalimdor"},{"3557","Kalimdor"},
};
std::string expansionAreaGroup(const std::string& section){
    const auto colon=section.find(':');
    auto area=section.substr(colon+1);
    if(const auto suffix=area.find_first_of("/@");suffix!=area.npos)area.resize(suffix);
    if(const char* group=find(ExpansionAreaGroups,area))return group;
    return "Outland";
}
std::string regionGroup(const std::string& section){
    if(section=="default")return "All regions";
    const auto colon=section.find(':');
    if(colon==section.npos)return "Other profiles";
    const auto map=section.substr(0,colon);
    if(mapDefaultSection(section)){
        if(const auto group=mapGroup(map);!group.empty())return group+" map defaults";
        return "Instance map defaults";
    }
    if(map=="Expansion01")return expansionAreaGroup(section);
    if(const auto group=mapGroup(map);!group.empty())return group;
    if(find(MapLabels,map))return "Instance regions";
    return "Other profiles";
}
int regionGroupOrder(const std::string& group){
    if(group=="All regions")return 0;
    if(group=="Eastern Kingdoms")return 1;
    if(group=="Kalimdor")return 2;
    if(group=="Outland")return 3;
    if(group=="Northrend")return 4;
    if(group=="Eastern Kingdoms map defaults")return 5;
    if(group=="Kalimdor map defaults")return 6;
    if(group=="Outland map defaults")return 7;
    if(group=="Northrend map defaults")return 8;
    if(group=="Instance regions")return 9;
    if(group=="Instance map defaults")return 10;
    return 11;
}
std::string folded(std::string text){
    for(std::size_t i=0;i<text.size();++i){
        const auto c=static_cast<unsigned char>(text[i]);
        if(c<128){text[i]=char(std::tolower(c));continue;}
        if(i+1>=text.size())break;
        const auto next=static_cast<unsigned char>(text[i+1]);
        if(c==0xd0&&next>=0x90&&next<0xa0)text[++i]=char(next+0x20);
        else if(c==0xd0&&next>=0xa0&&next<=0xaf){text[i]=char(0xd1);text[++i]=char(next-0x20);}
        else if((c==0xd0&&next==0x81)||(c==0xd1&&next==0x91)){text[i]=char(0xd0);text[++i]=char(0xb5);}
    }
    return text;
}
std::string searchWords(std::string text){
    for(char& c:text){const auto byte=static_cast<unsigned char>(c);if(byte<128&&!std::isalnum(byte))c=' ';}
    return folded(std::move(text));
}
std::string fieldLabel(const Field& f){
    if(f.file==NorthlightSettings::WaterHostFile)return "Use Tideglass";
    if(const char* l=find(Labels,f.key))return l;
    std::string body;const auto name=celestialName(f.key,&body);if(!name.empty())return body+": "+name;
    return f.key;
}
std::string rowLabel(const Field& f){
    if(const char* l=find(ShortLabels,f.key))return l;
    if(f.file==D::Celestial){const auto name=celestialName(f.key,nullptr);if(!name.empty())return name;}
    return fieldLabel(f);
}
std::string regionLabel(const std::string& section){
    if(section=="default")return "All regions (default)";
    if(mapDefaultSection(section)){
        const auto map=mapDirectory(section);const char* name=find(MapLabels,map);
        return std::string(name?name:map.c_str())+" (map default; "+section+")";
    }
    if(const char* zone=find(NorthlightSettings::Regions,section))return std::string(zone)+"  ("+section+")";
    return section;
}
std::string optionLabel(const Field& f,const std::string& value){
    if(f.file==NorthlightSettings::WaterFile){
        if(f.key=="ReflectionScenery")return value=="0"?"Sky only":"Scenery + sky";
        if(f.key=="ReflectionDistance")return value=="2"?"Far":value=="1"?"Short":"Standard";
        if(f.key=="ReflectionResolution")return value+"%";
    }
    if(value=="HBAOPlus")return "HBAO+ (D3D9)";

    if(f.file==D::Quality&&f.key=="ShadowProvider")return value=="0"?"Game":"Northlight";
    if(f.file==D::Quality&&f.key=="AutoNormalFilter")return value=="0"?"Legacy":"Filtered";
    if(D::modeField(f))return value=="0"?"Off":value=="1"?"Normal maps":value=="2"?"Normal maps + POM":"Displacement + normals";
    if(value=="HBAOPlus")return "HBAO+ (D3D9)";
    if(f.file==D::Renderer&&f.key=="Backend"){
        if(value=="dxvk")return "DXVK 3";
        if(value=="dxvk2")return "DXVK 2";
        if(value=="native")return "System Direct3D 9";
        if(value=="legacy")return "Existing d3d9.dll (legacy)";
    }
    return value;
}
const char* fileName(unsigned file){
    return file<NorthlightSettings::FileCount?NorthlightSettings::FileNames[file]:"?";
}
const char* originText(const std::string& origin){
    if(origin=="f"||origin=="override")return "set in the file";
    if(origin=="l")return "legacy shadow-experiment.ini";
    if(origin=="p")return "preset";
    if(origin=="d")return "default";
    if(origin=="inherited")return "inherited";
    return origin.c_str();
}
std::vector<std::string> words(const char* text){
    std::vector<std::string> out;if(!text)return out;std::string w;
    for(const char* c=text;;++c){if(*c==' '||*c==0){if(!w.empty())out.push_back(w);w.clear();if(!*c)break;}else w+=*c;}
    return out;
}
double number(const std::string& s,double fallback=0){char* end=nullptr;const double v=std::strtod(s.c_str(),&end);return end!=s.c_str()&&std::isfinite(v)?v:fallback;}
bool integral(const Field& f){return number(f.step,1)>=1&&std::floor(number(f.low))==number(f.low)&&std::floor(number(f.high))==number(f.high);}
int decimals(const Field& f){const double step=number(f.step,1);if(step>=1)return 0;return std::clamp(int(std::ceil(-std::log10(step)-1e-9)),0,4);}
double quantize(const Field& f,double v){
    const double lo=number(f.low),hi=number(f.high),step=number(f.step,0);
    if(step>0)v=lo+std::round((v-lo)/step)*step;
    return std::clamp(v,std::min(lo,hi),std::max(lo,hi));
}
std::string text(const Field& f,double v){char b[64];std::snprintf(b,sizeof b,"%.*f",decimals(f),quantize(f,v));return b;}
ImU32 u32(const ImVec4& c){return ImGui::ColorConvertFloat4ToU32(c);}
// Right-aligns the next item of the given width on the current line.
void alignRight(float width){const float x=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x-width;ImGui::SameLine(0,0);ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),x));}
float buttonWidth(const char* label){return ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2;}
} // namespace

struct SettingsPanel::State {
    using Service=NorthlightSettings::Service;
    enum class Confirm { None, Preset, Factory };
    std::shared_ptr<Service> service;
    std::string hotkey;
    std::shared_ptr<const NorthlightSettings::Snapshot> snapshot;
    NorthlightSettings::Status status;
    D::Draft draft;
    SettingsPanel::CalibrationDraw calibrationDraw;
    struct Patch{State* owner;float rect[4],color[4];};std::deque<Patch> calibrationPatches;
    bool calibrationOpen=false,calibrationLarge=false;float calibrationPeak=1000;
    std::map<std::string,std::vector<std::pair<std::string,std::string>>> displayDrafts;
    std::map<std::string,std::array<float,3>> live;   // a slider or colour being dragged: committed on release
    bool wantSnapshot=false,wantPreview=false,wantClear=false,previewing=false,saving=false,awaitingSnapshot=false;
    std::uint64_t commitSerial=0,appearancePresetSerial=0;
    char appearancePresetName[64]{};
    bool appearancePresetModal=false,appearancePresetDeleteModal=false,savingAppearancePreset=false;
    std::string appearancePresetToDelete;
    std::string region="default",note;                 // region: the selected key in the shared picker
    struct RegionChoice { std::string key,appearance,shadow,slime,label,group,search; };
    const NorthlightSettings::Snapshot* regionChoicesSnapshot=nullptr;
    std::vector<RegionChoice> regionChoices;
    std::array<char,128> regionFilter{},searchText{};
    int regionMap=0,editTime=0;
    struct SearchEntry { Page page;std::string title,path,words,key;const Field* field=nullptr;const char* action=nullptr; };
    std::vector<SearchEntry> searchEntries;
    std::vector<std::size_t> searchResults;
    const NorthlightSettings::Snapshot* searchSnapshot=nullptr;
    std::string searchQuery,searchField;
    const char* searchAction=nullptr;
    int searchSelected=0;
    bool searchUp=false,searchDown=false,searchEnter=false,searchScroll=false;
    enum class CurveDrag {None,Point,HandleIn,HandleOut};
    int curveChannel=0,selectedPoint=-1,selectedBand=0;
    CurveDrag curveDrag=CurveDrag::None;
    NorthlightTone::PointList curvePoints;   // the curve under the mouse while it is dragged
    std::string curveShown;                  // the curve the selection belongs to
    ImVec2 curveDragOffset{};
    bool curveDragged=false;
    double nextCurvePreview=0;
    bool compareRequested=false,comparing=false;
    mutable const NorthlightSettings::Snapshot* pageFieldsSnapshot=nullptr;
    mutable std::array<std::vector<const Field*>,std::size_t(Page::Count)> pageFieldCache;
    Page page=Page::Overview;                             // kept for the session
    std::array<Page,std::size(MenuSections)> lastPages{Page::Overview,Page::Lighting,Page::Materials,Page::Regional,Page::Water,Page::Spells,Page::Renderer};
    NorthlightMemoryProfile::Snapshot memorySnapshot;
    uint64_t memoryReadAt=0;
    std::string presetChoice;bool presetCelestial=true,presetRange=true;   // the preset dialog
    Confirm confirm=Confirm::None;
    float scale=1;
    NorthlightPerformance::Adapter monitorAdapter;
    std::unique_ptr<NorthlightPerformance::HardwareSampler> hardwareSampler;
    NorthlightPerformance::Hardware hardware;
    NorthlightPerformance::Timing timing;
    std::array<std::string,6> monitorSummary;bool monitorBoardMemory=false;
    uint64_t monitorReadAt=0;bool monitoringHud=false;

    bool locked()const{return saving||awaitingSnapshot||savingAppearancePreset||!snapshot;}
    // ---- Service ----
    void poll(){
        if(!service)return;
        if(wantSnapshot&&service->requestSnapshot())wantSnapshot=false;
        if(auto latest=service->snapshot();latest&&latest!=snapshot){
            const bool moved=snapshot&&snapshot->hdrProfile!=latest->hdrProfile;
            std::vector<std::pair<std::string,std::string>> restore;
            if(moved){auto& previous=displayDrafts[snapshot->hdrProfile];previous.clear();
                for(const auto& change:draft.changes())if(change.first.rfind("R\tHDR.",0)==0)previous.push_back(change);else restore.push_back(change);
                const auto& current=displayDrafts[latest->hdrProfile];restore.insert(restore.end(),current.begin(),current.end());draft.clear();live.clear();}
            snapshot=latest;draft.bind(&snapshot->fields);
            if(moved){for(const auto& change:restore)for(const auto& field:snapshot->fields)if(field.id()==change.first){draft.set(field,change.second);break;}wantPreview=true;}
            if(awaitingSnapshot){draft.clear();live.clear();awaitingSnapshot=false;}
            refreshRegionChoices();
        }
        status=service->status();
        if(saving&&status.serial>commitSerial&&status.state!=NorthlightSettings::Status::State::Busy){
            saving=false;
            if(status.state==NorthlightSettings::Status::State::Saved){if(snapshot)displayDrafts.erase(snapshot->hdrProfile);awaitingSnapshot=true;wantSnapshot=true;previewing=false;wantPreview=false;}
        }
        if(savingAppearancePreset&&status.serial>appearancePresetSerial&&status.state!=NorthlightSettings::Status::State::Busy){
            savingAppearancePreset=false;note=status.message;
        }
        if(wantClear&&!saving&&service->clearPreview()){wantClear=false;previewing=false;}
        if(wantPreview&&!saving&&!wantClear){
            auto pending=changes();
            if(comparing)if(const Field* strength=draft.field(D::Celestial,appearanceRegion(),"tone_curve_strength")){
                const auto id=strength->id();
                pending.erase(std::remove_if(pending.begin(),pending.end(),[&](const auto& c){return c.id==id;}),pending.end());
                pending.push_back({id,"0"});
            }
            if(service->preview(std::move(pending))){wantPreview=false;previewing=comparing||!draft.empty();}
        }
    }
    std::vector<NorthlightSettings::Change> changes()const{
        std::vector<NorthlightSettings::Change> out;for(auto& c:draft.changes())out.push_back({c.first,c.second});return out;
    }
    void edited(const Field& f){note.clear();if(f.apply=="F")wantPreview=true;}
    void set(const Field& f,const std::string& value){
        if(f.section=="RegionalSlime"||fogKey(f.key,"fog_tint_strength")||f.key=="shadow_contrast"||f.key=="tone_curve_strength")draft.pin(f,value);
        else draft.set(f,value);
        edited(f);
    }
    void inherit(const Field& f){if(!D::canInherit(f))return;draft.inherit(f);live.erase(f.id());edited(f);}
    void save(){
        if(!service||!snapshot||draft.empty()||locked())return;
        const auto serial=service->status().serial;
        if(service->commit(snapshot->revision,changes())){saving=true;commitSerial=serial;note.clear();}
        else note="The settings service is busy; try again in a moment.";
    }
    void saveAppearancePreset(){
        if(!service||!snapshot||locked())return;
        const std::string name=appearancePresetName;std::string error;
        if(!NorthlightAppearancePresets::validName(name,error)){note=error;return;}
        const auto serial=service->status().serial;
        if(service->savePreset(name,snapshot->revision,changes(),!ownAppearancePreset(name).empty())){savingAppearancePreset=true;appearancePresetSerial=serial;note.clear();}
        else{note="The settings service is busy; try again in a moment.";}
    }
    // The saved name of one of the player's own looks, matched without regard to letter case; empty if there is none.
    std::string ownAppearancePreset(const std::string& name)const{
        const Field* palette=draft.field(D::Celestial,"Appearance","Palette");
        if(!palette||NorthlightAppearancePresets::packaged(name))return {};
        const auto listed=NorthlightSettings::canonicalOption(name,palette->options);
        std::istringstream options(palette->options);std::string option;
        while(std::getline(options,option,','))if(option==listed)return listed;
        return {};
    }
    void deleteAppearancePreset(){
        if(!service||!snapshot||locked()||appearancePresetToDelete.empty())return;
        // The service returns a saved choice to Default; an unsaved choice of this look is the draft's to give up.
        if(const Field* palette=draft.field(D::Celestial,"Appearance","Palette"))
            if(draft.edited(*palette)&&NorthlightSettings::lower(draft.effective(*palette))==NorthlightSettings::lower(appearancePresetToDelete)){draft.set(*palette,draft.saved(*palette));edited(*palette);}
        const auto serial=service->status().serial;
        if(service->deletePreset(appearancePresetToDelete,snapshot->revision)){savingAppearancePreset=true;appearancePresetSerial=serial;note.clear();}
        else{note="The settings service is busy; try again in a moment.";}
    }
    void revert(){draft.clear();live.clear();wantPreview=false;wantClear=true;note.clear();}
    std::vector<const Field*> fields(unsigned file,const std::string* section=nullptr)const{
        std::vector<const Field*> out;if(!snapshot)return out;
        for(const auto& f:snapshot->fields)if(f.file==file&&(!section||f.section==*section))out.push_back(&f);
        return out;
    }

    // ---- Rows ----
    void marker(const Field& f){
        const float h=ImGui::GetFrameHeight(),r=std::max(2.5f,h*0.15f);
        const ImVec2 p=ImGui::GetCursorScreenPos(),c(p.x+r+1,p.y+h*0.5f);
        ImDrawList* dl=ImGui::GetWindowDrawList();
        if(draft.edited(f))dl->AddCircleFilled(c,r,u32(Color::Edited));
        else if(draft.overrides(f))dl->AddCircleFilled(c,r,u32(Color::Accent));
        else dl->AddCircle(c,r,u32(Color::Muted),0,std::max(1.f,scale));
        ImGui::Dummy(ImVec2(2*r+2,h));ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
    }
    void tooltip(const Field& f,D::Block block,const std::string& missing){
        if(!ImGui::BeginTooltip())return;
        ImGui::PushTextWrapPos(ImGui::GetFontSize()*30);
        ImGui::TextUnformatted(fieldLabel(f).c_str());
        auto help=Help.find(f.key);
        if(help==Help.end()&&f.file==D::Celestial)help=Help.find(f.key.substr(f.key.find('_')+1));
        if(help!=Help.end())for(const char* line:help->second){ImGui::Spacing();ImGui::TextWrapped("%s",line);}
        ImGui::Separator();
        ImGui::TextColored(Color::Muted,"%s  [%s]  %s",fileName(f.file),f.section.c_str(),f.key.c_str());
        const auto shown=draft.effective(f);
        ImGui::Text("Value: %s%s",shown.c_str(),draft.edited(f)?"  (unsaved)":"");
        ImGui::Text("Saved: %s   Running: %s",draft.saved(f).c_str(),f.available?f.active.c_str():"unavailable");
        const Field* base=f.file==D::ShadowRange&&f.section!="default"?draft.field(D::ShadowRange,"default",f.key):nullptr;
        const auto inherited=base?draft.effective(*base):f.inherited;
        ImGui::Text("Factory: %s   Inherits: %s",f.factory.c_str(),inherited.c_str());
        ImGui::TextColored(Color::Muted,"Source: %s%s",originText(f.origin),draft.overrides(f)?"":" - not written to the file");
        ImGui::Spacing();
        if(f.apply=="S")ImGui::TextColored(Color::Good,"Takes effect when you save.");
        else if(f.apply=="R")ImGui::TextColored(Color::Restart,draft.restartPending(f)?"Requires a game restart: the running renderer still uses %s.":"Requires a game restart.%s",draft.restartPending(f)?f.active.c_str():"");
        if(block==D::Block::Unavailable)ImGui::TextColored(Color::Bad,"Not available in this installation.");
        else if(block==D::Block::NeedsHdr)ImGui::TextColored(Color::Bad,"Enable HDR first.");
        else if(block==D::Block::NeedsManual)ImGui::TextColored(Color::Muted,"Choose Manual peak mode to edit this value.");
        else if(block==D::Block::NeedsKey){const char* l=find(Labels,missing);ImGui::TextColored(Color::Bad,"Requires: %s",l?l:missing.c_str());}
        if(D::canInherit(f)&&block==D::Block::None)ImGui::TextColored(Color::Muted,"Right-click: restore the inherited value.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    // After each item of a row (label, badge, control): its tooltip, and right-click restores the inherited value.
    void interact(const Field& f,D::Block block,const std::string& missing){
        if(!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))return;
        if(block==D::Block::None&&!locked()&&ImGui::IsMouseClicked(ImGuiMouseButton_Right))inherit(f);
        if(!ImGui::IsAnyItemActive()&&ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))tooltip(f,block,missing);
    }
    void settle(const Field& f){
        auto i=live.find(f.id());if(i==live.end()||ImGui::IsItemActive())return;
        std::string value;
        if(f.kind=="rgb"){for(unsigned c=0;c<3;++c)value+=(c?",":"")+text(f,i->second[c]);}
        else value=integral(f)?std::to_string(int(std::lround(quantize(f,i->second[0])))):text(f,i->second[0]);
        live.erase(i);set(f,value);
    }
    void control(const Field& f){
        const std::string id=f.id(),value=draft.effective(f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if(D::modeField(f)||f.kind=="enum"){
            const auto options=D::modeField(f)?D::modeOptions(f):D::split(f.options);
            if(ImGui::BeginCombo("##value",optionLabel(f,value).c_str())){
                for(const auto& o:options){const bool selected=o==value;if(ImGui::Selectable(optionLabel(f,o).c_str(),selected)&&!selected)set(f,o);if(selected)ImGui::SetItemDefaultFocus();}
                ImGui::EndCombo();
            }
            return;
        }
        if(f.kind=="check"){bool on=value=="1";if(ImGui::Checkbox("##value",&on))set(f,on?"1":"0");return;}
        if(f.kind=="curve"){
            std::array<char,513> curve{};std::snprintf(curve.data(),curve.size(),"%s",value.c_str());
            if(ImGui::InputText("##value",curve.data(),curve.size(),ImGuiInputTextFlags_EnterReturnsTrue)){
                std::array<float,NorthlightTone::CurveWords> points{};
                if(const char* error=NorthlightTone::parseCurve(curve.data(),points))note=error;
                else set(f,NorthlightTone::curveText(points.data()));
            }
            return;
        }
        auto held=live.find(id);
        const float lo=float(number(f.low)),hi=float(number(f.high));
        if(f.kind=="rgb"){
            float c[3]={0,0,0};
            if(held!=live.end())for(unsigned i=0;i<3;++i)c[i]=held->second[i];
            else{const auto parts=D::split(value);for(unsigned i=0;i<3&&i<parts.size();++i)c[i]=float(number(parts[i]));}
            ImGuiColorEditFlags flags=ImGuiColorEditFlags_Float|ImGuiColorEditFlags_NoOptions|ImGuiColorEditFlags_NoAlpha|ImGuiColorEditFlags_DisplayRGB|ImGuiColorEditFlags_InputRGB;
            if(hi>1)flags|=ImGuiColorEditFlags_HDR;
            if(ImGui::ColorEdit3("##value",c,flags)){for(float& x:c)x=std::clamp(x,lo,hi);live[id]={c[0],c[1],c[2]};}
            settle(f);return;
        }
        if(integral(f)){
            int v=held!=live.end()?int(std::lround(held->second[0])):int(std::lround(number(value,lo)));
            if(ImGui::SliderInt("##value",&v,int(lo),int(hi),"%d",ImGuiSliderFlags_AlwaysClamp))live[id]={float(v),0,0};
        }else{
            float v=held!=live.end()?held->second[0]:float(number(value,lo));char format[16];std::snprintf(format,sizeof format,"%%.%df",decimals(f));
            if(ImGui::SliderFloat("##value",&v,lo,hi,format,ImGuiSliderFlags_AlwaysClamp))live[id]={v,0,0};
        }
        settle(f);
    }
    // One table row: marker, label (amber while edited, "R" for restart-only keys), control.
    void row(const Field& f,const char* label=nullptr){
        std::string missing;const auto block=D::blocked(draft,f,missing);
        ImGui::PushID(f.id().c_str());
        ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
        marker(f);
        ImGui::AlignTextToFramePadding();
        const std::string name=label?label:rowLabel(f);
        const bool hit=!searchField.empty()&&searchField==f.id();
        if(hit)ImGui::TextColored(Color::Accent,"%s",name.c_str());
        else if(block!=D::Block::None)ImGui::TextDisabled("%s",name.c_str());
        else if(draft.edited(f))ImGui::TextColored(Color::Edited,"%s",name.c_str());
        else ImGui::TextUnformatted(name.c_str());
        if(hit)jumpTo(f);
        interact(f,block,missing);
        if(f.apply=="R"){ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x*2);ImGui::TextColored(draft.restartPending(f)?Color::Restart:Color::Muted,"R");interact(f,block,missing);}
        ImGui::TableSetColumnIndex(1);
        ImGui::BeginDisabled(block!=D::Block::None||locked());
        control(f);
        ImGui::EndDisabled();
        interact(f,block,missing);
        ImGui::PopID();
    }
    bool beginRows(const char* id){
        if(!ImGui::BeginTable(id,2,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_RowBg))return false;
        ImGui::TableSetupColumn("label",ImGuiTableColumnFlags_WidthStretch,1.2f);
        ImGui::TableSetupColumn("value",ImGuiTableColumnFlags_WidthStretch,1.0f);
        return true;
    }
    // A titled group: optional master checkbox in the title, a Reset button, its rows and a "Fine tuning" fold.
    void group(const Group& g,const std::map<std::string,const Field*>& byKey,std::map<std::string,bool>& used){
        auto take=[&](const char* list){std::vector<const Field*> out;for(const auto& k:words(list)){auto i=byKey.find(k);if(i!=byKey.end()&&!used[k]){out.push_back(i->second);used[k]=true;}}return out;};
        const Field* master=nullptr;
        if(g.master){auto i=byKey.find(g.master);if(i!=byKey.end()&&!used[g.master]){master=i->second;used[g.master]=true;}}
        const auto main=take(g.fields),advanced=take(g.advanced);
        if(!master&&main.empty()&&advanced.empty())return;
        ImGui::PushID(g.id);
        ImGui::Spacing();
        std::vector<const Field*> all=main;all.insert(all.end(),advanced.begin(),advanced.end());if(master)all.insert(all.begin(),master);
        if(master){
            std::string missing;const auto block=D::blocked(draft,*master,missing);
            ImGui::PushID(master->id().c_str());
            marker(*master);
            ImGui::BeginDisabled(block!=D::Block::None||locked());
            ImGui::PushStyleColor(ImGuiCol_Text,draft.edited(*master)?Color::Edited:Color::Heading);
            bool on=draft.effective(*master)=="1";
            if(ImGui::Checkbox(g.title,&on))set(*master,on?"1":"0");
            jumpTo(*master);
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            interact(*master,block,missing);
            ImGui::PopID();
        }else{ImGui::AlignTextToFramePadding();ImGui::TextColored(Color::Heading,"%s",g.title);}
        bool resettable=false;for(const Field* f:all)if(D::canInherit(*f))resettable=true;
        alignRight(buttonWidth("Reset"));
        ImGui::BeginDisabled(!resettable||locked());
        if(ImGui::SmallButton("Reset"))for(const Field* f:all)if(D::canInherit(*f)){draft.inherit(*f);live.erase(f->id());edited(*f);}
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Restore the inherited values of this group.");
        ImGui::Separator();
        if(!main.empty()&&beginRows("rows")){for(const Field* f:main)row(*f);ImGui::EndTable();}
        if(std::string(g.id)=="local"){
            const auto* total=draft.quality("LocalLightLimit");const auto* shadow=draft.quality("PointShadowLimit");
            const auto* lamps=draft.quality("LocalLights");const auto* maps=draft.quality("PointShadows");const auto* world=draft.quality("WorldShadows");
            if(total&&shadow&&lamps&&maps&&world){const unsigned count=draft.effective(*lamps)=="1"?unsigned(number(draft.effective(*total))):0;
                const unsigned shaded=draft.effective(*maps)=="1"&&draft.effective(*world)=="1"?std::min(count,unsigned(number(draft.effective(*shadow)))):0;
                ImGui::TextColored(Color::Muted,"Up to %u local lights; up to %u with shadows.",count,shaded);}
        }
        if(!advanced.empty()){
            char title[64];std::snprintf(title,sizeof title,"Fine tuning (%u)",unsigned(advanced.size()));
            if(searchScroll&&std::any_of(advanced.begin(),advanced.end(),[&](const auto* field){return field->id()==searchField;}))ImGui::SetNextItemOpen(true);
            if(ImGui::TreeNodeEx("fine",ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_NoTreePushOnOpen,"%s",title)&&beginRows("advanced")){
                for(const Field* f:advanced)row(*f);ImGui::EndTable();
            }
        }
        ImGui::PopID();
    }
    void loose(const char* id,const std::vector<const Field*>& rest){
        if(rest.empty())return;
        ImGui::Spacing();
        if(beginRows(id)){for(const Field* f:rest)row(*f);ImGui::EndTable();}
    }

    // ---- Pages ----
    // Quality keys without an assigned group stay accessible in Renderer fine tuning.
    std::vector<const Field*> unsortedQuality()const{
        std::vector<std::string> known{"Preset","Effects"};
        for(const char* key:CelestialQualityKeys)known.push_back(key);
        for(const auto& s:QualitySections)for(auto& k:sectionKeys(s))known.push_back(k);
        std::vector<const Field*> out;for(const Field* f:fields(D::Quality))if(std::find(known.begin(),known.end(),f->key)==known.end())out.push_back(f);
        return out;
    }
    // The fields a page shows (Sun & moon: every region), for its sidebar markers and Defaults.
    std::vector<const Field*> buildPageFields(Page p)const{
        if(p==Page::Water){
            auto rows=fields(NorthlightSettings::WaterHostFile);
            const auto tuning=fields(NorthlightSettings::WaterFile);
            rows.insert(rows.end(),tuning.begin(),tuning.end());return rows;
        }
        std::vector<const Field*> out;
        if(p==Page::Overview){if(const Field* f=draft.field(D::Celestial,"Appearance","Palette"))out.push_back(f);if(const Field* f=draft.quality("Preset"))out.push_back(f);for(auto& k:words(QuickKeys))if(const Field* f=draft.quality(k))out.push_back(f);return out;}
        if(p==Page::Celestial||p==Page::Regional||p==Page::Grading){
            for(const Field* field:fields(D::Celestial)){
                const bool grade=field->key=="tone_curve_strength"||NorthlightTone::gradeBindings().count(field->key)!=0;
                const bool appearance=NorthlightAppearancePresets::visualKey(field->key)||field->section=="Appearance";
                if(p==Page::Grading?grade:p==Page::Regional?appearance&&!grade:!appearance)out.push_back(field);
            }
            if(p==Page::Celestial)appendCelestialQuality(out);
            return out;
        }
        if(p==Page::ShadowRange)return fields(D::ShadowRange);
        if(p==Page::Renderer)out=fields(D::Renderer);
        if(const Section* s=qualitySection(Pages[unsigned(p)].quality))for(auto& k:sectionKeys(*s))if(const Field* f=draft.quality(k))out.push_back(f);
        if(p==Page::Renderer){auto extra=unsortedQuality();out.insert(out.end(),extra.begin(),extra.end());}
        return out;
    }
    const std::vector<const Field*>& pageFields(Page p)const{
        if(pageFieldsSnapshot!=snapshot.get()){
            for(const auto& info:Pages)pageFieldCache[unsigned(info.page)]=buildPageFields(info.page);
            pageFieldsSnapshot=snapshot.get();
        }
        return pageFieldCache[unsigned(p)];
    }
    void titled(const char* title,const std::vector<const Field*>& rows){
        if(rows.empty())return;
        ImGui::Spacing();ImGui::AlignTextToFramePadding();ImGui::TextColored(Color::Heading,"%s",title);ImGui::Separator();
        ImGui::PushID(title);if(beginRows("rows")){for(const Field* f:rows)row(*f);ImGui::EndTable();}ImGui::PopID();
    }
    void qualityPage(const Section& sec,bool withUnsorted){
        std::map<std::string,const Field*> mine;for(const auto& k:sectionKeys(sec))if(const Field* f=draft.quality(k))mine[k]=f;
        std::map<std::string,bool> used;
        for(const auto& g:sec.groups)group(g,mine,used);
        std::vector<const Field*> rest;for(const auto& k:words(sec.keys))if(mine.count(k)&&!used[k]){rest.push_back(mine[k]);used[k]=true;}
        titled(sec.title,rest);
        if(withUnsorted){
            const auto extra=unsortedQuality();
            if(searchScroll&&std::any_of(extra.begin(),extra.end(),[&](const auto* field){return field->id()==searchField;}))ImGui::SetNextItemOpen(true);
            if(!extra.empty()&&ImGui::TreeNode("Other fine tuning")){titled("Other",extra);ImGui::TreePop();}
        }
    }
    void overviewPage(){
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(Color::Muted,"Choose starting settings, then adjust effects in the categories on the left.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();ImGui::TextColored(Color::Heading,"Quality preset");ImGui::Separator();
        const Field* preset=draft.quality("Preset");
        const std::string current=preset?draft.effective(*preset):"";
        for(const auto& p:Presets){
            ImGui::PushID(p.key);
            const bool here=current==p.key;
            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();
            if(here)ImGui::TextColored(Color::Accent,"%s  (current)",p.key);else ImGui::TextUnformatted(p.key);
            if(here&&preset)jumpTo(*preset);
            ImGui::TextColored(Color::Muted,"%s",p.text);
            ImGui::EndGroup();
            ImGui::SameLine();alignRight(buttonWidth("Choose..."));
            ImGui::BeginDisabled(here||!preset||locked());
            if(ImGui::Button("Choose...")){presetChoice=p.key;confirm=Confirm::Preset;}
            ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Replace quality overrides with this preset's values after confirmation.\nRenderer and HDR settings are kept. Press Save to keep the changes.");
            ImGui::PopID();
            ImGui::Separator();
        }
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(Color::Muted,"Choosing a preset resets your quality changes to the preset's values. You will be asked to confirm.");
        ImGui::PopTextWrapPos();
        if(const auto* palette=draft.field(D::Celestial,"Appearance","Palette"))
            if(beginRows("world-look")){row(*palette,"Whole-world appearance");ImGui::EndTable();}
        waterPresetControl(false);
        std::vector<const Field*> quick;for(auto& k:words(QuickKeys))if(const Field* f=draft.quality(k))quick.push_back(f);
        titled("Quick toggles",quick);
        ImGui::Spacing();ImGui::TextColored(Color::Heading,"Summary");ImGui::Separator();
        unsigned set=0;if(snapshot)for(const auto& f:snapshot->fields)if(f.overrideValue!="~")++set;
        ImGui::Text("%u setting%s set in the INI files, %u unsaved change%s.",set,set==1?"":"s",unsigned(draft.size()),draft.size()==1?"":"s");
        if(const auto n=restartPending())ImGui::TextColored(Color::Restart,"%u change%s apply after a game restart.",unsigned(n),n==1?"":"s");
    }
    void rendererPage(){
        std::map<std::string,const Field*> byKey;for(const Field* f:fields(D::Renderer))byKey[f->key]=f;
        std::map<std::string,bool> used;
        if(auto b=byKey.find("Backend");b!=byKey.end()){used["Backend"]=true;titled("Renderer",{b->second});}
        {std::vector<const Field*> native;for(const auto* key:{"NativeLightStay","NativeMoreLights","NativeModelLightLimit","NativeTerrainLightLimit"})if(auto b=byKey.find(key);b!=byKey.end()){used[key]=true;native.push_back(b->second);}titled("Native lighting",native);}
        if(const Section* s=qualitySection("rendering"))qualityPage(*s,true);
        for(const auto& g:HdrGroups)group(g,byKey,used);
        if(service){const auto hdr=service->hdrStatus();ImGui::Text("HDR output: %s",hdr.active?"active":"inactive");if(!hdr.reason.empty())ImGui::TextWrapped("%s",hdr.reason.c_str());
            ImGui::TextWrapped("Display: %s",hdr.display.empty()?"Detecting...":hdr.display.c_str());
            ImGui::Text("Peak: %.0f nits (%s)",hdr.effectivePeak,hdr.peakSource.c_str());
            ImGui::Text("Effective white: %.0f nits   UI: %.0f nits",hdr.paperWhite,hdr.uiWhite);
            if(ImGui::TreeNode("Output details")){ImGui::TextUnformatted(hdr.active?"RGBA16F / linear scRGB, Rec.709 primaries":"SDR output");ImGui::TextWrapped("Display profile: %s",hdr.profile.empty()?"Stable identity unavailable":hdr.profile.c_str());ImGui::Text("Reported peak: %.0f nits",hdr.detectedPeak);ImGui::Text("HDR frame / output payload: %.2f MiB",double(hdr.payloadBytes)/1048576);ImGui::TextWrapped("Payload excludes other renderer resources and driver allocations; it is not residency.");ImGui::TextUnformatted("Values describe application signals, not measured panel brightness.");ImGui::TreePop();}
            ImGui::BeginDisabled(!hdr.active||!calibrationDraw);ImGui::Checkbox("Show HDR calibration",&calibrationOpen);ImGui::EndDisabled();
            jumpTo("hdr-calibration");
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Show brightness patches to compare ordinary white and peak highlights.\nRequires active HDR output. Does not change your saved display settings.");
            if(calibrationOpen&&hdr.active&&calibrationDraw){
                ImGui::TextWrapped("Adjust white and UI brightness for comfortable ordinary content. Then compare the peak gradient. These patches bypass world exposure, contrast, bloom and the scene peak limit.");
                ImGui::SliderFloat("Test peak (nits)",&calibrationPeak,80,10000,"%.0f",ImGuiSliderFlags_Logarithmic);
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Brightness used only for these test patches and the peak gradient.\nDoes not change Manual peak or the world's output limit.");
                ImGui::Checkbox("Wide field (panel ABL comparison)",&calibrationLarge);
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Enlarge the white patch to compare small and large bright areas.\nSome displays dim large areas through automatic brightness limiting (ABL).");
                auto patch=[&](float nits,float r,float g,float b,float w,float h){
                    const auto p=ImGui::GetCursorScreenPos();auto* list=ImGui::GetWindowDrawList();const auto lo=list->GetClipRectMin(),hi=list->GetClipRectMax();
                    calibrationPatches.push_back({this,{std::max(p.x,lo.x),std::max(p.y,lo.y),std::min(p.x+w,hi.x),std::min(p.y+h,hi.y)},{r*nits/80,g*nits/80,b*nits/80,1}});
                    list->AddCallback([](const ImDrawList*,const ImDrawCmd* command){auto& item=*static_cast<Patch*>(command->UserCallbackData);if(item.rect[2]>item.rect[0]&&item.rect[3]>item.rect[1])item.owner->calibrationDraw(item.rect,item.color);},&calibrationPatches.back());ImGui::Dummy(ImVec2(w,h));
                };
                const float available=ImGui::GetContentRegionAvail().x,side=std::max(12.f,(available-24)/3);
                ImGui::TextUnformatted("80 nits / 203 nits / test peak");
                patch(80,1,1,1,side,36);ImGui::SameLine();patch(203,1,1,1,side,36);ImGui::SameLine();patch(calibrationPeak,1,1,1,side,36);
                ImGui::TextUnformatted("Peak gradient: 50% to 150% of test peak");
                for(unsigned i=0;i<32;++i){if(i)ImGui::SameLine(0,0);patch(calibrationPeak*(.5f+float(i)/31),1,1,1,available/32,32);}
                ImGui::TextUnformatted("Color highlights");
                patch(calibrationPeak,1,.2f,.05f,side,32);ImGui::SameLine();patch(calibrationPeak,.05f,1,.2f,side,32);ImGui::SameLine();patch(calibrationPeak,.2f,.05f,1,side,32);
                patch(calibrationPeak,1,1,1,calibrationLarge?available:48,calibrationLarge?160:48);
            }
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(Color::Muted,"HDR needs DXVK 2 or 3, the separate render thread, windowed mode and HDR enabled for the current display in Windows. HDR and its brightness settings apply live.");
        ImGui::PopTextWrapPos();
        std::vector<const Field*> rest;for(auto& k:byKey)if(!used[k.first])rest.push_back(k.second);
        titled("Other",rest);
    }
    void captureDiagnostics(){
        ImGui::TextColored(Color::Heading,"Build");
        ImGui::Text("ID: %.12s",NORTHLIGHT_SOURCE_ID);
        ImGui::Text("Built: %s",NORTHLIGHT_BUILD_UTC);
        ImGui::Spacing();ImGui::Separator();
        using namespace NorthlightDiagnosticCapture;
        if(ImGui::Button("Capture world lighting"))request(World);
        ImGui::SameLine();if(ImGui::Button("Capture AO"))request(AO);
        if(ImGui::Button("Capture materials"))request(Materials);
        ImGui::SameLine();if(ImGui::Button("Capture fog"))request(Fog);
        if(ImGui::Button("Capture weather"))request(Weather);
        ImGui::TextWrapped("Saves a compact capture in northlight-diagnostics. ZIP the entire session folder, including .capture-blobs. Repeated data is stored once. Capture frames are excluded from FPS statistics.");
        using QaState=NorthlightQaScene::State;
        const auto qa=NorthlightQaScene::state.load();
        if(qa==QaState::Disabled)return;
        ImGui::Separator();
        ImGui::BeginDisabled(qa!=QaState::Recording);
        if(ImGui::Button("Save last 15 seconds"))NorthlightQaScene::requestSave();
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Ctrl+Alt+F12");
        ImGui::EndDisabled();
        if(qa==QaState::Saved)ImGui::TextUnformatted("Scene marked. The QA launcher will close the game and finish the package.");
    }
    void frameGraph(bool compact){
        const auto& t=timing;const float font=ImGui::GetFontSize(),height=(compact?76.f:112.f)*scale;
        const auto start=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(width,height));auto* draw=ImGui::GetWindowDrawList();
        const ImVec2 lo(start.x+font*3.6f,start.y+font*.7f),hi(start.x+width-5*scale,start.y+height-font*1.4f);
        if(hi.x<=lo.x||hi.y<=lo.y)return;
        float peak=0;double span=0;
        for(unsigned i=0;i<t.graphCount;++i){peak=std::max(peak,t.graph[i]);span+=t.graph[i];}
        float ceiling=1000.f/60;
        while(ceiling<peak*1.08f)ceiling*=2;
        const auto rgba=[](ImVec4 c,float alpha){c.w*=alpha;return ImGui::GetColorU32(c);};
        draw->AddRectFilled(lo,hi,rgba(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg),.3f),2*scale);
        for(unsigned i=0;i<=2;++i){const float y=hi.y-(hi.y-lo.y)*float(i)/2;char text[24];std::snprintf(text,sizeof text,"%.1f",ceiling*float(i)/2);
            draw->AddLine(ImVec2(lo.x,y),ImVec2(hi.x,y),rgba(Color::Muted,.18f));
            draw->AddText(ImVec2(start.x,y-font*.5f),rgba(Color::Muted,.9f),text);
        }
        std::array<ImVec2,NorthlightPerformance::Timing::GraphSize> points;
        const auto xy=[&](unsigned i){return ImVec2(hi.x-(hi.x-lo.x)*float(t.graphCount-1-i)/float(t.GraphSize-1),hi.y-(hi.y-lo.y)*t.graph[i]/ceiling);};
        for(unsigned i=0;i<t.graphCount;++i)points[i]=xy(i);
        if(t.graphCount>1)draw->AddPolyline(points.data(),int(t.graphCount),rgba(Color::Accent,1),ImDrawFlags_None,1.25f*scale);
        if(t.graphCount){draw->AddCircleFilled(points[t.graphCount-1],2*scale,rgba(Color::Accent,1));
            if(ImGui::IsItemHovered()){
                const float x=std::clamp(ImGui::GetIO().MousePos.x,lo.x,hi.x);
                const unsigned back=std::min(t.graphCount-1,unsigned(std::lround((hi.x-x)/(hi.x-lo.x)*float(t.GraphSize-1))));
                const auto p=points[t.graphCount-1-back];draw->AddLine(ImVec2(p.x,lo.y),ImVec2(p.x,hi.y),rgba(Color::Muted,.6f));
                ImGui::SetTooltip("%.3f ms | %u frame%s ago",t.graph[t.graphCount-1-back],back,back==1?"":"s");
            }
        }
        char label[80];std::snprintf(label,sizeof label,"ms  |  %u frames / %.2f s",t.graphCount,span*.001);
        draw->AddText(ImVec2(lo.x,hi.y+3*scale),rgba(Color::Muted,1),label);
        if(!t.graphCount)draw->AddText(ImVec2(lo.x+6*scale,lo.y+font),rgba(Color::Muted,1),"Waiting for frames...");
    }
    void monitorContents(bool compact){
        namespace P=NorthlightPerformance;P::store().touch(monitorAdapter.owner);
        if(!hardwareSampler)hardwareSampler=std::make_unique<P::HardwareSampler>(monitorAdapter);
        const auto now=P::nowMs();const bool refresh=!monitorReadAt||now-monitorReadAt>=250;
        if(refresh){P::store().read(monitorAdapter.owner,timing);hardwareSampler->read(hardware);monitorReadAt=now;}
        P::store().graph(monitorAdapter.owner,timing);
        const bool fresh=hardware.at&&now>=hardware.at&&now-hardware.at<=3000;
        const bool frameFresh=timing.frame.at&&now>=timing.frame.at&&now-timing.frame.at<=2000;
        auto metric=[](double v,const char* suffix="",unsigned precision=0){char text[64];if(v<0||!std::isfinite(v))return std::string("--")+suffix;std::snprintf(text,sizeof text,"%.*f%s",int(precision),v,suffix);return std::string(text);};
        const auto gib=[&](uint64_t bytes){return metric(double(bytes)/1073741824.," GiB",1);};
        const auto row=[](const char* name,const ImVec4& color,const std::string& text){ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextColored(color,"%s",name);ImGui::TableSetColumnIndex(1);ImGui::TextUnformatted(text.c_str());};
        if(refresh){
            monitorBoardMemory=fresh&&hardware.boardMemory;
            monitorSummary[0]=metric(fresh?hardware.gpu:-1,"%")+"   "+metric(fresh?hardware.temperature:-1," C")+"   "+metric(fresh?hardware.powerW:-1," W");
            const auto vram=monitorBoardMemory?gib(hardware.vramUsed)+" / "+gib(hardware.vramTotal):fresh&&hardware.gameMemory?gib(hardware.gameVram)+" / "+gib(hardware.gameVramBudget):"--";
            monitorSummary[1]=vram+"   "+metric(fresh?hardware.clockMHz:-1," MHz");
            monitorSummary[2]=metric(fresh?hardware.cpu:-1,"%")+"    game "+metric(fresh?hardware.gameCpu:-1,"%");
            monitorSummary[3]=fresh?gib(hardware.ramUsed)+" / "+gib(hardware.ramTotal)+"   game "+(hardware.gameRamKnown?gib(hardware.gameRam):"--"):"--";
            monitorSummary[4]=metric(frameFresh&&timing.samples?timing.fps:-1)+"    1% "+metric(frameFresh?timing.low1:-1)+"    0.1% "+metric(frameFresh?timing.low01:-1);
            monitorSummary[5]=metric(frameFresh&&timing.samples?timing.meanMs:-1," ms",2)+"    max "+metric(frameFresh&&timing.samples?timing.maxMs:-1," ms",2);
        }
        if(ImGui::BeginTable("monitor-main",2,ImGuiTableFlags_SizingStretchProp)){
            ImGui::TableSetupColumn("label",ImGuiTableColumnFlags_WidthFixed,ImGui::CalcTextSize("VRAM game").x+4);ImGui::TableSetupColumn("value",ImGuiTableColumnFlags_WidthStretch);
            row("GPU",Color::Good,monitorSummary[0]);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Graphics load, temperature (C), power (W). -- means the driver does not expose this sensor.");
            row(monitorBoardMemory?"VRAM":"VRAM game",Color::Good,monitorSummary[1]);
            row("CPU",Color::Accent,monitorSummary[2]);row("RAM",Color::Muted,monitorSummary[3]);
            row("FPS",Color::Heading,monitorSummary[4]);row("Frame",Color::Muted,monitorSummary[5]);ImGui::EndTable();
        }
        frameGraph(compact);
        if(ImGui::BeginTable("monitor-work",2,ImGuiTableFlags_SizingStretchProp)){
            ImGui::TableSetupColumn("label",ImGuiTableColumnFlags_WidthStretch,1.45f);ImGui::TableSetupColumn("value",ImGuiTableColumnFlags_WidthStretch);
            row("Effects CPU",Color::Heading,metric(frameFresh?timing.frame.effectsCpuMs:-1," ms",2));
            row("Effects GPU interval",Color::Heading,metric(frameFresh?timing.frame.effectsGpuMs:-1," ms",2));
            row("EVENT GPU wait",Color::Heading,metric(frameFresh?timing.frame.eventWaitMs:-1," ms",2));
            row("Render queue idle",Color::Heading,metric(frameFresh?timing.frame.queueIdleMs:-1," ms",2));
            row("Window",Color::Muted,fresh?std::to_string(hardware.windowWidth)+"x"+std::to_string(hardware.windowHeight):"--");
            row("Scene target",Color::Muted,timing.frame.sceneWidth?std::to_string(timing.frame.sceneWidth)+"x"+std::to_string(timing.frame.sceneHeight):"--");
            const auto size=ImGui::GetIO().DisplaySize;row("Output",Color::Muted,std::to_string(unsigned(size.x))+"x"+std::to_string(unsigned(size.y)));
            row("D3D presentation",Color::Muted,monitorAdapter.interval==D3DPRESENT_INTERVAL_IMMEDIATE?"Immediate":monitorAdapter.interval?"Interval "+std::to_string(monitorAdapter.interval):"Driver default");ImGui::EndTable();
        }
        if(hardware.gpuName[0])ImGui::TextWrapped("%s",hardware.gpuName);if(hardware.cpuName[0])ImGui::TextWrapped("%s",hardware.cpuName);
        if(!compact){
            ImGui::TextDisabled("%u frame intervals | hardware 1 Hz",timing.samples);
            if(ImGui::CollapsingHeader("How these metrics are measured")){
            ImGui::TextWrapped("Current FPS uses the latest ~250 ms and updates every 250 ms.");
            ImGui::TextWrapped("Last %u eligible intervals (up to 4096). Lows use the mean of the slowest 1%% / 0.1%%; at least 100 / 1000 samples are needed. Unfocused frames, capture frames and gaps over 2 seconds are excluded.",timing.samples);
            ImGui::TextWrapped("Hardware is sampled once per second. -- means unavailable. GPU interval covers Northlight effects and can include idle gaps; CPU is elapsed effects time. Neither measures the whole game's GPU busy time.");
            ImGui::TextWrapped("VRAM is driver-reported dedicated-memory use when available; VRAM game is this process's local-memory use / OS budget. It is not the card's total resident memory.");
            if(fresh&&hardware.gpuSource[0])ImGui::Text("GPU load source: %s",hardware.gpuSource);
            }
        }
    }
    void performanceMonitor(){
        ImGui::Checkbox("Show compact HUD while settings are closed (this session)",&monitoringHud);
        ImGui::SameLine();if(ImGui::Button("Reset samples")){NorthlightPerformance::store().reset(monitorAdapter.owner);timing={};monitorReadAt=0;}
        monitorContents(false);
        namespace M=NorthlightMemoryProfile;
        const uint64_t now=M::nowMs();
        if(!memoryReadAt||now-memoryReadAt>=1000){if(M::store().tryRead(memorySnapshot))memoryReadAt=now;}
        const auto& memory=memorySnapshot;
        ImGui::Spacing();ImGui::TextColored(Color::Heading,"Memory snapshot");ImGui::Separator();
        ImGui::TextWrapped("Sampled about every 10 seconds. Values below describe the last sample, not a live allocation limit.");
        const auto age=[&](uint64_t at){return at&&now>=at?double(now-at)/1000:0.;};
        const auto bytesRow=[](const char* label,uint64_t bytes){ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextUnformatted(label);ImGui::TableSetColumnIndex(1);ImGui::Text("%.2f MiB",double(bytes)/1048576);};
        const auto& admission=memory.admission;
        if(admission.at){
            ImGui::TextWrapped("Scene builder: %s (%.0f s ago)",admission.stage,age(admission.at));
            if(age(admission.at)>30)ImGui::TextColored(Color::Edited,"This sample is old; the scene builder may be idle.");
            if(!admission.valid)ImGui::TextColored(Color::Edited,"The process memory sample was unavailable.");
            else if(admission.available<admission.entryRequired)ImGui::TextColored(Color::Edited,"Free address space is below the next-build threshold.");
            if(beginRows("memory-admission")){
                if(admission.valid){bytesRow("Free virtual address space",admission.available);bytesRow(admission.largestExact?"Largest free block":"Free block (at least)",admission.largest);}
                bytesRow("Required to start a scene build",admission.entryRequired);bytesRow("Process safety reserve",admission.reserve);bytesRow("Temporary entry headroom",admission.entryHeadroom);
                if(admission.minimumAvailable)bytesRow("Lowest sampled free address space",admission.minimumAvailable);
                bytesRow("Largest sampled builder capacity",admission.maximumKnownCapacity);
                ImGui::EndTable();
            }
            ImGui::TextWrapped("The entry threshold is free address space required before building. It is not memory allocated to GI.");
        }
        if(!memory.builder.at&&!memory.solver.at&&!memory.gpu.at){
            ImGui::TextColored(Color::Muted,"No memory sample yet.");
            ImGui::TextWrapped("Enable Diagnostic logging, then enter a world area and wait for a sample. Saved settings are not changed by this view.");
            return;
        }
        if(ImGui::TreeNode("CPU memory breakdown")){
            if(memory.builder.at)ImGui::Text("Builder sample: %.0f s ago",age(memory.builder.at));else ImGui::TextDisabled("Builder: no sample yet.");
            if(memory.solver.at)ImGui::Text("GI worker sample: %.0f s ago",age(memory.solver.at));else ImGui::TextDisabled("GI worker: no sample yet.");
            ImGui::TextWrapped("Shared storage counts once within each column; the two columns overlap and must not be added.");
            if((memory.builder.at&&!memory.builder.complete)||(memory.solver.at&&!memory.solver.complete))ImGui::TextColored(Color::Edited,"Incomplete sample: some diagnostic bookkeeping could not be allocated.");
            if(ImGui::BeginTable("memory-cpu",3,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_RowBg)){
                ImGui::TableSetupColumn("Retained capacity",ImGuiTableColumnFlags_WidthStretch,1.8f);ImGui::TableSetupColumn("Builder");ImGui::TableSetupColumn("GI worker");ImGui::TableHeadersRow();
                const char* labels[]={"Geometry vertices","Geometry triangles","BVH / acceleration","CPU texture pixels","Upload pages / indices","GI probes / path records","Regional fog","Metadata"};
                uint64_t totals[2]={};
                for(unsigned i=0;i<NorthlightMemoryFootprint::KindCount;++i){
                    ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextUnformatted(labels[i]);
                    const M::Cpu* columns[]={&memory.builder,&memory.solver};
                    for(unsigned column=0;column<2;++column){ImGui::TableSetColumnIndex(int(column+1));if(columns[column]->at)ImGui::Text("%.2f MiB",double(columns[column]->bytes[i])/1048576);else ImGui::TextDisabled("No sample");totals[column]+=columns[column]->bytes[i];}
                }
                ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextUnformatted("Known capacity in this scope");
                for(unsigned column=0;column<2;++column){ImGui::TableSetColumnIndex(int(column+1));if((column?memory.solver:memory.builder).at)ImGui::Text("%.2f MiB",double(totals[column])/1048576);else ImGui::TextDisabled("No sample");}
                ImGui::EndTable();
            }
            ImGui::TextWrapped("Capacity estimates exclude allocator overhead and owners outside each sampled scope. They are not total process RAM usage.");
            ImGui::TreePop();
        }
        if(ImGui::TreeNodeEx("World GPU resource pools",ImGuiTreeNodeFlags_DefaultOpen)){
            const auto& gpu=memory.gpu;
            if(!gpu.at)ImGui::TextDisabled("No resource sample yet.");
            else{
                ImGui::Text("Last sample: %.0f s ago",age(gpu.at));
                if(!gpu.complete)ImGui::TextColored(Color::Edited,"Incomplete inventory (%u unknown descriptions).",gpu.unknown);
                if(beginRows("memory-gpu-pools")){bytesRow("Managed resources (logical)",gpu.managed);bytesRow("Default resources (logical)",gpu.defaults);bytesRow("System-memory resources (logical)",gpu.system);bytesRow("CPU material reuse cache",gpu.materialReuseCpu);bytesRow("Retired material resource ledger",gpu.retiredMaterials);ImGui::EndTable();}
                ImGui::TextWrapped("Managed resources can retain a system-memory copy. Default placement is chosen by the driver. These are logical sizes, not measured resident VRAM or RAM backing. Static and replay caches are reported separately in the log.");
                if(ImGui::TreeNode("Additional cache ledgers")){
                    if(beginRows("memory-cache-ledgers")){bytesRow("Static shadows (default)",gpu.staticDefaultLedger);bytesRow("Replay cache (default)",gpu.replayDefaultLedger);bytesRow("Live terrain buffer (default)",gpu.liveTerrainDefault);bytesRow("Captured geometry on CPU",gpu.captureCpu);bytesRow("Capture identity metadata",gpu.captureIdentity);ImGui::EndTable();}
                    ImGui::TextWrapped("The GPU caches above are outside the world-resource inventory. CPU capture data may also be referenced by the sampled scene scopes.");ImGui::TreePop();
                }
                if(ImGui::TreeNode("Resources by purpose")){
                    if(beginRows("memory-gpu-groups")){bytesRow("Geometry / cutout textures",gpu.geometry);bytesRow("GI probe textures",gpu.probes);bytesRow("Shadow maps / depth",gpu.shadows);bytesRow("Lighting / fog render targets",gpu.post);bytesRow("Fog fields / noise / constants",gpu.noise);bytesRow("Dynamic upload buffers",gpu.dynamic);ImGui::EndTable();}
                    ImGui::TextWrapped("This is another grouping of the same resources; do not add it to the pool totals.");ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }
    void waterPresetControl(bool showFields){
        const auto water=[&](const char* key){return draft.field(NorthlightSettings::WaterFile,"Water",key);};
        const Field* scenery=water("ReflectionScenery");
        const Field* resolution=water("ReflectionResolution");
        const Field* distance=water("ReflectionDistance");
        const Field* details=water("ReflectionSceneryDetails");
        const Field* highlights=water("FineHighlights");
        const Field* blur=water("SurfaceBlur");
        if(scenery && resolution && distance && details && highlights && blur){
            struct Tier { const char* name; const char* scenery; const char* resolution; const char* distance; const char* details; const char* highlights; const char* blur; };
            static constexpr Tier tiers[]={
                {"Ultra","1","100","2","1","1","1"},{"Quality","1","100","0","0","0","1"},
                {"Balanced","1","75","0","0","0","1"},{"Performance","0","50","1","0","0","0"},
            };
            const char* current="Custom";
            for(const auto& tier:tiers)
                if(draft.effective(*scenery)==tier.scenery && draft.effective(*resolution)==tier.resolution &&
                   draft.effective(*distance)==tier.distance && draft.effective(*details)==tier.details &&
                   draft.effective(*highlights)==tier.highlights &&
                   std::strtod(draft.effective(*blur).c_str(),nullptr)==std::strtod(tier.blur,nullptr))current=tier.name;
            ImGui::TextColored(Color::Heading,"Performance");ImGui::Separator();
            ImGui::BeginDisabled(locked());
            if(ImGui::BeginCombo("Water preset",current)){
                for(const auto& tier:tiers){
                    const bool selected=std::string(current)==tier.name;
                    if(ImGui::Selectable(tier.name,selected)){
                        set(*scenery,tier.scenery);set(*resolution,tier.resolution);set(*distance,tier.distance);
                        set(*details,tier.details);set(*highlights,tier.highlights);set(*blur,tier.blur);
                    }
                }
                ImGui::EndCombo();
            }
            if(showFields)jumpTo("water-preset");
            ImGui::EndDisabled();
            if(showFields)loose("water-performance",{scenery,resolution,distance,details,highlights,blur});
        }
    }
    void waterPage(){
        ImGui::TextWrapped("Save to apply water settings. Restart the game after switching Tideglass on or off.");
        W::cardTitle("General water settings");
        titled("Renderer",fields(NorthlightSettings::WaterHostFile));
        waterPresetControl(true);
        auto waterRows=[&](const char* title,const char* keys){
            std::vector<const Field*> rows;
            for(const auto& key:words(keys)){
                if(const Field* f=draft.field(NorthlightSettings::WaterFile,"Water",key))rows.push_back(f);
            }
            titled(title,rows);
        };
        waterRows("Waves and foam","WaveStrength WaveSpeed WaveCrestShape WaveCrestSharpness RippleStrength FoamStrength OpenWaterCrestFoam");
        waterRows("Reflections","Reflection NearReflection");
        waterRows("Clarity and underwater","Clarity Underwater UnderwaterVisibility UnderwaterScenery");
        waterRows("Other liquids","Slime Lava");
        W::cardTitle("Regional slime");
        const auto* scope=selectedRegion();
        const auto* slime=scope?draft.field(NorthlightSettings::WaterFile,"RegionalSlime",scope->slime):nullptr;
        if(!slime)W::hint("This profile has no separate slime setting.");
        else if(beginRows("regional-slime")){regionalRow(*slime,"Regional slime");ImGui::EndTable();}
    }
    void drawPage(){
        W::cardTitle(PageLabels[unsigned(page)]);
        if(page==Page::Regional||page==Page::Grading||page==Page::Celestial){pickRegion(page!=Page::Celestial);ImGui::Separator();}
        const auto appearance=appearanceRegion();
        switch(page){
        case Page::Overview:overviewPage();break;
        case Page::Water:waterPage();break;
        case Page::Regional:
            if(W::beginCard("world-look")){
                W::cardTitle("Whole world");
                if(const Field* palette=draft.field(D::Celestial,"Appearance","Palette"))loose("world-palette",{palette});
                if(!snapshot->presetErrors.empty())W::hint(snapshot->presetErrors.front().c_str());
                W::hint("Default keeps Northlight's regional look and your own region edits. Another look replaces the base for the whole world; your region edits still apply on top.");
                ImGui::BeginDisabled(locked());
                if(ImGui::Button("Save as preset...")){appearancePresetName[0]='\0';appearancePresetModal=true;}
                jumpTo("save-look");
                ImGui::EndDisabled();
                if(const Field* palette=draft.field(D::Celestial,"Appearance","Palette")){
                    const auto chosen=ownAppearancePreset(draft.effective(*palette));
                    ImGui::SameLine();ImGui::BeginDisabled(chosen.empty()||locked());
                    if(ImGui::Button("Delete preset...")){appearancePresetToDelete=chosen;appearancePresetDeleteModal=true;}
                    jumpTo("delete-look");
                    ImGui::EndDisabled();
                    if(chosen.empty()&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Looks that come with Northlight cannot be deleted.");
                }
                W::hint("A preset is the whole-world look as you see it now, saved under a name in appearance-presets. Its files can be shared.");
            }
            W::endCard();
            if(W::beginCard("regional-look")){
                W::cardTitle("Fog and contrast");
                if(!appearance.empty()){
                    if(beginRows("regional")){
                        if(const Field* f=draft.field(D::Celestial,appearance,"shadow_contrast"))regionalRow(*f,rowLabel(*f).c_str());
                        ImGui::EndTable();
                    }
                    const std::string time=NorthlightTone::timeName(GradeTimes[editTime]);
                    if(beginRows("fog")){
                        if(const Field* f=draft.field(D::Celestial,appearance,time+"_fog_tint_strength"))regionalRow(*f,rowLabel(*f).c_str());
                        for(const char* name:{"_fog_tint","_fog_brightness"})
                            if(const Field* f=draft.field(D::Celestial,appearance,time+name))row(*f);
                        ImGui::EndTable();
                    }
                }else W::hint("This region uses the default appearance profile.");
                W::hint("Fog tint colours the fog a region already has and brightness darkens or lifts it, each for Daylight, Dusk and Night; Dusk also shows at sunrise. Shadow contrast deepens dark detail at every hour. The region chosen here is the one Colour grading and Sun & moon edit too.");
            }
            W::endCard();
            break;
        case Page::Grading:
            if(W::beginCard("grading")){
                if(const Field* strength=draft.field(D::Celestial,appearance,"tone_curve_strength")){
                    if(beginRows("strength")){regionalRow(*strength,"Tone curve strength");ImGui::EndTable();}
                    if(number(draft.effective(*strength))==0)ImGui::TextWrapped("This region's grade is off while its strength is zero. Choose Tuned above to see your edits.");
                    W::hint("Strength scales the whole grade; at zero the grade is off. Dusk is the 19:30 look, shown through sunset and again at sunrise; what it leaves unset follows the Daylight-to-Night blend.");
                    ImGui::BeginDisabled(locked());gradeEditor(GradeTimes[editTime],appearance);ImGui::EndDisabled();
                }else W::hint("This region uses the default appearance profile.");
            }
            W::endCard();
            break;
        case Page::Celestial:celestialTab(appearance);break;
        case Page::ShadowRange:shadowRangeTab();break;
        case Page::Renderer:rendererPage();break;
        case Page::Performance:performanceMonitor();break;
        case Page::Diagnostics:captureDiagnostics();if(const Section* s=qualitySection("diagnostics"))qualityPage(*s,false);break;
        default:if(const Section* s=qualitySection(Pages[unsigned(page)].quality))qualityPage(*s,false);break;
        }
    }
    void selectPage(Page next){
        page=next;lastPages[menuSection(next)]=next;
        searchField.clear();searchAction=nullptr;searchScroll=false;
    }
    void sidebar(){
        const float h=ImGui::GetFrameHeight(),r=std::max(2.5f,h*0.14f);
        for(unsigned i=0;i<std::size(MenuSections);++i){
            const auto& section=MenuSections[i];
            if(section.pages[0]==Page::Water&&!draft.field(NorthlightSettings::WaterHostFile,"WaterIntegration","Enable"))continue;
            bool changed=false,restart=false;
            for(unsigned j=0;j<section.count;++j)for(const Field* f:pageFields(section.pages[j])){
                changed|=draft.edited(*f);restart|=draft.restartPending(*f);
            }
            if(ImGui::Selectable(section.title,menuSection(page)==i,0,ImVec2(0,h)))selectPage(lastPages[i]);
            const ImVec2 a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
            auto* draw=ImGui::GetWindowDrawList();float x=b.x-ImGui::GetStyle().ItemInnerSpacing.x-r;
            if(changed){draw->AddCircleFilled(ImVec2(x,(a.y+b.y)*0.5f),r,u32(Color::Edited));x-=2*r+ImGui::GetStyle().ItemInnerSpacing.x;}
            if(restart){const ImVec2 t=ImGui::CalcTextSize("R");draw->AddText(ImVec2(x-t.x*0.5f-r,(a.y+b.y-t.y)*0.5f),u32(Color::Restart),"R");}
        }
    }
    void subpages(){
        const auto& section=MenuSections[menuSection(page)];
        if(section.count==1)return;
        const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
        for(unsigned i=0;i<section.count;++i){
            const Page next=section.pages[i];
            const char* label=PageLabels[unsigned(next)];
            if(i&&ImGui::GetItemRectMax().x+ImGui::GetStyle().ItemSpacing.x+buttonWidth(label)<=right)ImGui::SameLine();
            const bool active=page==next;
            if(active)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
            if(ImGui::Button(label))selectPage(next);
            if(active)ImGui::PopStyleColor();
        }
        ImGui::Separator();
    }
    void refreshSearch(){
        if(!snapshot||searchSnapshot==snapshot.get())return;
        searchSnapshot=snapshot.get();searchEntries.clear();searchQuery.clear();searchResults.clear();
        std::set<std::pair<unsigned,std::string>> seen;
        auto add=[&](Page target,std::string title,const Field* field,const char* action,const char* aliases){
            std::string path=MenuSections[menuSection(target)].title;
            if(MenuSections[menuSection(target)].count>1)path+=" / "+std::string(PageLabels[unsigned(target)]);
            const std::string key=field?searchWords(field->key):std::string{};
            std::string terms=title+" "+path+" "+Pages[unsigned(target)].title+" "+aliases;
            if(field){
                terms+=" "+field->key+" "+fieldLabel(*field);
                if(field->kind=="enum"||D::modeField(*field)){
                    const auto options=D::modeField(*field)?D::modeOptions(*field):D::split(field->options);
                    for(const auto& option:options)terms+=" "+optionLabel(*field,option);
                }
                if(field->key.rfind("day_",0)==0)path+=" / Day";
                if(field->key.rfind("dusk_",0)==0)path+=" / Dusk";
                if(field->key.rfind("night_",0)==0)path+=" / Night";
                terms+=" "+path;
            }
            searchEntries.push_back({target,std::move(title),std::move(path),searchWords(std::move(terms)),key,field,action});
        };
        for(const auto& info:Pages)for(const Field* field:pageFields(info.page)){
            if(info.page==Page::Overview&&field->key!="Preset"&&field->key!="Effects")continue;
            const bool slime=field->file==NorthlightSettings::WaterFile&&field->section=="RegionalSlime";
            if(!seen.emplace(field->file,slime?"RegionalSlime":field->key).second)continue;
            const auto& key=field->key;
            const char* aliases="";
            if(key.rfind("Reflection",0)==0||key=="NearReflection"||key=="SurfaceBlur")aliases="reflection reflected";
            else if(key=="CelestialStyle")aliases="sky sun moon Default Forever";
            else if(key=="GI")aliases="global illumination indirect lighting";
            else if(key=="ContactAO")aliases="contact shading ambient occlusion";
            const auto title=slime?std::string("Regional slime"):rowLabel(*field);
            add(info.page,title,field,nullptr,slime?"slime canals":aliases);
        }
        if(draft.field(NorthlightSettings::WaterHostFile,"WaterIntegration","Enable"))add(Page::Water,"Water preset",nullptr,"water-preset","Ultra Quality Balanced Performance");
        add(Page::Regional,"Save as preset",nullptr,"save-look","appearance palette");
        add(Page::Regional,"Delete preset",nullptr,"delete-look","appearance palette");
        add(Page::Renderer,"HDR calibration",nullptr,"hdr-calibration","Show HDR calibration Test peak Wide field");
    }
    void updateSearch(){
        refreshSearch();
        const auto query=searchWords(searchText.data());
        if(query==searchQuery)return;
        searchQuery=query;searchResults.clear();searchSelected=0;
        const auto tokens=words(query.c_str());
        if(tokens.empty())return;
        std::vector<std::pair<int,std::size_t>> ranked;
        for(std::size_t i=0;i<searchEntries.size();++i){
            const auto& hit=searchEntries[i];
            if(!std::all_of(tokens.begin(),tokens.end(),[&](const auto& token){return hit.words.find(token)!=std::string::npos;}))continue;
            const auto title=searchWords(hit.title);
            int rank=(hit.key==query?150:0)+(title==query?125:0)+(title.rfind(query,0)==0?50:0);
            for(const auto& token:tokens)rank+=(title.find(token)!=std::string::npos?10:0)+(hit.key.find(token)!=std::string::npos?7:0);
            ranked.emplace_back(-rank,i);
        }
        std::sort(ranked.begin(),ranked.end(),[&](const auto& a,const auto& b){
            return a.first!=b.first?a.first<b.first:searchEntries[a.second].title<searchEntries[b.second].title;
        });
        for(const auto& item:ranked)searchResults.push_back(item.second);
        searchScroll=true;
    }
    void openSearchHit(const SearchEntry& hit){
        selectPage(hit.page);searchText[0]='\0';searchAction=hit.action;searchScroll=true;
        if(!hit.field)return;
        const auto& field=*hit.field;
        const Field* target=&field;
        const auto* scope=selectedRegion();
        if(scope){
            if(field.file==D::Celestial&&field.section!="Appearance")target=draft.field(field.file,scope->appearance,field.key);
            if(field.file==D::ShadowRange)target=draft.field(field.file,scope->shadow.empty()?"default":scope->shadow,field.key);
            if(field.file==NorthlightSettings::WaterFile&&field.section=="RegionalSlime")target=draft.field(field.file,"RegionalSlime",scope->slime);
        }
        if(target)searchField=target->id();
        if(const auto i=NorthlightTone::gradeBindings().find(field.key);i!=NorthlightTone::gradeBindings().end()){
            const auto& binding=i->second;
            editTime=int(binding.time);curveChannel=int(binding.channel);selectedBand=int(binding.band);
        }else{
            if(field.key.rfind("day_",0)==0)editTime=0;
            if(field.key.rfind("dusk_",0)==0)editTime=1;
            if(field.key.rfind("night_",0)==0)editTime=2;
        }
    }
    bool searchHeader(){
        searchUp=searchDown=false;
        const char* clearLabel="Clear";
        ImGui::SetNextItemWidth(std::max(70.f,ImGui::GetContentRegionAvail().x-buttonWidth(clearLabel)-ImGui::GetStyle().ItemSpacing.x));
        const auto callback=[](ImGuiInputTextCallbackData* data){
            auto& self=*static_cast<State*>(data->UserData);
            self.searchUp=data->EventKey==ImGuiKey_UpArrow;self.searchDown=data->EventKey==ImGuiKey_DownArrow;return 0;
        };
        searchEnter=ImGui::InputTextWithHint("##settings-search","Search settings...",searchText.data(),searchText.size(),
            ImGuiInputTextFlags_EscapeClearsAll|ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_CallbackHistory,callback,this);
        ImGui::SameLine();ImGui::BeginDisabled(!searchText[0]);
        if(ImGui::Button(clearLabel))searchText[0]='\0';
        ImGui::EndDisabled();
        return !words(searchWords(searchText.data()).c_str()).empty();
    }
    void drawSearch(){
        ImGui::TextUnformatted("Search results");
        if(!snapshot){W::hint("Loading settings...");return;}
        updateSearch();
        if(searchResults.empty()){W::hint("No matches. Try a setting name or INI key.");return;}
        ImGui::TextColored(Color::Muted,"Matches: %zu",searchResults.size());
        const auto size=int(searchResults.size());
        if(searchUp||searchDown){searchSelected=(searchSelected+(searchUp?size-1:1))%size;searchScroll=true;}
        if(searchEnter){openSearchHit(searchEntries[searchResults[searchSelected]]);return;}
        ImGui::Separator();
        const float line=ImGui::GetTextLineHeightWithSpacing();
        ImGuiListClipper clipper;clipper.Begin(size,2*line+ImGui::GetStyle().ItemSpacing.y);
        if(searchScroll)clipper.IncludeItemByIndex(searchSelected);
        while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i){
            const auto& hit=searchEntries[searchResults[i]];
            const ImVec2 origin=ImGui::GetCursorScreenPos();
            ImGui::PushID(i);
            const bool clicked=ImGui::Selectable("##result",i==searchSelected,0,ImVec2(0,2*line));
            ImGui::PopID();
            auto* draw=ImGui::GetWindowDrawList();
            draw->AddText(origin,ImGui::GetColorU32(ImGuiCol_Text),hit.title.c_str());
            const auto path=hit.path+(hit.field?"  /  "+(hit.field->section=="RegionalSlime"?std::string("RegionalSlime"):hit.field->key):std::string{});
            draw->AddText(ImVec2(origin.x,origin.y+line),u32(Color::Muted),path.c_str());
            if(searchScroll&&i==searchSelected){ImGui::SetScrollHereY(.25f);searchScroll=false;}
            if(clicked){openSearchHit(hit);return;}
        }
    }
    bool jumpTo(const Field& field){
        if(searchField.empty()||searchField!=field.id())return false;
        if(searchScroll){ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleCenterY);searchScroll=false;}
        return true;
    }
    void jumpTo(const char* action){
        if(searchScroll&&searchAction&&!std::strcmp(action,searchAction)){ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleCenterY);searchScroll=false;}
    }
    std::vector<std::string> sections(unsigned file)const{
        std::vector<std::string> out;if(!snapshot)return out;
        for(const auto& f:snapshot->fields)if(f.file==file&&std::find(out.begin(),out.end(),f.section)==out.end())out.push_back(f.section);
        std::sort(out.begin(),out.end(),[](const std::string& a,const std::string& b){
            if(a=="default"||b=="default")return a=="default"&&b!="default";
            return regionLabel(a)<regionLabel(b);});
        return out;
    }
    void appendCelestialQuality(std::vector<const Field*>& target)const{
        for(const char* key:CelestialQualityKeys)if(const Field* f=draft.quality(key))target.push_back(f);
    }
    void refreshRegionChoices(){
        if(!snapshot||regionChoicesSnapshot==snapshot.get())return;
        regionChoices.clear();
        std::map<std::string,RegionChoice> choices;
        const auto appearance=sections(D::Celestial);
        const std::set<std::string> appearanceKeys(appearance.begin(),appearance.end());
        for(const auto& section:sections(D::ShadowRange))choices[section].shadow=section;
        for(const auto& section:appearance){
            if(section=="Appearance")continue;
            auto key=section;
            if(section.size()>7&&section.compare(section.size()-7,7,"@inside")==0){
                const auto area=section.substr(0,section.size()-7);
                if(choices.count(area)&&!appearanceKeys.count(area))key=area;
            }
            auto& choice=choices[key];
            choice.appearance=section;
            if(!mapDefaultSection(section)){
                const auto id=NorthlightSettings::liquidAreaKey(section);
                if(draft.field(NorthlightSettings::WaterFile,"RegionalSlime",id))choice.slime=id;
            }
        }
        for(auto& [key,choice]:choices){
            choice.key=key;choice.label=regionLabel(key);choice.group=regionGroup(key);
            choice.search=folded(choice.label+" "+key+" "+choice.appearance+" "+choice.group+" "+choice.slime);
            regionChoices.push_back(std::move(choice));
        }
        const auto inGroupOrder=[](const RegionChoice& a,const RegionChoice& b){
            const int left=regionGroupOrder(a.group),right=regionGroupOrder(b.group);
            return left==right?a.label<b.label:left<right;
        };
        std::sort(regionChoices.begin(),regionChoices.end(),inGroupOrder);
        if(!choices.count(region))region="default";
        regionChoicesSnapshot=snapshot.get();
    }
    const RegionChoice* selectedRegion()const{
        const auto choice=std::find_if(regionChoices.begin(),regionChoices.end(),[&](const auto& item){return item.key==region;});
        return choice==regionChoices.end()?nullptr:&*choice;
    }
    std::string appearanceRegion()const{
        const auto* choice=selectedRegion();
        return choice?choice->appearance:std::string{};
    }
    void pickRegion(bool time=false){
        ImGui::PushID("region-scope");
        const bool inlineTime=time&&ImGui::GetContentRegionAvail().x>=460*scale;
        const auto* current=selectedRegion();
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Region");ImGui::SameLine();
        ImGui::SetNextItemWidth(inlineTime?220*scale:-FLT_MIN);
        if(ImGui::BeginCombo("##region",current?current->label.c_str():"All regions (default)",ImGuiComboFlags_HeightLarge)){
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##region-search","Name, map or ID",regionFilter.data(),regionFilter.size());
            const char* maps[]={"","Azeroth:","Kalimdor:","Expansion01:","Northrend:"};
            const char* labels[]={"All maps","Eastern Kingdoms","Kalimdor","Outland","Northrend"};
            ImGui::SetNextItemWidth(-FLT_MIN);ImGui::Combo("##region-map",&regionMap,labels,IM_ARRAYSIZE(labels));
            ImGui::Separator();
            const std::string query=folded(regionFilter.data());std::string visibleGroup;bool found=false;
            for(const auto& choice:regionChoices){
                if(regionMap&&choice.key!="default"&&choice.key.rfind(maps[regionMap],0)!=0)continue;
                if(!query.empty()&&choice.search.find(query)==choice.search.npos)continue;
                if(choice.group!=visibleGroup){
                    if(found)ImGui::Spacing();
                    ImGui::TextDisabled("%s",choice.group.c_str());visibleGroup=choice.group;
                }
                const bool active=choice.key==region;
                ImGui::PushID(choice.key.c_str());
                if(ImGui::Selectable(choice.label.c_str(),active)){region=choice.key;regionFilter[0]='\0';regionMap=0;searchField.clear();}
                if(active)ImGui::SetItemDefaultFocus();found=true;
                ImGui::PopID();
            }
            if(!found)ImGui::TextDisabled("No matches.");
            ImGui::EndCombo();
        }
        if(time)for(unsigned i=0;i<TimeTitles.size();++i){
            if(i||inlineTime)ImGui::SameLine();
            if(ImGui::RadioButton(TimeTitles[i],editTime==int(i))){editTime=int(i);searchField.clear();}
        }
        ImGui::PopID();
    }
    // The curve graph: click an empty spot to add a point and drag it; the selected point has two handles that bend
    // the curve on either side; right-click removes a point, or a handle.
    void gradeEditor(NorthlightTone::TimeStop time,const std::string& appearance){
        namespace T=NorthlightTone;
        ImGui::PushID(int(time));
        const char* channels[]={"Brightness","Red","Green","Blue"};
        const Field* pointsField=draft.field(D::Celestial,appearance,T::pointsKey(time,curveChannel));
        const Field* levels[5]{};
        T::Curve saved;
        for(unsigned i=0;i<5;++i){
            levels[i]=draft.field(D::Celestial,appearance,T::gradeKey(time,curveChannel,i));
            if(!levels[i]||!pointsField){ImGui::PopID();return;}
            float lo,hi;
            *T::parameter(saved,i,lo,hi)=float(number(draft.effective(*levels[i])));
        }
        T::parseCurve(draft.effective(*pointsField),saved.points);
        // While something is dragged, the points under the mouse are the curve; the look follows a few times a second.
        const std::string shown=pointsField->id();
        if(curveShown!=shown){curveShown=shown;selectedPoint=-1;curveDrag=CurveDrag::None;}
        if(curveDrag==CurveDrag::None)curvePoints=T::pointsOf(saved);
        T::PointList& list=curvePoints;
        if(selectedPoint>=int(list.count))selectedPoint=-1;
        const T::Shape curve=T::shape(T::curveOf(list));
        const ImVec2 origin=ImGui::GetCursorScreenPos();
        // Square, so a slope on screen is the slope of the curve; the channel choice and readout sit beside it.
        const float side=std::clamp(ImGui::GetContentRegionAvail().x*.46f,190*scale,300*scale);
        const ImVec2 size(side,side);
        const float pad=6*scale,w=std::max(size.x-2*pad,1.f),h=size.y-2*pad;
        auto pos=[&](float x,float y){return ImVec2(origin.x+pad+x*w,origin.y+pad+(1-y)*h);};
        // A handle's end: sideways by its reach, up or down by the slope of that side.
        auto handleEnd=[&](int i,bool out){
            const float reach=T::handleReach(list,unsigned(i),out)*(out?1.f:-1.f);
            const auto& knot=curve.knots[unsigned(i)+1];
            return pos(list.points[i].x+reach,list.points[i].y+float(out?knot.out:knot.in)*reach);
        };
        ImGui::InvisibleButton("Curve graph",size);
        const auto mouse=ImGui::GetIO().MousePos;
        const float mouseX=(mouse.x+curveDragOffset.x-origin.x-pad)/w,mouseY=1-(mouse.y+curveDragOffset.y-origin.y-pad)/h;
        auto commit=[&]{
            const auto text=T::curveText(T::curveOf(list).points.data());
            if(text!=draft.effective(*pointsField))set(*pointsField,text);
        };
        int hoverPoint=-1;CurveDrag hoverHandle=CurveDrag::None;
        if(ImGui::IsItemHovered()&&curveDrag==CurveDrag::None){
            float best=12*scale;
            if(selectedPoint>=0)for(const bool out:{false,true}){
                const auto end=handleEnd(selectedPoint,out);
                const float dist=std::hypot(mouse.x-end.x,mouse.y-end.y);
                if(dist<best){best=dist;hoverHandle=out?CurveDrag::HandleOut:CurveDrag::HandleIn;}
            }
            if(hoverHandle==CurveDrag::None)for(int i=0;i<int(list.count);++i){
                const auto pt=pos(list.points[i].x,list.points[i].y);
                const float dist=std::hypot(mouse.x-pt.x,mouse.y-pt.y);
                if(dist<best){best=dist;hoverPoint=i;}
            }
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Right)&&!locked()){
                if(hoverHandle!=CurveDrag::None){
                    auto& point=list.points[selectedPoint];
                    (hoverHandle==CurveDrag::HandleOut?point.out:point.in)=T::StraightHandle;commit();
                }else if(hoverPoint>=0){T::removePoint(list,unsigned(hoverPoint));selectedPoint=-1;hoverPoint=-1;commit();}
            }
        }
        if(ImGui::IsItemActivated()){
            curveDragged=false;curveDragOffset={};
            if(hoverHandle!=CurveDrag::None){
                curveDrag=hoverHandle;
                const auto end=handleEnd(selectedPoint,hoverHandle==CurveDrag::HandleOut);
                curveDragOffset={end.x-mouse.x,end.y-mouse.y};
            }else if(hoverPoint>=0){
                curveDrag=CurveDrag::Point;selectedPoint=hoverPoint;
                const auto pt=pos(list.points[hoverPoint].x,list.points[hoverPoint].y);
                curveDragOffset={pt.x-mouse.x,pt.y-mouse.y};
            }else{
                const int added=T::addPoint(list,(mouse.x-origin.x-pad)/w,1-(mouse.y-origin.y-pad)/h);
                if(added>=0){selectedPoint=added;curveDrag=CurveDrag::Point;commit();}
            }
        }
        if(curveDrag!=CurveDrag::None&&ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left,1.f))curveDragged=true;
        if(curveDrag!=CurveDrag::None&&curveDragged&&selectedPoint>=0&&(ImGui::IsItemActive()||ImGui::IsItemDeactivated())){
            if(curveDrag==CurveDrag::Point)T::movePoint(list,unsigned(selectedPoint),mouseX,mouseY);
            else{
                const bool out=curveDrag==CurveDrag::HandleOut;
                const float run=mouseX-list.points[selectedPoint].x,rise=mouseY-list.points[selectedPoint].y;
                // The handle keeps to its own side of the point; Alt moves it alone, otherwise the other follows in line.
                if(out?run>.004f:run<-.004f)T::setHandle(list,unsigned(selectedPoint),out,rise/run,!ImGui::GetIO().KeyAlt);
            }
            if(ImGui::IsItemDeactivated()||ImGui::GetTime()>=nextCurvePreview){commit();nextCurvePreview=ImGui::GetTime()+.12;}
        }
        if(ImGui::IsItemDeactivated())curveDrag=CurveDrag::None;
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(27,33,39,255),3);
        for(int i=1;i<4;++i){
            draw->AddLine(pos(i*.25f,0),pos(i*.25f,1),IM_COL32(57,67,75,255));
            draw->AddLine(pos(0,i*.25f),pos(1,i*.25f),IM_COL32(57,67,75,255));
        }
        draw->AddLine(pos(0,0),pos(1,1),IM_COL32(100,109,115,255));
        const ImU32 inks[]={IM_COL32(226,223,209,255),IM_COL32(255,139,128,255),IM_COL32(130,220,165,255),IM_COL32(133,186,255,255)};
        draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
        const T::Shape drawn=T::shape(T::curveOf(list));
        ImVec2 last=pos(0,0);
        for(int i=1;i<=120;++i){
            const float x=i/120.f;
            const auto pt=pos(x,std::clamp(T::sample(drawn,x),0.f,1.f));
            draw->AddLine(last,pt,inks[curveChannel],2*scale);last=pt;
        }
        if(selectedPoint>=0)for(const bool out:{false,true}){
            const auto pt=pos(list.points[selectedPoint].x,list.points[selectedPoint].y);
            const float reach=T::handleReach(list,unsigned(selectedPoint),out)*(out?1.f:-1.f);
            const auto& knot=drawn.knots[unsigned(selectedPoint)+1];
            const auto end=pos(list.points[selectedPoint].x+reach,list.points[selectedPoint].y+float(out?knot.out:knot.in)*reach);
            const bool set=T::hasHandle(out?list.points[selectedPoint].out:list.points[selectedPoint].in);
            const bool hot=hoverHandle==(out?CurveDrag::HandleOut:CurveDrag::HandleIn)||curveDrag==(out?CurveDrag::HandleOut:CurveDrag::HandleIn);
            draw->AddLine(pt,end,IM_COL32(200,205,210,set?255:120),1.2f*scale);
            const float r=(hot?4.5f:3.5f)*scale;
            if(set)draw->AddRectFilled({end.x-r,end.y-r},{end.x+r,end.y+r},IM_COL32(226,230,234,255));
            else{draw->AddRectFilled({end.x-r,end.y-r},{end.x+r,end.y+r},IM_COL32(27,33,39,255));draw->AddRect({end.x-r,end.y-r},{end.x+r,end.y+r},IM_COL32(200,205,210,200));}
        }
        for(int i=0;i<int(list.count);++i){
            const auto pt=pos(list.points[i].x,list.points[i].y);
            if(selectedPoint==i)draw->AddCircleFilled(pt,5.5f*scale,inks[curveChannel]);
            else{draw->AddCircleFilled(pt,4.5f*scale,IM_COL32(27,33,39,255));draw->AddCircle(pt,4.5f*scale,inks[curveChannel],0,(hoverPoint==i?2.4f:1.5f)*scale);}
        }
        draw->PopClipRect();
        ImGui::SameLine();ImGui::BeginGroup();
        for(int i=0;i<4;++i)ImGui::RadioButton(channels[i],&curveChannel,i);
        ImGui::Spacing();
        if(selectedPoint>=0){
            const auto& point=list.points[selectedPoint];
            ImGui::Text("Point %d of %u: %.0f%% in, %.1f%% out (%+.1f)",selectedPoint+1,list.count,100*point.x,100*point.y,100*(point.y-point.x));
        }else if(list.count)ImGui::Text("%u points",list.count);
        else ImGui::TextUnformatted("A straight line: no points");
        W::hint("Click an empty spot to add a point, then drag it. The selected point has two handles that bend the curve; hold Alt to move one alone. Right-click removes a point, or a handle.");
        ImGui::BeginDisabled(selectedPoint<0);
        if(ImGui::Button("Remove point")&&selectedPoint>=0){T::removePoint(list,unsigned(selectedPoint));selectedPoint=-1;commit();}
        ImGui::SameLine();
        if(ImGui::Button("Remove handles")&&selectedPoint>=0){list.points[selectedPoint].in=list.points[selectedPoint].out=T::StraightHandle;commit();}
        ImGui::EndDisabled();
        if(ImGui::Button("Inherit curve")){inherit(*pointsField);for(const Field* level:levels)inherit(*level);selectedPoint=-1;}
        ImGui::Button("Hold to compare");
        if(ImGui::IsItemActive())compareRequested=true;
        ImGui::EndGroup();
        const bool revealCurve=searchScroll&&(searchField==pointsField->id()||std::any_of(std::begin(levels),std::end(levels),[&](const Field* field){return field->id()==searchField;}));
        if(revealCurve)ImGui::SetNextItemOpen(true);
        if(ImGui::TreeNode("Curve values and levels")){
            if(draft.effective(*pointsField)!="levels"){
                if(ImGui::Button("Use the level sliders"))set(*pointsField,"levels");
            }
            if(beginRows("curve-values")){
                row(*pointsField,"Curve points");
                const char* names[]={"Low input","Low bend","Shadows","Midtones","Highlights"};
                for(unsigned i=0;i<std::size(levels);++i)row(*levels[i],names[i]);
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
        if(const Field* f=draft.field(D::Celestial,appearance,NorthlightTone::vibranceKey(time)))
            loose("colour-grade",{f});
        // Colour ranges: choose one, then its three adjustments.
        ImGui::Spacing();
        ImGui::TextColored(Color::Heading,"Colour ranges");
        auto bandTitle=[](NorthlightTone::HueBand band){
            std::string title=NorthlightTone::bandName(band);
            if(!title.empty())title[0]=char(std::toupper(static_cast<unsigned char>(title[0])));
            return title;
        };
        // Red, Green and Blue are also channel names above: the ranges need their own ID scope.
        ImGui::PushID("ranges");
        int index=0;
        for(const auto band:HueBands){if(index)ImGui::SameLine();ImGui::RadioButton(bandTitle(band).c_str(),&selectedBand,index);++index;}
        ImGui::PopID();
        index=0;
        for(const auto band:HueBands){
            if(index++!=selectedBand)continue;
            const Field* values[3]{};
            bool canReset=false,any=false;
            for(unsigned i=0;i<SelectiveProperties.size();++i){
                const auto key=NorthlightTone::selectiveKey(time,band,SelectiveProperties[i]);
                values[i]=draft.field(D::Celestial,appearance,key);
                if(values[i]){any=true;canReset|=values[i]->available&&D::canInherit(*values[i]);}
            }
            if(!any)continue;
            ImGui::PushID(int(band));
            if(beginRows("selective-adjustments")){
                for(unsigned i=0;i<SelectiveProperties.size();++i)if(values[i]){
                    std::string label=NorthlightTone::propertyName(SelectiveProperties[i]);
                    std::replace(label.begin(),label.end(),'_',' ');
                    if(!label.empty())label[0]=char(std::toupper(static_cast<unsigned char>(label[0])));
                    if(SelectiveProperties[i]==NorthlightTone::SelectiveProperty::HueShift)label+=" (degrees)";
                    row(*values[i],label.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::BeginDisabled(!canReset||locked());
            if(ImGui::SmallButton("Inherit range"))for(const Field* f:values)if(f&&f->available&&D::canInherit(*f))inherit(*f);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        W::hint("Each range acts on nearby hues only, after the curve. Zero leaves it alone. These and Vibrance need filterable 16-bit colour volume support; the curves work without it.");
        ImGui::PopID();
    }
    // A regional strength as one table row. It has three states: follow the parent region, pinned off here, or tuned here.
    void regionalRow(const Field& f,const char* title){
        ImGui::PushID(f.id().c_str());
        ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
        marker(f);ImGui::AlignTextToFramePadding();
        if(!searchField.empty()&&searchField==f.id())ImGui::TextColored(Color::Accent,"%s",title);
        else if(draft.edited(f))ImGui::TextColored(Color::Edited,"%s",title);else ImGui::TextUnformatted(title);
        jumpTo(f);
        ImGui::TableSetColumnIndex(1);
        const auto value=draft.effective(f);
        const std::string mode=!draft.overrides(f)?"Inherit":number(value)==0?"Off":"Tuned";
        ImGui::BeginDisabled(locked());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*0.36f);
        if(ImGui::BeginCombo("##mode",mode.c_str())){
            for(const char* choice:{"Inherit","Off","Tuned"})if(ImGui::Selectable(choice,mode==choice)){
                if(std::string(choice)=="Inherit")inherit(f);
                else{draft.pin(f,std::string(choice)=="Off"?"0":number(value)>0?value:"0.5");edited(f);}
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if(mode=="Tuned")control(f);
        else{
            ImGui::AlignTextToFramePadding();
            if(mode=="Inherit")ImGui::TextDisabled("follows parent: %s",value.c_str());else ImGui::TextDisabled("off in this region");
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    void celestialTab(const std::string& appearance){
        std::vector<const Field*> controls;
        appendCelestialQuality(controls);
        loose("celestial-controls",controls);
        ImGui::TextColored(Color::Muted,"Default: game placement and size. Forever: imported orbit and horizon size curves.");
        ImGui::Spacing();
        if(appearance.empty()){W::hint("This region uses the default appearance profile.");return;}
        std::map<std::string,const Field*> byKey;for(const Field* f:fields(D::Celestial,&appearance))byKey[f->key]=f;
        std::map<std::string,bool> used;
        for(const auto& key:NorthlightSettings::AppearanceKeys)used[key]=true;
        ImGui::PushID(appearance.c_str());
        for(const auto& g:CelestialGroups)group(g,byKey,used);
        std::vector<const Field*> rest;for(auto& k:byKey)if(!used[k.first])rest.push_back(k.second);
        loose("rest",rest);
        ImGui::PopID();
    }
    void shadowRangeTab(){
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(Color::Muted,"Terrain shadow reach in yards. Save, then restart the game to apply changes.");
        ImGui::PopTextWrapPos();
        if(const Field* base=draft.field(D::ShadowRange,"default","terrain_radius")){
            if(beginRows("default-range")){row(*base,"Default reach (yards)");ImGui::EndTable();}
        }
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(Color::Muted,"Zones without an override follow the default. Right-click a zone to restore it.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        const auto* scope=selectedRegion();
        if(!scope||scope->shadow.empty())W::hint("Showing the shared shadow range for this region.");
        else if(scope->shadow!="default")if(const auto* field=draft.field(D::ShadowRange,scope->shadow,"terrain_radius")){
            if(beginRows("zone-range")){row(*field,scope->label.c_str());ImGui::EndTable();}
        }
    }

    // ---- Footer ----
    std::size_t restartPending()const{
        std::size_t n=0;if(!snapshot)return n;
        for(const auto& f:snapshot->fields)if(draft.restartPending(f))++n;
        return n;
    }
    void statusLine(){
        using S=NorthlightSettings::Status::State;
        if(!service){ImGui::TextColored(Color::Bad,"Settings are unavailable (see northlight-renderer.log).");return;}
        if(!note.empty()){ImGui::TextColored(Color::Bad,"%s",note.c_str());return;}
        if(!snapshot){ImGui::TextColored(Color::Muted,status.state==S::Error?"%s":"Loading settings...",status.message.c_str());return;}
        if(saving||status.state==S::Busy)ImGui::TextColored(Color::Edited,"%s",saving?"Saving...":"Working...");
        else if(status.state==S::Error)ImGui::TextColored(Color::Bad,"%s",status.message.empty()?"The last request failed.":status.message.c_str());
        else if(!draft.empty())ImGui::TextColored(Color::Edited,"%u unsaved change%s%s",unsigned(draft.size()),draft.size()==1?"":"s",previewing?" (previewing)":"");
        else if(status.state==S::Saved)ImGui::TextColored(Color::Good,"%s",status.message.empty()?"Saved.":status.message.c_str());
        else ImGui::TextColored(Color::Muted,"No unsaved changes.");
        if(status.restartRequired||restartPending()){ImGui::SameLine();ImGui::TextColored(Color::Restart,"  R: restart required");}
    }
    void legend(){
        const float h=ImGui::GetTextLineHeight(),r=std::max(2.5f,h*0.2f);
        auto dot=[&](const ImVec4& c,bool filled,const char* label){
            const ImVec2 p=ImGui::GetCursorScreenPos();const ImVec2 at(p.x+r+1,p.y+ImGui::GetFrameHeight()*0.5f);
            if(filled)ImGui::GetWindowDrawList()->AddCircleFilled(at,r,u32(c));else ImGui::GetWindowDrawList()->AddCircle(at,r,u32(c),0,std::max(1.f,scale));
            ImGui::Dummy(ImVec2(2*r+2,ImGui::GetFrameHeight()));ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::AlignTextToFramePadding();ImGui::TextColored(Color::Muted,"%s",label);ImGui::SameLine();
        };
        const auto& st=ImGui::GetStyle();
        const float width=4*(2*r+2+st.ItemInnerSpacing.x+st.ItemSpacing.x)+ImGui::CalcTextSize("set").x+ImGui::CalcTextSize("inherited").x+ImGui::CalcTextSize("unsaved").x+ImGui::CalcTextSize("R restart").x;
        alignRight(width);
        dot(Color::Accent,true,"set");dot(Color::Muted,false,"inherited");dot(Color::Edited,true,"unsaved");
        ImGui::TextColored(Color::Restart,"R");ImGui::SameLine(0,st.ItemInnerSpacing.x);ImGui::TextColored(Color::Muted,"restart");
    }
    void footer(){
        ImGui::Separator();
        statusLine();
        // Only while the service is idle: a queued preview would otherwise finish after the commit and hide its result.
        const bool idle=status.state!=NorthlightSettings::Status::State::Busy;
        ImGui::BeginDisabled(draft.empty()||locked()||!service||!idle);
        const bool dirty=!draft.empty()&&!locked()&&idle;
        if(dirty){ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.20f,0.42f,0.70f,1));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0.26f,0.50f,0.80f,1));}
        if(ImGui::Button("Save"))save();
        if(dirty)ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Write the changes to the INI files. Live settings stay applied; R settings apply after a restart.");
        ImGui::SameLine();
        ImGui::BeginDisabled((draft.empty()&&!previewing)||locked());
        if(ImGui::Button("Revert"))revert();
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Discard the unsaved changes and return to the saved settings.");
        ImGui::SameLine();
        ImGui::BeginDisabled(locked()||page==Page::Performance);
        if(ImGui::Button("Defaults..."))confirm=Confirm::Factory;
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",factoryScope().c_str());
        ImGui::SameLine();legend();
    }
    std::string factoryScope()const{
        if(page==Page::Celestial)return "Restore global Sun & moon settings and the factory profile of the selected region ("+regionLabel(region)+").";
        if(page==Page::Regional||page==Page::Grading)return "Restore the settings on this page for "+regionLabel(region)+".";
        if(page==Page::ShadowRange)return "Restore the factory shadow reach of every region.";
        return std::string("Restore the factory values of every setting on the ")+Pages[unsigned(page)].title+" page.";
    }
    void factoryDefaults(){
        auto targets=pageFields(page);
        if(page==Page::Celestial||page==Page::Regional||page==Page::Grading){
            const auto appearance=appearanceRegion();
            targets.erase(std::remove_if(targets.begin(),targets.end(),[&](const Field* field){
                return field->file==D::Celestial&&field->section!=appearance;
            }),targets.end());
        }
        for(const Field* f:targets)if(f->available){draft.factory(*f);live.erase(f->id());edited(*f);}
    }
    // The old menu's preset choice: quality overrides back to inherit, optionally Sun & moon and Shadow range to factory.
    void choosePreset(){
        const Field* preset=draft.quality("Preset");if(!preset)return;
        for(const Field* f:fields(D::Quality))if(f->key!="Preset"&&D::canInherit(*f)){draft.inherit(*f);live.erase(f->id());edited(*f);}
        if(presetCelestial)for(const Field* f:pageFields(Page::Celestial))if(f->file==D::Celestial&&f->available){draft.factory(*f);live.erase(f->id());edited(*f);}
        if(presetRange)for(const Field* f:fields(D::ShadowRange))if(f->available){draft.factory(*f);live.erase(f->id());edited(*f);}
        set(*preset,presetChoice);
    }
    void modals(){
        if(confirm==Confirm::Preset)ImGui::OpenPopup("Choose preset?");
        if(confirm==Confirm::Factory)ImGui::OpenPopup("Restore defaults?");
        if(appearancePresetModal){ImGui::OpenPopup("Save as preset");appearancePresetModal=false;}
        if(appearancePresetDeleteModal){ImGui::OpenPopup("Delete preset?");appearancePresetDeleteModal=false;}
        confirm=Confirm::None;
        const ImVec2 centre=ImGui::GetMainViewport()->GetCenter();
        auto ask=[&](const char* title,const char* body,const char* yes,auto&& accept){
            ImGui::SetNextWindowPos(centre,ImGuiCond_Appearing,ImVec2(0.5f,0.5f));
            if(!ImGui::BeginPopupModal(title,nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings))return;
            ImGui::PushTextWrapPos(ImGui::GetFontSize()*26);ImGui::TextWrapped("%s",body);ImGui::PopTextWrapPos();
            ImGui::TextColored(Color::Muted,"Nothing is written until you press Save.");
            ImGui::Spacing();
            if(ImGui::Button(yes)){accept();ImGui::CloseCurrentPopup();}
            ImGui::SameLine();
            if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        };
        ImGui::SetNextWindowPos(centre,ImGuiCond_Appearing,ImVec2(0.5f,0.5f));
        if(ImGui::BeginPopupModal("Choose preset?",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings)){
            ImGui::PushTextWrapPos(ImGui::GetFontSize()*26);
            ImGui::TextWrapped("Preset: %s. Every quality setting you have changed returns to the preset's value.",presetChoice.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Checkbox("Also reset Sun & moon to factory values",&presetCelestial);
            ImGui::Checkbox("Also reset Shadow range to factory values",&presetRange);
            ImGui::TextColored(Color::Muted,"Renderer & HDR settings are kept. Nothing is written until you press Save.");
            ImGui::Spacing();
            if(ImGui::Button("Choose preset")){choosePreset();ImGui::CloseCurrentPopup();}
            ImGui::SameLine();
            if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        const std::string scope=factoryScope();
        ask("Restore defaults?",scope.c_str(),"Restore",[&]{factoryDefaults();});
        ImGui::SetNextWindowPos(centre,ImGuiCond_Appearing,ImVec2(0.5f,0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(440*scale,0),ImVec2(440*scale,FLT_MAX));
        if(ImGui::BeginPopupModal("Save as preset",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings)){
            ImGui::TextWrapped("Save the whole-world look as you see it now, with every regional difference and your unsaved edits, under a name.");
            if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(280*scale);ImGui::InputText("Name",appearancePresetName,sizeof appearancePresetName);
            std::string error;bool valid=NorthlightAppearancePresets::validName(appearancePresetName,error);
            const bool shipped=valid&&NorthlightAppearancePresets::packaged(appearancePresetName);
            const auto replaces=valid?ownAppearancePreset(appearancePresetName):std::string();
            if(shipped){valid=false;ImGui::TextColored(Color::Bad,"That look comes with Northlight. Choose another name.");}
            else if(!replaces.empty())ImGui::TextColored(Color::Edited,"This replaces your preset %s.",replaces.c_str());
            else ImGui::TextWrapped("%s",NorthlightAppearancePresets::NameRuleText);
            ImGui::TextColored(Color::Muted,"The file is written at once; Revert does not remove it.");
            ImGui::Spacing();
            ImGui::BeginDisabled(!valid||locked()||status.state==NorthlightSettings::Status::State::Busy);
            if(ImGui::Button(replaces.empty()?"Save preset":"Replace preset")){saveAppearancePreset();if(savingAppearancePreset)ImGui::CloseCurrentPopup();}
            ImGui::EndDisabled();ImGui::SameLine();
            if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::SetNextWindowPos(centre,ImGuiCond_Appearing,ImVec2(0.5f,0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(440*scale,0),ImVec2(440*scale,FLT_MAX));
        if(ImGui::BeginPopupModal("Delete preset?",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings)){
            ImGui::TextWrapped("Delete your preset %s? Its file is removed from appearance-presets at once and Revert does not bring it back. If it is the look in use, the world returns to Default.",appearancePresetToDelete.c_str());
            ImGui::Spacing();
            ImGui::BeginDisabled(locked()||status.state==NorthlightSettings::Status::State::Busy);
            if(ImGui::Button("Delete preset")){deleteAppearancePreset();if(savingAppearancePreset)ImGui::CloseCurrentPopup();}
            ImGui::EndDisabled();ImGui::SameLine();
            if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
};

SettingsPanel::SettingsPanel(std::shared_ptr<NorthlightSettings::Service> service,std::string hotkey,CalibrationDraw calibration):s(std::make_unique<State>()){s->service=std::move(service);s->hotkey=std::move(hotkey);s->calibrationDraw=std::move(calibration);}
SettingsPanel::~SettingsPanel()=default;
void SettingsPanel::opened(){s->wantSnapshot=true;}
void SettingsPanel::poll(){if(s->comparing){s->comparing=false;s->wantPreview=true;}s->poll();}
bool SettingsPanel::pending()const{return s->service&&(s->wantSnapshot||s->wantPreview||s->wantClear||s->saving||s->awaitingSnapshot||s->savingAppearancePreset);}
bool SettingsPanel::unsaved()const{return !s->draft.empty();}
void SettingsPanel::performanceDevice(IDirect3DDevice9* device){
    NorthlightPerformance::Adapter info;info.owner=reinterpret_cast<uintptr_t>(device);D3DDEVICE_CREATION_PARAMETERS cp{};
    if(device&&SUCCEEDED(device->GetCreationParameters(&cp))){info.window=cp.hFocusWindow;IDirect3D9* api=nullptr;
        if(SUCCEEDED(device->GetDirect3D(&api))&&api){D3DADAPTER_IDENTIFIER9 id{};if(SUCCEEDED(api->GetAdapterIdentifier(cp.AdapterOrdinal,0,&id))){info.vendor=id.VendorId;info.device=id.DeviceId;std::snprintf(info.name,sizeof info.name,"%s",id.Description);}
            IDirect3D9Ex* ex=nullptr;if(SUCCEEDED(api->QueryInterface(__uuidof(IDirect3D9Ex),reinterpret_cast<void**>(&ex)))&&ex){LUID luid{};if(SUCCEEDED(ex->GetAdapterLUID(cp.AdapterOrdinal,&luid)))info.luid=uint64_t(uint32_t(luid.HighPart))<<32|luid.LowPart;ex->Release();}api->Release();}
        IDirect3DSwapChain9* chain=nullptr;if(SUCCEEDED(device->GetSwapChain(0,&chain))&&chain){D3DPRESENT_PARAMETERS pp{};if(SUCCEEDED(chain->GetPresentParameters(&pp))){info.interval=pp.PresentationInterval;if(pp.hDeviceWindow)info.window=pp.hDeviceWindow;}chain->Release();}}
    if(s->monitorAdapter.owner==info.owner&&s->monitorAdapter.luid==info.luid&&s->monitorAdapter.vendor==info.vendor&&s->monitorAdapter.device==info.device&&s->monitorAdapter.window==info.window){s->monitorAdapter=info;return;}
    s->hardwareSampler.reset();s->monitorAdapter=info;s->monitorReadAt=0;s->hardware={};s->timing={};
}
bool SettingsPanel::hudEnabled()const{return s->monitoringHud;}
void SettingsPanel::drawHud(float scale){
    const auto* view=ImGui::GetMainViewport();s->scale=scale;
    ImGui::SetNextWindowPos(ImVec2(view->WorkPos.x+12*scale,view->WorkPos.y+12*scale),ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(350*scale,0),ImGuiCond_Always);ImGui::SetNextWindowBgAlpha(.78f);
    const auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_AlwaysAutoResize;
    if(ImGui::Begin("##NorthlightPerformanceHUD",nullptr,flags)){ImGui::SetWindowFontScale(.82f);s->monitorContents(true);}ImGui::End();
}
void SettingsPanel::notice(float scale){
    const ImGuiViewport* view=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(view->WorkPos.x+12*scale,view->WorkPos.y+view->WorkSize.y-12*scale),ImGuiCond_Always,ImVec2(0,1));
    ImGui::SetNextWindowBgAlpha(0.55f);
    const ImGuiWindowFlags flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|
        ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav;
    if(ImGui::Begin("##NorthlightUnsaved",nullptr,flags))
        ImGui::TextColored(Color::Edited,"Northlight: unsaved settings%s (%s)",s->previewing?" preview":"",s->hotkey.c_str());
    ImGui::End();
}

bool SettingsPanel::draw(float scale){
    s->calibrationPatches.clear();
    State& st=*s;st.scale=scale;
    st.poll();
    st.compareRequested=false;
    bool open=true;
    ImGui::SetNextWindowPos(ImVec2(48*scale,96*scale),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760*scale,680*scale),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(560*scale,360*scale),ImVec2(FLT_MAX,FLT_MAX));
    char title[96];std::snprintf(title,sizeof title,"Northlight settings | build %.12s###NorthlightSettings",NORTHLIGHT_SOURCE_ID);
    if(!ImGui::Begin(title,&open,ImGuiWindowFlags_NoCollapse)){
        if(st.comparing){st.comparing=false;st.wantPreview=true;}
        ImGui::End();
        return open;
    }
    const auto& style=ImGui::GetStyle();
    const float footer=ImGui::GetTextLineHeightWithSpacing()+ImGui::GetFrameHeightWithSpacing()+style.ItemSpacing.y*2+style.SeparatorTextBorderSize+2;
    if(st.searchHeader()){
        if(ImGui::BeginChild("search-results",ImVec2(0,-footer)))st.drawSearch();
        ImGui::EndChild();
    }else{
        if(st.snapshot&&(st.page==Page::Water||st.page==Page::ShadowRange)){st.pickRegion();ImGui::Separator();}
        if(ImGui::BeginChild("nav",ImVec2(190*scale,-footer),ImGuiChildFlags_Borders))st.sidebar();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::PushID(int(st.page));
        if(ImGui::BeginChild("page",ImVec2(0,-footer))){
            if(!st.snapshot)ImGui::TextDisabled(st.service?"Loading settings...":"The settings service is not running.");
            else{st.subpages();st.drawPage();}
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
    st.footer();
    st.modals();
    ImGui::End();
    if(st.comparing!=st.compareRequested){st.comparing=st.compareRequested;st.wantPreview=true;}
    return open;
}
} // namespace NorthlightOverlay
