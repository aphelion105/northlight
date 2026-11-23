#!/usr/bin/env python3
# northlight-test:
"""Keep the curated pipeline slice tied to the current bytecode and code paths."""
import copy
import json
from pathlib import Path
from shutil import copyfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import audit_shaders
import northlight_paths as fp
import shader_pipeline_map as pipeline


def rejected(inventory, key, mutate):
    registry = json.loads(pipeline.REGISTRY.read_text(encoding='utf-8'))
    mutate(registry)
    try:
        pipeline.build(inventory, registry)
    except ValueError as error:
        assert key in str(error), error
    else:
        raise AssertionError(f'Accepted invalid map: {key}')


def rain_bindings():
    folder = fp.output_dir() / 'rain-bindings'
    folder.mkdir(exist_ok=True)
    path = folder / 'capture-42-frame-parity-atmosphere.jsonl'
    copyfile(fp.COMPILED / 'RainScrub.bin', folder / 'capture-42-frame-parity-atmosphere-0.bin')
    begin = {'event': 'begin', 'renderer_frame': 42, 'scope': 'atmosphere-pass-bindings'}
    end = {'event': 'end', 'render_completed': True, 'export_complete': True}
    frames = [
        begin,
        {'event': 'binding-pass', 'entry': 'RainScrub', 'pass': 1, 'draw_HRESULT': '00000000'},
        {'event': 'binding-target', 'pass': 1, 'slot': 0, 'width': 37, 'height': 23, 'format': 21, 'samples': 0},
        {'event': 'shader-binding', 'pass': 1, 'shader': 0},
        *[{'event': 'binding-sampler', 'pass': 1, 'slot': slot, 'present': True, 'width': 37, 'height': 23,
           'format': 1515474505, 'minFilter': 1, 'magFilter': 1} for slot in (0, 1)],
        end,
    ]
    def save(rows):
        path.write_text(''.join(json.dumps(row) + '\n' for row in rows), encoding='utf-8')
    save(frames)
    assert pipeline.rain_scrub_bindings(folder, 42, 37, 23)['state'] == 'executed'
    frames[2]['samples'] = 2
    save(frames)
    try:
        pipeline.rain_scrub_bindings(folder, 42, 37, 23)
    except ValueError as error:
        assert 'RainScrub target' in str(error)
    else:
        raise AssertionError('Accepted multisampled resolved rain mask')
    save([begin, {'event': 'binding-skip', 'entry': 'RainScrub', 'reason': 'resolve-failed'}, end])
    assert pipeline.rain_scrub_bindings(folder, 42, 37, 23)['state'] == 'skipped'
    assert pipeline.water_resolve_bindings(folder, 42, 37, 23) is None
    copyfile(fp.COMPILED / 'WaterMergeDistanceMS.bin', folder / 'capture-42-frame-parity-atmosphere-1.bin')
    water = [
        begin,
        {'event': 'binding-pass', 'entry': 'WaterMergeDistanceMS', 'pass': 2, 'draw_HRESULT': '00000000'},
        {'event': 'binding-target', 'pass': 2, 'slot': 0, 'width': 37, 'height': 23, 'format': 115, 'samples': 0},
        {'event': 'shader-binding', 'pass': 2, 'shader': 1},
        {'event': 'binding-resolve-source', 'pass': 2, 'width': 37, 'height': 23, 'format': 114, 'samples': 2, 'sample_quality': 0},
        *[{'event': 'binding-sampler', 'pass': 2, 'slot': slot, 'present': True, 'width': 37, 'height': 23,
           'format': fmt, 'minFilter': 1, 'magFilter': 1} for slot, fmt in enumerate((50, 114))],
        end,
    ]
    save(water)
    assert pipeline.water_resolve_bindings(folder, 42, 37, 23)['state'] == 'executed'
    water[4]['samples'] = 0
    save(water)
    try:
        pipeline.water_resolve_bindings(folder, 42, 37, 23)
    except ValueError as error:
        assert 'source MSAA' in str(error)
    else:
        raise AssertionError('Accepted non-MSAA input to WaterMergeDistanceMS')


def main():
    inventory = audit_shaders.inventory()
    report = pipeline.build(inventory)
    world = pipeline.query(report, 'WorldCompositeHdr')
    assert world['direction'] == 'upstream'
    assert {'world-composite', 'scene-inputs', 'world-normals', 'world-lighting', 'temporal-light', 'model-normals',
            'contact-ao', 'contact-ao-blur', 'ao-actor-prepare'} <= world['passes'].keys()
    blur = pipeline.query(report, 'AOBlurNoWater')
    assert {row['register']: row['resource'] for row in blur['selection']['bindings']} == {
        's0': 'screen-ao-raw', 's1': 'scene-depth', 's2': 'screen-ao-raw', 's7': 'effect-coverage'}
    assert 'ao-actor-prepare' in blur['passes'] and 'water-mask' not in blur['passes']['contact-ao-blur']['reads']
    bindings = {row['register']: row['resource'] for row in world['selection']['bindings']}
    assert {key: bindings[key] for key in ('s1', 's2', 's3', 's8', 's9', 's13')} == {
        's1': 'scene-depth', 's2': 'hdr-linear-base', 's3': 'local-light', 's8': 'temporal-light', 's9': 'fog-color', 's13': 'rain-mask'}
    assert 'material-occlusion' not in world['passes']['world-composite']['reads']
    assert 'c80' in world['selection']['registers']['read_registers']
    assert world['selection']['target'] == 'ps_3_0'
    assert world['passes']['world-composite']['owner'] == 'NorthlightWorldComposite::draw'
    assert world['passes']['temporal-light']['owner'] == 'NorthlightWorldLightHistory::resolveLighting'
    assert report['passes']['actor-tiles-final']['owner'] == 'NorthlightWorldActorShadows::buildTiles'
    assert report['passes']['actor-visibility-resolve']['owner'] == 'NorthlightWorldActorShadows::resolveVisibility'
    assert report['passes']['cascade-near-environment-union']['owner'] == 'NorthlightWorldDirectionalTargets::merge'
    flash = pipeline.query(report, 'FlashShadow')
    flash_bindings = {row['register']: row['resource'] for row in flash['selection']['bindings']}
    assert flash_bindings['s2'] == 'flash-near-map' and flash_bindings['s3'] == 'flash-far-map'
    assert flash['passes']['flash-caster-depth']['owner'] == 'NorthlightWorldFlashShadows::render'
    prelit = pipeline.query(report, 'WorldPrelitCompositeHdr')
    assert {row['register']: row['resource'] for row in prelit['selection']['bindings']}['s14'] == 'prelit-daylight'
    material = pipeline.query(report, 'MaterialCompositeHdr')
    assert {row['register']: row['resource'] for row in material['selection']['bindings']}['s0'] == 'composite-scratch'
    assert 'shaders/world_view.hlsl' in report['variants']['WorldNormals']['sources']
    assert 'shaders/world_effects_composite.hlsl' in report['variants']['WorldCompositeHdr']['sources']
    assert report['resources']['current-distance']['writers'] == ['world-normals', 'fog-distance', 'temporal-light']
    assert not report['resources']['previous-distance']['writers']
    for obj in ('M2 section', 'ADT'):
        route = pipeline.query(report, obj)
        assert route['direction'] == 'downstream'
        assert {'draw-handoff', 'material-capture', 'model-normals', 'world-normals', 'world-composite', 'hdr-output'} <= route['passes'].keys()
        assert 'tests/test_capture_handoff.py' in route['passes']['draw-handoff']['tests']
    for obj in ('M2', 'WMO', 'ADT'):
        route = pipeline.query(report, obj)
        assert {'shadow-caster-admission', 'cascade-live-depth', 'cascade-union', 'actor-map-depth',
                'point-face-depth', 'point-face-union', 'world-local-lighting', 'world-lighting'} <= route['passes'].keys()
        assert ('static-caster-geometry' in route['passes']) == (obj != 'M2')
    union = pipeline.query(report, 'ShadowUnion')
    usages = {use['pass']: {b['register']: b for b in use['bindings']} for use in union['selection']['usages']}
    assert {name: [slots[s]['resource'] for s in ('s0', 's1')] for name, slots in usages.items()} == {
        'cascade-union': ['cascade-dynamic', 'cascade-static'],
        'cascade-near-environment-union': ['cascade-dynamic', 'cascade-static'],
        'cascade-history-copy': ['neutral-shadow', 'cascade-static'],
        'point-face-union': ['point-dynamic-face', 'point-static-face'],
    }
    assert 'cascade-near-environment' in usages['cascade-union']['s1']['alternatives']
    assert 'point-static-face' in usages['point-face-union']['s0']['alternatives']
    assert 'cascade-near-environment' in union['passes']['cascade-union']['reads']
    assert union['selection']['definition']['path'] == 'shaders/world_effects_shadow_casters.hlsl'
    actor = pipeline.query(report, 'ActorTiles')
    assert {use['pass']: use['bindings'][0]['resource'] for use in actor['selection']['usages']} == {
        'actor-tiles-first': 'actor-shadow-depth', 'actor-tiles-final': 'actor-tiles-mid'}
    transition = pipeline.query(report, 'DirectionalShadowTransitionPrevious')
    assert [b['resource'] for b in transition['selection']['bindings'] if b['register'] in ('s6', 's7', 's8', 's9')] == [
        'cascade-history', 'cascade-history', 'cascade-dynamic', 'cascade-dynamic']
    assert 'cascade-history-copy' in transition['passes']
    assert 'actor-tiles-final' not in transition['passes']
    assert pipeline.query(report, 'LocalLightingPcss')['passes']['world-local-lighting']['reads'] == [
        'baseline-light', 'material-detail', 'point-cube', 'scene-depth', 'water-mask', 'world-normals']
    fog = pipeline.query(report, 'FogBlur')
    assert {use['pass']: {b['register']: b['resource'] for b in use['bindings']}
            for use in fog['selection']['usages']} == {
        'fog-blur-horizontal': {'s9': 'fog-raw', 's11': 'water-mask', 's15': 'current-distance'},
        'fog-blur-vertical': {'s9': 'fog-horizontal', 's11': 'water-mask', 's15': 'current-distance'},
    }
    history = pipeline.query(report, 'FogTemporal')['selection']['bindings']
    assert {b['register']: b['resource'] for b in history} == {
        's1': 'scene-depth', 's9': 'fog-raw', 's14': 'fog-previous', 's15': 'previous-distance'}
    volume_fade = pipeline.query(report, 'VolumeInjectActorTransition')['selection']
    assert volume_fade['entry_point'] == 'VolumeInjectActor'
    assert volume_fade['definition']['path'] == 'shaders/fog_volume.hlsl'
    assert {b['register']: b['resource'] for b in volume_fade['bindings']}['s7'] == 'cascade-history'
    volume = pipeline.query(report, 'VolumeResolve')
    assert {'fog-volume-inject', 'fog-volume-temporal', 'fog-volume-prefix'} <= volume['passes'].keys()
    water = pipeline.query(report, 'Water')
    assert {'water-draw', 'water-legacy-resolve', 'water-compact-correct', 'water-compact-merge',
            'fog-raymarch', 'fog-volume-resolve', 'world-composite'} <= water['passes'].keys()
    assert {b['register']: b['resource'] for b in report['variants']['WaterMergeDistanceMS']['bindings']} == {
        's0': 'water-compact-coverage', 's1': 'water-compact-distance'}
    assert report['variants']['WaterMergeDistance']['bindings'][0]['resource'] == 'water-compact-distance'
    assert {b['register']: b['resource'] for b in report['variants']['RainScrub']['bindings']} == {
        's0': 'scene-depth', 's1': 'rain-depth-snapshot'}
    assert {'native-weather', 'weather-menu-precipitation', 'weather-lens', 'fog-raymarch'} <= pipeline.query(report, 'Weather')['passes'].keys()
    assert {'sky-background', 'hdr-sky-foreground', 'celestial-disc', 'hdr-decode'} <= pipeline.query(report, 'Sky')['passes'].keys()
    assert 'hdr-particle-transfer' in pipeline.query(report, 'Particles')['passes']
    ui = pipeline.query(report, 'HdrUi-ps_2_0')['selection']
    assert ui['entry_point'] == 'HdrUi' and ui['target'] == 'ps_2_0'
    assert 'hdr-ui-transfer' in pipeline.query(report, 'UI')['passes']
    ao = pipeline.query(report, 'AO-only')
    assert ao['direction'] == 'mode' and set(ao['passes']['world-composite']['variants']) == {'WorldAOCompositeHdr'}
    assert 'fog-color' not in ao['passes']['world-composite']['reads']
    fog_only = pipeline.query(report, 'fog-only')
    assert fog_only['selection']['outputs']['current-distance'] == 'fog-distance'
    assert 'world-normals' not in fog_only['passes']
    assert 'native-weather' not in pipeline.query(report, 'Menu')['passes']
    assert 'WaterMergeDistance' not in pipeline.query(report, '2x MSAA')['passes']['water-compact-merge']['variants']
    assert 'flowchart LR' in pipeline.mermaid(report) and 'WorldCompositeHdr' in pipeline.markdown(report)
    rejected(inventory, 'unknown object consumer', lambda data: data['objects']['ADT']['passes'].append('missing-pass'))
    rejected(inventory, 'Unknown pipeline resource', lambda data: data['samplers'].update(Depth='bad-resource'))
    rejected(inventory, 'unmapped sampler ModelNormals', lambda data: data['samplers'].pop('ModelNormals'))
    rejected(inventory, 'missing test', lambda data: data['passes']['draw-handoff']['tests'].append('missing_test.py'))
    rejected(inventory, 'Stale pipeline anchor', lambda data: data['passes']['draw-handoff']['code']['src/proxy/renderer.cpp'].append('no such selector'))
    rejected(inventory, 'unused sampler alternative', lambda data: data['passes']['point-face-union']['binding_alternatives'].update(
        Absent={'point-static-face': 'unused input'}))
    rejected(inventory, 'inconsistent source across passes', lambda data: data['passes']['actor-tiles-final'].update(
        entry_sources={'ActorTiles': 'world_normals.hlsl'}))
    rejected(inventory, 'invalid mode producer', lambda data: data['modes']['fog-only']['outputs'].update(
        {'current-distance': 'fog-raymarch'}))
    rejected(inventory, 'unused entry sampler override', lambda data: data['passes']['water-compact-merge']['entry_bindings'][
        'WaterMergeDistanceMS'].update(Absent='water-compact-distance'))
    altered = copy.deepcopy(inventory)
    next(row for row in altered['entries'] if row['entry'] == 'WorldCompositeHdr')['sha256'] = '0' * 64
    rejected(altered, 'stale shader bytecode', lambda data: None)
    stale = fp.output_dir() / 'stale-replay'
    stale.mkdir(exist_ok=True)
    (stale / 'validation.json').write_text(json.dumps({
        'stand_identity': '0' * 64, 'trace_summary': {'resources': 'x' * 65536},
    }), encoding='utf-8')
    try:
        pipeline.replay_shadow_products(stale)
    except ValueError as error:
        assert 'source identity differs' in str(error)
    else:
        raise AssertionError('Accepted shadow products from a stale renderer')
    rain_bindings()
    print(f"PASS pipeline map: {len(report['variants'])} bytecodes, {len(report['resources'])} resources, four modes and eleven refusal cases")


if __name__ == '__main__':
    main()
