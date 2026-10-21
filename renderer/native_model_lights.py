"""Stage model-only dynamic emitters inside the installer's existing art transaction."""
import struct
import build_equipment_emitters
import build_spell_emitters
import build_mount_emitters


def build(assets, work, read_archive):
    payload, steps = {}, []

    def stage(name, archive, generate, empty, prefixes):
        output = work / name
        try:
            report = generate(output)
        except ValueError as error:
            if str(error) != empty:
                raise
            steps.append({'step': name, 'skipped': str(error)})
            return
        models = read_archive(output / archive)
        for path, blob in models.items():
            key = path.lower().replace('/', '\\')
            if not key.startswith(prefixes) or not key.endswith('.m2') or any(p in ('.', '..') for p in key.split('\\')):
                raise ValueError('Dynamic emitter archive contains a non-model entry: ' + path)
            if key in payload and payload[key] != blob:
                raise ValueError('Conflicting dynamic emitter models: ' + path)
            payload[key] = blob
        steps.append({'step': name, 'models': len(models), 'counts': report['counts']})

    stage('equipment_model_lights', 'equipment-lights-staged.mpq',
          lambda out: build_equipment_emitters.build(assets, out),
          'No supported equipment emitters', ('item\\objectcomponents\\', 'spells\\'))
    stage('spell_model_lights', 'spell-lights-staged.mpq',
          lambda out: build_spell_emitters.build(assets, out, excluded=set(payload)),
          'No supported spell emitters', ('spells\\',))
    # Creature entries are server-owned. A fresh client may have no WDB yet;
    # the renderer's live-particle path still handles visible mount effects.
    cache = assets.client / 'Cache' / 'WDB' / assets.locale / 'creaturecache.wdb'
    if cache.is_file():
        try:
            blob = cache.read_bytes()
            build_mount_emitters.creature_cache(blob)
        except (ValueError, OSError, struct.error) as error:
            steps.append({'step': 'mount_model_lights', 'skipped': 'Unavailable supported server cache: ' + str(error)})
        else:
            stage('mount_model_lights', 'mount-lights-staged.mpq',
                  lambda out: build_mount_emitters.build(assets, blob, out),
                  'No proven mount emitters in the resolved client/server models', ('creature\\',))
    else:
        steps.append({'step': 'mount_model_lights', 'skipped': 'No server creature cache; live particle lighting remains available'})
    return payload, steps
