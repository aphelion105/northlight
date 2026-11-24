#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Production fog/material screen passes without a geometry publication, without the game."""
from pathlib import Path
import hashlib,importlib.util,json,os,shutil,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
repo=fp.REPO
out=fp.output_dir()/'world-screen-gpu';out.mkdir(parents=True,exist_ok=False);(out/'settings').mkdir()
spec=importlib.util.spec_from_file_location('screen_guard',repo/'tests/validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
backend=Path(os.environ['NORTHLIGHT_TEST_D3D9']) if os.environ.get('NORTHLIGHT_TEST_D3D9') else fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'
env={k:v for k,v in os.environ.items() if not k.startswith(('NORTHLIGHT_','DXVK_','TRACE_','RETRACE_','APITRACE_'))}
baseline=os.environ.get('NORTHLIGHT_PIPELINE_BASELINE')
if baseline:env['NORTHLIGHT_PIPELINE_BASELINE_RUN']='1'
for key,name in [('TEMP','temp'),('TMP','temp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global'),('DXVK_SHADER_CACHE_PATH','shader-cache'),('DXVK_STATE_CACHE_PATH','state-cache'),('__GL_SHADER_DISK_CACHE_PATH','driver-cache')]:
    (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
(out/'dxvk.conf').write_bytes(b'dxvk.allowFse = False\nd3d9.presentInterval = 0\n')
env.update(DXVK_CONFIG='d3d9.customVendorId = 1002',DISABLE_VULKAN_OBS_CAPTURE='1',DXVK_CONFIG_FILE=str(out/'dxvk.conf'),DXVK_LOG_PATH=str(out))
# Explicit per-test RDP adapter overrides; never change client/global settings.
if os.environ.get('DXVK_CONFIG'):env['DXVK_CONFIG']+='; '+os.environ['DXVK_CONFIG']
if os.environ.get('DXVK_FILTER_DEVICE_NAME'):env['DXVK_FILTER_DEVICE_NAME']=os.environ['DXVK_FILTER_DEVICE_NAME']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
inputs={str(p):sha(p) for p in [fp.TESTS/'world_screen_effects_gpu.cpp',fp.TESTS/'capture_file_reader.h',fp.src('diagnostic_files.h'),*(fp.src(s) for s in sources),fp.src('hdr_compiled_shaders.h'),fp.src('world_renderer.h'),fp.src('world_fog_screen.h'),fp.src('world_regional_fog_gpu.h'),fp.src('world_fog_field.h'),fp.src('world_light_history.h'),fp.src('world_normals_pass.h'),fp.src('world_actor_shadows.h'),fp.src('world_directional_targets.h'),fp.src('world_directional_pass.inl'),fp.src('world_soft_shadows.inl'),fp.src('world_screen_lighting.inl'),fp.src('world_screen_lighting.h'),fp.src('world_local_lighting.inl'),fp.src('world_local_lighting.h'),fp.src('world_atmospheric_fog.inl'),fp.src('world_frame_constants.inl'),fp.src('world_composite.h'),fp.src('saved_state.h'),fp.src('world_screen_resources.inl'),fp.src('renderer.cpp'),fp.src('settings_service.h'),backend]}
imported_fog=fp.src('world_forever_fog_shaders.h')
inputs[str(imported_fog)]=sha(imported_fog)
directional_visibility=fp.src('world_directional_visibility.h')
inputs[str(directional_visibility)]=sha(directional_visibility)
for name in ('world_flash_shadows.h','world_flash_rendering.inl','world_atmosphere_commands.h','world_moon_floor_fog.h','material_pixel_audit.h'):
    flash_source=fp.src(name)
    inputs[str(flash_source)]=sha(flash_source)
for name in ('screen_quad.h','screen_quad.inl','world_local_scissor.h','world_probe_atlas.h','world_shadow_programs.h','world_shadow_transition.h','world_shadow_transition.inl',
             'world_near_environment.inl','point_shadow_face_pass.h','world_mesh_pool.h','world_render_target.h','gpu_scene.h'):
    source=fp.src(name)
    inputs[str(source)]=sha(source)
reference_header=os.environ.get('NORTHLIGHT_WORLD_RENDERER_REFERENCE')
reference_files=[]
if reference_header:
    header=Path(reference_header)
    reference_files=[header]
    for name in ('world_screen_resources.inl','world_geometry_resources.inl','world_near_environment.inl','world_shadow_transition.inl','world_flash_rendering.inl','world_flash_shadows.h','world_directional_pass.inl','world_soft_shadows.inl','world_directional_visibility.h','world_screen_lighting.inl','world_light_history.h','world_local_lighting.inl','world_local_lighting.h','world_atmospheric_fog.inl','world_fog_field.h','world_frame_constants.inl','world_composite.h','world_forever_fog_shaders.h'):
        resource_header=header.with_name(name)
        if resource_header.is_file():reference_files.append(resource_header)
    inputs.update({str(path):sha(path) for path in reference_files})
report={'pass':False,'inputs':inputs,'commands':[],'game_launched':False,'actual_world_renderer':True,'private_render_target':True,'geometry_available':False}
report['environment']={key:env.get(key) for key in ('DXVK_CONFIG','DXVK_FILTER_DEVICE_NAME')}
def run(label,command,extra=None):
    guard.processes();p=subprocess.run(list(map(str,command)),cwd=out,env={**env,**(extra or {})},capture_output=True,text=True,timeout=240)
    (out/(label+'.log')).write_text(p.stdout+p.stderr);report['commands'].append({'label':label,'exit_code':p.returncode});print(label,p.returncode,(p.stdout+p.stderr)[-6000:],flush=True);assert p.returncode==0,label
try:
    exe=out/'world-screen-gpu.exe'
    sourceRoot=Path(baseline) if baseline else fp.REPO
    if baseline:
        inputs.update({str(p):sha(p) for p in (sourceRoot/'src').rglob('*') if p.is_file()})
    includes=[str(sourceRoot/Path(f).relative_to(fp.REPO)) if f!='-I' else f for f in fp.test_include_flags()]
    if reference_header:
        overlay=out/'reference-header';overlay.mkdir()
        shutil.copyfile(reference_files[0],overlay/'world_renderer.h')
        for companion in reference_files[1:]:shutil.copyfile(companion,overlay/companion.name)
        includes=['-I',str(overlay),*includes]
    selected_header=Path(reference_header) if reference_header else sourceRoot/fp.src('world_renderer.h').relative_to(fp.REPO)
    selected_text=selected_header.read_text()
    solar_material='float sunSurfaceSpecularGain(' in selected_text
    if solar_material:includes+=['-DNORTHLIGHT_SOLAR_MATERIAL_TEST=1']
    probe_atlas_owner='NorthlightWorldProbeAtlas probeAtlas' in selected_text
    if probe_atlas_owner:includes+=['-DNORTHLIGHT_PROBE_ATLAS_OWNER_TEST=1']
    replay_probe_pass='#include "world_replay_probe_pass.h"' in selected_text
    if replay_probe_pass:
        includes+=['-DNORTHLIGHT_REPLAY_PROBE_PASS_TEST=1']
        probe_source=sourceRoot/'src/world/world_replay_probe_pass.h'
        inputs[str(probe_source)]=sha(probe_source)
    screen_fallbacks='#include "world_screen_fallbacks.h"' in selected_text
    if screen_fallbacks:
        includes+=['-DNORTHLIGHT_SCREEN_FALLBACKS_TEST=1']
        for name in ('src/world/world_screen_fallbacks.h','src/sky/dither_noise.h','src/world/world_geometry_resources.inl'):
            path=sourceRoot/name
            inputs[str(path)]=sha(path)
    if 'NorthlightWorldShadowPrograms shadowPrograms' in selected_text:includes+=['-DNORTHLIGHT_SHADOW_PROGRAM_OWNER_TEST=1']
    regional_fog_gpu='NorthlightRegionalFogGpu regionalFogGpu' in selected_text
    if regional_fog_gpu:includes+=['-DNORTHLIGHT_REGIONAL_FOG_GPU_TEST=1']
    screen_input='composite.prepareScreenInput(' in selected_text
    if screen_input:includes+=['-DNORTHLIGHT_WORLD_SCREEN_INPUT_TEST=1']
    resource_owner='composite.prepareScratch(d,' in selected_text
    if resource_owner:includes+=['-DNORTHLIGHT_WORLD_RENDER_TARGET_OWNER_TEST=1']
    material_owner='pendingMesh->materialUpload.texture()' in selected_text
    if material_owner:
        includes+=['-DNORTHLIGHT_WORLD_MATERIAL_UPLOAD_TEST=1']
        material_upload=selected_header.with_name('world_mesh_material_upload.h')
        if not material_upload.is_file():material_upload=sourceRoot/'src/world/world_mesh_material_upload.h'
        inputs[str(material_upload)]=sha(material_upload)
    if '#include "actor_texture_read.h"' in selected_text:
        for name in ('src/replay/actor_texture_read.h','src/world/world_mesh_publication.inl'):
            path=sourceRoot/name
            inputs[str(path)]=sha(path)
    shared_frame='composite.clearDiagnostic(' in selected_text
    if shared_frame:includes+=['-DNORTHLIGHT_WORLD_SHARED_FRAME_TEST=1']
    frame_setup='screenLighting.prepareFrame(' in selected_text
    if frame_setup:includes+=['-DNORTHLIGHT_WORLD_FRAME_SETUP_TEST=1']
    celestial_source='screenLighting.bindSource(' in selected_text
    if celestial_source:includes+=['-DNORTHLIGHT_WORLD_CELESTIAL_SOURCE_TEST=1']
    if 'NorthlightWorldFogField regionalFog' not in selected_text:
        includes+=['-DNORTHLIGHT_LEGACY_FOG_FIELD_FIXTURE=1']
    visibility_owner='#include "world_directional_visibility.h"' in selected_text
    local_light_owner='NorthlightWorldLocalLighting localLighting' in selected_text
    local_surface=False
    if not local_light_owner:
        includes+=['-DNORTHLIGHT_LEGACY_LAMP_OWNER_FIXTURE=1']
    else:
        local_header=selected_header.with_name('world_local_lighting.h')
        if not local_header.is_file():
            local_header=sourceRoot/fp.src('world_local_lighting.h').relative_to(fp.REPO)
        local_surface='bool drawSurface(' in local_header.read_text()
        if not local_surface:includes+=['-DNORTHLIGHT_LEGACY_LOCAL_SURFACE_FIXTURE=1']
        screen_resources=selected_header.with_name('world_screen_resources.inl')
        if not screen_resources.is_file():
            screen_resources=sourceRoot/fp.src('world_screen_resources.inl').relative_to(fp.REPO)
        lazy_removal='history.createLightingShaders(pixel,needs.shadows)' in screen_resources.read_text()
        includes+=[f'-DNORTHLIGHT_LAZY_REMOVAL_TEST={int(lazy_removal)}']
    imported_fog_owner='NorthlightWorldForeverFogShaders foreverFog' in selected_text
    if not imported_fog_owner:includes+=['-DNORTHLIGHT_LEGACY_FOREVER_FOG_FIXTURE=1']
    if 'lightingUnshadowedGIPS' in selected_text or 'NorthlightWorldScreenLighting screenLighting' in selected_text:
        includes+=['-DNORTHLIGHT_GI_MERGE_TEST=1']
    if 'enclosingState->coversDevice(d)' in selected_text:
        includes+=['-DNORTHLIGHT_WORLD_STATE_SCOPE_TEST=1']
    if 'sharedResolveRequested' in selected_text or 'resolveDirectionalVisibility(' in selected_text:
        includes+=['-DNORTHLIGHT_SHARED_SHADOW_TEST=1']
        if not visibility_owner:
            includes+=['-DNORTHLIGHT_LEGACY_DIRECTIONAL_VISIBILITY_FIXTURE=1']
    if any(symbol in selected_header.read_text() for symbol in ('distancePS', 'NorthlightWorldLightHistory history')):includes+=['-DNORTHLIGHT_FOG_DISTANCE_TEST=1']
    if 'NorthlightFogVolume fogVolume' in selected_header.read_text():includes+=['-DNORTHLIGHT_FOG_VOLUME_TEST=1']
    inspection_views='bool renderNormalsView(' in selected_text
    if inspection_views:includes+=['-DNORTHLIGHT_INSPECTION_VIEW_GPU_TEST=1']
    run('compile',[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*includes,'-I'+str(fp.TESTS),fp.TESTS/'world_screen_effects_gpu.cpp',*(sourceRoot/fp.src(s).relative_to(fp.REPO) for s in sources),'-luser32','-luuid','-o',exe])
    for samples in [0,2]:
        settings=out/('settings-msaa'+str(samples));settings.mkdir()
        run('gpu-msaa'+str(samples),[exe,backend,settings],{'NORTHLIGHT_TEST_MSAA':str(samples)})
        log=(out/f'gpu-msaa{samples}.log').read_text()
        if inspection_views:assert 'WORLD_INSPECTION_GPU_PASS ' in log
        if solar_material:assert 'WORLD_SOLAR_MATERIAL_PASS ' in log
        if resource_owner:assert 'WORLD_ALLOCATION_OWNERS_GPU_PASS ' in log
        if material_owner:assert 'WORLD_MATERIAL_UPLOAD_GPU_PASS ' in log
        if probe_atlas_owner:
            assert 'WORLD_PROBE_ATLAS_OWNER_PASS ' in log
        if replay_probe_pass:
            assert 'WORLD_REPLAY_PROBE_PASS ' in log
        if screen_fallbacks:
            assert 'WORLD_SCREEN_FALLBACKS_PASS ' in log
        if imported_fog_owner:
            assert 'WORLD_FOREVER_FOG_SHADERS_PASS ' in log
        if regional_fog_gpu:
            assert 'WORLD_REGIONAL_FOG_SHADERS_PASS ' in log
        if local_light_owner:
            assert 'WORLD_REMOVAL_DEMAND_PASS ' in log
        if local_surface:
            assert 'WORLD_LOCAL_SURFACE_PASS ' in log
        if screen_input:
            assert 'WORLD_SCREEN_INPUT_PASS ' in log
        if shared_frame:
            assert 'WORLD_SHARED_FRAME_PASS ' in log
        if frame_setup:
            assert 'WORLD_FRAME_SETUP_PASS ' in log
        if celestial_source:
            assert 'WORLD_CELESTIAL_SOURCE_PASS ' in log
        if visibility_owner:
            assert 'WORLD_VISIBILITY_TARGET_PASS ' in log
            assert 'WORLD_VISIBILITY_DEVICE_RESET_PASS ' in log
    report['pass']=True
finally:
    guard.processes();report['inputs_unchanged']=all(sha(Path(p))==v for p,v in inputs.items());report['pass']&=report['inputs_unchanged'];(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['pass']
