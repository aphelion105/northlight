"""Stage and run a private WoW 12340 D3D9 scene from local game data."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_archives
from prepare_native_scene import digest, pe32, tool_runtime
from capture_files import BLOBS
import northrend_fixture_server as fixture
from compile_native_replay_plan import client_code_inputs, load_json, packet_reads, snapshot_inputs
import northrend_frame_parity as parity
import northrend_frame_timing as frame_timing
import northrend_camera_pan as camera_pan
import northrend_point_shadows as point_shadows
import northrend_capture_work as capture_work
import northrend_native_batches as native_batches
from northrend_compat import strict_zip


SPAWN = fixture.SPAWN
SCENE = 'Northrend'
LOG = 'northlight-renderer.log'
DXVK_ADAPTER_ENV = 'DXVK_FILTER_DEVICE_NAME'
DXVK_LOG = 'Wow_d3d9.log'
LOOPBACK = '127.0.0.1'
REALMLIST = 'Data/enGB/realmlist.wtf'
PAN_ADDON = 'NorthlightRatchetPan'
PAN_ADDON_FILES = tuple(f'Interface/AddOns/{PAN_ADDON}/{PAN_ADDON}.{suffix}' for suffix in ('toc', 'lua'))
RATCHET_TILES = {f'{x}_{y}' for x in range(38, 41) for y in range(32, 35)}
PACKAGE_RUNTIME_FILES = frozenset((
    'northlight-renderer.ini', 'northlight-quality.ini', 'celestial-profiles.ini',
    'shadow-range-profiles.ini', 'forever-atmosphere.bin',
    'renderer-backends/dxvk/dxvk_d3d9.dll', 'renderer-backends/dxvk2/dxvk2_d3d9.dll',
))
PARITY_SETTLE_MS = 1800
RESET_OK = re.compile(r'^CSTREAM RESET end frame=(\d+) ran=1 result=0x00000000(?: |$)')
RESET_SIZE = re.compile(r'^CSTREAM RESET begin frame=(\d+) .*previous=(\d+)x(\d+)/\d+ .*requested=(\d+)x(\d+)/\d+ ')
STOCK_WOW_SHA256 = '99155d4d6ed4937a5ab1f5972d11e8605b986561b9f08686ab66af2c582194ec'
# WoW 12340's game-clock setter clamps speeds to the float minimum in .rdata.
# The diagnostic PE changes only that floor, so the fixture's login speed 0 holds noon.
CLOCK_FLOOR_OFFSET = 0x637be4
CLOCK_FLOOR_ORIGINAL = bytes.fromhex('8988883c')
GRAPHICS = set('componenttexturelevel environmentdetail extshadowquality farclip gamma groundeffectdensity '
               'groundeffectdist hwdetect objectfade particledensity projectedtextures readeula readtos '
               'shadowlevel skycloudlod specular texturefilteringmode timingmethod videooptionsversion '
               'weatherdensity'.split())
SOUND_DISABLED = 'SET Sound_EnableAllSound "0"'
RUN_SCRATCH = ('temp', 'shader-cache', 'state-cache')


def write_json(path, data):
    with path.open('x', encoding='utf-8') as file:
        json.dump(data, file, indent=2)
        file.write('\n')


def data_locale(data, wanted=None):
    """The locale of a Data folder with Data/<xxXX>/locale-<xxXX>.MPQ: the wanted one, else the only one."""
    installed = sorted(path.name for path in Path(data).iterdir() if path.is_dir() and len(path.name) == 4 and
                       any(file.name.lower() == f'locale-{path.name.lower()}.mpq' for file in path.iterdir()))
    found = ', '.join(installed) or 'none'
    matches = [name for name in installed if wanted is None or name.lower() == wanted.lower()]
    if wanted and not matches:
        raise ValueError(f'{data}: locale {wanted} is not installed (found: {found})')
    if len(matches) != 1:
        raise ValueError(f'{data}: several locales are installed ({found}); pass --locale' if matches else
                         f'{data}: no <locale>/locale-<locale>.MPQ client archives')
    return next((locale for locale in client_archives.LOCALES if locale.lower() == matches[0].lower()), matches[0])


def realmlist_path(locale):
    return f'Data/{locale}/realmlist.wtf'


def graphics_profile(text, resolution, msaa, locale='enGB', maximize=None):
    options = {}
    for key, value in re.findall(r'^SET (\w+) "([^"\r\n]*)"', text, re.M | re.I):
        key = key.lower()
        if key.startswith('gx') or key in GRAPHICS:
            options[key] = value
    options.update(realmlist=LOOPBACK, patchlist=LOOPBACK, locale=locale, gxapi='d3d9', gxwindow='1',
                   gxmaximize='1' if (resolution == '3840x2160' if maximize is None else maximize) else '0',
                   gxresolution=resolution, gxmultisample=str(msaa), gxvsync='0', hwdetect='0',
                   videooptionsversion='3', showtutorials='0', maxfps='0', maxfpsbk='0',
                   accountname=fixture.ACCOUNT.decode('ascii'))
    return ''.join(f'SET {name} "{value}"\n' for name, value in sorted(options.items())) + SOUND_DISABLED + '\n'


def muted_client_config(data):
    """Disable sound in a private client profile."""
    ending = b'\r\n' if b'\r\n' in data else b'\n'
    data = re.sub(rb'(?im)^[ \t]*set[ \t]+sound_enableallsound\b[^\r\n]*(?:\r?\n|$)', b'', data)
    if data and not data.endswith(b'\n'):
        data += ending
    return data + SOUND_DISABLED.encode('ascii') + ending


def runtime_client_config(data, endpoint):
    """Pin the loopback endpoint and silence an existing private launch profile."""
    ending = b'\r\n' if b'\r\n' in data else b'\n'
    data = re.sub(rb'(?im)^[ \t]*set[ \t]+realmlist\b[^\r\n]*(?:\r?\n|$)', b'', data)
    if data and not data.endswith(b'\n'):
        data += ending
    return muted_client_config(data + f'SET realmList "{endpoint}"'.encode('ascii') + ending)


def realmlist_config(endpoint):
    return f'set realmlist {endpoint}\nset patchlist {LOOPBACK}\n'


def renderer_profile(text):
    sections = {'renderer': ['Backend=dxvk', 'BackendPath=backend_d3d9.dll', 'ExperimentalHDR=1', 'SettingsOverlay=0']}
    section = ''
    for line in text.splitlines():
        header = re.fullmatch(r'\s*\[([^\]]+)\]\s*(?:[;#].*)?', line)
        if header:
            section = header[1].strip().lower()
        elif section == 'renderer' and re.match(r'^\s*Dxvk(?:EngineName|PresentWait)Workaround\s*=', line, re.I):
            sections['renderer'].append(line.strip())
        elif (section == 'renderer' or re.fullmatch(r'hdr\.[0-9a-f]+', section)) and re.match(
                r'^\s*HDR\w*\s*=', line, re.I):
            sections.setdefault(section, []).append(line.strip())
    return ''.join(f'[{name}]\n' + '\n'.join(lines) + '\n' for name, lines in sections.items())


def qa_selection(lines):
    marks = [line for line in lines if line.startswith('QA_SCENE saved ')]
    if not marks:
        return None
    mark = re.fullmatch(r'QA_SCENE saved first=(\d+) last=(\d+) durationMs=(\d+)', marks[0])
    if len(marks) != 1 or not mark:
        raise ValueError('Expected one complete QA scene mark')
    first, last, duration = map(int, mark.groups())
    if not 0 <= first < last < 2**32 or not duration:
        raise ValueError('QA scene has no playable Present interval')
    return {'first_present': first, 'last_present': last, 'duration_ms': duration}


def qa_archive(run, root, client, sidecar=None):
    # The game, backend and world caches are supplied by the replay operator.
    for path in (root / 'baseline.json', root / 'northlight-renderer.ini', client / 'northlight-quality.ini'):
        shutil.copyfile(path, run / path.name)
    archive = run.with_name(run.name + '.zip')
    pending = archive.with_suffix('.zip.pending')
    with ZipFile(pending, 'x') as output:
        for name in ('baseline.json', 'northlight-renderer.ini', 'northlight-quality.ini', 'wow.trace', LOG,
                     'validation.json', 'capture-wrapper.dll'):
            output.write(run / name, name)
        for name in (DXVK_LOG, 'WowClient64_d3d9.log'):
            if (run / name).is_file():
                output.write(run / name, name)
        if sidecar:
            for path in sorted(sidecar.iterdir()):
                output.write(path, path.relative_to(run).as_posix())
    pending.rename(archive)
    return archive


def copies(source, world=SCENE, game='Wow.exe', tiles=None, *, data=None):
    if game == 'WowClient64.exe':
        yield Path('WardenWorker32.exe')
        yield Path('Wow.exe')
    for name in (game, 'northlight-quality.ini', 'Battle.net.dll', 'DivxDecoder.dll',
                 'XvidDecoder.dll', 'msvcr80.dll', 'Microsoft.VC80.CRT.manifest',
                 'world-cache.instances.json', 'forever-atmosphere.bin', 'northlight-emission.bin',
                 'fogdata.bin', 'shadow-range-profiles.ini', 'celestial-profiles.ini', 'water-core.dll'):
        if (source / name).is_file():
            yield Path(name)
    data = data or source / 'Data'
    yield from (Path('Data') / p.relative_to(data) for p in data.rglob('*')
                if p.is_file() and p.suffix.lower() == '.mpq')
    if world is None:
        return
    cache = source / 'world-cache'
    # Include only meshes referenced by the staged tile records. The runtime
    # can select new neighboring tiles along the same test route.
    models = set()
    for path in sorted((cache / world).glob('*.fg3')):
        if tiles is not None and path.stem not in tiles:
            continue
        tile = path.read_bytes()
        if tile[:4] != b'FGS3' or len(tile) < 12 or (len(tile) - 12) % 116:
            raise ValueError(f'Unsupported static placement file: {path}')
        models.update(tile[i:i + 32].hex() + '.fgs' for i in range(12, len(tile), 116))
        yield path.relative_to(source)
    for name in sorted(models):
        path = cache / 'models' / name
        if not path.is_file():
            raise ValueError(f'{world} placement refers to a missing model: {path}')
        yield path.relative_to(source)
    for folder in (world, 'lights', 'fog', 'celestial'):
        # Per-light working caches and other maps are generated independently.
        candidates = sorted((cache / folder).glob('*'))
        if folder == 'fog' and (cache / folder / world).is_dir():
            candidates += sorted((cache / folder / world).glob('*'))
        for path in candidates:
            if folder == 'lights' and path.name not in (f'{world}.fgl', f'{world}.fga', 'native-fixtures.bin'):
                continue
            if path.is_file() and not (folder == world and path.suffix == '.fg3'):
                yield path.relative_to(source)


def shared_asset(relative):
    return relative.parts[0] == 'world-cache' or (relative.parts[0] == 'Data' and relative.suffix.lower() == '.mpq')


def link_asset(origin, target):
    origin = origin.resolve(strict=True)
    try:
        target.symlink_to(origin)
        return
    except OSError as error:
        if getattr(error, 'winerror', None) != 1314:
            raise
    try:
        os.link(origin, target)
    except OSError as error:
        if getattr(error, 'winerror', None) != 17:
            raise
        raise ValueError(f'Cannot link {origin} into {target.anchor}: without Windows Developer Mode the archives '
                         'are hard-linked, so put --output on the same drive as the client and Data') from error


def fixed_clock_image(original):
    if (hashlib.sha256(original).hexdigest() != STOCK_WOW_SHA256 or
            original[CLOCK_FLOOR_OFFSET:CLOCK_FLOOR_OFFSET + 4] != CLOCK_FLOOR_ORIGINAL):
        raise ValueError('Fixed-clock parity needs the verified original WoW 12340 executable')
    return original[:CLOCK_FLOOR_OFFSET] + bytes(4) + original[CLOCK_FLOOR_OFFSET + 4:]


def world_clock(journal, packets, frames, minute):
    if len(frames) != parity.FRAME_COUNT:
        raise ValueError('Fixed clock evidence requires both selected Presents')
    return world_clock_window(journal, packets, frames, minute)


def world_clock_window(journal, packets, frames, minute):
    first, last = min(frames), max(frames)
    expected = struct.pack('<f', minute / 1440)
    observed = {}
    for row, data in strict_zip(journal, packets):
        frame = row['frame']
        if row['trigger'] != 2 or not first <= frame <= last:
            continue
        # Two ordered reads of the client DayNight render state. The archive's
        # World trigger is joined to the exact producer Present during replay.
        reads = packet_reads(data).get((0xd38b00, 0x388), ())
        if len(reads) != 2 or any(read is None or read[4:8] != expected for read in reads):
            raise ValueError(f'World clock was not fixed at minute {minute} on frame {frame}')
        if frame in observed:
            raise ValueError(f'Duplicate World clock snapshot on frame {frame}')
        observed[frame] = row['record']
    if set(observed) != set(range(first, last + 1)):
        raise ValueError('Fixed clock evidence is missing a World snapshot in the comparison window')
    return {'minute_of_day': minute, 'samples': len(observed), 'frames': {
        str(frame): {'record': observed[frame], 'day_fraction': struct.unpack('<f', expected)[0]} for frame in frames}}


def visual_clock(lines, frames):
    discarded = set()
    parity.selected_frames(lines, discarded)
    ticks = {}
    for line in lines:
        if not line.startswith('FRAME_PARITY_CLOCK '):
            continue
        match = re.fullmatch(r'FRAME_PARITY_CLOCK present=(\d+) hz=60 tick=(\d+)', line)
        if not match:
            raise ValueError('Malformed parity visual clock')
        frame, tick = map(int, match.groups())
        if (frame not in frames and frame not in discarded) or frame in ticks or tick != ((1000 + frame * 1000 // 60) & 0xffffffff):
            raise ValueError('Parity visual clock differs from the selected Present timeline')
        ticks[frame] = tick
    if ticks.keys() != set(frames) | discarded:
        raise ValueError('Missing parity visual clock evidence')
    return {'hz': 60, 'tick_ms': {str(frame): ticks[frame] for frame in frames}}


def stage_pe(path, game):
    if game == 'Wow.exe':
        pe32(path)
        return
    with path.open('rb') as binary:
        header = binary.read(64)
        if len(header) == 64 and header[:2] == b'MZ':
            binary.seek(struct.unpack_from('<I', header, 60)[0])
            coff = binary.read(26)
            if len(coff) == 26 and coff[:6] == b'PE\0\0\x64\x86' and coff[24:] == b'\x0b\x02':
                return
    raise ValueError(f'Expected PE32+ AMD64: {path}')


def stage(args):
    source, out = args.client.resolve(), args.output.resolve()
    if out.is_relative_to(source) or source.is_relative_to(out):
        raise ValueError('Private output and installed client must be disjoint')
    if args.frame_parity and args.msaa != 2:
        raise ValueError('Frame parity requires a 2× MSAA stage')
    ratchet = getattr(args, 'scene', None) == fixture.RATCHET_PAN
    game = 'WowClient64.exe' if getattr(args, 'x64', False) else 'Wow.exe'
    map_id, _, world = fixture.map_for(fixture.RATCHET_PAN if ratchet else 'solo')
    qa = getattr(args, 'qa_scene', False)
    if qa and (args.frame_parity or ratchet):
        raise ValueError('QA capture staging requires D3D9 Northrend without --frame-parity')
    qa_backend = getattr(args, 'qa_backend_kind', 'dxvk')
    if qa_backend != 'dxvk' and (not qa or game == 'Wow.exe'):
        raise ValueError('Select an alternate QA backend only with --x64 --qa-scene')
    if args.frame_parity and (ratchet or game != 'Wow.exe'):
        raise ValueError('Fixed-clock replay parity requires the original x86 Northrend stage')
    for binary in (source / game, args.dll, args.backend):
        stage_pe(binary, game)
    if game == 'WowClient64.exe':
        for name in ('WardenWorker32.exe', 'Wow.exe'):
            pe32(source / name)
    if not (source / 'Data/enGB/base-enGB.MPQ').is_file() or not (source / 'world-cache' / world).is_dir():
        raise ValueError(f'The fixture requires enGB MPQs and the installed {world} cache')
    clock_image = fixed_clock_image((source / 'Wow.exe').read_bytes()) if args.frame_parity else None
    tiles = RATCHET_TILES if ratchet else None
    files = list(copies(source, world, game, tiles))
    if ratchet and Path('world-cache/Kalimdor/39_33.fg3') not in files:
        raise ValueError('Missing the Ratchet static geometry tile')
    if args.resume and (out / 'baseline.json').exists():
        raise ValueError('A completed stage cannot be changed')
    out.mkdir(parents=True, exist_ok=args.resume)
    client = out / 'client'
    files = {name: source / name for name in files}
    files.update({Path('d3d9.dll'): args.dll, Path('northlight-assets.bin'): args.assets,
                  Path('backend_d3d9.dll'): args.backend})
    if ratchet:
        for relative in map(Path, PAN_ADDON_FILES):
            files[relative] = Path(__file__).with_name('fixtures') / 'ratchet-pan' / relative.name
    hashed, linked = {}, []
    for relative, origin in files.items():
        target = client / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if origin.resolve().is_relative_to(out):
            raise ValueError(f'Overlapping stage input: {origin}')
        if clock_image is not None and relative == Path('Wow.exe'):
            target.unlink(missing_ok=True)
            target.write_bytes(clock_image)
            hashed[relative.as_posix()] = digest(target)
            continue
        if shared_asset(relative):
            linked.append(relative.as_posix())
            if not (args.resume and target.exists() and target.samefile(origin)):
                if args.resume:
                    target.unlink(missing_ok=True)
                link_asset(origin, target)
            hashed[relative.as_posix()] = digest(target)
            continue
        if args.resume and target.is_file() and target.stat().st_size == origin.stat().st_size:
            expected = digest(origin)
            if digest(target) == expected:
                hashed[relative.as_posix()] = expected
                continue
        shutil.copyfile(origin, target)
        hashed[relative.as_posix()] = digest(target)
    quality = client / 'northlight-quality.ini'
    if not quality.is_file():
        raise ValueError('Provide a client with the baseline northlight-quality.ini')
    if args.frame_parity or ratchet or qa:
        settings = quality.read_text(encoding='utf-8-sig')
        flags = ['CommandStream=1', 'StreamFrameSkip=0']
        if ratchet:
            flags += ['Diagnostics=1', 'RenderProfile=1']
        if qa:
            flags += ['Diagnostics=1', 'RenderProfile=0']
        keys = '|'.join(flag.split('=')[0] for flag in flags)
        settings = re.sub(rf'(?im)^[ \t]*(?:{keys})[ \t]*=[^\n]*\n?', '', settings)
        settings, sections = re.subn(r'(?im)^\[Quality\][ \t]*$', '\n'.join(('[Quality]', *flags)), settings)
        if sections != 1:
            raise ValueError('Expected exactly one [Quality] section in northlight-quality.ini')
        quality.write_text(settings, encoding='utf-8')
        hashed[quality.name] = digest(quality)
    renderer = client / 'northlight-renderer.ini'
    installed_renderer = source / renderer.name
    settings = renderer_profile(installed_renderer.read_text(encoding='utf-8-sig', errors='replace')
                                if installed_renderer.is_file() else '')
    if qa:
        settings = settings.replace('ExperimentalHDR=1', 'ExperimentalHDR=0').replace('SettingsOverlay=0', 'SettingsOverlay=1')
        if game != 'Wow.exe':
            settings = settings.replace('Backend=dxvk', f'Backend={qa_backend}').replace('BackendPath=backend_d3d9.dll', 'BackendPath=qa-tracer.dll')
    renderer.write_text(settings, encoding='utf-8')
    config = client / 'WTF/Config.wtf'
    config.parent.mkdir(exist_ok=True)
    realmlist = client / REALMLIST
    # Carry only graphics options from the original. The local fixture owns
    # account, realm and window state independently of the player installation.
    original = (source / 'WTF/Config.wtf').read_text(encoding='utf-8', errors='replace')
    config.write_text(graphics_profile(original, args.resolution, args.msaa), encoding='utf-8')
    realmlist.write_text(realmlist_config(LOOPBACK), encoding='ascii')
    shutil.copyfile(config, out / 'Config.wtf')
    shutil.copyfile(renderer, out / renderer.name)
    for path in (renderer, config, realmlist):
        hashed[path.relative_to(client).as_posix()] = digest(path)
    manifest = {'format': 1, 'map': world, 'map_id': map_id, 'spawn': fixture.RATCHET_SPAWN if ratchet else SPAWN, 'realm': LOOPBACK,
                'resolution': args.resolution, 'msaa': args.msaa, 'input_sha256': hashed,
                'linked_assets': sorted(linked), 'frame_parity': args.frame_parity}
    if qa:
        manifest['qa_scene'] = True
        if game != 'Wow.exe':
            manifest['qa_backend_kind'] = qa_backend
    if game != 'Wow.exe':
        manifest['game_exe'] = game
    if clock_image is not None:
        manifest['fixed_world_minute'] = fixture.WORLD_MINUTE
        manifest['original_client_sha256'] = STOCK_WOW_SHA256
    write_json(out / 'baseline.json', manifest)
    print(f'Staged {len(hashed)} frozen client inputs at {client}', flush=True)


def scan(lines, spawn=SPAWN, world=SCENE, *, camera_history=False):
    result = {'identity': None, 'backend': None, 'world_seen': False, 'camera': None,
              'gi_tick': None, 'static_tick': None, 'frames': [], 'capture_active': False}
    for line in lines:
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        if line.startswith('BUILD identity '):
            result['identity'] = fields.get('source')
        elif line.startswith('BACKEND selected='):
            result['backend'] = fields.get('runtime')
            selected = re.fullmatch(r'BACKEND selected=(.*?) runtime=.*? rules=\S+ BackendPath=.* fallback=([01])', line)
            if selected:
                result['backend_path'], fallback = selected.groups()
                result['backend_fallback'] = fallback == '1'
        elif line.startswith('RESOURCES NORTHLIGHT_ASSETS_V1:'):
            result['assets'] = line.split(':', 1)[1].split()[0] if fields.get('loaded') == '1' else None
        elif line.startswith('NATIVE capture '):
            result['capture_active'] = fields.get('active') == '1'
        elif line.startswith('HDR experimental ') and fields.get('active') in ('0', '1'):
            result['hdr_active'] = fields['active'] == '1'
            result['hdr_state'] = {name: fields[name] for name in ('requested', 'active', 'status', 'reason') if name in fields}
            if not result['hdr_active']:
                result.pop('output', None)
            result.pop('sdr_output', None)
        elif line.startswith('HDR additional target ') and fields.get('active') == '1':
            result['output'] = {'resolution': line.split()[3], 'msaa': int(fields['msaa']) or 1}
        elif line.startswith('SDR primary target ') and fields.get('format') in ('21', '22'):
            result['sdr_output'] = {'resolution': line.split()[3], 'msaa': int(fields['msaa']) or 1}
        elif line.startswith(f'WORLD probe atlas ready map={world} '):
            result['world_seen'] = True
            result['gi_tick'] = int(fields['tick'])
        elif line.startswith('WORLD context validated: map="Menu:'):
            result['menu_seen'] = True
        elif line.startswith('QUALITY preset=') and fields.get('problems', '').isdigit():
            result['quality_problems'] = int(fields['problems'])
        elif line.startswith('WORLD camera '):
            match = re.search(r'eye=\(([^)]+)\) forward=\(([^)]+)\)', line)
            if match:
                eye, forward = ([float(component) for component in values.split()] for values in match.groups())
                row = {'tick': int(fields['tick']), 'eye': eye, 'forward': forward}
                if math.dist(eye, spawn[:3]) <= 32:
                    result.setdefault('first_camera_tick', row['tick'])
                result['camera'] = row
                if camera_history:
                    result.setdefault('camera_samples', []).append(row)
        elif line.startswith('WORLD geometry transition ') and result['world_seen']:
            result['static_tick'] = int(fields['tick']) if fields.get('ready') == '1' else None
        elif line.startswith('FRAME interval '):
            result['frames'].append(fields)
    return result


def measured_output(observation, mode):
    if mode == 'sdr':
        return observation.get('sdr_output') if observation.get('hdr_active') is False else None
    if mode == 'hdr' and observation.get('hdr_active') is not False:
        return observation.get('output')
    return None


def sdr_stand_profile(settings):
    updated, count = re.subn(rb'(?m)^ExperimentalHDR=1(?=\r?$)', b'ExperimentalHDR=0', settings)
    if count != 1:
        raise ValueError('Expected one enabled HDR key in the frozen renderer profile')
    return updated


def warm_windows(state, count, *, background=False, skip_intervals=0):
    if not state['static_tick']:
        return []
    # Allow two seconds of stabilization plus a complete 120-frame window.
    earliest = state['static_tick'] + 2000
    windows = [row for row in state['frames'][skip_intervals:] if row.get('focus') == ('0' if background else '1') and row.get('enabled') == '1'
               and row.get('samples') == '120' and row.get('complete') != '0'
               and int(row['tick']) - round(120 * float(row['meanMs'])) >= earliest]
    return windows[:count]


def observe_native_m2(lines, required, require_grouped=False, require_world_batches=False):
    producers = {}
    grouped = {}
    samples = []
    verified = 'NATIVE_M2 client12340=1' in lines
    grouped_verified = 'NATIVE_M2 groupedClient12340=1' in lines
    grouped_fields = {'producerFrame', 'shaderDraws', 'softwareDraws', 'alphaTested', 'matched',
                      'multiInstance', 'instances', 'triangles', 'rangeMismatch', 'paletteUnavailable',
                      'paletteMismatch'}
    for line in lines:
        if line.startswith('NATIVE_M2 grouped '):
            values = re.findall(r'(\w+)=(\d+)(?=\s|$)', line)
            row = {name: int(value) for name, value in values}
            if len(values) != len(grouped_fields) or row.keys() != grouped_fields or not grouped_verified:
                raise ValueError('Invalid native grouped M2 producer record')
            frame = row['producerFrame']
            if (frame in grouped or row['alphaTested'] > row['shaderDraws'] or
                    row['multiInstance'] > row['matched'] or row['instances'] < row['matched'] + row['multiInstance'] or
                    row['shaderDraws'] != sum(row[name] for name in
                                             ('matched', 'rangeMismatch', 'paletteUnavailable', 'paletteMismatch'))):
                raise ValueError(f'Inconsistent native grouped M2 counts at producer frame {frame}')
            grouped[frame] = row
            continue
        if not line.startswith(('NATIVE_M2 producerFrame=', 'NATIVE_M2 normals ')):
            continue
        row = dict(re.findall(r'(\w+)=([^\s]+)', line))
        try:
            for name, value in row.items():
                if name != 'domain':
                    row[name] = float(value) if name.endswith('Us') else int(value)
            frame = row['producerFrame']
            if 'domain' not in row:
                if row['matched']:
                    if frame in producers:
                        raise ValueError(f'Repeated native M2 producer frame {frame}')
                    producers[frame] = row
                continue
            producer = producers.get(frame)
            if not producer or any(sample['producer']['producerFrame'] == frame for sample in samples):
                raise ValueError(f'Unmatched or repeated native M2 sample at producer frame {frame}')
            if not (0 < row['seen'] <= producer['matched'] and
                    0 <= row['immutable'] <= row['queued'] <= row['eligible'] <= row['seen'] and
                    row['fallback'] == row['seen'] - row['queued'] and
                    0 <= row['paletteChecks'] <= producer['paletteChecks'] and
                    0 <= row['paletteUnavailable'] and row['paletteChecks'] + row['paletteUnavailable'] <= row['queued']):
                raise ValueError(f'Inconsistent native M2 counts at producer frame {frame}')
            if any(row[k] for k in ('rangeMismatch', 'coverageMismatch', 'paletteMismatch')) or producer['paletteMismatch']:
                raise ValueError(f'Native M2 consumer comparison failed at producer frame {frame}')
            samples.append({'producer': producer, 'normals': row})
        except KeyError as error:
            raise ValueError(f'Missing native M2 audit field {error}') from error
    warm = sum(s['normals']['domain'] == 'world-ready' and s['normals']['immutable'] > 0 and
               s['normals']['paletteChecks'] > 0 and not s['normals']['paletteUnavailable'] for s in samples)
    warm_frames = {s['producer']['producerFrame'] for s in samples if s['normals']['domain'] == 'world-ready'}
    grouped_samples = [dict(row, world_ready=frame in warm_frames) for frame, row in grouped.items()]
    grouped_warm = sum(row['world_ready'] and row['multiInstance'] > 0 and row['matched'] == row['shaderDraws']
                       for row in grouped_samples)
    grouped_ready = grouped_verified and grouped_warm >= required
    world_batches = native_batches.observe(lines, warm_frames, required)
    return {'ready': (verified and warm >= required and (not require_grouped or grouped_ready) and
                      (not require_world_batches or world_batches['ready'])),
            'verified_client': verified,
            'required_warm_samples': required, 'warm_samples': warm, 'samples': samples,
            'grouped': {'required': require_grouped, 'ready': grouped_ready, 'verified_client': grouped_verified,
                        'warm_samples': grouped_warm, 'samples': grouped_samples},
            'world_batches': dict(world_batches, required=require_world_batches)}


def parity_settled(state):
    if not state['static_tick'] or not state['camera']:
        return False
    age = (state['camera']['tick'] - state['static_tick']) & 0xffffffff
    return PARITY_SETTLE_MS <= age < 0x80000000


def observe_crowd(lines, events, roster, expected_spawn, map_name=SCENE):
    named = {int(event.split('=', 1)[1]) for event in events if event.startswith('CROWD_NAME guid=')}
    sample = {'native_published': 0, 'particle_accepted': 0, 'particle_published': 0}
    world = False
    for line in lines:
        if line.startswith('WORLD probe atlas ready map='):
            world = line.startswith(f'WORLD probe atlas ready map={map_name} ')
        if not world:
            continue
        if line.startswith('LOCAL_SOURCE frame='):
            counters = dict(native_published='nativePublished', particle_accepted='particleAccepted',
                            particle_published='particlePublished')
        elif line.startswith('EQUIPMENT lights nativePoint=1 '):
            counters = dict(native_published='output')
        elif line.startswith('PARTICLELIGHT probes='):
            counters = dict(particle_accepted='accepted', particle_published='output')
        else:
            continue
        fields = dict(re.findall(r'(\w+)=(\d+)(?=\s|$)', line))
        for name, field in counters.items():
            if field in fields:
                sample[name] = max(sample[name], int(fields[field]))
    # Each name request confirms the client instantiated the corresponding player.
    spawned = expected_spawn in events
    return {'players': len(roster), 'outfits': fixture.outfit_count(roster),
            'client_name_requests': len(named & roster.keys()),
            'spawn_sha256': expected_spawn.rsplit('=', 1)[1] if spawned else None,
            'peak_lights': sample, 'ready': spawned and named == roster.keys() and all(sample.values())}


def observe_m2_stress(lines, events, roster, expected_spawn, *, x64=False):
    crowd = observe_crowd(lines, events, roster, expected_spawn)
    roster_ready = crowd['spawn_sha256'] and crowd['client_name_requests'] * 4 >= len(roster) * 3
    if x64:
        # The x64 proxy has no native client M2 hook. Use completed normal
        # passes on distinct frames to confirm a sustained dense draw workload.
        verified = any(re.match(r'^HOST exe=[^\r\n]+[\\/]WowClient64\.exe module=', line, re.I) for line in lines)
        dense = {}
        for line in lines:
            if not line.startswith('MODEL_NORMALS frame='):
                continue
            row = dict(re.findall(r'(\w+)=(\d+)(?=\s|$)', line))
            if (row.get('failed') == row.get('pending') == '0' and
                    int(row.get('compactDraws', '0')) >= 6000):
                dense[row['frame']] = int(row['compactDraws'])
        return dict(crowd, proxy_compact_normal_draws=list(dense.values()),
                    ready=bool(roster_ready and verified and len(dense) >= 2))
    audit = observe_native_m2(lines, 2)
    # Player name requests can lag well behind visible rendering. Require
    # most of the hashed roster and qualify density with the native M2 census.
    dense = [sample['seen'] for row in audit['samples'] if (sample := row['normals'])['seen'] >= 6000 and
             sample['domain'] == 'world-ready' and sample['immutable'] > 0 and
             sample['paletteChecks'] > 0 and not sample['paletteUnavailable']]
    return dict(crowd, native_m2_draws=dense,
                ready=bool(roster_ready and audit['verified_client'] and len(dense) >= 2))


def child_environment(run):
    env = {key: value for key, value in os.environ.items() if not key.upper().startswith(
        ('NORTHLIGHT_NATIVE_', 'NORTHLIGHT_RETRACE_', 'NORTHLIGHT_APITRACE_', 'NORTHLIGHT_FRAME_PARITY_', 'NORTHLIGHT_QA_SCENE_',
         'APITRACE_', 'TRACE_', 'DXVK_'))}
    if adapter := os.environ.get(DXVK_ADAPTER_ENV):
        env[DXVK_ADAPTER_ENV] = adapter
    (run / 'temp').mkdir()
    (run / 'dxvk.conf').write_text('d3d9.presentInterval = 0\ndxvk.allowFse = False\n', encoding='ascii')
    env.update(TEMP=str(run / 'temp'), TMP=str(run / 'temp'), DISABLE_VULKAN_OBS_CAPTURE='1',
               DXVK_CONFIG_FILE=str(run / 'dxvk.conf'), DXVK_LOG_PATH=str(run), DXVK_SHADER_CACHE_PATH=str(run / 'shader-cache'),
               DXVK_STATE_CACHE_PATH=str(run / 'state-cache'))
    return env


def verify_dxvk_pipeline_options(path, options):
    observed = {key: set() for key in options}
    for line in log_lines(path):
        if match := re.fullmatch(r'info:\s+(dxvk\.\w+) = (\S+)', line):
            if match[1] in observed:
                observed[match[1]].add(match[2])
    for key, expected in options.items():
        if observed[key] != {expected}:
            raise ValueError(f'DXVK log did not confirm {key} = {expected}')


def seed_dxvk_shader_cache(source, directory):
    files = (source, source.with_suffix('.lut'))
    if not source.name.endswith('.dxvk.bin') or not all(p.is_file() and p.stat().st_size for p in files):
        raise ValueError('DXVK shader cache seed requires nonempty .dxvk.bin and .dxvk.lut files')
    directory.mkdir()
    inputs = {}
    for path in files:
        target = directory / path.name
        shutil.copyfile(path, target)
        inputs[path.name] = digest(target)
    return inputs


def verify_dxvk_shader_cache(path, name):
    found = False
    loaded = 0
    for line in log_lines(path):
        if line.endswith(name) and 'cache file: ' in line:
            found = 'Found cache file: ' in line
            loaded = 0
        elif found and (match := re.fullmatch(r'info:\s+Cache: (\d+) shaders \(.*\)', line)):
            loaded = int(match[1])
    if not loaded:
        raise ValueError(f'DXVK did not load shaders from the seeded cache {name}')
    return loaded


def prepare_parity_images(images, raw):
    images.mkdir()
    if not raw:
        (images / BLOBS).mkdir()


def discard_work(report, owner, names):
    """Remove only disposable children of a run-owned directory."""
    for name in names:
        path = owner / name
        try:
            if (not path.is_relative_to(owner) or path == owner or owner.resolve() != owner or
                    path.is_symlink() or path.resolve() != path):
                raise ValueError(f'Refusing to remove redirected scratch path: {path}')
            if path.is_dir():
                shutil.rmtree(path)
            else:
                path.unlink(missing_ok=True)
        except (OSError, ValueError) as error:
            report.setdefault('scratch_cleanup_errors', []).append(str(error))


def restore_client_state(client, run, names, report, keep):
    for name in names:
        current = client / name
        try:
            if current.is_symlink() or current.resolve() != current:
                raise ValueError(f'Refusing to retire redirected client state: {current}')
            if keep and current.exists():
                current.replace(run / f'generated-{name}')
            else:
                discard_work(report, client, (name,))
            previous = run / f'prior-{name}'
            if previous.exists():
                previous.rename(current)
        except (OSError, ValueError) as error:
            report.setdefault('scratch_cleanup_errors', []).append(str(error))


def dxvk_adapter(log, required=None):
    data = log.read_bytes()
    text = data.decode('utf-8').replace('\r\n', '\n')
    selected = set(re.findall(r'^info:  Creating device:\ninfo:  ([^\n]+):$', text, re.M))
    if len(selected) != 1:
        raise ValueError(f'DXVK did not create a single device: {sorted(selected)}')
    device = selected.pop()
    if required and required.casefold() not in device.casefold():
        raise ValueError(f'DXVK created {device!r}, expected a device matching {required!r}')
    return {'filter': required, 'device': device, 'log_sha256': hashlib.sha256(data).hexdigest()}


def captured_native_more_lights(lines):
    rows = [line for line in lines if line.startswith('NATIVE more lights status=')]
    if not rows:
        return False
    status = re.fullmatch(r'NATIVE more lights status=(\S+) requested=([01]) max=8 '
                          r'runtime-only=1 files-written=0 shader-path=1', rows[0])
    if len(rows) != 1 or not status:
        raise ValueError('Invalid native light shader evidence in the capture log')
    return status[2] == '1' and status[1] in ('active', 'already-active', 'active-protection-restore-failed')


def verify_far_shadow_alignment(lines, aligned):
    rows = [line for line in lines if line.startswith('FRAME_PARITY_FAR_SHADOWS ')]
    if rows != (['FRAME_PARITY_FAR_SHADOWS aligned=1'] if aligned else []):
        raise ValueError('The renderer log does not confirm the requested far-shadow cadence')


def process_windows(pid, window_class=None):
    user = ctypes.windll.user32
    windows = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    @callback_type
    def visit(hwnd, unused):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user.IsWindowVisible(hwnd):
            name = ctypes.create_unicode_buffer(256)
            if not window_class or (user.GetClassNameW(hwnd, name, len(name)) and name.value == window_class):
                windows.append(hwnd)
        return True

    user.EnumWindows(visit, 0)
    return windows


def focus_private_window(hwnd):
    user = ctypes.windll.user32
    user.GetForegroundWindow.restype = wintypes.HWND
    user.SetForegroundWindow.argtypes = (wintypes.HWND,)
    user.GetWindowThreadProcessId.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.DWORD))
    user.AttachThreadInput.argtypes = (wintypes.DWORD, wintypes.DWORD, wintypes.BOOL)
    if user.GetForegroundWindow() == hwnd:
        return True
    user.SetForegroundWindow(hwnd)
    foreground = user.GetForegroundWindow()
    if foreground == hwnd:
        return True
    if not foreground:
        return False
    owner = user.GetWindowThreadProcessId(foreground, None)
    current = ctypes.windll.kernel32.GetCurrentThreadId()
    if not owner or owner == current:
        return False
    message = wintypes.MSG()
    user.PeekMessageW(ctypes.byref(message), None, 0, 0, 0)
    if not user.AttachThreadInput(current, owner, True):
        return False
    try:
        user.SetForegroundWindow(hwnd)
        return user.GetForegroundWindow() == hwnd
    finally:
        user.AttachThreadInput(current, owner, False)


def window_extent(hwnd, client=False):
    user = ctypes.WinDLL('user32', use_last_error=True)
    rect = wintypes.RECT()
    if not (user.GetClientRect if client else user.GetWindowRect)(wintypes.HWND(hwnd), ctypes.byref(rect)):
        raise ctypes.WinError(ctypes.get_last_error())
    return rect.right - rect.left, rect.bottom - rect.top


def post_window_resize(hwnd, extent):
    user = ctypes.WinDLL('user32', use_last_error=True)
    # SWP_ASYNCWINDOWPOS queues the resize in the owned window's message loop.
    # A synchronous send can invoke Reset inside the command stream's wait pump.
    flags = 0x4000 | 0x0010 | 0x0004 | 0x0002
    if not user.SetWindowPos(wintypes.HWND(hwnd), None, 0, 0, *extent, flags):
        raise ctypes.WinError(ctypes.get_last_error())


class ParityResize:
    def __init__(self, size):
        self.size = size
        self.smaller = (size[0] - 160, size[1] - 90)
        self.window = None
        self.outer = None
        self.offset = 0
        self.steps = []

    def advance(self, pid, lines):
        windows = process_windows(pid)
        if self.window is None:
            self.window = next((hwnd for hwnd in windows if window_extent(hwnd, client=True) == self.size), None)
            if self.window is None:
                return False
            self.outer = window_extent(self.window)
            self.offset = len(observed_reset_frames(lines))
            post_window_resize(self.window, (self.outer[0] - 160, self.outer[1] - 90))
        elif self.window not in windows:
            raise ValueError('The private window disappeared during the resize cycle')
        elif len(self.steps) < 2:
            before, after = (self.size, self.smaller) if not self.steps else (self.smaller, self.size)
            if window_extent(self.window, client=True) != after:
                return False
            observed = size_reset(lines, self.offset, before, after)
            if observed is None:
                return False
            self.offset = observed['epoch']
            self.steps.append(observed)
            if len(self.steps) == 1:
                post_window_resize(self.window, self.outer)
        return len(self.steps) == 2


def close_client(process, window_class=None, *, timeout=12):
    if process.poll() is not None:
        return False
    for hwnd in process_windows(process.pid, window_class):
        ctypes.windll.user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
    try:
        process.wait(timeout=timeout)
        return False
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)
        return True


def press_key(hwnd, key):
    user = ctypes.windll.user32
    scan_code = user.MapVirtualKeyW(key, 0) << 16
    user.PostMessageW(hwnd, 0x0100, key, 1 | scan_code)
    user.PostMessageW(hwnd, 0x0101, key, 0xc0000001 | scan_code)


def observed_camera_turn(before, after):
    return bool(before and after and after['tick'] > before['tick'] and
                math.dist(before['eye'], after['eye']) <= 16 and
                math.dist(before['forward'], after['forward']) >= .20)


def login_input(hwnd):
    for char in fixture.PASSWORD:
        ctypes.windll.user32.PostMessageW(hwnd, 0x0102, char, 1)  # WM_CHAR to the owned client window.
    press_key(hwnd, 0x0d)


def observed_menu_scene(lines):
    admitted = False
    for line in lines:
        if line.startswith('WORLD context validated: map="Menu:'):
            return True
        if line.startswith('MENU scene admitted '):
            admitted = True
        if admitted and line.startswith('WORLD screen geometry=1 meshReason=ready ') and 'map="Menu:' in line:
            return True
    return False


def observed_menu_return(lines, events, after_line):
    if 'PLAYER_LOGOUT' not in events:
        return False
    logout = events.index('PLAYER_LOGOUT')
    return 'CHAR_LIST' in events[logout + 1:] and observed_menu_scene(lines[after_line:])


def menu_parity_lifecycle(lines, frames):
    first, last = min(frames), max(frames)
    boundaries = {}
    for number, line in enumerate(lines):
        if m := re.match(r'^FRAME_PARITY present=(\d+) saved=1 ', line):
            boundaries[int(m[1])] = number
    before = boundaries.get(first)
    after = boundaries.get(last)
    if before is None or after is None or before >= after:
        raise ValueError('Menu cycle is missing its two World frame markers')
    return {'initial_menu': observed_menu_scene(lines[:before]), 'world_presents': [first, last],
            'returned_to_menu': observed_menu_scene(lines[after + 1:])}


def observed_rain(lines, events):
    sent = any(line.startswith('WEATHER_SENT kind=rain ') and line.endswith(f'packet={fixture.rain_packet().hex()}')
               for line in events)
    recent = next((line for line in reversed(lines) if line.startswith('WEATHER kind=')), '')
    counts = re.search(r'^WEATHER kind=rain prims=(\d+) draws=(\d+) intensity=([\d.]+) blend=([\d.]+).*\brainMask=(\d+)\b', recent)
    ready = bool(sent and counts and int(counts[1]) and int(counts[2]) and float(counts[4]) >= .5 and int(counts[5]))
    return {'ready': ready, 'packet': sent, 'renderer_line': recent or None}


def observed_water(lines):
    recent = next((line for line in reversed(lines) if line.startswith('WATER mask draws=')), '')
    fields = {name: int(value) for name, value in re.findall(r'(\w+)=(\d+)', recent)}
    ready = fields.get('draws', 0) > 0 and fields.get('mrt', 0) + fields.get('redraw', 0) > 0 and fields.get('targets', 0) > 0
    return {'ready': ready, 'renderer_line': recent or None}


def observed_interior_lamps(lines):
    pool = None
    refreshes = []
    for line in lines:
        if line.startswith('WORLD probe atlas ready map='):
            pool = None
            refreshes.clear()
        if match := re.match(r'^POINT pool limit=\d+ selected=(\d+) ready=(\d+) ', line):
            pool = {'selected': int(match[1]), 'ready': int(match[2])}
        if match := re.match(r'^POINT schedule updates=(\d+) reuses=(\d+) ', line):
            if int(match[1]):
                refreshes.append({'updates': int(match[1]), 'reuses': int(match[2])})
    return {'ready': bool(pool and pool['selected'] >= 2 and pool['ready'] >= 2 and len(refreshes) >= 2),
            'pool': pool, 'refresh_intervals': refreshes}


def observed_terrain_capture(lines):
    recent = next((line for line in reversed(lines) if line.startswith('TERRAIN_SWAP frame=')), '')
    fields = {name: int(value) for name, value in re.findall(r'(\w+)=(\d+)', recent)}
    ready = fields.get('setPS', 0) > 0 and fields.get('apiFailures') == 0
    return {'ready': ready, 'renderer_line': recent or None}


def advance_login(hwnd, events, actions, start, now, enter_world=True, *, x64=False, slow_login=False):
    slow_login = x64 or slow_login
    if 'login' not in actions and now - start >= (7 if slow_login else 3):
        login_input(hwnd)
        actions['login'] = now
    if (slow_login and 'login' in actions and 'login_retry' not in actions and 'REALM_LIST_SENT' not in events and
            now - actions['login'] >= 6):
        press_key(hwnd, 0x0d)
        actions['login_retry'] = now
    if ('REALM_LIST_SENT' in events and 'WORLD_AUTH_OK' not in events and
            now - actions.setdefault('realm', now) >= 1):
        press_key(hwnd, 0x0d)
        actions['realm'] = now
    if (enter_world and 'CHAR_LIST' in events and not any(line.startswith('PLAYER_SPAWN ') for line in events) and
            now - actions.setdefault('character', now) >= 1):
        press_key(hwnd, 0x0d)
        actions['character'] = now


def log_lines(path):
    if not path.is_file():
        return []
    text = path.read_text(encoding='utf-8', errors='replace')
    return text[:text.rfind('\n') + 1].splitlines()


def run_client(args):
    if os.name != 'nt':
        raise ValueError('The live fixture requires a Windows desktop and GPU')
    root, run = args.stage.resolve(), args.output.resolve()
    manifest = json.loads((root / 'baseline.json').read_text(encoding='utf-8'))
    client = (root / 'client').resolve()
    if run == root or not run.is_relative_to(root) or run.is_relative_to(client):
        raise ValueError('Put run results under the private stage, outside client/')
    installed = fp.client_root(required=False)
    if (installed and client.is_relative_to(installed.resolve()) or
            not (client.is_relative_to(root) or client.is_relative_to(fp.out().resolve()))):
        raise ValueError('The private client must belong to the stage or configured output directory')
    inputs = manifest['input_sha256']
    game = manifest.get('game_exe', 'Wow.exe')
    if game not in ('Wow.exe', 'WowClient64.exe'):
        raise ValueError('Unrecognized fixture executable')
    launch = [str(client / game), '-d3d9' if game == 'Wow.exe' else '-d3d']
    fixed = (game, 'd3d9.dll', 'northlight-assets.bin', 'backend_d3d9.dll',
             'northlight-renderer.ini', 'northlight-quality.ini', REALMLIST)
    if game == 'WowClient64.exe':
        fixed += ('WardenWorker32.exe', 'Wow.exe')
    if manifest.get('map') == 'Kalimdor':
        fixed += PAN_ADDON_FILES
    mutable = {'WTF/Config.wtf': root / 'Config.wtf', 'northlight-renderer.ini': root / 'northlight-renderer.ini'}
    for relative in (*fixed, 'WTF/Config.wtf', *manifest.get('linked_assets', ())):
        if digest(mutable.get(relative, client / relative)) != inputs[relative]:
            raise ValueError(f'Frozen runtime input differs from the stage manifest: {relative}')
    wrapper = args.capture_wrapper.resolve() if args.capture_wrapper else None
    qa = getattr(args, 'qa_scene', False)
    qa_baked = qa and game == 'WowClient64.exe'
    if qa_baked != bool(manifest.get('qa_backend_kind')):
        raise ValueError('Use --qa-scene to run the staged x64 QA backend')
    qa_realm = getattr(args, 'qa_realm', None)
    if qa and (not manifest.get('qa_scene') or not wrapper or args.target != 'world' or args.windows):
        raise ValueError('Use a --qa-scene stage, --capture-wrapper and --windows 0 for QA recording')
    if qa_realm and (not qa or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.-]*(?::[0-9]{1,5})?', qa_realm)):
        raise ValueError('--qa-realm requires --qa-scene and a realm hostname or hostname:port')
    scene = getattr(args, 'scene', 'solo')
    map_id, _, world = fixture.map_for(scene)
    ratchet = scene == fixture.RATCHET_PAN
    if world != manifest.get('map', SCENE) or map_id != manifest.get('map_id', 571):
        raise ValueError('The scene and stage must use the same world map')
    if wrapper and game != 'Wow.exe' and not qa_baked:
        raise ValueError('Fixture trace capture requires Wow.exe')
    m2_stress = scene == 'm2-stress'
    private_desktop = m2_stress or ratchet
    game_window_class = 'GxWindowClassD3d' if private_desktop else None
    timeout = args.timeout if args.timeout is not None else (240 if m2_stress else 90)
    background = private_desktop or getattr(args, 'background', False)
    camera_turn = getattr(args, 'camera_turn', False)
    if camera_turn and (args.windows or args.target != 'world' or args.manual or qa_realm):
        raise ValueError('--camera-turn requires an automated World run with --windows 0')
    return_to_menu = getattr(args, 'return_to_menu', False)
    if ratchet and (args.windows or args.target != 'world' or args.manual or wrapper or camera_turn or return_to_menu):
        raise ValueError('Ratchet requires an automated, untraced World run with --windows 0')
    if return_to_menu and (args.windows or args.target != 'world' or args.manual or
                           (wrapper and not manifest.get('frame_parity'))):
        raise ValueError('--return-to-menu requires automated World operation with --windows 0 and a parity stage for tracing')
    package_runtime = getattr(args, 'package_runtime', None)
    if package_runtime and (wrapper or package_runtime.keys() - PACKAGE_RUNTIME_FILES or
                            any(path is None and name != 'forever-atmosphere.bin' for name, path in package_runtime.items())):
        raise ValueError('Package staging is restricted to the live release smoke payload')
    output_mode = 'sdr' if qa else getattr(args, 'output_mode', 'hdr')
    if output_mode not in ('hdr', 'sdr') or (output_mode == 'sdr' and not (package_runtime or qa)):
        raise ValueError('The SDR output contract is reserved for a packaged live smoke')
    native_m2_samples = getattr(args, 'native_m2_audit_samples', 0)
    require_grouped_m2 = getattr(args, 'native_m2_require_grouped', False)
    require_world_batches = getattr(args, 'native_world_batch_audit', False)
    if (require_grouped_m2 or require_world_batches) and not native_m2_samples:
        raise ValueError('Native grouped M2 and world batch audits require --native-m2-audit-samples')
    if native_m2_samples and (wrapper or args.target != 'world' or args.windows):
        raise ValueError('Native M2 audit requires an untraced World run with --windows 0')
    candidate = getattr(args, 'live_dll', None)
    assets = getattr(args, 'live_assets', None)
    if qa_baked and (candidate or assets):
        raise ValueError('Stage the x64 QA renderer and its assets together')
    if (candidate is None) != (assets is None):
        raise ValueError('Use --live-dll and --live-assets together')
    renderer_hash = inputs['d3d9.dll']
    assets_hash = inputs['northlight-assets.bin']
    if candidate is not None:
        candidate = candidate.resolve()
        assets = assets.resolve()
        stage_pe(candidate, game)
        assets_hash = digest(assets)
        renderer_hash = digest(candidate)
    spawn = fixture.spawn_for(scene)
    requested_crowd_size = getattr(args, 'crowd_size', None)
    if requested_crowd_size is not None and scene != 'crowd':
        raise ValueError('--crowd-size requires --scene crowd')
    crowd_size = fixture.DEFAULT_CROWD_SIZE if requested_crowd_size is None else requested_crowd_size
    roster = fixture.scene_roster(scene, crowd_size)
    packets = fixture.crowd_packets(roster)
    trace_budget_gib = (6 if qa_baked else 2) + len(packets)
    trace_budget = trace_budget_gib << 30
    if scene != 'solo' and args.target != 'world':
        raise ValueError(f'The {scene} scene requires the world checkpoint')
    frame_parity = bool(wrapper and manifest.get('frame_parity'))
    resize = None
    if getattr(args, 'resize_before_parity', False):
        if not frame_parity or args.target != 'world':
            raise ValueError('A window resize cycle requires a live World parity capture')
        resize = ParityResize(tuple(map(int, manifest['resolution'].split('x'))))
    draw_probe = getattr(args, 'draw_probe', None)
    if draw_probe and not frame_parity:
        raise ValueError('An indexed draw probe requires a live parity capture')
    fixed_visual_clock = getattr(args, 'fixed_visual_clock', False)
    if fixed_visual_clock and not frame_parity:
        raise ValueError('The fixed visual clock requires a live parity capture')
    align_far_shadows = getattr(args, 'align_far_shadow_presents', False)
    if align_far_shadows and not fixed_visual_clock:
        raise ValueError('Aligning far shadows requires the fixed visual clock in live parity')
    light_history_audit = getattr(args, 'light_history_audit', False)
    if light_history_audit and not frame_parity:
        raise ValueError('The light history audit requires a live parity capture')
    light_history_between = getattr(args, 'light_history_between', False)
    if light_history_between and not light_history_audit:
        raise ValueError('Auditing frames between selected Presents requires the light history audit')
    fixed_static_stream = getattr(args, 'fixed_static_stream', False)
    if fixed_static_stream and not (fixed_visual_clock and light_history_audit):
        raise ValueError('Fixed static streaming requires the fixed visual clock and light history audit')
    fixed_shadow_bounds = getattr(args, 'fixed_shadow_bounds', False)
    if fixed_shadow_bounds and not (fixed_visual_clock and light_history_audit):
        raise ValueError('Fixed shadow bounds require the fixed visual clock and light history audit')
    fixed_gi = getattr(args, 'fixed_gi_publication', False)
    if fixed_gi and not (fixed_visual_clock and light_history_audit):
        raise ValueError('Fixed GI publication requires the fixed visual clock and light history audit')
    fixed_gpu_budget = getattr(args, 'fixed_gpu_budget', False)
    if fixed_gpu_budget and not fixed_visual_clock:
        raise ValueError('Fixed GPU budget requires the fixed visual clock')
    if frame_parity and manifest['msaa'] != 2:
        raise ValueError('A parity capture requires 2× MSAA')
    if roster:
        expected_spawn = fixture.spawn_record(roster, b''.join(packets))
    if wrapper:
        stage_pe(wrapper, game)
        binary = wrapper.read_bytes()
        if not qa_baked and (b'NORTHLIGHT_APITRACE_D3D9_TARGET\0' not in binary or 'NLTEX1;'.encode('utf-16le') not in binary):
            raise ValueError('Build the private apitrace wrapper with the downstream path and texture witnesses')
        if draw_probe and b'NORTHLIGHT_APITRACE_DRAW_PROBE\0' not in binary:
            raise ValueError('Build the private apitrace wrapper with indexed draw probes')
        if args.target != 'world':
            raise ValueError('The capture runner requires the Northrend world checkpoint')
    run.mkdir(parents=True, exist_ok=False)
    mode = 'camera-pan' if ratchet else 'capture' if wrapper else 'baseline' if args.windows else 'smoke'
    report = {'format': 2 if frame_parity else 1, 'status': 'INCOMPLETE', 'mode': mode,
              'target': args.target, 'scene': scene, 'baseline_sha256': digest(root / 'baseline.json'),
              'runtime_inputs': {name: inputs[name] for name in fixed},
              'fps_eligible': False, 'replay_validated': False}
    timing_line_count = 0
    report['window_mode'] = 'background' if background else 'focused'
    timing_windows = []
    report['runtime_inputs'].update({'d3d9.dll': renderer_hash, 'northlight-assets.bin': assets_hash})
    if qa_baked:
        report['qa_transport'] = 'd3d9-backend'
        report['qa_backend_kind'] = manifest['qa_backend_kind']
    proxy = client / 'd3d9.dll'
    shader_bundle = client / 'northlight-assets.bin'
    saved = run / 'downstream/northlight-capture.dll'
    saved_assets = saved.with_name(shader_bundle.name)
    original = run / 'staged-d3d9.dll'
    original_assets = run / 'staged-northlight-assets.bin'
    env = child_environment(run)
    qa_arm = run / 'qa-save'
    if qa:
        env['NORTHLIGHT_QA_SCENE_ARM'] = str(qa_arm)
    cache_seed = getattr(args, 'dxvk_shader_cache_seed', None)
    if cache_seed:
        report['dxvk_shader_cache'] = {'inputs': seed_dxvk_shader_cache(cache_seed, Path(env['DXVK_SHADER_CACHE_PATH']))}
    pipeline_options = {}
    if (threads := getattr(args, 'dxvk_compiler_threads', None)) is not None:
        pipeline_options['dxvk.numCompilerThreads'] = str(threads)
    if getattr(args, 'dxvk_gpl_only', False):
        pipeline_options['dxvk.enableGraphicsPipelineLibrary'] = 'True'
    if pipeline_options:
        with (run / 'dxvk.conf').open('a', encoding='ascii') as config:
            for name, value in pipeline_options.items():
                config.write(f'{name} = {value}\n')
    if getattr(args, 'dxvk_direct_adapter', False):
        with (run / 'dxvk.conf').open('a', encoding='ascii') as config:
            config.write('d3d9.enumerateByDisplays = False\n')
        report['dxvk_adapter_enumeration'] = 'direct'
    images = run / 'frame-parity'
    if frame_parity:
        prepare_parity_images(images, getattr(args, 'raw_parity', False))
        env['NORTHLIGHT_FRAME_PARITY_ROOT'] = str(images)
        if draw_probe:
            env['NORTHLIGHT_APITRACE_DRAW_PROBE'] = draw_probe
        if fixed_visual_clock:
            env['NORTHLIGHT_FRAME_PARITY_CLOCK'] = '60'
        if align_far_shadows:
            env['NORTHLIGHT_FRAME_PARITY_ALIGN_FAR_SHADOWS'] = '1'
        if light_history_audit:
            env['NORTHLIGHT_FRAME_PARITY_LIGHT_HISTORY'] = '1'
            env['NORTHLIGHT_FRAME_PARITY_POINT_INPUTS'] = '1'
        if light_history_between:
            env['NORTHLIGHT_FRAME_PARITY_LIGHT_HISTORY_BETWEEN'] = '1'
        if fixed_static_stream:
            env['NORTHLIGHT_FRAME_PARITY_STATIC_STREAM'] = '1'
        if fixed_shadow_bounds:
            env['NORTHLIGHT_FRAME_PARITY_SHADOW_BOUNDS'] = '1'
        if fixed_gi:
            env['NORTHLIGHT_FRAME_PARITY_GI_PUBLICATION'] = '1'
    server = process = desktop = None
    process_start_ns = 0
    log = client / LOG
    realmlist = client / REALMLIST
    original_realmlist = run / 'staged-realmlist.wtf'
    replaced_payload = []
    private_state = []
    lock = client.resolve().parent / 'client-running.lock'
    with lock.open('x', encoding='ascii') as owner:
        owner.write(str(os.getpid()))
    try:
        fixture_log = run / 'server.log'
        logout_signal = (run / 'return-to-menu').resolve()
        turn_signal = (run / 'camera-turn').resolve()
        server_args = [sys.executable, '-u', str(Path(fixture.__file__).resolve()),
                       '--auth-port', '0', '--scene', scene]
        if return_to_menu:
            server_args.extend(('--return-to-menu', str(logout_signal)))
        if camera_turn:
            server_args.extend(('--camera-turn', str(turn_signal)))
        if scene == 'crowd':
            server_args.extend(('--crowd-size', str(crowd_size)))
        if qa_realm:
            endpoint = qa_realm
        else:
            with fixture_log.open('x', encoding='utf-8') as output:
                server = subprocess.Popen(server_args, cwd=run,
                                          stdout=output, stderr=subprocess.STDOUT, env=env,
                                          creationflags=subprocess.CREATE_NO_WINDOW)
            for _ in range(50):
                readiness = next((line for line in log_lines(fixture_log) if line.startswith('READY ')), None)
                if readiness:
                    break
                if server.poll() is not None:
                    raise ValueError('The loopback fixture server exited before readiness')
                time.sleep(.1)
            else:
                raise ValueError('Loopback fixture server readiness timed out')
            ports = re.fullmatch(r'READY auth=([0-9]{1,5}) world=([0-9]{1,5})', readiness)
            if not ports:
                raise ValueError('Loopback fixture server reported invalid ports')
            auth_port, world_port = map(int, ports.groups())
            if not (1 <= auth_port <= 65535 and 1 <= world_port <= 65535 and auth_port != world_port):
                raise ValueError('Loopback fixture server reported invalid ports')
            endpoint = f'{LOOPBACK}:{auth_port}'
            report['fixture_ports'] = {'auth': auth_port, 'world': world_port}

        for name in ('WTF', 'Cache'):
            previous = client / name
            if previous.is_symlink() or previous.resolve() != previous:
                raise ValueError(f'The private client state must be local to the stage: {previous}')
            if previous.exists():
                previous.replace(run / f'prior-{name}')
            private_state.append(name)
        for relative, path in mutable.items():
            target = client / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            if relative == 'WTF/Config.wtf':
                config = runtime_client_config(path.read_bytes(), endpoint)
                if qa_realm:
                    config = re.sub(rb'(?im)^SET accountname [^\r\n]*\r?\n', b'', config)
                if qa and camera_turn:
                    config = re.sub(rb'(?im)^(SET maxfps(?:bk)? )"0"', rb'\g<1>"30"', config)
                target.write_bytes(config)
                report['runtime_inputs'][relative] = hashlib.sha256(config).hexdigest()
                if background:
                    caps = re.findall(rb'^SET maxfpsbk "(\d+)"\s*$', config, re.I | re.M)
                    report['configured_background_fps_cap'] = int(caps[0]) if len(caps) == 1 else None
            else:
                shutil.copyfile(path, target)
        realmlist.replace(original_realmlist)
        realmlist.write_text(realmlist_config(endpoint), encoding='ascii')
        report['runtime_inputs'][REALMLIST] = digest(realmlist)
        if fixed_gpu_budget:
            quality = run / 'parity-quality.ini'
            settings = (client / 'northlight-quality.ini').read_text(encoding='utf-8-sig')
            quality.write_bytes(replay_quality_config(settings, fixed_gpu_budget=True).encode('utf-8'))
            report['parity_quality_sha256'] = digest(quality)
            package_runtime = {'northlight-quality.ini': quality}
        if package_runtime:
            private_client = client.resolve()
            for relative, source in package_runtime.items():
                target = client / relative
                if not target.resolve().is_relative_to(private_client):
                    raise ValueError(f'Package runtime path escapes the private client: {relative}')
                previous = run / 'prior-payload' / relative
                if target.exists():
                    previous.parent.mkdir(parents=True, exist_ok=True)
                    target.replace(previous)
                replaced_payload.append((target, previous))
                if source is not None:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(source, target)
                report['runtime_inputs'][relative] = digest(target) if source is not None else None
        if qa_baked:
            downstream = run / 'downstream'
            downstream.mkdir()
            shutil.copyfile(client / 'backend_d3d9.dll', downstream / 'd3d9.dll')
            tracer = client / 'qa-tracer.dll'
            if tracer.exists():
                raise ValueError('QA tracer already exists in the private stage')
            replaced_payload.append((tracer, run / 'unused-qa-tracer'))
            shutil.copyfile(wrapper, tracer)
            shutil.copyfile(wrapper, run / 'capture-wrapper.dll')
            report['wrapper_sha256'] = digest(tracer)
            env.update(TRACE_FILE=str(run / 'wow.trace'), APITRACE_FORCE_MODULE_PATH=str(downstream),
                       NORTHLIGHT_QA_SCENE_D3D9_TRACER=str(tracer))
        elif wrapper:
            from build_renderer_assets import verify_pair

            saved.parent.mkdir()
            (run / 'snapshots').mkdir()
            shutil.copyfile(candidate if candidate is not None else proxy, saved)
            if digest(saved) != renderer_hash:
                raise ValueError('Live DLL changed while preparing the capture')
            shutil.copyfile(assets if candidate is not None else shader_bundle, saved_assets)
            if verify_pair(saved.read_bytes(), saved_assets.read_bytes()) != assets_hash:
                raise ValueError('Live shader assets changed while preparing the capture')
            report['capture_shader_assets_sha256'] = assets_hash
            proxy.replace(original)
            shutil.copyfile(wrapper, proxy)
            env.update(TRACE_FILE=str(run / 'wow.trace'), APITRACE_FORCE_MODULE_PATH=str(saved.parent),
                       NORTHLIGHT_NATIVE_CAPTURE_ROOT=str(run / 'snapshots'))
            env['NORTHLIGHT_APITRACE_D3D9_TARGET'] = str(saved)
            shutil.copyfile(wrapper, run / 'capture-wrapper.dll')
            report['wrapper_sha256'] = digest(wrapper)
        elif candidate is not None and candidate != proxy.resolve():
            proxy.replace(original)
            shutil.copyfile(candidate, proxy)
            if digest(proxy) != renderer_hash:
                raise ValueError('Live DLL changed while preparing the timing run')
        if assets_hash != inputs['northlight-assets.bin']:
            shader_bundle.replace(original_assets)
            shutil.copyfile(saved_assets if wrapper else assets, shader_bundle)
            if digest(shader_bundle) != assets_hash:
                raise ValueError('Live assets changed while preparing the timing run')
        # Archive prior logs even after interruption so they cannot satisfy
        # the next run's readiness checks.
        for name in (LOG, 'northlight-renderer.prev.log'):
            old = client / name
            if old.exists():
                old.replace(run / ('prior-' + name))
        process_start_ns = time.time_ns()
        if private_desktop:
            from northrend_private_desktop import Desktop
            desktop = Desktop()
            report['private_desktop'] = desktop.name
            process = desktop.launch(launch, client, env, run / 'client.stdout')
        else:
            startup = None
            if background:
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = 4  # SW_SHOWNOACTIVATE
            with (run / 'client.stdout').open('xb') as output:
                process = subprocess.Popen(launch, cwd=client, env=env,
                                           stdout=output, stderr=subprocess.STDOUT, startupinfo=startup)
        report['client_pid'] = process.pid
        print(f'Launched isolated {game} PID {process.pid}', flush=True)
        start = time.monotonic()
        actions = {}
        focus_retry = start
        world_ready = False
        stress_skip_intervals = 0
        logout_line = None
        before_turn = None
        turn_sent = False
        qa_turn_at = None
        while process.poll() is None and time.monotonic() - start < timeout:
            if desktop is not None:
                desktop.confirm_inactive()
            lines = log_lines(log)
            if qa and qa_selection(lines):
                report['checkpoint'] = 'qa-scene-marked'
                break
            state = scan(lines, spawn, world, camera_history=ratchet)
            events = log_lines(fixture_log)
            if logout_line is not None:
                if observed_menu_return(lines, events, logout_line):
                    report['checkpoint'] = 'menu-world-menu-ready'
                    break
                time.sleep(.25)
                continue
            sample = state['camera']
            correct_camera = sample and math.dist(sample['eye'], spawn[:3]) <= 32
            world_ready = state['world_seen'] and correct_camera and state['static_tick']
            if mode == 'smoke':
                world_ready = world_ready and state['gi_tick']
            if roster:
                report['crowd'] = (observe_m2_stress(lines, events, roster, expected_spawn, x64=game == 'WowClient64.exe') if m2_stress else
                                   observe_crowd(lines, events, roster, expected_spawn, world))
                world_ready = world_ready and report['crowd']['ready']
            if scene in ('rain', 'shoreline'):
                report['rain'] = observed_rain(lines, events)
                world_ready = world_ready and report['rain']['ready']
            if scene == 'shoreline':
                report['water'] = observed_water(lines)
                world_ready = world_ready and report['water']['ready']
            if ratchet:
                markers = camera_pan.markers(events)
                if 'camera_ready_tick' not in report:
                    if markers:
                        raise ValueError('Camera fixture started before the water, crowd and warm Present checkpoint')
                    report['water'] = observed_water(lines)
                    backend = state.get('backend')
                    warm = warm_windows(state, 1, background=background)
                    if (world_ready and backend and backend != 'non-DXVK' and state.get('backend_fallback') is False and
                            report['water']['ready'] and parity.stream_without_skips(lines) and warm):
                        report['camera_ready_tick'] = int(warm[-1]['tick'])
                        print('Ratchet scene ready. Waiting for the idle and camera sweeps.', flush=True)
                if len(markers) == len(fixture.RATCHET_PHASES):
                    phases = [{'name': name, 'start': start, 'end': end}
                              for (name, start), (_, end) in zip(markers, markers[1:])]
                    report['camera_phases'] = phases
                    result = camera_pan.assess(state['frames'], phases, state.get('camera_samples', ()), spawn,
                                               background=background)
                    if result is not None:
                        report['camera_pan'] = result
                        report['checkpoint'] = 'ratchet-pan-measured'
                        if not result['pass']:
                            report['status'] = 'FAIL'
                            report['error'] = 'Camera sweep or recovery exceeded the Present frame-time budget'
                        break
            if scene == 'interior':
                report['interior_lamps'] = observed_interior_lamps(lines)
                world_ready = world_ready and report['interior_lamps']['ready']
            if scene == 'terrain':
                report['terrain_capture'] = observed_terrain_capture(lines)
                world_ready = world_ready and report['terrain_capture']['ready']
            if native_m2_samples:
                report['native_m2'] = observe_native_m2(lines, native_m2_samples, require_grouped_m2, require_world_batches)
                world_ready = world_ready and report['native_m2']['ready']
            if camera_turn and world_ready:
                if qa and qa_turn_at is None:
                    qa_turn_at = time.monotonic() + 15
                if qa and time.monotonic() < qa_turn_at:
                    time.sleep(.25)
                    continue
                if not turn_sent:
                    before_turn = sample
                    pending_turn = turn_signal.with_suffix('.pending')
                    pending_turn.touch(exist_ok=False)
                    pending_turn.rename(turn_signal)
                    turn_sent = True
                world_ready = 'CAMERA_TURN_ACK sequence=1' in events and observed_camera_turn(before_turn, sample)
                if world_ready:
                    report.setdefault('camera_turn', {'before': before_turn, 'after': sample, 'ack_sequence': 1})
                    if qa and not qa_arm.exists():
                        qa_arm.touch(exist_ok=False)
            resize_ready = resize is None or len(resize.steps) == 2
            if not resize_ready and state['world_seen'] and correct_camera:
                resize_ready = resize.advance(process.pid, lines)
            parity_ready = not frame_parity
            if frame_parity and resize_ready and world_ready and parity_settled(state):
                if not (images / 'arm').is_file():
                    if not parity.stream_without_skips(lines):
                        raise ValueError('Parity requires one active command stream with frame skipping disabled')
                    (images / 'arm').touch(exist_ok=False)
                parity_ready = len(parity.selected_frames(lines)) == parity.FRAME_COUNT
            if (args.target == 'menu' and state['identity'] and state.get('assets') and
                    (not package_runtime or ('CHAR_LIST' in events and state.get('menu_seen') and process_windows(process.pid))) and
                    any(line.startswith('CreateDevice HRESULT=0x00000000') for line in lines)):
                report['checkpoint'] = 'menu-character-ready' if package_runtime else 'device'
                break
            if args.target == 'world' and world_ready and parity_ready and not ratchet and not qa:
                if m2_stress and not stress_skip_intervals:
                    # The first interval emitted after admission can include loading frames.
                    stress_skip_intervals = len(state['frames']) + 1
                windows = warm_windows(state, args.windows, background=background,
                                       skip_intervals=stress_skip_intervals) if args.windows else []
                if windows and len(windows) >= len(timing_windows):
                    timing_line_count, timing_windows = len(lines), windows
                if wrapper or len(windows) == args.windows:
                    if return_to_menu:
                        report['world_checkpoint'] = 'northrend-static-ready'
                        logout_line = len(lines)
                        logout_signal.touch(exist_ok=False)
                        continue
                    report['checkpoint'] = 'northrend-static-ready'
                    break
            if args.target == 'world' and not args.manual and not qa_realm and state['identity']:
                handles = process_windows(process.pid, game_window_class)
                if handles:
                    now = time.monotonic()
                    if (package_runtime and not background and not timing_windows and now >= focus_retry and
                            report.get('foreground_attempts', 0) < 6):
                        report['foreground_attempts'] = report.get('foreground_attempts', 0) + 1
                        report['foreground_request'] = focus_private_window(handles[0])
                        focus_retry = now + 4
                    advance_login(handles[0], events, actions, start, now, x64=game != 'Wow.exe',
                                  enter_world=not return_to_menu or observed_menu_scene(lines))
            elif args.target == 'menu' and package_runtime and not args.manual and state['identity']:
                handles = process_windows(process.pid)
                if handles:
                    advance_login(handles[0], events, actions, start, time.monotonic(), enter_world=False)
            if wrapper and (run / 'wow.trace').is_file() and (run / 'wow.trace').stat().st_size > trace_budget:
                raise ValueError(f'Capture exceeded the {trace_budget_gib} GiB live fixture budget')
            time.sleep(.25)
        if 'checkpoint' not in report:
            if qa:
                raise ValueError('QA recording ended before receiving a scene mark')
            if logout_line is not None:
                raise ValueError('The private WoW did not acknowledge logout and reach character selection')
            if resize and len(resize.steps) != 2:
                raise ValueError('Client did not restore its size after both successful D3D9 Resets')
            if roster and not report.get('crowd', {}).get('ready'):
                raise ValueError('Actor scene did not reach its spawn and rendering checkpoint')
            if scene in ('rain', 'shoreline') and not report.get('rain', {}).get('ready'):
                raise ValueError('Rain scene did not reach the native precipitation and rain mask checkpoint')
            if scene == 'shoreline' and not report.get('water', {}).get('ready'):
                raise ValueError('Shoreline did not reach the native water mask checkpoint')
            if scene == 'interior' and not report.get('interior_lamps', {}).get('ready'):
                raise ValueError('Interior did not publish multiple point shadow maps across refresh intervals')
            if scene == 'terrain' and not report.get('terrain_capture', {}).get('ready'):
                raise ValueError('Terrain did not submit a sample of material capture without D3D errors')
            if camera_turn and 'camera_turn' not in report:
                raise ValueError('The live renderer did not observe the requested camera turn')
            if ratchet:
                raise ValueError('Ratchet did not reach the water and timing checkpoint or complete both camera sweeps')
            if args.target == 'world' and args.windows and (world_ready or timing_windows):
                raise ValueError(f'The private WoW window completed {len(timing_windows)} of '
                                 f"{args.windows} requested {report['window_mode']} warm Present windows")
            if require_world_batches and not report.get('native_m2', {}).get('world_batches', {}).get('ready'):
                raise ValueError('The native WMO/ADT audit did not reach the requested warm samples')
            if native_m2_samples and not report.get('native_m2', {}).get('ready'):
                raise ValueError('The native M2 audit did not reach the requested warm samples')
            raise ValueError('Client exited or timed out before the requested graphics checkpoint')
    except (OSError, ValueError) as error:
        report['error'] = str(error)
    finally:
        try:
            if process is not None:
                report['forced_shutdown'] = close_client(process, game_window_class)
                report['exit_code'] = process.returncode
        finally:
            if desktop is not None:
                if process is not None:
                    process.close()
                report['background_samples'] = desktop.samples
                desktop.close()
        if server is not None and server.poll() is None:
            server.terminate()
            server.wait(timeout=10)
        if process is not None:
            try:
                if package_runtime:
                    names = ('d3d9.dll', 'northlight-assets.bin', *package_runtime.keys())
                    report['runtime_payload_after'] = {
                        name: digest(client / name) if (client / name).is_file() else None for name in names}
                if report.get('error') or report['exit_code']:
                    for crash in (client / 'Errors').glob('*'):
                        if (crash.suffix.lower() in ('.txt', '.dmp') and not crash.is_symlink() and crash.is_file() and
                                crash.resolve().is_relative_to(client) and crash.stat().st_mtime_ns >= process_start_ns):
                            saved_crash = run / 'crash' / crash.name
                            saved_crash.parent.mkdir(exist_ok=True)
                            crash.replace(saved_crash)
                            report.setdefault('crash_files', []).append(saved_crash.relative_to(run).as_posix())
            except OSError as error:
                report.setdefault('error', str(error))
        for target, previous in reversed(replaced_payload):
            if previous.exists():
                previous.replace(target)
            else:
                target.unlink(missing_ok=True)
        if original.is_file():
            original.replace(proxy)
        if original_assets.is_file():
            original_assets.replace(shader_bundle)
        if original_realmlist.is_file():
            original_realmlist.replace(realmlist)
        keep_work = getattr(args, 'keep_work', False)
        restore_client_state(client, run, private_state, report, keep_work)
        if not keep_work:
            discard_work(report, run, RUN_SCRATCH)
        if report.get('scratch_cleanup_errors'):
            report.setdefault('error', 'Could not retire scratch files or restore the staged client state')
        if process is not None and log.is_file():
            frozen_log = run / LOG
            log.replace(frozen_log)
            report['log_sha256'] = digest(frozen_log)
            lines = log_lines(frozen_log)
            state = scan(lines, spawn, world)
            if 'camera_pan' in report:
                report['camera_pan']['event_wait'] = camera_pan.event_wait_evidence(lines, report['camera_pan'])
            dxvk_log = run / f'{Path(game).stem}_d3d9.log'
            if pipeline_options:
                try:
                    verify_dxvk_pipeline_options(dxvk_log, pipeline_options)
                    report['dxvk_pipeline_options'] = pipeline_options
                except (OSError, ValueError) as error:
                    report.setdefault('error', str(error))
            if cache_seed:
                try:
                    report['dxvk_shader_cache']['loaded_shaders'] = verify_dxvk_shader_cache(dxvk_log, cache_seed.name)
                except (OSError, ValueError) as error:
                    report.setdefault('error', str(error))
            if m2_stress and 'checkpoint' in report:
                try:
                    report['crowd'] = observe_m2_stress(lines, log_lines(fixture_log), roster, expected_spawn,
                                                       x64=game == 'WowClient64.exe')
                    if not report['crowd']['ready']:
                        raise ValueError('The closed log did not confirm the M2 stress scene')
                except ValueError as error:
                    report.setdefault('error', str(error))
            if env.get(DXVK_ADAPTER_ENV) and state.get('backend') != 'non-DXVK':
                try:
                    report['dxvk_adapter'] = dxvk_adapter(dxvk_log, env[DXVK_ADAPTER_ENV])
                except (OSError, ValueError) as error:
                    report.setdefault('error', str(error))
            if return_to_menu and report.get('checkpoint') == 'menu-world-menu-ready':
                if not observed_menu_return(lines, log_lines(fixture_log), logout_line):
                    report.setdefault('error', 'The closed logs did not confirm the character menu after logout')
                else:
                    report['menu_cycle'] = {'world_checkpoint': report['world_checkpoint'],
                                            'after_world_log_line': logout_line,
                                            'fixture_log_sha256': digest(fixture_log)}
            if native_m2_samples:
                try:
                    report['native_m2'] = observe_native_m2(lines, native_m2_samples, require_grouped_m2, require_world_batches)
                    if not report['native_m2']['ready'] and 'checkpoint' in report:
                        raise ValueError('The closed log did not contain the required native M2 comparisons')
                except ValueError as error:
                    report['error'] = str(error)
            report['observation'] = {key: value for key, value in state.items() if key != 'frames'}
            # Recheck the live observation in the closed log. A later Reset can
            # start another warm-up during the run or while the client closes.
            timing_state = scan(lines[:timing_line_count], spawn) if timing_windows else None
            report['windows'] = warm_windows(timing_state, args.windows, background=background,
                                             skip_intervals=stress_skip_intervals) if timing_state else []
            report['frame_intervals'] = len(state['frames'])
            if timing_windows:
                report['timing_checkpoint'] = {'log_lines': timing_line_count, 'static_tick': timing_state['static_tick']}
                if m2_stress:
                    report['timing_checkpoint']['skipped_intervals'] = stress_skip_intervals
                if report['windows'] != timing_windows:
                    report.setdefault('error', 'The closed log changed the selected live Present windows')
            if report['windows']:
                report['mean_ms'] = sum(float(window['meanMs']) for window in report['windows']) / len(report['windows'])
                try:
                    report['frame_timing'] = frame_timing.summarize(report['windows'], background=background)
                except ValueError as error:
                    report.setdefault('error', str(error))
                for window in report['windows']:
                    window.pop('deltas', None)
            readiness = timing_state or state
            if readiness.get('first_camera_tick') is not None and readiness['gi_tick'] and readiness['static_tick']:
                report['readiness_ms'] = {key: (readiness[key] - readiness['first_camera_tick']) & 0xffffffff
                                          for key in ('gi_tick', 'static_tick')}
        if wrapper and (run / 'wow.trace').is_file():
            trace = run / 'wow.trace'
            report['trace'] = {'bytes': trace.stat().st_size, 'sha256': digest(trace)}
        sidecar = None
        if qa_baked and 'trace' in report and 'log_sha256' in report:
            try:
                from qa_d3d9_replay import certify, dxvk_runtime
                selection = qa_selection(lines)
                if selection:
                    report['qa_scene'] = certify(trace, wrapper.parents[2], selection, run, args.timeout or 120)
                    if manifest['qa_backend_kind'] != 'native':
                        report['qa_dxvk'] = dxvk_runtime((run / 'WowClient64_d3d9.log').read_text(encoding='utf-8'))
            except (OSError, ValueError, subprocess.SubprocessError) as error:
                report.setdefault('error', str(error))
        elif wrapper and 'trace' in report:
            journals = sorted((run / 'snapshots').glob('*/native-capture-status.json'))
            report['sidecars'] = [(path.parent.relative_to(run).as_posix() if qa else str(path.parent)) for path in journals]
            if len(journals) == 1:
                try:
                    sidecar = journals[0].parent
                    host = load_json(sidecar / 'capture-host.json')
                    if (host.get('client_sha256') != inputs['Wow.exe'] or host.get('renderer_sha256') != renderer_hash or
                            host.get('wrapper_sha256') != report['wrapper_sha256'] or host.get('authored_transport', False)):
                        raise ValueError('The game trace did not load the staged producer, renderer and wrapper')
                    _, proof = client_code_inputs(sidecar, host, False, {})
                    _, records, packets, _, _ = snapshot_inputs(sidecar)
                    report['client_code_observation'] = proof
                    if 'fixed_world_minute' in manifest:
                        selected = parity.selected_frames(log_lines(run / LOG))
                        report['world_clock'] = world_clock(records, packets, selected, manifest['fixed_world_minute'])
                    report['complete_snapshot_records'] = len(records)
                    if qa:
                        selection = qa_selection(lines)
                        world_frames = [row['frame'] for row in records if selection and row['trigger'] == 2 and
                                        selection['first_present'] <= row['frame'] <= selection['last_present']]
                        if len(world_frames) < 2:
                            raise ValueError('The marked interval needs at least two World snapshots')
                        selection['preview_presents'] = sorted({world_frames[0], world_frames[len(world_frames)//2], world_frames[-1]})
                        report['qa_scene'] = selection
                except (ValueError, OSError) as error:
                    report['sidecar_error'] = str(error)
        if frame_parity and 'checkpoint' in report:
            try:
                frames = parity.selected_frames(log_lines(run / LOG))
                if len(frames) != parity.FRAME_COUNT:
                    raise ValueError('Live capture did not complete the selected Presents')
                if return_to_menu:
                    lifecycle = menu_parity_lifecycle(lines, frames)
                    if not (lifecycle['initial_menu'] and lifecycle['returned_to_menu'] and 'menu_cycle' in report):
                        raise ValueError('The closed trace did not confirm a menu before and after the World parity frames')
                    report['menu_cycle'].update(lifecycle)
                if resize:
                    report['window_resize'] = resize_evidence(lines, frames, resize.steps)
                if fixed_visual_clock:
                    report['visual_clock'] = visual_clock(log_lines(run / LOG), frames)
                if align_far_shadows:
                    verify_far_shadow_alignment(lines, True)
                    report['far_shadow_alignment'] = 'present-boundary'
                size = tuple(map(int, manifest['resolution'].split('x')))
                parity_frames = {}
                for frame, quality in frames.items():
                    journal = parity.frame_journal(images, frame, size)
                    native, _ = parity.native_color_plane(images, frame, size, quality, journal=journal)
                    scene_input, _ = parity.scene_input_color_plane(images, frame, size)
                    parity_frames[frame] = {'quality': quality, 'native_color': native, 'scene_input_color': scene_input, 'sha256': {
                        name: plane[0] for name, plane in parity.planes(images, frame, size, quality, journal=journal)},
                        'composition': parity.composition_receipts(images, frame, size, journal)}
                report['parity_frames'] = parity_frames
                report['world_normal_hashes'] = {str(frame): parity.world_normal_plane(images, frame, size)[0]
                                                 for frame in frames}
                if draw_probe:
                    witness, _ = parity.draw_witness(images, size, manifest['msaa'])
                    report['draw_witness'] = {'selector': draw_probe, **witness}
                if light_history_audit:
                    history_frames = light_history_interval(frames) if light_history_between else frames
                    report.update(parity.lighting_evidence(images, size, history_frames))
                    if light_history_between:
                        if any(not row.get(parity.RESOLVED_LIGHT) for row in report['light_history_inputs'].values()):
                            raise ValueError('Missing the resolved temporal light history on an audited Present')
                        report['light_history_between'] = True
                        boundaries = parity.hdr_boundary_images(images, max(frames), size)
                        if boundaries:
                            report['hdr_boundary_images'] = {name: sha for name, (sha, _) in boundaries.items()}
                    report['point_inputs'] = point_shadows.sampled_products(images, size, frames)[0]
                if fixed_static_stream:
                    report['fixed_static_shadow_stream'] = parity.static_shadow_stream(log_lines(run / LOG), frames)
                    report['directional_depth_hashes'] = parity.directional_depth_hashes(
                        images, frames, allow_skipped=light_history_between)
                if fixed_shadow_bounds:
                    report['shadow_bounds_work'] = parity.shadow_bounds_work(log_lines(run / LOG), frames)
                if fixed_gi:
                    report['fixed_gi_publication'] = parity.fixed_gi_publication(log_lines(run / LOG), max(frames))
                if fixed_gpu_budget:
                    verify_fixed_gpu_budget(log_lines(run / LOG))
            except (OSError, ValueError) as error:
                report['error'] = str(error)
        if (not report.get('error') and report.get('exit_code') == 0 and not report.get('forced_shutdown') and
                report.get('observation', {}).get('assets') == assets_hash and
                measured_output(report['observation'], output_mode) == {key: manifest[key] for key in ('resolution', 'msaa')} and
                 (not qa or 'qa_scene' in report) and
                  (not wrapper or (report.get('trace', {}).get('bytes') and report['trace']['bytes'] <= trace_budget and
                                   (qa_baked or (report['observation']['capture_active'] and report.get('complete_snapshot_records')))))):
            report['status'] = 'PASS'
        elif 'error' not in report:
            report['error'] = 'Measured output, renderer identity, trace or shutdown did not satisfy the run contract'
        write_json(run / 'validation.json', report)
        if qa and report['status'] == 'PASS':
            try:
                print(f'QA capture package: {qa_archive(run, root, client, sidecar)}', flush=True)
            except OSError as error:
                report.update(status='INCOMPLETE', error=str(error))
                write_json(run / 'validation.json', report)
        if not any((run / f'prior-{name}').exists() for name in private_state):
            lock.unlink()
    print(f'{report["status"]}: {run / "validation.json"}')
    return 0 if report['status'] == 'PASS' else 1


def run_logged(command, directory, env, name, timeout):
    with (directory / (name + '.stdout')).open('xb') as stdout, (directory / (name + '.stderr')).open('xb') as stderr:
        return subprocess.run([str(item) for item in command], cwd=directory, env=env,
                              stdout=stdout, stderr=stderr, timeout=timeout).returncode


def profile_frames(value):
    if not re.fullmatch(r'\d{1,6}:\d{1,6}:\d{1,6}', value):
        raise ValueError('Profile window must be FIRST:LAST:STEP')
    first, last, step = map(int, value.split(':'))
    if first > last or not step or (last - first) // step >= 120:
        raise ValueError('Profile window must contain between 1 and 120 ordered frames')
    return range(first, last + 1, step)


def light_history_frames(frames, predecessors):
    if not 0 <= predecessors <= 8 or min(frames) < predecessors:
        raise ValueError('Light history predecessors must fit within the trace and the eight-Present limit')
    return sorted({frame - offset for frame in frames for offset in range(predecessors + 1)})


def light_history_interval(frames):
    return list(range(min(frames), max(frames) + 1))


def stand_profile(lines, wanted):
    sections = {'GPU frame': 'frame', 'GPU profile': 'frame', 'CPU profile': 'frame', 'CPU capture split': 'frame', 'DRAWGATE': 'frame',
                'POINT_COLD': 'rendererFrame', 'POINT_PHASE': 'rendererFrame', 'POINT_ORDER': 'rendererFrame',
                'SHADOW_REFRESH': 'rendererFrame',
                'MODEL_NORMALS replay': 'frame', 'MODEL_NORMALS pose layout': 'frame', 'MODEL_NORMALS palette instances': 'frame',
                'MODEL_NORMALS lifted rows': 'frame',
                'MODEL_PREPARE': 'rendererFrame',
                'CSTREAM QUERYFRAME': 'gameFrame', 'CSTREAM EVENT_CPU': 'gameFrame',
                'CSTREAM EVENT_QUEUE': 'gameFrame',
                'WORLD directional terrain': 'rendererFrame', 'WORLD directional replay': 'rendererFrame',
                'WORLD directional index pairs': 'rendererFrame',
                'WORLD shadow packet bank': 'rendererFrame',
                'POINT workload': 'rendererFrame', 'POINT palette': 'rendererFrame',
                'POINT_INDEX_PAIRS': 'rendererFrame', 'POINT_INDEX_BATCHES': 'rendererFrame',
                'POINT_INDEX_ADMISSION': 'rendererFrame', 'POINT_EFFECTIVE_POSE': 'rendererFrame',
                'POINT_MIXED_BAKE': 'rendererFrame',
                'POINT_REPLAY_STATE': 'rendererFrame', 'MODEL_NORMALS': 'frame',
                'MODEL_NORMALS range bank': 'frame', 'REPLAY COPY work': 'frame', 'REPLAY COPY memory': 'frame'}
    rows = {frame: {} for frame in wanted}
    for line in lines:
        if line.startswith('MODEL_NORMALS coverage '):
            match = re.fullmatch(r'MODEL_NORMALS coverage frame=(\d+) packets=(\d+) draws=(\d+) order=(0x[0-9a-f]{16})', line)
            if not match:
                raise ValueError('Malformed ModelNormals coverage receipt')
            frame, packets, draws = map(int, match.groups()[:3])
            if frame in rows:
                if 'MODEL_NORMALS coverage' in rows[frame]:
                    raise ValueError('Repeated ModelNormals coverage receipt')
                rows[frame]['MODEL_NORMALS coverage'] = dict(packets=packets, draws=draws, order=match[4])
            continue
        section = next((name for name, key in sections.items() if line.startswith(f'{name} {key}=')), None)
        if section is None:
            continue
        fields = {key: float(value) for key, value in re.findall(r'\b(\w+)=(-?\d+(?:\.\d+)?)', line)}
        if section == 'CSTREAM QUERYFRAME' and fields.get('type') != 8:
            continue
        frame = fields.pop(sections[section], -1)
        if frame in rows and ' discarded:' not in line and not fields.get('marks_truncated'):
            rows[frame][section] = fields
    return rows


def replay_quality_config(settings, limit=None, adaptive=None, model_bump=None, normal_filter=None,
                          gi_dynamic_probes=None, shadow_refresh_cpu_ms=None, far_shadow_interval=None,
                          world_shadows=None, world_fog=None, *, fixed_profile_work=False, fixed_gpu_budget=False):
    if limit is not None and not 1 <= limit <= 128:
        raise ValueError('PointShadowLimit must be between 1 and 128')
    if far_shadow_interval is not None and not 1 <= far_shadow_interval <= 16:
        raise ValueError('FarShadowInterval must be between 1 and 16')
    quality = list(re.finditer(r'(?im)^\[Quality\][ \t]*(?:\n|$)', settings))
    if len(quality) != 1:
        raise ValueError('Expected exactly one [Quality] section in northlight-quality.ini')
    start = quality[0].end()
    following = re.search(r'(?m)^[ \t]*\[', settings[start:])
    end = start + following.start() if following else len(settings)
    body = settings[start:end]
    overrides = {'PointShadowLimit': limit, 'PointShadowAdaptiveRefresh': adaptive,
                 'AutoBumpModels': model_bump, 'AutoNormalFilter': normal_filter, 'GIDynamicProbes': gi_dynamic_probes,
                 'ShadowRefreshCpuMs': shadow_refresh_cpu_ms, 'FarShadowInterval': far_shadow_interval,
                 'WorldShadows': world_shadows, 'WorldFog': world_fog}
    if fixed_gpu_budget or fixed_profile_work:
        overrides['GpuBudgetMs'] = 0
    if fixed_profile_work:
        overrides['ActorShadowCpuMs'] = 0
    lines = []
    for name, value in overrides.items():
        if value is None:
            continue
        body = re.sub(rf'(?im)^[ \t]*{name}[ \t]*=[^\n]*(?:\n|$)', '', body)
        lines.append(f'{name}={int(value)}\n')
    prefix = settings[:start]
    if not prefix.endswith('\n'):
        prefix += '\n'
    return f'{prefix}{"".join(lines)}{body}{settings[end:]}'


def verify_fixed_gpu_budget(lines):
    loaded = [line for line in lines if line.startswith('QUALITY preset=')]
    if not loaded or any('GpuBudgetMs=0(file)' not in line.split() for line in loaded):
        raise ValueError('The renderer did not load GpuBudgetMs=0 from its private settings')
    if any(line.startswith('GPUBUDGET level ') for line in lines):
        raise ValueError('The renderer changed GPU quality level during fixed-quality parity')


def observed_reset_frames(lines):
    resets = [int(match[1]) for line in lines if (match := RESET_OK.match(line))]
    if resets != sorted(resets) or any(not 0 <= frame < 0xffffffff for frame in resets):
        raise ValueError('Reset order is invalid for the World checkpoint timeline')
    return resets


def observed_reset_epoch(lines, frames):
    resets = observed_reset_frames(lines)
    if any(min(frames) < frame <= max(frames) for frame in resets):
        raise ValueError('Capture parity frames cross a Reset or Reset order is invalid')
    preceding = [frame for frame in resets if frame <= min(frames)]
    return len(preceding), preceding[-1] if preceding else 0


def size_reset(lines, offset, before, after):
    begin = None
    epoch = 0
    for line in lines:
        if match := RESET_SIZE.match(line):
            begin = tuple(map(int, match.groups()))
        elif match := RESET_OK.match(line):
            epoch += 1
            frame = int(match[1])
            if epoch > offset and begin == (frame, *before, *after):
                return {'epoch': epoch, 'present': frame, 'before': list(before), 'after': list(after)}
            begin = None
        elif line.startswith('CSTREAM RESET end '):
            begin = None
    return None


def resize_evidence(lines, frames, steps):
    if len(steps) != 2:
        raise ValueError('Parity needs both recorded resize transitions')
    shrink, restore = steps
    if (shrink['epoch'] >= restore['epoch'] or shrink['before'] != restore['after'] or
            shrink['after'] != restore['before'] or any(
                size_reset(lines, step['epoch'] - 1, step['before'], step['after']) != step for step in steps)):
        raise ValueError('The frozen renderer log does not prove both resize transitions')
    epoch, first = observed_reset_epoch(lines, frames)
    if epoch < restore['epoch'] or min(frames) <= first:
        raise ValueError('Selected Presents precede the completed resize cycle')
    for offset in range(restore['epoch'], epoch):
        unchanged = size_reset(lines, offset, restore['after'], restore['after'])
        if unchanged is None or unchanged['epoch'] != offset + 1:
            raise ValueError('The back-buffer size changed again before the selected Presents')
    return {'steps': steps, 'selected_epoch': epoch, 'epoch_first_present': first}


def geometry_transitions(lines):
    transitions = []
    for line in lines:
        if not line.startswith('WORLD geometry transition '):
            continue
        match = re.fullmatch(r'WORLD geometry transition frame=\d+ rendererFrame=(\d+) tick=\d+ '
                             r'ready=([01]) screen=([01]) mesh=([01]) targets=([01])', line)
        if not match:
            raise ValueError('Malformed capture geometry readiness transition')
        frame, ready, screen, mesh, targets = map(int, match.groups())
        if not 0 < frame < 0xffffffff or transitions and frame <= transitions[-1][0]:
            raise ValueError('Geometry readiness transitions have invalid Present order')
        if ready and not (screen and mesh and targets):
            raise ValueError('A ready geometry transition is missing its inputs')
        transitions.append((frame, ready))
    return transitions


def observed_geometry_start(lines, frames, epoch_first=0):
    # One stationary World entry. A ready Menu before the cold entry is allowed.
    window = [(frame, ready) for frame, ready in geometry_transitions(lines) if epoch_first <= frame <= max(frames)]
    supported = ([0, 1],) if epoch_first else ([0, 1], [1, 0, 1])
    if ([ready for _, ready in window] not in supported or
            window[-1][0] >= min(frames)):
        raise ValueError('Capture has no single cold-to-ready World entry before both parity frames')
    return window[-1][0]


def observed_world_checkpoints(lines, frames, audit_scene, *, scene_epochs=None, carry_cold=False):
    """Record the stationary World entry in each Reset epoch leading to the images."""
    final_epoch, _ = observed_reset_epoch(lines, frames)
    starts = [0, *observed_reset_frames(lines), 0xffffffff]
    transitions = geometry_transitions(lines)
    audited = parity.local_light_input_frames(lines, max(frames)) if audit_scene else {}
    checkpoints = []
    for epoch in range(final_epoch + 1):
        first, end = starts[epoch:epoch + 2]
        states = [(frame, ready) for frame, ready in transitions if first <= frame < end and frame <= max(frames)]
        if first == 0 and states and states[0][1]:
            states.pop(0)  # Ready Menu before World, including a Reset before the first Present.
        audit_epoch = audit_scene and (scene_epochs is None or epoch in scene_epochs)
        if epoch and carry_cold and (not states or states[0][1]):
            # A successful Reset while World is already cold need not emit
            # a transition. The preceding edge and certified Reset bound it.
            preceding = next((ready for frame, ready in reversed(transitions) if frame < first), None)
            if preceding == 0:
                states.insert(0, (first, 0))
        if epoch and audit_epoch and (not states or states[0][1]):
            # A Reset while geometry is already cold emits no readiness edge.
            # The first active World audit can prove that epoch was cold.
            cold_limit = states[0][0] if states else end
            for frame in sorted(f for f in audited if first <= f < cold_limit):
                world_rows = [row for row in audited[frame] if row['map'] == SCENE]
                if world_rows:
                    if all(row['ready'] == '0' for row in world_rows):
                        states.insert(0, (frame, 0))
                    break
        if not states:
            continue
        if [ready for _, ready in states] not in ([0], [0, 1]):
            raise ValueError('World checkpoint requires one cold entry per Reset epoch')
        cold = states[0][0]
        ready = states[1][0] if len(states) == 2 else None
        scene = None
        if audit_epoch:
            scene = parity.observed_world_scene_start(lines, ready if ready is not None else end - 1,
                                                       first, require_active=ready is not None)
        checkpoints.append(dict(epoch=epoch, first=first, end=end, cold=cold, scene=scene, geometry=ready))
    if (not checkpoints or checkpoints[-1]['epoch'] != final_epoch or
            checkpoints[-1]['geometry'] is None or checkpoints[-1]['geometry'] >= min(frames)):
        raise ValueError('World checkpoint timeline does not reach the selected geometry')
    return checkpoints


def verify_world_checkpoints(lines, frames, expected, *, carry_cold=False):
    markers = [line for line in lines if line.startswith('REPLAY_GEOMETRY_CHECKPOINT ')]
    expected_markers = [f"REPLAY_GEOMETRY_CHECKPOINT present={point['geometry']} ready=1"
                        for point in expected if point['geometry'] is not None]
    # Archived captures may contain geometry and lighting history but no
    # local-light scene audit. Compare only the scene starts the capture certifies.
    scene_epochs = {point['epoch'] for point in expected if point['scene'] is not None}
    if markers != expected_markers or observed_world_checkpoints(
            lines, frames, True, scene_epochs=scene_epochs, carry_cold=carry_cold) != expected:
        raise ValueError('Replay did not match the recorded World checkpoints across Reset epochs')


def certify_world_checkpoints(checkpoints, summary, log_hash):
    bound = []
    for item in checkpoints:
        fields = {'scene': item['scene'], 'geometry': item['geometry']}
        if item['geometry'] is None:
            fields['cold'] = item['cold']
        calls = {}
        for key, frame in fields.items():
            if frame is None:
                continue
            record = summary.get('world_checkpoints', {}).get(str(frame), {})
            if (record.get('epoch'), record.get('epoch_first_present')) != (item['epoch'], item['first']) or 'trace_call' not in record:
                raise ValueError(f'Trace join did not certify World {key} checkpoint at Present {frame}')
            calls[key] = record['trace_call']
        bound.append({**item, 'trace_calls': calls, 'capture_log_sha256': log_hash})
    return bound


def replay_backend_qa(args, capture, run, source):
    from qa_d3d9_replay import certify, dxvk_runtime

    trace = capture / 'wow.trace'
    capture_log = capture / LOG
    selection = qa_selection(log_lines(capture_log))
    if (source.get('status') != 'PASS' or not selection or digest(trace) != source['trace']['sha256'] or
            digest(capture_log) != source['log_sha256']):
        raise ValueError('Use a complete, unmodified x64 QA capture')
    backend = args.qa_backend or (args.stage or capture.parent) / 'client/backend_d3d9.dll'
    backend = backend.resolve()
    stage_pe(backend, 'WowClient64.exe')
    backend_sha = digest(backend)
    if backend_sha != source['runtime_inputs']['backend_d3d9.dll']:
        raise ValueError('Local D3D9 backend differs from the captured runtime')
    if run.is_relative_to(capture):
        raise ValueError('Replay output must be separate from the capture')
    tools = args.tool_root.resolve()
    host, parser = (tools / 'bin' / name for name in ('d3dretrace.exe', 'apitrace.exe'))
    for tool in (host, parser):
        stage_pe(tool, 'WowClient64.exe')
    run.mkdir(parents=True, exist_ok=False)
    env = child_environment(run)
    result = {'status': 'INCOMPLETE', 'qa_transport': source['qa_transport'], 'trace_sha256': source['trace']['sha256'],
              'backend_sha256': backend_sha, 'fps_eligible': False}
    try:
        marked = certify(trace, tools, selection, run, args.timeout)
        if marked != source['qa_scene']:
            raise ValueError('Backend trace markers differ from the closed capture receipt')
        result['qa_scene'] = marked
        for path in (host, *host.parent.glob('*.dll')):
            shutil.copyfile(path, run / path.name)
        if source.get('qa_backend_kind') != 'native':
            shutil.copyfile(backend, run / 'd3d9.dll')
            captured_dxvk = dxvk_runtime((capture / 'WowClient64_d3d9.log').read_text(encoding='utf-8'))
            if source.get('qa_dxvk', captured_dxvk) != captured_dxvk:
                raise ValueError('DXVK profile differs from the closed capture receipt')
            (run / 'dxvk.conf').write_text(''.join(f'{key} = {value}\n' for key, value in captured_dxvk['options'].items()), encoding='ascii')
        (run / 'preview').mkdir()
        calls = marked['preview_calls']
        terminal = (1 << 32) - 2
        if marked['last_boundary_call'] >= terminal:
            raise ValueError('QA trace exceeds the replay snapshot call range')
        selection_arg = ','.join(map(str, [*calls.values(), terminal]))
        options = ['--query-handling=run', '--snapshot', selection_arg, '--call-nos=true', '--snapshot-prefix', 'preview/present-']
        if not args.qa_visible:
            options.append('--headless')
        result['host_exit_code'] = run_logged([run / host.name, *options, trace], run, env, 'retrace', args.timeout)
        if source.get('qa_backend_kind') != 'native':
            replayed_dxvk = dxvk_runtime((run / 'WowClient64_d3d9.log').read_text(encoding='utf-8'))
            if replayed_dxvk != captured_dxvk:
                raise ValueError('DXVK replay used a different effective profile or device')
            result['dxvk'] = replayed_dxvk
        previews = {frame: f'preview/present-{call:010d}.png' for frame, call in calls.items()}
        warnings = log_lines(run / 'retrace.stderr')
        if any(re.match(r'^\d+: warning: (?:unknown object|failed with)', line) for line in warnings):
            raise ValueError('Backend retrace lost a D3D9 resource or failed a recorded call; see retrace.stderr')
        if result['host_exit_code'] or any(not (run / path).is_file() or not (run / path).stat().st_size for path in previews.values()):
            raise ValueError('Backend retrace did not produce all QA preview frames')
        result['previews'] = previews
        result['status'] = 'PASS'
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        result['error'] = str(error)
    write_json(run / 'validation.json', result)
    print(f'{result["status"]}: {run / "validation.json"}')
    return 0 if result['status'] == 'PASS' else 1


def replay(args):
    capture, run = args.capture.resolve(), args.output.resolve()
    source = json.loads((capture / 'validation.json').read_text(encoding='utf-8'))
    if source.get('qa_transport') == 'd3d9-backend':
        return replay_backend_qa(args, capture, run, source)
    if source.get('qa_transport') == 'vulkan-gfxr':
        from qa_vulkan import replay as replay_vulkan
        return replay_vulkan(args, capture, run, source)
    if source.get('qa_transport') == 'windows-apitrace':
        from qa_api_replay import replay as replay_api
        return replay_api(args, capture, run, source)
    qa = source.get('qa_scene')
    local_client = getattr(args, 'qa_client', None)
    local_backend = getattr(args, 'qa_backend', None)
    qa_visible = getattr(args, 'qa_visible', False)
    if qa_visible and not qa:
        raise ValueError('--qa-visible requires a QA scene capture')
    if (local_client or local_backend) and (not qa or not local_client or not local_backend or args.stage):
        raise ValueError('Use --qa-client and --qa-backend together for an extracted QA package')
    root = capture if local_client else args.stage.resolve() if getattr(args, 'stage', None) else capture.parent
    if (not qa and not capture.is_relative_to(root)) or capture.is_relative_to(root / 'client'):
        raise ValueError('Replay capture must belong to the private stage outside client/')
    baseline = json.loads((root / 'baseline.json').read_text(encoding='utf-8'))
    window_resize = source.get('window_resize')
    trace = capture / 'wow.trace'
    profile = getattr(args, 'profile_frames', None)
    requested = profile_frames(profile) if profile else None
    profile_sdr = getattr(args, 'profile_sdr', False)
    if profile_sdr and requested is None:
        raise ValueError('SDR requires --profile-frames. Image parity uses the captured output mode.')
    profile_fixed_work = getattr(args, 'profile_fixed_work', False)
    if profile_fixed_work and requested is None:
        raise ValueError('Fixed profiling work requires --profile-frames')
    output_mode = 'sdr' if profile_sdr or qa else 'hdr'
    fixed_visual_clock = getattr(args, 'fixed_visual_clock', None)
    if fixed_visual_clock is None:
        fixed_visual_clock = requested is None and 'visual_clock' in source
    if fixed_visual_clock and (not baseline.get('frame_parity') or (requested is not None and not profile_fixed_work)):
        raise ValueError('The fixed visual clock requires an image parity replay or --profile-fixed-work')
    captured_far_alignment = source.get('far_shadow_alignment')
    if captured_far_alignment not in (None, 'present-boundary'):
        raise ValueError('Unknown captured far-shadow cadence')
    align_far_shadows = (getattr(args, 'align_far_shadow_presents', False) or
                         (captured_far_alignment is not None and requested is None))
    if align_far_shadows and (not fixed_visual_clock or requested is not None):
        raise ValueError('Aligning far shadows requires the fixed visual clock in image parity')
    match_geometry_start = getattr(args, 'match_geometry_start', False) or profile_fixed_work
    if match_geometry_start and not (fixed_visual_clock or profile_fixed_work):
        raise ValueError('Matching geometry readiness requires the fixed visual clock or fixed profiling work')
    if profile_fixed_work and not baseline.get('frame_parity'):
        raise ValueError('Fixed profiling work requires a capture with recorded World checkpoints')
    audited_capture = 'light_history_inputs' in source
    light_history_audit = getattr(args, 'light_history_audit', False) or (audited_capture and requested is None)
    light_history_between = source.get('light_history_between') is True and requested is None
    fixed_static_stream = 'fixed_static_shadow_stream' in source and requested is None
    if fixed_static_stream and not (fixed_visual_clock and match_geometry_start and light_history_audit):
        raise ValueError('Replaying fixed static streaming requires the fixed visual clock, geometry checkpoint and light history audit')
    fixed_gi = 'fixed_gi_publication' in source and requested is None
    if fixed_gi and not (fixed_visual_clock and match_geometry_start and light_history_audit):
        raise ValueError('Replaying fixed GI publication requires the fixed visual clock, geometry checkpoint and light history audit')
    fixed_gpu_budget = 'parity_quality_sha256' in source and requested is None
    if fixed_gpu_budget and not fixed_visual_clock:
        raise ValueError('Replaying fixed GPU budget requires the fixed visual clock')
    captured_fixed_bounds = 'shadow_bounds_work' in source and requested is None
    fixed_shadow_bounds = getattr(args, 'fixed_shadow_bounds', False) or captured_fixed_bounds
    if fixed_shadow_bounds and not (fixed_visual_clock and match_geometry_start and light_history_audit):
        raise ValueError('Replaying fixed shadow bounds requires the fixed visual clock, geometry checkpoint and light history audit')
    history_predecessors = getattr(args, 'light_history_predecessors', 0)
    if history_predecessors and not light_history_audit:
        raise ValueError('Light history predecessors require --light-history-audit')
    if light_history_audit and (requested is not None or not baseline.get('frame_parity')):
        raise ValueError('The light history audit requires an image parity replay outside profiling')
    shadow_limit = getattr(args, 'point_shadow_limit', None)
    shadow_adaptive = getattr(args, 'point_shadow_adaptive', None)
    shadow_refresh_cpu_ms = getattr(args, 'shadow_refresh_cpu_ms', None)
    far_shadow_interval = getattr(args, 'far_shadow_interval', None)
    model_bump = getattr(args, 'model_bump', None)
    normal_filter = getattr(args, 'normal_filter', None)
    gi_dynamic_probes = getattr(args, 'gi_dynamic_probes', None)
    world_shadows = getattr(args, 'world_shadows', None)
    world_fog = getattr(args, 'world_fog', None)
    if requested is None and (world_shadows is not None or world_fog is not None):
        raise ValueError('World shadow and fog overrides require --profile-frames; image parity uses the captured effects')
    legacy_normal_declarations = getattr(args, 'legacy_normal_declarations', None)
    legacy_normal_pose_declaration = getattr(args, 'legacy_normal_pose_declaration', None)
    legacy_normal_range_bank = getattr(args, 'legacy_normal_range_bank', None)
    legacy_shadow_projection_bank = getattr(args, 'legacy_shadow_projection_bank', None)
    legacy_shadow_packet_bank = getattr(args, 'legacy_shadow_packet_bank', None)
    legacy_copy_admission = getattr(args, 'legacy_copy_admission', None)
    legacy_event_spin = getattr(args, 'legacy_event_spin', None)
    legacy_directional_terrain = getattr(args, 'legacy_directional_terrain', None)
    legacy_directional_replay = getattr(args, 'legacy_directional_replay', None)
    legacy_actor_inset = getattr(args, 'legacy_actor_inset', None)
    directional_index_pairs = getattr(args, 'directional_index_pairs', None)
    point_replay_state_reuse = getattr(args, 'point_replay_state_reuse', None)
    point_index_batches = getattr(args, 'point_index_batches', None)
    point_index_admission_reuse = getattr(args, 'point_index_admission_reuse', None)
    point_effective_pose = getattr(args, 'point_effective_pose', None)
    point_mixed_reuse = getattr(args, 'point_mixed_reuse', None)
    normal_palette_instancing = getattr(args, 'normal_palette_instancing', None)
    normal_palette_binding_reuse = getattr(args, 'normal_palette_binding_reuse', None)
    normal_palette_instance_rows = getattr(args, 'normal_palette_instance_rows', None)
    if normal_palette_instance_rows and normal_palette_instancing is False:
        raise ValueError('Per-instance ModelNormals rows require palette instancing')
    crowd_bounds_operations = getattr(args, 'crowd_bounds_operations', None)
    point_adaptive_motion = getattr(args, 'point_adaptive_motion', None)
    shadow_audit = getattr(args, 'point_shadow_audit', False)
    work_frame = getattr(args, 'capture_work_frame', None)
    if work_frame is not None and (requested is not None or not baseline.get('frame_parity') or
                                   'fixed_world_minute' not in baseline or not 0 < work_frame <= 0xFFFFFFFF):
        raise ValueError('Capture work requires a positive World frame in a fixed-clock capture outside profiling')
    if requested is not None and (not baseline.get('frame_parity') or 'fixed_world_minute' not in baseline):
        raise ValueError('A stand performance profile needs a certified parity capture with a fixed clock')
    if legacy_actor_inset is not None and (requested is not None or not baseline.get('frame_parity')):
        raise ValueError('The actor inset comparison requires a replay with image parity enabled')
    if shadow_audit and not baseline.get('frame_parity'):
        raise ValueError('Point shadow audit requires certified Present ordinals')
    if (source['mode'] != 'capture' or source.get('status') != 'PASS' or len(source['sidecars']) != 1 or
            source['baseline_sha256'] != digest(root / 'baseline.json') or source['trace']['sha256'] != digest(trace)):
        raise ValueError('Replay requires one complete closed capture from this exact private stage')
    sidecar = Path(source['sidecars'][0])
    if qa:
        if (not baseline.get('qa_scene') or not re.fullmatch(r'snapshots/capture-[0-9-]+', sidecar.as_posix()) or
                not (capture / sidecar).resolve().is_relative_to(capture / 'snapshots')):
            raise ValueError('QA package has an invalid snapshot directory')
        sidecar = capture / sidecar
        capture_log = capture / LOG
        if digest(capture_log) != source['log_sha256'] or qa_selection(log_lines(capture_log)) != {
                key: qa[key] for key in ('first_present', 'last_present', 'duration_ms')}:
            raise ValueError('QA Present selection differs from the closed renderer log')
        preview = qa['preview_presents']
        if (not 2 <= len(preview) <= 3 or preview != sorted(set(preview)) or
                any(type(frame) is not int or not qa['first_present'] <= frame <= qa['last_present'] for frame in preview)):
            raise ValueError('QA preview frames lie outside the marked interval')
    if not qa and 'capture_shader_assets_sha256' in source:
        asset_hash = source['capture_shader_assets_sha256']
        if (asset_hash != source['runtime_inputs']['northlight-assets.bin'] or
                asset_hash != digest(capture / 'downstream/northlight-assets.bin')):
            raise ValueError('The captured live shader bundle changed or differs from runtime evidence')
    if not getattr(args, 'host', None):
        raise ValueError('The x86 imported-scene replay requires --host')
    host, stand, assets = args.host.resolve(), args.stand_dll.resolve(), args.stand_assets.resolve()
    if host.name.lower() != 'northlight-native-retrace.exe':
        raise ValueError('Choose the dedicated Northlight retrace executable')
    pe32(host)
    pe32(stand)
    tools = tool_runtime(args.tool_root)
    if run.is_relative_to(capture) or run.is_relative_to(local_client.resolve() if local_client else root / 'client'):
        raise ValueError('Replay output must be separate from its live capture and client')
    run.mkdir(parents=True, exist_ok=False)
    env = child_environment(run)
    if shadow_audit:
        env['NORTHLIGHT_RETRACE_POINT_SHADOW_AUDIT'] = '1'
    report = {'format': 1, 'status': 'INCOMPLETE', 'capture': str(capture), 'host_sha256': digest(host),
              'stand_sha256': digest(stand), 'stand_assets_sha256': digest(assets), 'fps_eligible': False,
              'trace_sha256': source['trace']['sha256'], 'stage_sha256': source['baseline_sha256'],
              'color_depth_parity': 'unverified', 'trace_join_validated': False, 'stand_world_observed': False,
              'scene': source.get('scene', 'solo'), 'stand_output_mode': output_mode}
    if 'crowd' in source:
        report['live_crowd'] = source['crowd']
    try:
        captured_adapter = source.get('dxvk_adapter')
        if captured_adapter:
            if dxvk_adapter(capture / DXVK_LOG, captured_adapter['filter']) != captured_adapter:
                raise ValueError('The captured DXVK device log changed after validation')
            env.setdefault(DXVK_ADAPTER_ENV, captured_adapter['filter'])
        stand_root = run / 'stand'
        stand_root.mkdir()
        staged = stand_root / 'northlight-stand.dll'
        shutil.copyfile(stand, staged)
        shutil.copyfile(assets, stand_root / 'northlight-assets.bin')
        client = local_client.resolve() if local_client else root / 'client'
        for relative, expected in baseline['input_sha256'].items():
            if not relative.startswith('world-cache/') and relative not in (
                    'backend_d3d9.dll', 'northlight-quality.ini', 'northlight-renderer.ini', 'forever-atmosphere.bin',
                    'northlight-emission.bin', 'fogdata.bin', 'shadow-range-profiles.ini', 'celestial-profiles.ini',
                    'world-cache.instances.json'):
                continue
            if Path(relative).is_absolute() or '\\' in relative or ':' in relative or '..' in Path(relative).parts:
                raise ValueError('Invalid relative replay input path')
            destination = stand_root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            if local_client and relative == 'backend_d3d9.dll':
                origin = local_backend.resolve()
            elif relative == 'northlight-renderer.ini' or (local_client and relative == 'northlight-quality.ini'):
                origin = root / relative
            else:
                origin = client / relative
            if shared_asset(Path(relative)):
                link_asset(origin, destination)
            else:
                shutil.copyfile(origin, destination)
            if digest(destination) != expected:
                raise ValueError(f'Frozen replay input differs: {relative}')
        renderer_ini = stand_root / 'northlight-renderer.ini'
        if qa:
            renderer_ini.write_text(renderer_profile(renderer_ini.read_text(encoding='utf-8-sig'))
                                    .replace('ExperimentalHDR=1', 'ExperimentalHDR=0'), encoding='utf-8')
        if profile_sdr:
            renderer_ini.write_bytes(sdr_stand_profile(renderer_ini.read_bytes()))
        report['stand_renderer_ini_sha256'] = digest(renderer_ini)
        if fixed_gpu_budget:
            frozen_quality = capture / 'parity-quality.ini'
            quality = stand_root / 'northlight-quality.ini'
            expected = replay_quality_config(quality.read_text(encoding='utf-8-sig'), fixed_gpu_budget=True).encode('utf-8')
            if (frozen_quality.read_bytes() != expected or digest(frozen_quality) != source['parity_quality_sha256'] or
                    source['runtime_inputs']['northlight-quality.ini'] != source['parity_quality_sha256']):
                raise ValueError('Fixed GPU budget settings differ from the frozen live profile')
            quality.write_bytes(expected)
            report['parity_quality_sha256'] = digest(quality)
        if profile_fixed_work or any(value is not None for value in (shadow_limit, shadow_adaptive, model_bump, normal_filter,
                                                                     gi_dynamic_probes, shadow_refresh_cpu_ms, far_shadow_interval,
                                                                     world_shadows, world_fog)):
            quality = stand_root / 'northlight-quality.ini'
            quality.write_text(replay_quality_config(quality.read_text(encoding='utf-8-sig'), shadow_limit, shadow_adaptive,
                                                     model_bump, normal_filter, gi_dynamic_probes,
                                                     shadow_refresh_cpu_ms, far_shadow_interval,
                                                     world_shadows, world_fog, fixed_profile_work=profile_fixed_work), encoding='utf-8')
            report['stand_quality'] = {'sha256': digest(quality)}
            if profile_fixed_work:
                report['stand_quality'].update(gpu_budget_ms=0, actor_shadow_cpu_ms=0)
            if shadow_limit is not None:
                report['stand_quality']['point_shadow_limit'] = shadow_limit
            if shadow_adaptive is not None:
                report['stand_quality']['point_shadow_adaptive_refresh'] = shadow_adaptive
            if model_bump is not None:
                report['stand_quality']['model_bump'] = model_bump
            if normal_filter is not None:
                report['stand_quality']['normal_filter'] = normal_filter
            if gi_dynamic_probes is not None:
                report['stand_quality']['gi_dynamic_probes'] = gi_dynamic_probes
            if shadow_refresh_cpu_ms is not None:
                report['stand_quality']['shadow_refresh_cpu_ms'] = shadow_refresh_cpu_ms
            if far_shadow_interval is not None:
                report['stand_quality']['far_shadow_interval'] = far_shadow_interval
            if world_shadows is not None:
                report['stand_quality']['world_shadows'] = world_shadows
            if world_fog is not None:
                report['stand_quality']['world_fog'] = world_fog
        exe = run / host.name
        shutil.copyfile(host, exe)
        for path in map(Path, tools['retrace_dependencies']):
            shutil.copyfile(path, run / path.name)
        plan = run / 'plan'
        command = [sys.executable, fp.SCRIPTS / 'compile_native_replay_plan.py', '--trace', trace,
                   '--snapshot-dir', sidecar, '--client', client / 'Wow.exe', '--stand-dll', staged,
                   '--tool-root', Path(tools['root']), '--output', plan]
        if (capture / 'capture-wrapper.dll').is_file():
            command.extend(['--capture-wrapper', capture / 'capture-wrapper.dll'])
        frames = {}
        history_frames = []
        world_checkpoints = []
        menu_cycle = source.get('menu_cycle') if requested is None else None
        images = run / 'frame-parity'
        if qa:
            command.extend(['--qa-capture-report', capture / 'validation.json'])
            for frame in preview:
                command.extend(['--present-frame', str(frame)])
        if baseline.get('frame_parity'):
            capture_log = capture / LOG
            log_hash = source.get('log_sha256')
            if (log_hash or match_geometry_start or window_resize or menu_cycle or captured_far_alignment) and digest(capture_log) != log_hash:
                raise ValueError('The frozen capture renderer log does not match its validation hash')
            capture_lines = log_lines(capture_log)
            if captured_far_alignment:
                verify_far_shadow_alignment(capture_lines, True)
            native_more_lights = bool(log_hash and captured_native_more_lights(capture_lines))
            env['NORTHLIGHT_RETRACE_NATIVE_MORE_LIGHTS'] = str(int(native_more_lights))
            frames = {int(frame): row['quality'] for frame, row in source.get('parity_frames', {}).items()}
            if (baseline['msaa'] != 2 or len(frames) != parity.FRAME_COUNT or
                    frames != parity.selected_frames(capture_lines) or not parity.stream_without_skips(capture_lines)):
                raise ValueError('Live capture has no certified 2× MSAA Present selection')
            if window_resize and resize_evidence(capture_lines, frames, window_resize['steps']) != window_resize:
                raise ValueError('The capture resize receipt differs from its renderer log')
            if menu_cycle:
                fixture_log = capture / 'server.log'
                logout = menu_cycle.get('after_world_log_line')
                lifecycle = menu_parity_lifecycle(capture_lines, frames)
                if (digest(fixture_log) != menu_cycle.get('fixture_log_sha256') or type(logout) is not int or
                        not 0 < logout < len(capture_lines) or
                        not observed_menu_return(capture_lines, log_lines(fixture_log), logout) or
                        any(menu_cycle.get(key) != value for key, value in lifecycle.items()) or
                        not (lifecycle['initial_menu'] and lifecycle['returned_to_menu'])):
                    raise ValueError('The capture menu cycle receipt differs from the closed logs')
            if requested is None:
                prepare_parity_images(images, getattr(args, 'raw_parity', False))
            present_frames = set(frames)
            if light_history_audit:
                history_frames = light_history_frames(frames, history_predecessors)
                if light_history_between:
                    history_frames = sorted(set(history_frames) | set(light_history_interval(frames)))
                present_frames.update(history_frames)
            if work_frame is not None:
                present_frames.add(work_frame)
            if match_geometry_start:
                audit_scene = audited_capture and light_history_audit
                world_checkpoints = observed_world_checkpoints(capture_lines, frames, audit_scene,
                                                               carry_cold=profile_fixed_work)
                checkpoint_frames = {frame for point in world_checkpoints for frame in (
                    point['scene'], point['geometry'] if point['geometry'] is not None else point['cold']) if frame is not None}
                for frame in sorted(checkpoint_frames):
                    command.extend(['--checkpoint-frame', str(frame)])
            for frame in sorted(present_frames):
                command.extend(['--present-frame', str(frame)])
        try:
            compile_exit = run_logged(command, run, env, 'compile-plan', args.timeout)
        finally:
            if not getattr(args, 'keep_work', False):
                discard_work(report, run, ('plan/trace.dump',))
        if compile_exit:
            raise ValueError('Strict trace/snapshot join refused; see compile-plan.stderr and plan/validation.json')
        joined = json.loads((plan / 'validation.json').read_text(encoding='utf-8'))
        if joined.get('pass') is not True or joined.get('authored_transport_only') is not False:
            raise ValueError('Compiler did not certify the genuine trace join')
        report['trace_join_validated'] = True
        report['trace_summary'] = joined['summary']
        replay_options = ['--query-handling=run']
        if not qa_visible:
            replay_options.append('--headless')
        if qa:
            if qa['last_present'] >= joined['summary']['presents']:
                raise ValueError('QA interval ends beyond the closed trace')
            preview_calls = joined['summary']['selected_presents']
            (run / 'preview').mkdir()
            # apitrace stops at the last requested snapshot. Keep replaying
            # through device retirement with a call number past the trace.
            terminal = (1 << 32) - 2
            if joined['summary']['final_release'] >= terminal:
                raise ValueError('QA trace exceeds the replay snapshot call range')
            calls = [preview_calls[str(frame)] for frame in preview] + [terminal]
            replay_options.extend(['--snapshot', ','.join(map(str, calls)), '--call-nos=true',
                                   '--snapshot-prefix', 'preview/present-'])
            report['qa_scene'] = {**qa, 'preview_trace_calls': preview_calls}
        if window_resize:
            if (joined['summary'].get('selected_present_epoch'), joined['summary'].get('selected_epoch_first_present')) != (
                    window_resize['selected_epoch'], window_resize['epoch_first_present']):
                raise ValueError('The trace join did not select the restored window Reset epoch')
            report['window_resize'] = window_resize
        env['NORTHLIGHT_RETRACE_PLAN'] = str(plan / 'scene.nljoin')
        if world_checkpoints:
            bound_checkpoints = certify_world_checkpoints(world_checkpoints, joined['summary'], source['log_sha256'])
            env['NORTHLIGHT_RETRACE_WORLD_CHECKPOINTS'] = ';'.join(
                f"{point['first']}:{point['scene'] or 0}:{point['geometry'] or point['end']}:{point['end']}"
                for point in world_checkpoints)
        if legacy_normal_declarations is not None:
            report['stand_normal_declarations'] = 'legacy' if legacy_normal_declarations else 'tracked'
            env['NORTHLIGHT_RETRACE_LEGACY_NORMAL_DECLARATIONS'] = str(int(legacy_normal_declarations))
        if legacy_normal_pose_declaration is not None:
            report['stand_normal_pose_declaration'] = 'getter' if legacy_normal_pose_declaration else 'borrowed'
            env['NORTHLIGHT_RETRACE_LEGACY_NORMAL_POSE_DECLARATION'] = str(int(legacy_normal_pose_declaration))
        if legacy_normal_range_bank is not None:
            report['stand_normal_range_bank'] = 'mirror' if legacy_normal_range_bank else 'material-bank'
            env['NORTHLIGHT_RETRACE_LEGACY_NORMAL_RANGE_BANK'] = str(int(legacy_normal_range_bank))
        if legacy_shadow_projection_bank is not None:
            env['NORTHLIGHT_RETRACE_LEGACY_SHADOW_PROJECTION_BANK'] = str(int(legacy_shadow_projection_bank))
        if legacy_shadow_packet_bank is not None:
            env['NORTHLIGHT_RETRACE_LEGACY_SHADOW_PACKET_BANK'] = str(int(legacy_shadow_packet_bank))
        if legacy_copy_admission is not None:
            report['stand_copy_admission'] = 'two-frames' if legacy_copy_admission else 'requested-span'
            env['NORTHLIGHT_RETRACE_LEGACY_COPY_ADMISSION'] = str(int(legacy_copy_admission))
        if legacy_event_spin is not None:
            report['stand_event_spin'] = 'legacy' if legacy_event_spin else 'early-timer'
            env['NORTHLIGHT_RETRACE_LEGACY_EVENT_SPIN'] = str(int(legacy_event_spin))
        if legacy_directional_terrain is not None:
            report['stand_directional_terrain'] = 'legacy' if legacy_directional_terrain else 'batched'
            env['NORTHLIGHT_RETRACE_LEGACY_DIRECTIONAL_TERRAIN'] = str(int(legacy_directional_terrain))
        if legacy_directional_replay is not None:
            report['stand_directional_replay'] = 'legacy' if legacy_directional_replay else 'grouped'
            env['NORTHLIGHT_RETRACE_LEGACY_DIRECTIONAL_REPLAY'] = str(int(legacy_directional_replay))
        if legacy_actor_inset is not None:
            report['stand_actor_inset'] = 'corners' if legacy_actor_inset else 'extremal'
            env['NORTHLIGHT_RETRACE_LEGACY_ACTOR_INSET'] = str(int(legacy_actor_inset))
        if directional_index_pairs is not None:
            if legacy_directional_replay:
                raise ValueError('Directional index pair comparison requires grouped replay')
            report['stand_directional_index_pairs'] = directional_index_pairs
            env['NORTHLIGHT_RETRACE_DIRECTIONAL_INDEX_PAIRS'] = str(int(directional_index_pairs))
        if point_replay_state_reuse is not None:
            report['stand_point_replay_state_reuse'] = point_replay_state_reuse
            env['NORTHLIGHT_RETRACE_POINT_REPLAY_STATE_REUSE'] = str(int(point_replay_state_reuse))
        if point_index_batches is not None:
            report['stand_point_index_batches'] = point_index_batches
            env['NORTHLIGHT_RETRACE_POINT_INDEX_BATCHES'] = str(int(point_index_batches))
        if point_index_admission_reuse is not None:
            report['stand_point_index_admission_reuse'] = point_index_admission_reuse
            env['NORTHLIGHT_RETRACE_POINT_INDEX_ADMISSION_REUSE'] = str(int(point_index_admission_reuse))
        if point_effective_pose is not None:
            report['stand_point_effective_pose'] = point_effective_pose
            env['NORTHLIGHT_RETRACE_POINT_EFFECTIVE_POSE'] = str(int(point_effective_pose))
        if point_mixed_reuse is not None:
            report['stand_point_mixed_reuse'] = point_mixed_reuse
            env['NORTHLIGHT_RETRACE_POINT_MIXED_SUBSET'] = str(int(point_mixed_reuse))
        if normal_palette_instancing is not None:
            report['stand_normal_palette_instancing'] = normal_palette_instancing
            env['NORTHLIGHT_RETRACE_NO_NORMAL_PALETTE'] = str(int(not normal_palette_instancing))
        if normal_palette_binding_reuse is not None:
            report['stand_normal_palette_binding_reuse'] = normal_palette_binding_reuse
            env['NORTHLIGHT_RETRACE_NO_NORMAL_PALETTE_BINDING_REUSE'] = str(int(not normal_palette_binding_reuse))
        if normal_palette_instance_rows is not None:
            report['stand_normal_palette_instance_rows'] = normal_palette_instance_rows
            env['NORTHLIGHT_RETRACE_NORMAL_INSTANCE_ROWS'] = str(int(normal_palette_instance_rows))
        if crowd_bounds_operations is not None:
            report['stand_crowd_bounds_operations'] = crowd_bounds_operations
            env['NORTHLIGHT_RETRACE_CROWD_BOUNDS_OPERATIONS'] = str(int(crowd_bounds_operations))
        if point_adaptive_motion is not None:
            report['stand_point_adaptive_motion'] = point_adaptive_motion
            env['NORTHLIGHT_RETRACE_POINT_ADAPTIVE_MOTION'] = str(int(point_adaptive_motion))
        if requested is not None:
            if requested[-1] >= joined['summary']['presents'] - 2:
                raise ValueError('Leave two Presents after the last requested profile frame for timestamp polling')
            _, journal, packets, _, _ = snapshot_inputs(Path(source['sidecars'][0]))
            minute = baseline['fixed_world_minute']
            report['world_clock'] = world_clock(journal, packets, frames, minute)
            if report['world_clock'] != source.get('world_clock'):
                raise ValueError('Performance replay clock differs from the fixed live capture')
            report['profile_world_clock'] = world_clock_window(journal, packets, requested, minute)
            env['NORTHLIGHT_RETRACE_PROFILE_FRAMES'] = profile
            env['NORTHLIGHT_RETRACE_PROFILE_FIXED_WORK'] = str(int(profile_fixed_work))
            if fixed_visual_clock:
                env['NORTHLIGHT_FRAME_PARITY_CLOCK'] = '60'
            report['profile_visual_clock_hz'] = 60 if env.get('NORTHLIGHT_FRAME_PARITY_CLOCK') == '60' else 0
        elif frames:
            env.update(NORTHLIGHT_FRAME_PARITY_ROOT=str(images), NORTHLIGHT_FRAME_PARITY_FRAMES=','.join(map(str, frames)))
            if any(option is not None for option in (point_effective_pose, point_mixed_reuse,
                   normal_palette_instancing, normal_palette_binding_reuse, normal_palette_instance_rows)):
                presents = sorted(frames)
                step = math.gcd(*(frame - presents[0] for frame in presents)) or 1
                env['NORTHLIGHT_RETRACE_PROFILE_FRAMES'] = f'{presents[0]}:{presents[-1]}:{step}'
            if fixed_visual_clock:
                env['NORTHLIGHT_FRAME_PARITY_CLOCK'] = '60'
            if align_far_shadows:
                env['NORTHLIGHT_FRAME_PARITY_ALIGN_FAR_SHADOWS'] = '1'
            if light_history_audit:
                env['NORTHLIGHT_FRAME_PARITY_LIGHT_HISTORY'] = '1'
                env['NORTHLIGHT_FRAME_PARITY_HISTORY_PREDECESSORS'] = str(history_predecessors)
                if light_history_between:
                    env['NORTHLIGHT_FRAME_PARITY_LIGHT_HISTORY_BETWEEN'] = '1'
                if 'point_inputs' in source:
                    env['NORTHLIGHT_FRAME_PARITY_POINT_INPUTS'] = '1'
            if fixed_static_stream:
                env['NORTHLIGHT_FRAME_PARITY_STATIC_STREAM'] = '1'
            if fixed_shadow_bounds:
                env['NORTHLIGHT_FRAME_PARITY_SHADOW_BOUNDS'] = '1'
            if fixed_gi:
                env['NORTHLIGHT_FRAME_PARITY_GI_PUBLICATION'] = '1'
        if work_frame is not None:
            if str(work_frame) not in joined['summary']['selected_presents']:
                raise ValueError('Capture work requires a certified World Present from this capture')
            env['NORTHLIGHT_RETRACE_CAPTURE_WORK_FRAME'] = str(work_frame)
        started = time.perf_counter()
        report['host_exit_code'] = run_logged([exe, *replay_options, trace], run, env,
                                              'retrace', args.timeout)
        if requested is not None:
            report['host_seconds'] = time.perf_counter() - started
        lines = log_lines(run / 'retrace.stderr')
        report['host_result'] = next((line for line in lines if line.startswith('NORTHLIGHT_NATIVE_BRIDGE PASS ')), None)
        stand_log = log_lines(stand_root / LOG)
        if frames:
            lighting = [line for line in stand_log if line.startswith('NATIVE more lights replay active=')]
            if native_more_lights and lighting != ['NATIVE more lights replay active=1']:
                raise ValueError('Replay did not activate the captured native light shader path')
            if lighting:
                if lighting != [f'NATIVE more lights replay active={int(native_more_lights)}']:
                    raise ValueError('Replay native light shader activation differs from the capture')
                report['stand_native_more_lights'] = native_more_lights
        if fixed_gpu_budget:
            verify_fixed_gpu_budget(stand_log)
        if env.get(DXVK_ADAPTER_ENV):
            report['dxvk_adapter'] = dxvk_adapter(run / DXVK_LOG, env[DXVK_ADAPTER_ENV])
            if captured_adapter and report['dxvk_adapter']['device'] != captured_adapter['device']:
                raise ValueError('Replay and live capture chose different DXVK devices')
        if world_checkpoints:
            verify_world_checkpoints(stand_log, frames, world_checkpoints, carry_cold=profile_fixed_work)
            report['stand_world_checkpoints'] = bound_checkpoints
            last = bound_checkpoints[-1]
            report['stand_geometry_checkpoint'] = {'present': last['geometry'], 'trace_call': last['trace_calls']['geometry'],
                                                   'capture_log_sha256': source['log_sha256']}
            if last['epoch']:
                report['stand_geometry_checkpoint'].update(epoch=last['epoch'], epoch_first_present=last['first'])
            if last['scene'] is not None:
                report['stand_world_scene_checkpoint'] = {'present': last['scene'], 'trace_call': last['trace_calls']['scene'],
                                                          'capture_log_sha256': source['log_sha256']}
        if legacy_shadow_projection_bank is not None:
            policies = [line for line in stand_log if line.startswith('SHADOW_CAPTURE_PROJECTION policy ')]
            if policies != [f'SHADOW_CAPTURE_PROJECTION policy legacy={int(legacy_shadow_projection_bank)}']:
                raise ValueError('Replay did not load the selected shadow capture projection policy')
            report['stand_shadow_projection_bank'] = 'getter' if legacy_shadow_projection_bank else 'material-bank'
        if legacy_shadow_packet_bank is not None:
            policies = [line for line in stand_log if line.startswith('SHADOW_CAPTURE_PACKET_BANK policy ')]
            if policies != [f'SHADOW_CAPTURE_PACKET_BANK policy legacy={int(legacy_shadow_packet_bank)}']:
                raise ValueError('Replay did not load the selected shadow packet bank policy')
            report['stand_shadow_packet_bank'] = 'getter' if legacy_shadow_packet_bank else 'material-bank'
        if legacy_copy_admission is not None:
            policies = [line for line in stand_log if line.startswith('REPLAY COPY policy ')]
            if policies != [f'REPLAY COPY policy legacy={int(legacy_copy_admission)}']:
                raise ValueError('Replay did not load the selected VB/IB copy admission policy')
        if point_index_batches is not None:
            replay_rows = [line for line in stand_log if line.startswith('POINT_INDEX_BATCHES ')]
            expected_batches = f'enabled={int(point_index_batches)}'
            if not replay_rows or any(expected_batches not in line.split() for line in replay_rows):
                raise ValueError('The stand did not record the selected point index batch path')
        if point_index_admission_reuse is not None:
            replay_rows = [line for line in stand_log if line.startswith('POINT_INDEX_ADMISSION ')]
            expected_reuse = f'reuse={int(point_index_admission_reuse)}'
            if not replay_rows or any(expected_reuse not in line.split() for line in replay_rows):
                raise ValueError('The stand did not record the selected point index admission policy')
        if point_effective_pose is not None:
            replay_rows = [line for line in stand_log if line.startswith('POINT_EFFECTIVE_POSE ')]
            expected_pose = f'enabled={int(point_effective_pose)}'
            if not replay_rows or any(expected_pose not in line.split() for line in replay_rows):
                raise ValueError('The stand did not record the selected point pose comparison')
            pose_frames = requested if requested is not None else frames
            report['point_effective_pose_samples'] = {str(frame): int(row['POINT_EFFECTIVE_POSE']['merged'])
                for frame, row in stand_profile(replay_rows, pose_frames).items() if 'POINT_EFFECTIVE_POSE' in row}
        if point_mixed_reuse is not None:
            section = 'POINT_MIXED_BAKE'
            rows = [line for line in stand_log if line.startswith(section + ' ')]
            expected = f'enabled={int(point_mixed_reuse)}'
            if not rows or any(expected not in line.split() for line in rows):
                raise ValueError('The stand did not record the selected mixed point face policy')
            samples = requested if requested is not None else frames
            report['point_mixed_reuse_samples'] = {str(frame): row[section]
                for frame, row in stand_profile(rows, samples).items() if section in row}
        if normal_palette_instancing is not None or normal_palette_binding_reuse is not None:
            section = 'MODEL_NORMALS palette instances'
            rows = [line for line in stand_log if line.startswith(section + ' ')]
            for key, value in (('enabled', normal_palette_instancing), ('retainBindings', normal_palette_binding_reuse)):
                if value is not None and (not rows or any(f'{key}={int(value)}' not in line.split() for line in rows)):
                    raise ValueError(f'The stand did not record ModelNormals {key}={int(value)}')
            normal_frames = requested if requested is not None else frames
            fields = ('draws', 'packets')
            if normal_palette_binding_reuse is not None:
                fields += ('inheritedDraws', 'skippedBinds')
            report['normal_palette_samples'] = {str(frame): {key: int(row[section][key]) for key in fields}
                for frame, row in stand_profile(rows, normal_frames).items() if section in row}
        if normal_palette_instance_rows is not None:
            section = 'MODEL_NORMALS lifted rows'
            rows = [line for line in stand_log if line.startswith(section + ' ')]
            expected = f'enabled={int(normal_palette_instance_rows)}'
            if not rows or any(expected not in line.split() for line in rows):
                raise ValueError('The stand did not record the selected ModelNormals instance rows policy')
            normal_frames = requested if requested is not None else frames
            report['normal_palette_instance_row_samples'] = {str(frame): row[section]
                for frame, row in stand_profile(rows, normal_frames).items() if section in row}
        if crowd_bounds_operations is not None:
            replay_rows = [line for line in stand_log if line.startswith('POINT workload ')]
            expected_operations = f'crowdOperations={int(crowd_bounds_operations)}'
            if not replay_rows or any(expected_operations not in line.split() for line in replay_rows):
                raise ValueError('The stand did not record the selected replay bounds operation budget')
        if point_adaptive_motion is not None:
            rows = [line for line in stand_log if line.startswith('SHADOW_REFRESH ')]
            expected = f'adaptiveMotion={int(point_adaptive_motion)}'
            if not rows or any(expected not in line.split() for line in rows):
                raise ValueError('The stand did not record the selected adaptive point shadow path')
        if point_replay_state_reuse is not None:
            replay_rows = [line for line in stand_log if line.startswith('POINT_REPLAY_STATE ')]
            expected_reuse = f'reuse={int(point_replay_state_reuse)}'
            if not replay_rows or any(expected_reuse not in line.split() for line in replay_rows):
                raise ValueError('The stand did not record the selected point replay state path')
        if directional_index_pairs is not None:
            replay_rows = [line for line in stand_log if line.startswith('WORLD directional index pairs ')]
            expected_pairs = f'enabled={int(directional_index_pairs)}'
            if not replay_rows or any(expected_pairs not in line.split() for line in replay_rows):
                raise ValueError('The stand did not record the selected directional index pair mode')
        if legacy_directional_replay is not None:
            replay_rows = [line for line in stand_log if line.startswith('WORLD directional replay ')]
            expected_replay = f'legacy={int(legacy_directional_replay)}'
            if not replay_rows or any(expected_replay not in line.split() or 'fallbacks=0' not in line.split() for line in replay_rows):
                raise ValueError('The stand did not record the selected directional replay path without fallbacks')
        if legacy_actor_inset is not None:
            replay_rows = [line for line in stand_log if line.startswith('WORLD directional replay ')]
            expected_inset = f'legacyActorInset={int(legacy_actor_inset)}'
            if not replay_rows or any(expected_inset not in line.split() for line in replay_rows):
                raise ValueError('The stand did not record the selected actor inset projection')
        if legacy_directional_terrain is not None:
            terrain_rows = [line for line in stand_log if line.startswith('WORLD directional terrain ')]
            expected_terrain = f'legacy={int(legacy_directional_terrain)}'
            if not terrain_rows or any(expected_terrain not in line.split() for line in terrain_rows):
                raise ValueError('The stand did not record the selected directional terrain path')
        if legacy_event_spin is not None:
            policy_rows = [line for line in stand_log if line.startswith('CSTREAM EVENT_CPU ')]
            expected_policy = f'legacyGpuSpin={int(legacy_event_spin)}'
            if not policy_rows or any(expected_policy not in line.split() for line in policy_rows):
                raise ValueError('The stand did not record the selected EVENT wait policy')
        if 'stand_quality' in report:
            loaded = [line for line in stand_log if line.startswith('QUALITY preset=')]
            fixed_budget = 0 if profile_fixed_work else None
            for key, value in (('PointShadowLimit', shadow_limit), ('PointShadowAdaptiveRefresh', shadow_adaptive),
                               ('AutoBumpModels', model_bump), ('AutoNormalFilter', normal_filter),
                               ('GIDynamicProbes', gi_dynamic_probes), ('ShadowRefreshCpuMs', shadow_refresh_cpu_ms),
                               ('FarShadowInterval', far_shadow_interval), ('WorldShadows', world_shadows),
                               ('WorldFog', world_fog), ('GpuBudgetMs', fixed_budget), ('ActorShadowCpuMs', fixed_budget)):
                if value is None:
                    continue
                expected = f'{key}={int(value)}(file)'
                if not loaded or any(expected not in line.split() for line in loaded):
                    raise ValueError(f'Replay did not load the requested {key} from its private settings')
        observation = scan(stand_log, fixture.spawn_for(source.get('scene', 'solo')))
        report['stand_identity'] = observation['identity']
        report['stand_world_observed'] = observation['world_seen']
        report['stand_static_mesh_ready'] = bool(observation['static_tick'])
        report['stand_output'] = measured_output(observation, output_mode)
        report['stand_hdr_state'] = observation.get('hdr_state')
        report['stand_camera'] = observation['camera']
        if report['host_exit_code'] or not report['host_result']:
            raise ValueError('The Northlight bridge did not finish genuine transport; see retrace.stderr')
        if qa:
            previews = {str(frame): Path(f'preview/present-{preview_calls[str(frame)]:010d}.png') for frame in preview}
            if any(not (run / path).is_file() or not (run / path).stat().st_size for path in previews.values()):
                raise ValueError('Replay did not produce all requested QA preview images')
            report['qa_scene']['previews'] = {frame: path.as_posix() for frame, path in previews.items()}
        elif not report['stand_world_observed'] or not report['stand_static_mesh_ready']:
            raise ValueError('Northrend GI and static mesh readiness were not observed on the stand')
        if output_mode == 'hdr' and observation.get('hdr_active') is False:
            report['stand_sdr_output'] = observation.get('sdr_output')
            state = observation['hdr_state']
            raise ValueError(f"Replay HDR is inactive: {state.get('reason', 'unknown')} ({state.get('status', 'unknown')})")
        if profile_sdr and observation.get('hdr_state', {}).get('requested') != '0':
            raise ValueError('Replay did not load ExperimentalHDR=0 from its private settings')
        if report['stand_output'] != {key: baseline[key] for key in ('resolution', 'msaa')}:
            raise ValueError('Replay used a different output size or MSAA mode')
        source_camera = source['observation']['camera']
        if not qa and (not observation['camera'] or any(math.dist(source_camera[key], observation['camera'][key]) > tolerance
                                                       for key, tolerance in (('eye', .25), ('forward', .002)))):
            raise ValueError('Replay camera differs from the captured stationary scene')
        if requested is not None:
            if not parity.stream_without_skips(stand_log):
                raise ValueError('Performance replay requires the unskipped command stream')
            if observation.get('assets') != report['stand_assets_sha256']:
                raise ValueError('Performance replay did not load the selected shader bundle')
            if profile_fixed_work:
                modes = {line for line in stand_log if line.startswith('STAND_PROFILE fixedWork=')}
                if modes != {'STAND_PROFILE fixedWork=1'}:
                    raise ValueError('The stand did not confirm fixed profiling work')
                report['stand_profile_fixed_work'] = True
            report['profile_frames'] = stand_profile(stand_log, requested)
            if any(not {'GPU frame', 'GPU profile', 'CPU profile'} <= rows.keys() for rows in report['profile_frames'].values()):
                raise ValueError('At least one requested Present has no complete CPU/GPU profile')
            if profile_fixed_work and any(
                    not (coverage := rows.get('MODEL_NORMALS coverage')) or coverage['packets'] != coverage['draws']
                    for rows in report['profile_frames'].values()):
                raise ValueError('At least one requested Present has no complete ModelNormals coverage receipt')
            if legacy_normal_declarations is not None and any(
                    rows.get('MODEL_NORMALS replay', {}).get('legacy') != int(legacy_normal_declarations)
                    for rows in report['profile_frames'].values()):
                raise ValueError('At least one requested Present did not use the selected model-normal declaration path')
            if legacy_normal_pose_declaration is not None and any(
                    rows.get('MODEL_NORMALS pose layout', {}).get('legacy') != int(legacy_normal_pose_declaration)
                    for rows in report['profile_frames'].values()):
                raise ValueError('At least one requested Present has no evidence for the selected normal pose declaration path')
            if legacy_normal_range_bank is not None and any(
                    rows.get('MODEL_NORMALS range bank', {}).get('legacy') != int(legacy_normal_range_bank)
                    for rows in report['profile_frames'].values()):
                raise ValueError('At least one requested Present has no evidence for the selected normal range bank path')
            if legacy_copy_admission is not None and any(
                    rows.get('REPLAY COPY work', {}).get('legacy') != int(legacy_copy_admission)
                    for rows in report['profile_frames'].values()):
                raise ValueError('At least one requested Present has no evidence for the selected VB/IB copy admission policy')
            if legacy_event_spin is not None and not any('CSTREAM EVENT_CPU' in rows for rows in report['profile_frames'].values()):
                raise ValueError('The selected frame window has no measured EVENT wait')
            if shadow_audit:
                report['profile_timing_eligible'] = False
                report['point_batch_census'] = point_shadows.census(stand_log, requested)
        elif frames:
            if not parity.stream_without_skips(stand_log) or parity.selected_frames(stand_log) != frames:
                raise ValueError('Stand did not export the same 2× MSAA Presents without skipping')
            verify_far_shadow_alignment(stand_log, align_far_shadows)
            if align_far_shadows:
                report['stand_far_shadow_alignment'] = 'present-boundary'
            if fixed_visual_clock:
                report['stand_visual_clock'] = visual_clock(stand_log, frames)
            report['visual_clock_matches_capture'] = report.get('stand_visual_clock') == source.get('visual_clock')
            if 'fixed_world_minute' in baseline:
                _, journal, packets, _, _ = snapshot_inputs(Path(source['sidecars'][0]))
                report['world_clock'] = world_clock(journal, packets, frames, baseline['fixed_world_minute'])
                if report['world_clock'] != source.get('world_clock'):
                    raise ValueError('Joined replay world clock differs from the fixed live capture')
            if work_frame is not None:
                work = capture_work.census(stand_log, work_frame)
                work['trace_sha256'] = source['trace']['sha256']
                work['stand_identity'] = report['stand_identity']
                work['present_trace_call'] = joined['summary']['selected_presents'][str(work_frame)]
                work['world_clock'] = world_clock_window(journal, packets, (work_frame,), baseline['fixed_world_minute'])
                copy_memory = stand_profile(stand_log, (work_frame,))[work_frame].get('REPLAY COPY memory')
                if copy_memory is not None:
                    work['replay_copy_memory'] = {key: int(value) for key, value in copy_memory.items()}
                write_json(run / 'capture-work.json', work)
                report['capture_work'] = work['summary']
            size = tuple(map(int, baseline['resolution'].split('x')))
            parity_calls = {str(frame): joined['summary']['selected_presents'][str(frame)] for frame in frames}
            comparison = parity.compare(capture / 'frame-parity', images, size, frames, parity_calls)
            for row in comparison['frames']:
                frozen = source['parity_frames'][str(row['present_ordinal'])]
                if any(row[name]['live_sha256'] != frozen['sha256'][name] for name in parity.PLANES):
                    raise ValueError('Live parity images changed after capture')
                for name in ('native_color', 'scene_input_color'):
                    if name in frozen and row[name]['live'] != frozen[name]:
                        raise ValueError(f"Live {name.replace('_', ' ')} image changed after capture at Present {row['present_ordinal']}")
                    row[name]['live_evidence_frozen'] = name in frozen
                if 'composition' in frozen and frozen['composition'] != {
                        name: product['live'] for name, product in row['composition'].items()}:
                    raise ValueError(f"Live composition products changed after capture at Present {row['present_ordinal']}")
                row['composition_live_evidence_frozen'] = 'composition' in frozen
            report['parity'] = comparison
            report['color_depth_parity'] = 'match' if comparison['match'] else 'mismatch'
            if source.get('format', 1) >= 2:
                report['world_normal_parity'] = parity.compare_world_normals(
                    capture / 'frame-parity', images, size, frames, source.get('world_normal_hashes', {}))
            if legacy_actor_inset is not None:
                report['stand_actor_depth_hashes'] = parity.directional_depth_hashes(images, frames, ('shadow-actor',))
            if fixed_static_stream:
                live_static = parity.static_shadow_stream(capture_lines, frames)
                if live_static != source['fixed_static_shadow_stream']:
                    raise ValueError('Static shadow capture evidence differs from the live report')
                stand_static = parity.static_shadow_stream(stand_log, frames)
                report['fixed_static_shadow_stream'] = {'live': live_static, 'stand': stand_static,
                                                        'match': live_static == stand_static}
                report['directional_depth_comparison'] = parity.compare_directional_depth(
                    capture / 'frame-parity', images, frames, source.get('directional_depth_hashes', {}),
                    allow_skipped=light_history_between)
                if any(row.get('state') == 'different-use' for frame in report['directional_depth_comparison'].values()
                       for row in frame.values()):
                    raise ValueError('Live WoW and the replay stand used different directional shadow fallbacks')
            if fixed_gi:
                live_gi = parity.fixed_gi_publication(capture_lines, max(frames))
                if live_gi != source['fixed_gi_publication']:
                    raise ValueError('Fixed GI publication evidence differs from the live report')
                report['fixed_gi_publication'] = {'live': live_gi, 'stand': parity.fixed_gi_publication(stand_log, max(frames))}
            if fixed_shadow_bounds:
                stand_bounds = parity.shadow_bounds_work(stand_log, frames)
                live_bounds = parity.shadow_bounds_work(capture_lines, frames) if captured_fixed_bounds else None
                if captured_fixed_bounds and live_bounds != source['shadow_bounds_work']:
                    raise ValueError('Shadow bounds evidence differs from the live report')
                report['shadow_bounds_work'] = {'live': live_bounds, 'stand': stand_bounds,
                                                'match': live_bounds == stand_bounds if captured_fixed_bounds else None}
                if captured_fixed_bounds:
                    report['shadow_bounds_work']['timeline'] = parity.compare_shadow_bounds_timeline(capture_lines, stand_log)
                    live_actor = parity.actor_shadow_work(capture_lines)
                    stand_actor = parity.actor_shadow_work(stand_log)
                    if live_actor or stand_actor:
                        report['actor_shadow_routing'] = parity.compare_present_work(live_actor, stand_actor)
            if any(value is not None for value in (legacy_normal_declarations, legacy_normal_pose_declaration,
                                                    normal_palette_instancing, normal_palette_binding_reuse,
                                                    normal_palette_instance_rows)):
                report['normal_products'] = parity.normal_hashes(images, size, frames)
            if shadow_audit:
                report['point_cubes'] = point_shadows.cube_products(images, frames)
            if light_history_audit:
                report['light_history_trace_calls'] = {str(frame): joined['summary']['selected_presents'][str(frame)]
                                                       for frame in history_frames}
                report.update(parity.lighting_evidence(images, size, history_frames))
                if 'hdr_boundary_images' in source:
                    report['hdr_boundary_comparison'] = parity.compare_hdr_boundaries(
                        (capture / 'frame-parity', images), max(frames), size, source['hdr_boundary_images'])
                if audited_capture:
                    compare_frames = light_history_interval(frames) if light_history_between else frames
                    report['live_lighting_comparison'] = parity.compare_live_lighting(
                        (capture / 'frame-parity', images), size, compare_frames, source, report)
                    inputs = parity.compare_local_light_inputs(capture_lines, stand_log, frames)
                    world_start = min((point['scene'] or point['first']
                                       for point in report.get('stand_world_checkpoints', ())), default=0)
                    gi_work = parity.compare_gi_timeline(capture_lines, stand_log, world_start, max(frames))
                    if inputs is not None or gi_work is not None:
                        if digest(capture_log) != source.get('log_sha256'):
                            raise ValueError('The lighting input capture log does not match its validation hash')
                    if inputs is not None:
                        report['live_local_light_inputs'] = inputs
                    if gi_work is not None:
                        report['gi_actor_atlas_timeline'] = gi_work
                if 'point_inputs' in source:
                    report['point_inputs'] = point_shadows.sampled_products(images, size, frames)[0]
                    report['live_point_comparison'] = point_shadows.compare_sampled(
                        (capture / 'frame-parity', images), size, frames, (source['point_inputs'], report['point_inputs']))
            if menu_cycle:
                menu_transitions = menu_parity_lifecycle(stand_log, frames)
                report['stand_menu_cycle'] = menu_transitions
            if not report.get('world_normal_parity', {}).get('match', True):
                report['status'] = 'FAIL'
                raise ValueError('Live WoW and the replay stand produced different world normals')
            if not comparison['match']:
                report['status'] = 'FAIL'
                raise ValueError('Live WoW and the replay stand produced different resolved pixels')
            if not report['visual_clock_matches_capture']:
                raise ValueError('Live capture and replay used different visual clocks')
            if menu_cycle and not (menu_transitions['initial_menu'] and menu_transitions['returned_to_menu']):
                report['status'] = 'FAIL'
                raise ValueError('Replay did not render the menu before and after the World parity frames')
        report['status'] = 'PASS'
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        report['error'] = str(error)
    finally:
        if not getattr(args, 'keep_work', False):
            discard_work(report, run, (*RUN_SCRATCH, 'stand/world-cache', 'stand/Cache'))
        if report.get('scratch_cleanup_errors') and report['status'] == 'PASS':
            report['status'] = 'INCOMPLETE'
            report['error'] = 'Could not retire replay scratch files'
        write_json(run / 'validation.json', report)
    print(f'{report["status"]}: {run / "validation.json"}')
    return 0 if report['status'] == 'PASS' else 1


def compare_replays(args):
    reference, candidate = args.reference.resolve(), args.candidate.resolve()
    output = args.output.resolve()
    report = {'format': 1, 'kind': 'same-trace-replay-comparison', 'status': 'INCOMPLETE', 'fps_eligible': False}
    try:
        if reference == candidate:
            raise ValueError('Use two independent replay outputs')
        folders = (reference, candidate)
        paths = [folder / 'validation.json' for folder in folders]
        hashes = [digest(path) for path in paths]
        # The bounded history window can contain full GI and projection
        # manifests for each of the audited Presents.
        inputs = [load_json(path, limit=1 << 20) for path in paths]
        left, right = inputs
        if any(item['status'] not in ('PASS', 'FAIL') or item['host_exit_code'] != 0 or
               not (item['host_result'] or '').startswith('NORTHLIGHT_NATIVE_BRIDGE PASS ') or
               any(item[key] is not True for key in ('trace_join_validated', 'stand_world_observed', 'stand_static_mesh_ready'))
               for item in inputs):
            raise ValueError('Both replays must have certified trace and world execution')
        if left['capture'] != right['capture']:
            raise ValueError('Both replays must use the same live capture')
        if any(left[key] != right[key] for key in ('host_sha256', 'stand_assets_sha256', 'stand_output')):
            raise ValueError('The retrace host, shader assets and output mode must match')
        mode = left['stand_output']
        if mode['msaa'] != 2:
            raise ValueError('Both replays must use the 2× MSAA parity profile')
        size = tuple(map(int, mode['resolution'].split('x')))
        if len(size) != 2:
            raise ValueError('Invalid replay resolution')
        capture = Path(left['capture'])
        live = load_json(capture / 'validation.json')
        trace = capture / 'wow.trace'
        trace_hash = live['trace']['sha256']
        if live['status'] != 'PASS' or digest(trace) != trace_hash:
            raise ValueError('The live capture is incomplete or its trace changed')
        frames = {int(frame): row['quality'] for frame, row in live['parity_frames'].items()}
        selected = tuple(item['trace_summary']['selected_presents'] for item in inputs)
        calls = {str(frame): selected[0][str(frame)] for frame in frames}
        if len(frames) != parity.FRAME_COUNT or any(selected[1].get(key) != call for key, call in calls.items()):
            raise ValueError('Both replays must certify the same two Present calls')
        checkpoints = [item.get('stand_geometry_checkpoint') for item in inputs]
        if any(checkpoint is not None for checkpoint in checkpoints):
            capture_log = capture / LOG
            log_hash = digest(capture_log)
            if log_hash != live.get('log_sha256'):
                raise ValueError('The frozen capture geometry log does not match its validation hash')
            capture_lines = log_lines(capture_log)
            epoch, epoch_first = observed_reset_epoch(capture_lines, frames)
            modern = any('stand_world_checkpoints' in item for item in inputs)
            schedule = observed_world_checkpoints(capture_lines, frames, 'light_history_inputs' in live) if modern else None
            present = schedule[-1]['geometry'] if schedule else observed_geometry_start(capture_lines, frames, epoch_first)
            for checkpoint, item in strict_zip(checkpoints, inputs):
                if checkpoint is None:
                    continue
                summary = item['trace_summary']
                call = summary['selected_presents'].get(str(present))
                if 'stand_world_checkpoints' in item:
                    if item['stand_world_checkpoints'] != certify_world_checkpoints(schedule, summary, log_hash):
                        raise ValueError('Replay World checkpoint timeline differs from the authenticated capture')
                    call = item['stand_world_checkpoints'][-1]['trace_calls']['geometry']
                expected = {'present': present, 'trace_call': call, 'capture_log_sha256': log_hash}
                if epoch:
                    expected.update(epoch=epoch, epoch_first_present=epoch_first)
                    if (summary.get('selected_present_epoch'), summary.get('selected_epoch_first_present')) != (epoch, epoch_first):
                        raise ValueError('Replay geometry checkpoint differs from the authenticated Reset epoch')
                if call is None or checkpoint != expected:
                    raise ValueError('Replay geometry checkpoint differs from the authenticated capture Present')
        saved = []
        for folder, item in strict_zip(folders, inputs):
            plan = load_json(folder / 'plan/validation.json', limit=32 << 20)
            if (plan['pass'] is not True or plan['authored_transport_only'] is not False or
                    plan['inputs'].get(str(trace)) != trace_hash or plan['summary'] != item['trace_summary'] or
                    plan['inputs'].get(str(folder / 'stand/northlight-stand.dll')) != item['stand_sha256']):
                raise ValueError(f'Replay plan does not certify the captured trace: {folder}')
            evidence = {row['present_ordinal']: row for row in item['parity']['frames']}
            if len(evidence) != len(item['parity']['frames']) or evidence.keys() != frames.keys() or any(
                    row['trace_call'] != calls.get(str(frame)) or row['sample_quality'] != frames[frame]
                    for frame, row in evidence.items()):
                raise ValueError(f'Replay report does not bind the selected images: {folder}')
            saved.append(evidence)
        comparison = parity.compare(reference / 'frame-parity', candidate / 'frame-parity', size, frames, calls)
        for row in comparison['frames']:
            frame = row['present_ordinal']
            for name in parity.PLANES:
                plane = row[name]
                for label, key, evidence in strict_zip(('reference', 'candidate'), ('live_sha256', 'stand_sha256'), saved):
                    actual = plane.pop(key)
                    if actual != evidence[frame][name]['stand_sha256']:
                        raise ValueError(f'Replay image changed after validation: {label} Present {frame} {name}')
                    plane[f'{label}_sha256'] = actual
            for name in ('native_color', 'scene_input_color'):
                color = row[name]
                for label, key, evidence in strict_zip(('reference', 'candidate'), ('live', 'stand'), saved):
                    actual = color.pop(key)
                    frozen = evidence[frame].get(name)
                    if frozen is not None and actual != frozen['stand']:
                        raise ValueError(f"Replay {name.replace('_', ' ')} image changed after validation: {label} Present {frame}")
                    color[label] = actual
            for label, key, evidence in strict_zip(('reference', 'candidate'), ('live', 'stand'), saved):
                frozen = evidence[frame].get('composition')
                for name, product in row['composition'].items():
                    actual = product.pop(key)
                    if frozen is not None and actual != frozen.get(name, {}).get('stand'):
                        raise ValueError(f'Replay {name} product changed after validation: {label} Present {frame}')
                    product[label] = actual
        if hashes != [digest(path) for path in paths]:
            raise ValueError('Replay reports changed during the comparison')
        history_audits = [item.get('light_history_inputs') for item in inputs]
        history_frames = frames
        if any(audit is not None for audit in history_audits):
            if any(audit is None for audit in history_audits):
                raise ValueError('Compare light history inputs with the audit enabled in both runs')
            history_calls = [item.get('light_history_trace_calls', calls) for item in inputs]
            if history_calls[0] != history_calls[1] or any(
                    not isinstance(window, dict) or not calls.keys() <= window.keys() or window.keys() != audit.keys() or
                    any(frame not in presents or presents[frame] != call for frame, call in window.items())
                    for window, audit, presents in strict_zip(history_calls, history_audits, selected)):
                raise ValueError('Light history inputs do not certify the same Present calls')
            history_frames = sorted(map(int, history_calls[0]))
            report['light_history_trace_calls'] = history_calls[0]
            report['light_history_inputs'], report['light_history_overlap_with_raw'] = parity.compare_history(
                tuple(folder / 'frame-parity' for folder in folders), size, history_frames, history_audits)
        projection_audits = [item.get('directional_inputs') for item in inputs]
        if any(audit is not None for audit in projection_audits):
            if any(audit is None for audit in projection_audits):
                raise ValueError('Compare directional projection evidence with the capture present in both runs')
            report['directional_inputs'] = parity.compare_directional(
                tuple(folder / 'frame-parity' for folder in folders), size, history_frames, projection_audits)
        gi_audits = [item.get('gi_inputs') for item in inputs]
        if any(audit is not None for audit in gi_audits):
            if any(audit is None for audit in gi_audits):
                raise ValueError('Compare GI evidence with the capture present in both runs')
            report['gi_inputs'] = parity.compare_gi(
                tuple(folder / 'frame-parity' for folder in folders), size, history_frames, gi_audits)
        audit_cubes = ['point_cubes' in item for item in inputs]
        if any(audit_cubes) and not all(audit_cubes):
            raise ValueError('Compare point-shadow audit outputs with the same cube capture enabled')
        cube_match = True
        if all(audit_cubes):
            cube_inputs = [point_shadows.cube_products(folder / 'frame-parity', frames) for folder in folders]
            if any(cubes != item['point_cubes'] for cubes, item in strict_zip(cube_inputs, inputs)):
                raise ValueError('Point cube images changed after validation')
            cubes = point_shadows.compare_cubes(*cube_inputs)
            different = any(any(row['different_faces'].values()) for row in cubes.values())
            incomplete = any(row['reference_only'] or row['candidate_only'] or row['incompatible_publications']
                             for row in cubes.values())
            cube_match = not (different or incomplete)
            report.update(point_cube_comparison=cubes, point_cube_status=(
                'mismatch' if different else 'incomplete' if incomplete else 'match'))
        audit_normals = ['normal_products' in item for item in inputs]
        if any(audit_normals) and not all(audit_normals):
            raise ValueError('Compare normal replay controls with both normal outputs certified')
        normal_match = True
        if all(audit_normals):
            normal_inputs = [parity.normal_hashes(folder / 'frame-parity', size, frames) for folder in folders]
            if any(products != item['normal_products'] for products, item in strict_zip(normal_inputs, inputs)):
                raise ValueError('Normal images changed after validation')
            before, after = normal_inputs
            normal_match = before == after
            report['normal_status'] = 'match' if normal_match else 'mismatch'
            report['normal_comparison'] = {frame: {
                name: {'reference_sha256': digest, 'candidate_sha256': after[frame][name],
                       'match': digest == after[frame][name]} for name, digest in products.items()}
                for frame, products in before.items()}
            report['normal_product_match'] = {
                name: all(products[name]['match'] for products in report['normal_comparison'].values())
                for name in next(iter(before.values()))
            }
        same_inputs = ('shadow_bounds_work' in left) == ('shadow_bounds_work' in right) and all(left.get(key) == right.get(key) for key in
                          ('stand_sha256', 'stand_quality', 'stand_normal_declarations', 'stand_normal_pose_declaration',
                           'stand_normal_palette_instancing', 'stand_normal_palette_binding_reuse', 'stand_normal_palette_instance_rows',
                           'stand_normal_range_bank', 'stand_shadow_projection_bank', 'stand_shadow_packet_bank',
                           'stand_copy_admission', 'stand_visual_clock',
                           'stand_geometry_checkpoint', 'stand_world_checkpoints', 'stand_far_shadow_alignment',
                           'stand_event_spin', 'stand_directional_terrain', 'stand_directional_replay',
                           'stand_directional_index_pairs', 'stand_point_replay_state_reuse', 'stand_point_index_batches',
                           'stand_point_index_admission_reuse', 'stand_point_effective_pose', 'stand_point_mixed_reuse',
                           'stand_crowd_bounds_operations', 'stand_point_adaptive_motion'))
        report.update(status='PASS' if comparison['match'] and cube_match and normal_match else 'FAIL', trace_sha256=trace_hash,
                      mode='A/A' if same_inputs else 'A/B', comparison=comparison)
        for label, path, sha, item in strict_zip(('reference', 'candidate'), paths, hashes, inputs):
            report[label] = {'path': str(path), 'sha256': sha, 'dll_sha256': item['stand_sha256'],
                             'identity': item['stand_identity']}
            if 'stand_quality' in item:
                report[label]['quality'] = item['stand_quality']
            if 'stand_normal_declarations' in item:
                report[label]['normal_declarations'] = item['stand_normal_declarations']
            if 'stand_normal_pose_declaration' in item:
                report[label]['normal_pose_declaration'] = item['stand_normal_pose_declaration']
            if 'stand_normal_palette_instancing' in item:
                report[label]['normal_palette_instancing'] = item['stand_normal_palette_instancing']
            if 'stand_normal_palette_binding_reuse' in item:
                report[label]['normal_palette_binding_reuse'] = item['stand_normal_palette_binding_reuse']
            if 'stand_normal_palette_instance_rows' in item:
                report[label]['normal_palette_instance_rows'] = item['stand_normal_palette_instance_rows']
                report[label]['normal_palette_instance_row_samples'] = item['normal_palette_instance_row_samples']
            if 'normal_palette_samples' in item:
                report[label]['normal_palette_samples'] = item['normal_palette_samples']
            if 'stand_normal_range_bank' in item:
                report[label]['normal_range_bank'] = item['stand_normal_range_bank']
            if 'stand_shadow_projection_bank' in item:
                report[label]['shadow_projection_bank'] = item['stand_shadow_projection_bank']
            if 'stand_shadow_packet_bank' in item:
                report[label]['shadow_packet_bank'] = item['stand_shadow_packet_bank']
            if 'stand_copy_admission' in item:
                report[label]['copy_admission'] = item['stand_copy_admission']
            if 'stand_visual_clock' in item:
                report[label]['visual_clock'] = item['stand_visual_clock']
            if 'stand_far_shadow_alignment' in item:
                report[label]['far_shadow_alignment'] = item['stand_far_shadow_alignment']
            if 'stand_geometry_checkpoint' in item:
                report[label]['geometry_checkpoint'] = item['stand_geometry_checkpoint']
            if 'stand_world_checkpoints' in item:
                report[label]['world_checkpoints'] = item['stand_world_checkpoints']
            if 'stand_event_spin' in item:
                report[label]['event_spin'] = item['stand_event_spin']
            if 'stand_directional_terrain' in item:
                report[label]['directional_terrain'] = item['stand_directional_terrain']
            if 'stand_directional_replay' in item:
                report[label]['directional_replay'] = item['stand_directional_replay']
            if 'stand_directional_index_pairs' in item:
                report[label]['directional_index_pairs'] = item['stand_directional_index_pairs']
            if 'stand_point_replay_state_reuse' in item:
                report[label]['point_replay_state_reuse'] = item['stand_point_replay_state_reuse']
            if 'stand_point_index_batches' in item:
                report[label]['point_index_batches'] = item['stand_point_index_batches']
            if 'stand_point_index_admission_reuse' in item:
                report[label]['point_index_admission_reuse'] = item['stand_point_index_admission_reuse']
            if 'stand_point_effective_pose' in item:
                report[label]['point_effective_pose'] = item['stand_point_effective_pose']
                report[label]['point_effective_pose_samples'] = item['point_effective_pose_samples']
            if 'stand_point_mixed_reuse' in item:
                report[label]['point_mixed_reuse'] = item['stand_point_mixed_reuse']
                report[label]['point_mixed_reuse_samples'] = item['point_mixed_reuse_samples']
            if 'stand_crowd_bounds_operations' in item:
                report[label]['crowd_bounds_operations'] = item['stand_crowd_bounds_operations']
    except (OSError, ValueError, TypeError, KeyError, AttributeError) as error:
        report.update(status='INCOMPLETE', error=str(error))
    write_json(output, report)
    print(f'{report["status"]}: {output}')
    return 0 if report['status'] == 'PASS' else 1


def end_to_end(args):
    from build_renderer import source_identity

    root, run = args.stage.resolve(), args.output.resolve()
    if run == root or not run.is_relative_to(root) or run.is_relative_to(root / 'client'):
        raise ValueError('Put end-to-end results under the private stage outside client/')
    run.mkdir(parents=True, exist_ok=False)
    manifest = root / 'baseline.json'
    capture, replay_output = run / 'capture', run / 'replay'
    report = {'format': 1, 'status': 'INCOMPLETE', 'stage': str(root), 'fps_eligible': False,
              'scene': getattr(args, 'scene', 'solo')}
    try:
        staged = json.loads(manifest.read_text(encoding='utf-8'))
        if staged.get('frame_parity') is not True or staged.get('msaa') != 2:
            raise ValueError('End-to-end requires a frozen --frame-parity stage at 2x MSAA')
        report['baseline_sha256'] = digest(manifest)
        identity = source_identity()
        report['expected_source'] = identity
        override = getattr(args, 'live_dll', None)
        live_assets = getattr(args, 'live_assets', None)
        if (override is None) != (live_assets is None):
            raise ValueError('Use --live-dll and --live-assets together')
        fixed_visual_clock = getattr(args, 'fixed_visual_clock', False)
        match_geometry_start = getattr(args, 'match_geometry_start', False)
        if match_geometry_start and not fixed_visual_clock:
            raise ValueError('Matching geometry readiness requires the fixed visual clock')
        if getattr(args, 'fixed_static_stream', False) and not match_geometry_start:
            raise ValueError('Fixed static streaming requires matching geometry readiness')
        if getattr(args, 'fixed_shadow_bounds', False) and not match_geometry_start:
            raise ValueError('Fixed shadow bounds require matching geometry readiness')
        if getattr(args, 'fixed_gi_publication', False) and not match_geometry_start:
            raise ValueError('Fixed GI publication requires matching geometry readiness')
        live_dll = override.resolve() if override is not None else root / 'client/d3d9.dll'
        for dll in (live_dll, args.stand_dll.resolve()):
            if identity.encode('ascii') + b'\0' not in dll.read_bytes():
                raise ValueError(f'Rebuild the DLL from the current source tree: {dll}')
        report['live_dll_sha256'] = digest(live_dll)
        report['live_assets_sha256'] = digest(live_assets) if live_assets else staged['input_sha256']['northlight-assets.bin']

        camera_turn = getattr(args, 'camera_turn', False)
        return_to_menu = getattr(args, 'return_to_menu', False)
        live_exit = run_client(argparse.Namespace(stage=root, output=capture, target='world', scene=report['scene'],
                                                  crowd_size=getattr(args, 'crowd_size', None),
                                                  timeout=args.live_timeout, windows=0 if camera_turn or return_to_menu else 3,
                                                  manual=False, return_to_menu=return_to_menu,
                                                  background=getattr(args, 'background', False),
                                                  capture_wrapper=args.capture_wrapper,
                                                  camera_turn=camera_turn,
                                                  keep_work=getattr(args, 'keep_work', False),
                                                  raw_parity=getattr(args, 'raw_parity', False),
                                                  fixed_visual_clock=fixed_visual_clock,
                                                  align_far_shadow_presents=getattr(args, 'align_far_shadow_presents', False),
                                                  light_history_audit=getattr(args, 'light_history_audit', False),
                                                  light_history_between=getattr(args, 'light_history_between', False),
                                                  fixed_static_stream=getattr(args, 'fixed_static_stream', False),
                                                  fixed_shadow_bounds=getattr(args, 'fixed_shadow_bounds', False),
                                                  fixed_gi_publication=getattr(args, 'fixed_gi_publication', False),
                                                  fixed_gpu_budget=getattr(args, 'fixed_gpu_budget', False),
                                                  resize_before_parity=getattr(args, 'resize_before_parity', False),
                                                  live_dll=live_dll if override is not None else None, live_assets=live_assets))
        live_path = capture / 'validation.json'
        live = json.loads(live_path.read_text(encoding='utf-8'))
        report['capture'] = {'report': 'capture/validation.json', 'sha256': digest(live_path),
                             'status': live['status'], 'identity': live.get('observation', {}).get('identity')}
        if live_exit or live['status'] != 'PASS':
            raise ValueError('Live capture did not complete; see capture/validation.json')
        if (report['capture']['identity'] != identity or live['runtime_inputs']['d3d9.dll'] != report['live_dll_sha256'] or
                live['runtime_inputs']['northlight-assets.bin'] != report['live_assets_sha256']):
            raise ValueError('Live runtime DLL or shader assets differ from the candidate')

        replay_exit = replay(argparse.Namespace(stage=root, capture=capture, output=replay_output,
                                                tool_root=args.tool_root, host=args.host, timeout=args.timeout,
                                                stand_dll=args.stand_dll, stand_assets=args.stand_assets,
                                                keep_work=getattr(args, 'keep_work', False),
                                                raw_parity=getattr(args, 'raw_parity', False),
                                                match_geometry_start=match_geometry_start))
        replay_path = replay_output / 'validation.json'
        result = json.loads(replay_path.read_text(encoding='utf-8'))
        report['replay'] = {'report': 'replay/validation.json', 'sha256': digest(replay_path),
                            'status': result['status'], 'identity': result.get('stand_identity'),
                            'trace_join_validated': result['trace_join_validated'],
                            'world_observed': result['stand_world_observed'], 'host_exit_code': result.get('host_exit_code'),
                            'plane_match': result.get('parity', {}).get('plane_match'),
                            'live_lighting_compared': 'live_lighting_comparison' in result}
        if source_identity() != identity:
            raise ValueError('The renderer source tree changed after live capture')
        if result.get('stand_identity') != identity and (result.get('stand_identity') or result['status'] != 'INCOMPLETE'):
            raise ValueError('Replay runtime source identity differs from the candidate')
        report['status'] = result['status']
        if replay_exit:
            report['error'] = result['error']
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        report['error'] = str(error)
    finally:
        write_json(run / 'validation.json', report)
    print(f'{report["status"]}: {run / "validation.json"}')
    return 0 if report['status'] == 'PASS' else 1


def replay_tool_options(parser, *, optional_host=False):
    parser.add_argument('--tool-root', type=Path, required=True)
    parser.add_argument('--host', type=Path, required=not optional_host)
    parser.add_argument('--stand-dll', type=Path, default=fp.RENDERER / 'frd9-stand-replay.dll')
    parser.add_argument('--stand-assets', type=Path, default=fp.RENDERER / 'frd9-stand-replay.assets.bin')
    parser.add_argument('--timeout', type=int, default=120)


def artifact_options(parser):
    parser.add_argument('--keep-work', action='store_true', help='Retain disposable client or replay files for debugging')
    parser.add_argument('--raw-parity', action='store_true', help='Write raw parity images for tools that cannot read compact captures')


def indexed_draw_selector(value):
    if not re.fullmatch(r'[0-9]{1,10}(?:,[0-9]{1,10}){3}', value):
        raise argparse.ArgumentTypeError('Use MIN,VERTICES,START,COUNT for a triangle list with base vertex 0')
    min_vertex, vertices, start, count = map(int, value.split(','))
    if max(min_vertex, vertices, start, count) >= 1 << 32 or not vertices or not count:
        raise argparse.ArgumentTypeError('Invalid indexed draw selector')
    return f'{min_vertex},{vertices},{start},{count}'


def main():
    parser = argparse.ArgumentParser(__doc__)
    commands = parser.add_subparsers(required=True)
    from qa_vulkan import record as record_vulkan
    vulkan = commands.add_parser('record-vulkan', help='Capture a Vulkan scene from launch')
    for name in ('client', 'tool-root', 'output'):
        vulkan.add_argument('--' + name, type=Path, required=True)
    vulkan.add_argument('--shaders', type=Path, help='Vulkan shader directory for the chosen client')
    vulkan.set_defaults(action=record_vulkan)
    from qa_api_replay import RENDERERS, record as record_api
    graphics = commands.add_parser('record-api', help='Capture an x64 D3D9Ex, D3D11 or OpenGL scene')
    graphics.add_argument('--renderer', choices=RENDERERS, required=True)
    for name in ('client', 'tool-root', 'presentmon', 'output'):
        graphics.add_argument('--' + name, type=Path, required=True)
    graphics.add_argument('--timeout', type=int, default=300, help='Timeout for checking the closed API trace')
    graphics.set_defaults(action=record_api)
    prepare = commands.add_parser('stage')
    prepare.add_argument('--client', type=Path, default=fp.client_root(required=False))
    prepare.add_argument('--dll', type=Path, default=fp.RENDERER / 'frd9.dll')
    prepare.add_argument('--assets', type=Path, default=fp.RENDERER / 'northlight-assets.bin')
    prepare.add_argument('--backend', type=Path, required=True)
    prepare.add_argument('--scene', choices=('solo', fixture.RATCHET_PAN), default='solo', help='Stage Northrend or the Ratchet waterfront')
    prepare.add_argument('--x64', action='store_true', help='Stage WowClient64.exe and AMD64 DXVK')
    prepare.add_argument('--output', type=Path, required=True)
    prepare.add_argument('--resolution', choices=('1280x720', '1920x1080', '3840x2160'), default='3840x2160')
    prepare.add_argument('--msaa', type=int, choices=(1, 2), default=2)
    prepare.add_argument('--frame-parity', action='store_true', help='Freeze the 2x MSAA live/replay pixel comparison profile')
    prepare.add_argument('--qa-scene', action='store_true', help='Stage the SDR QA capture profile')
    prepare.add_argument('--qa-backend-kind', choices=('dxvk', 'dxvk2', 'native'), default='dxvk',
                         help='D3D9 runtime for x64 QA captures')
    prepare.add_argument('--resume', action='store_true', help='Verify and reuse files from an unfinished stage')
    prepare.set_defaults(action=stage)
    run = commands.add_parser('run')
    run.add_argument('--stage', type=Path, required=True)
    run.add_argument('--output', type=Path, required=True)
    run.add_argument('--target', choices=('menu', 'world'), default='world')
    run.add_argument('--scene', choices=fixture.SCENES, default='solo')
    run.add_argument('--crowd-size', type=int, choices=fixture.CROWD_SIZES, metavar='50..128',
                     help=f'Remote players in the crowd (default {fixture.DEFAULT_CROWD_SIZE})')
    run.add_argument('--timeout', type=int, choices=range(5, 301), metavar='SECONDS',
                     help='Timeout in seconds (90 by default, 240 for m2-stress)')
    run.add_argument('--windows', type=int, default=3, choices=range(11), metavar='COUNT',
                     help='Number of warm timing windows. Use 0 for a graphics readiness smoke.')
    run.add_argument('--background', action='store_true', help='Launch without requesting focus and time background windows')
    run.add_argument('--dxvk-direct-adapter', action='store_true', help='Use DXVK adapters without enumerating connected displays')
    run.add_argument('--dxvk-compiler-threads', type=int, choices=range(129), metavar='COUNT',
                     help='Number of DXVK compiler threads in this run (0 uses DXVK automatic selection)')
    run.add_argument('--dxvk-gpl-only', action='store_true',
                     help='Use graphics pipeline libraries without the background optimization pass in DXVK')
    run.add_argument('--dxvk-shader-cache-seed', type=Path, metavar='CACHE.dxvk.bin',
                     help='Copy a DXVK shader cache and its .lut into the private run and verify DXVK reused it')
    run.add_argument('--return-to-menu', action='store_true',
                     help='After World readiness, require a completed logout and a fresh character menu')
    run.add_argument('--camera-turn', action='store_true', help='Require a confirmed heading change after cold World entry')
    run.add_argument('--native-m2-audit-samples', type=int, default=0, choices=range(11), metavar='COUNT',
                     help='Require this many warm M2 section and normal-palette comparisons with --windows 0')
    run.add_argument('--native-m2-require-grouped', action='store_true',
                     help='Also require warm samples with verified grouped M2 draws, including multi-instance draws')
    run.add_argument('--native-world-batch-audit', action='store_true',
                     help='Also require warm samples with verified WMO and ADT draw bounds')
    run.add_argument('--manual', action='store_true')
    run.add_argument('--capture-wrapper', type=Path, help='Capture with the private apitrace wrapper and a unique downstream module name')
    run.add_argument('--qa-scene', action='store_true', help='Record from launch until the in-game QA button marks the last 15 seconds')
    run.add_argument('--qa-realm', help='Use this QA realm host or host:port with manual login')
    run.add_argument('--draw-probe', type=indexed_draw_selector, metavar='MIN,VERTICES,START,COUNT',
                     help='Save native RGB inputs and outputs of the first matching triangle list after parity arms')
    run.add_argument('--resize-before-parity', action='store_true',
                     help='Resize the owned window and restore its size before selecting parity Presents')
    run.add_argument('--fixed-visual-clock', action='store_true', help='Use a fixed 60 Hz visual timeline for a parity capture')
    run.add_argument('--align-far-shadow-presents', action='store_true', help='Align far-shadow refresh on fixed Present boundaries')
    run.add_argument('--fixed-static-stream', action='store_true', help='Advance static shadow streaming with a fixed per-frame byte budget')
    run.add_argument('--fixed-shadow-bounds', action='store_true', help='Advance caster bounds by work counts for image parity')
    run.add_argument('--fixed-gi-publication', action='store_true', help='Publish complete near and coarse GI together for image parity')
    run.add_argument('--fixed-gpu-budget', action='store_true', help='Keep the full GPU effects level during image parity')
    run.add_argument('--light-history-between', action='store_true', help='Audit lighting on every frame between the two selected Presents')
    run.add_argument('--light-history-audit', action='store_true', help='Capture lighting inputs on the two parity Presents')
    run.add_argument('--live-dll', type=Path, help='Use a candidate DLL on the existing private stage')
    run.add_argument('--live-assets', type=Path, help='Asset bundle built with --live-dll')
    artifact_options(run)
    run.set_defaults(action=run_client)
    retrace = commands.add_parser('replay')
    for name in ('capture', 'output'):
        retrace.add_argument('--' + name, type=Path, required=True)
    retrace.add_argument('--stage', type=Path, help='Required when capture is nested under the private stage')
    retrace.add_argument('--qa-client', type=Path, help='Matching game files and world cache for an extracted QA package')
    retrace.add_argument('--qa-backend', type=Path, help='Local DXVK x86 DLL matching the QA package')
    retrace.add_argument('--qa-visible', action='store_true', help='Show the QA replay in a window')
    retrace.add_argument('--profile-frames', metavar='FIRST:LAST:STEP', help='Collect stand CPU/GPU timestamps without image readback')
    retrace.add_argument('--profile-fixed-work', action='store_true',
                         help='Use count-limited pose bounds and stable caster order for stand profiling')
    retrace.add_argument('--profile-sdr', action='store_true',
                         help='Disable HDR in a private timing replay. Requires --profile-frames and does not validate HDR parity.')
    retrace.add_argument('--fixed-visual-clock', action=argparse.BooleanOptionalAction,
                         help='Use a fixed 60 Hz visual timeline for image parity or fixed-work profiling')
    retrace.add_argument('--fixed-shadow-bounds', action='store_true',
                         help='Use work counts for caster bounds on an existing parity trace')
    retrace.add_argument('--match-geometry-start', action='store_true',
                         help='Start consuming World geometry at the captured readiness Present')
    retrace.add_argument('--align-far-shadow-presents', action='store_true',
                         help='Refresh reusable far cascades at fixed Present boundaries during image diagnostics')
    retrace.add_argument('--light-history-audit', action='store_true',
                         help='Compare the actual inputs to lighting history on the selected Presents')
    retrace.add_argument('--light-history-predecessors', type=int, choices=range(9), default=0, metavar='0..8',
                         help='Also audit this many preceding Presents before each selected image')
    retrace.add_argument('--point-shadow-limit', type=int, choices=range(1, 129), metavar='1..128',
                         help='Override PointShadowLimit in this private replay directory')
    retrace.add_argument('--point-shadow-adaptive', action=argparse.BooleanOptionalAction,
                         help='Override PointShadowAdaptiveRefresh in this private replay directory')
    retrace.add_argument('--shadow-refresh-cpu-ms', type=int, choices=range(21), metavar='0..20',
                         help='Override ShadowRefreshCpuMs in this private replay (0 removes the wall-clock hold budget)')
    retrace.add_argument('--far-shadow-interval', type=int, choices=range(1, 17), metavar='1..16',
                         help='Override FarShadowInterval in this private replay (1 redraws every far cascade)')
    retrace.add_argument('--model-bump', type=int, choices=range(4), metavar='0..3',
                         help='Override AutoBumpModels in this private replay directory')
    retrace.add_argument('--normal-filter', type=int, choices=(0, 1),
                         help='Override AutoNormalFilter (0 legacy, 1 filtered) in this private replay directory')
    retrace.add_argument('--gi-dynamic-probes', action=argparse.BooleanOptionalAction,
                         help='Override GIDynamicProbes in this private replay directory')
    retrace.add_argument('--world-shadows', action=argparse.BooleanOptionalAction,
                         help='Override WorldShadows in a private replay profile')
    retrace.add_argument('--world-fog', action=argparse.BooleanOptionalAction,
                         help='Override WorldFog in a private replay profile')
    retrace.add_argument('--legacy-normal-declarations', action=argparse.BooleanOptionalAction,
                         help='Compare per-packet and shared vertex declaration references in a replay build')
    retrace.add_argument('--legacy-normal-pose-declaration', action=argparse.BooleanOptionalAction,
                         help='Compare a COM getter and a borrowed declaration in the normal pose bounds check')
    retrace.add_argument('--legacy-normal-range-bank', action=argparse.BooleanOptionalAction,
                         help='Use mirror reads instead of the same-draw material bank for the normal range proof')
    retrace.add_argument('--normal-palette-instancing', action=argparse.BooleanOptionalAction,
                         help='Compare ModelNormals instancing and individual draws on the same replay DLL')
    retrace.add_argument('--normal-palette-binding-reuse', action=argparse.BooleanOptionalAction,
                         help='Compare retained and per-batch ModelNormals instancing bindings')
    retrace.add_argument('--normal-palette-instance-rows', action=argparse.BooleanOptionalAction,
                         help='Compare ModelNormals instancing with and without per-instance non-palette rows')
    retrace.add_argument('--legacy-shadow-projection-bank', action=argparse.BooleanOptionalAction,
                         help='Use the existing getter instead of the same-draw material bank for shadow capture projection')
    retrace.add_argument('--legacy-shadow-packet-bank', action=argparse.BooleanOptionalAction,
                         help='Use getters for shadow packet float rows even if the same draw has a certified material bank')
    retrace.add_argument('--legacy-copy-admission', action=argparse.BooleanOptionalAction,
                         help='Fill regular replay buffer copies after two recent frames regardless of requested span')
    retrace.add_argument('--legacy-event-spin', action=argparse.BooleanOptionalAction,
                         help='Compare two spin grace periods for DXVK EVENT waits in a replay build')
    retrace.add_argument('--legacy-directional-terrain', action=argparse.BooleanOptionalAction,
                         help='Compare per-batch and coalesced dynamic directional terrain draws on the stand')
    retrace.add_argument('--legacy-directional-replay', action=argparse.BooleanOptionalAction,
                         help='Compare per-draw and grouped directional shadow replay on the stand')
    retrace.add_argument('--legacy-actor-inset', action=argparse.BooleanOptionalAction,
                         help='Compare eight-corner and extremal actor bounds with actor depth capture')
    retrace.add_argument('--directional-index-pairs', action=argparse.BooleanOptionalAction,
                         help='Compare the shared index buffer path inside grouped directional replay')
    retrace.add_argument('--point-replay-state-reuse', action=argparse.BooleanOptionalAction,
                         help='Compare shared and per-face caster state in a point shadow cube')
    retrace.add_argument('--point-index-batches', action=argparse.BooleanOptionalAction,
                         help='Compare point shadow index pairs with batches of up to four ranges')
    retrace.add_argument('--point-index-admission-reuse', action=argparse.BooleanOptionalAction,
                         help='Compare cached and per-face admission for point index merging')
    retrace.add_argument('--point-effective-pose', action=argparse.BooleanOptionalAction,
                         help='Compare point range merging using read registers or the full captured pose')
    retrace.add_argument('--point-mixed-reuse', action=argparse.BooleanOptionalAction,
                         help='Compare cached opaque depth and complete draws on mixed point shadow faces')
    retrace.add_argument('--crowd-bounds-operations', action=argparse.BooleanOptionalAction,
                         help='Compare fixed and crowd-scaled replay bounds operation limits')
    retrace.add_argument('--point-adaptive-motion', action=argparse.BooleanOptionalAction,
                         help='Allow adaptive holds for small moving native shadow emitters')
    retrace.add_argument('--capture-work-frame', type=int,
                         help='Audit material, normals and world capture on one certified renderer frame')
    retrace.add_argument('--point-shadow-audit', action='store_true',
                         help='Collect caster batch census with profiling, or cube faces with image capture')
    replay_tool_options(retrace, optional_host=True)
    artifact_options(retrace)
    retrace.set_defaults(action=replay)
    compare = commands.add_parser('compare-replays', help='Compare two existing replay outputs for the exact same trace')
    for name in ('reference', 'candidate', 'output'):
        compare.add_argument('--' + name, type=Path, required=True)
    compare.set_defaults(action=compare_replays)
    paired = commands.add_parser('end-to-end', help='Capture, replay and compare a frozen 2x MSAA candidate')
    for name in ('stage', 'output', 'capture-wrapper'):
        paired.add_argument('--' + name, type=Path, required=True)
    paired.add_argument('--live-dll', type=Path, help='Capture a candidate DLL using the existing private client')
    paired.add_argument('--live-assets', type=Path, help='Asset bundle built with --live-dll')
    paired.add_argument('--scene', choices=fixture.SCENES, default='solo')
    paired.add_argument('--crowd-size', type=int, choices=fixture.CROWD_SIZES, metavar='50..128',
                        help=f'Remote players in the crowd (default {fixture.DEFAULT_CROWD_SIZE})')
    paired.add_argument('--fixed-visual-clock', action='store_true', help='Use the same fixed visual timeline for capture and replay')
    paired.add_argument('--align-far-shadow-presents', action='store_true',
                        help='Use the same far-shadow refresh boundaries in capture and replay')
    paired.add_argument('--fixed-static-stream', action='store_true', help='Use the same fixed per-frame static shadow byte budget')
    paired.add_argument('--fixed-shadow-bounds', action='store_true', help='Use the same work counts for caster bounds')
    paired.add_argument('--fixed-gi-publication', action='store_true', help='Publish complete near and coarse GI together in capture and replay')
    paired.add_argument('--fixed-gpu-budget', action='store_true', help='Keep the full GPU effects level in capture and replay')
    paired.add_argument('--light-history-between', action='store_true', help='Audit lighting between both selected Presents in capture and replay')
    paired.add_argument('--light-history-audit', action='store_true', help='Compare lighting inputs on the two parity Presents')
    paired.add_argument('--match-geometry-start', action='store_true',
                        help='Start consuming World geometry at the captured readiness Present')
    paired.add_argument('--resize-before-parity', action='store_true',
                        help='Resize the owned window and restore its size before selecting parity Presents')
    paired.add_argument('--camera-turn', action='store_true', help='Select parity Presents after a verified heading change')
    paired.add_argument('--return-to-menu', action='store_true',
                        help='Trace and verify the character menu before and after the World parity frames')
    paired.add_argument('--background', action='store_true', help='Launch the private capture client without requesting focus')
    paired.add_argument('--live-timeout', type=int, default=90, choices=range(5, 301), metavar='SECONDS')
    replay_tool_options(paired)
    artifact_options(paired)
    paired.set_defaults(action=end_to_end)
    args = parser.parse_args()
    return args.action(args)


if __name__ == '__main__':
    raise SystemExit(main())
