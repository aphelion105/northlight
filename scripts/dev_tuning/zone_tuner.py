#!/usr/bin/env python3
"""Developer-only zone tuner for a dev-tuning renderer build (stdlib only). Never shipped.

    python3 scripts/dev_tuning/zone_tuner.py serve [--client GAME] [--port 8765]
    python3 scripts/dev_tuning/zone_tuner.py export [--client GAME] [--dry-run]

serve: a page on http://127.0.0.1:<port>/ (bound to 127.0.0.1 only) with sliders for the zone the
camera is in. It reads <game>/northlight-tuning-state.json (written by the dev DLL) and writes
<game>/northlight-tuning.ini (read by the dev DLL), atomically (tmp + os.replace). Editing one
section rewrites only that section's lines; other sections and comments stay as they are.

export: merges the overrides into client-config/celestial-profiles.ini (sun_/moon_ keys) and
client-config/shadow-range-profiles.ini (terrain_radius), and prints the ground fog scales for
NorthlightRegionalFog::policy (src/sky/regional_fog.h), which has no ini. An existing key line is
replaced in place, a missing key is appended after the section's last key, a missing section is
appended at the end of the file with a `# <Zone name>` comment. Nothing else is touched. A zone
whose terrain_radius goes back to 928 keeps its shadow-range section with 928 (no section is
deleted); a new section at 928 is not added, as unlisted zones already get 928.

The file formats are those of the dev DLL's parser (src/sky/zone_tuning.h).
"""
import argparse
import difflib
import http.server
import json
import math
import os
import re
import subprocess
import sys
import threading
import time
import urllib.parse
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))
import northlight_paths  # noqa: E402

HERE = Path(__file__).resolve().parent
PAGE = HERE / 'zone_tuner.html'
CONFIG = REPO / 'client-config'
OVERRIDES = 'northlight-tuning.ini'
STATE = 'northlight-tuning-state.json'
STALE_SECONDS = 3.0
MAPS = ('Azeroth', 'Kalimdor', 'Expansion01', 'Northrend')
HEADER = '# Northlight dev tuning overrides. Written by scripts/dev_tuning/zone_tuner.py.'
AREA_TABLE = 'DBFilesClient\\AreaTable.dbc'
DEFAULT_RADIUS = 928

# key: (minimum, maximum, rgb). sun_ and moon_ share the celestial ranges.
CELESTIAL = {'disc': (0, 1, True), 'disc_gain': (0, 4, False), 'light': (0, 1, True), 'light_mix': (0, 1, False),
             'light_strength': (0, 4, False), 'fog_gain': (0, 8, False), 'fog_height_scale': (.25, 4, False),
             'horizon_haze': (0, 4, False)}
ZONE_ONLY = {'terrain_radius': (DEFAULT_RADIUS, 4096, False), 'ground_fog_day_scale': (0, 4, False),
             'ground_fog_night_scale': (0, 4, False), 'ground_fog_day_add': (0, .05, False),
             'ground_fog_night_add': (0, .05, False)}
FOG_KEYS = ('ground_fog_day_scale', 'ground_fog_night_scale', 'ground_fog_day_add', 'ground_fog_night_add')
KEYS = {**{s + k: r for s in ('sun_', 'moon_') for k, r in CELESTIAL.items()}, **ZONE_ONLY}
# The DLL's built-in profile (NorthlightCelestialProfiles::Profile) under celestial-profiles.ini [default].
BUILTIN = {'sun_disc': [1, .96, .88], 'sun_disc_gain': 3.5, 'sun_light': [1, 1, 1], 'sun_light_mix': 0,
           'sun_light_strength': 1, 'sun_fog_gain': 1, 'sun_fog_height_scale': 1, 'sun_horizon_haze': 1,
           'moon_disc': [.94, .97, 1], 'moon_disc_gain': 1.1, 'moon_light': [1, 1, 1], 'moon_light_mix': 0,
           'moon_light_strength': 1, 'moon_fog_gain': 1, 'moon_fog_height_scale': 1, 'moon_horizon_haze': 1,
           'terrain_radius': DEFAULT_RADIUS, 'ground_fog_day_scale': 1, 'ground_fog_night_scale': 1,
           'ground_fog_day_add': 0, 'ground_fog_night_add': 0}

SECTION_RE = re.compile(r'^\s*\[([^\]]*)\]\s*(#.*)?$')
KEY_RE = re.compile(r'^(\s*)([^=#\[\s][^=#]*?)(\s*=\s*)([^#]*?)(\s*#.*)?$')


# ---- values ----

def fmt(v, rgb=False, places=4):
    """Up to 4 decimals; RGB components keep at least 2 (the client-config style, 0.90)."""
    s = f'{v:.{places}f}'.rstrip('0')
    if rgb:
        s += '0' * max(0, 2 - len(s.split('.')[1]))
    s = s.rstrip('.')
    return '0' if s in ('-0', '') else s


def format_value(key, v):
    if KEYS[key][2]:
        return ', '.join(fmt(c, True) for c in v)
    return fmt(v, places=6 if key.endswith('_add') else 4)   # densities are .0002-sized: 4 decimals would lose them


def parse_value(key, text):
    """The value of one key line; ValueError with the reason."""
    if key not in KEYS:
        raise ValueError(f'unknown key {key}')
    parts = [p for p in re.split(r'[\s,]+', text.strip()) if p]
    lo, hi, rgb = KEYS[key]
    if len(parts) != (3 if rgb else 1):
        raise ValueError(f'{key}: expected {3 if rgb else 1} number(s)')
    try:
        numbers = [float(p) for p in parts]
    except ValueError:
        raise ValueError(f'{key}: not a number: {text.strip()}') from None
    value = numbers if rgb else numbers[0]
    check_value(key, value)
    return value


def check_value(key, v):
    """The contract's ranges (and the DLL's); ValueError with the reason."""
    if key not in KEYS:
        raise ValueError(f'unknown key {key}')
    lo, hi, rgb = KEYS[key]
    values = v if rgb else [v]
    if rgb and (not isinstance(v, (list, tuple)) or len(v) != 3):
        raise ValueError(f'{key}: expected 3 numbers')
    for c in values:
        if isinstance(c, bool) or not isinstance(c, (int, float)) or not math.isfinite(c) or not lo <= c <= hi:
            raise ValueError(f'{key}: {c!r} is outside {lo}..{hi}')
    if key.endswith('_light') and sum(values) < .01:
        raise ValueError(f'{key}: light hue cannot be black (sum < .01); use light_strength = 0')


def section_kind(name):
    """'tuning', 'default' or 'zone'; ValueError for anything else."""
    if name in ('tuning', 'default'):
        return name
    m = re.fullmatch(r'([A-Za-z0-9]+):(\d{1,6})', name)
    if not m or m.group(1) not in MAPS or int(m.group(2)) == 0:
        raise ValueError(f'unknown section [{name}] (expected [default] or [Map:ZoneID], map one of {", ".join(MAPS)})')
    return 'zone'


def check_section(name, values):
    """A [default] or [Map:ZoneID] section's {key: value}; ValueError with the reason."""
    kind = section_kind(name)
    if kind == 'tuning':
        raise ValueError('[tuning] holds only enabled')
    for key, v in values.items():
        if kind == 'default' and key in ZONE_ONLY:
            raise ValueError(f'{key} is a zone-only key, not allowed in [default]')
        check_value(key, v)


def same(a, b):
    a, b = (a if isinstance(a, list) else [a]), (b if isinstance(b, list) else [b])
    return len(a) == len(b) and all(abs(x - y) < 5e-5 for x, y in zip(a, b))


# ---- line-preserving ini ----

class IniLines:
    """An ini file as its lines. Edits touch only the lines of the key or section they change."""

    def __init__(self, text=''):
        self.newline = '\r\n' if '\r\n' in text else '\n'
        self.lines = text.splitlines()

    def text(self):
        return self.newline.join(self.lines) + self.newline if self.lines else ''

    def sections(self):
        """[(name, header index, end index)] in file order; end is the next header (or len)."""
        heads = [(i, m.group(1).strip()) for i, line in enumerate(self.lines) if (m := SECTION_RE.match(line))]
        return [(name, i, heads[n + 1][0] if n + 1 < len(heads) else len(self.lines)) for n, (i, name) in enumerate(heads)]

    def section(self, name):
        return next(((s, e) for n, s, e in self.sections() if n == name), None)

    def keys(self, name):
        """{key: (line index, value text)} of a section."""
        span, out = self.section(name), {}
        if span:
            for i in range(span[0] + 1, span[1]):
                m = KEY_RE.match(self.lines[i])
                if m and not self.lines[i].lstrip().startswith('#'):
                    out[m.group(2).strip()] = (i, m.group(4))
        return out

    def set(self, name, key, value_text, header_comment=None, comment_line=None):
        """Replace the key's value in place, else add it after the section's last key, else add
        the section at the end (with an inline header comment or a comment line above it)."""
        found = self.keys(name).get(key)
        if found:
            m = KEY_RE.match(self.lines[found[0]])
            self.lines[found[0]] = m.group(1) + m.group(2) + m.group(3) + value_text + (m.group(5) or '')
            return
        span = self.section(name)
        if span is None:
            if self.lines and self.lines[-1].strip():
                self.lines.append('')
            if comment_line:
                self.lines.append(comment_line)
            self.lines.append(f'[{name}]' + (f' {header_comment}' if header_comment else ''))
            self.lines.append(f'{key} = {value_text}')
            return
        start, end = span
        last = start
        for i in range(start + 1, end):
            stripped = self.lines[i].strip()
            if stripped and not stripped.startswith('#'):
                last = i
        self.lines.insert(last + 1, f'{key} = {value_text}')

    def remove(self, name, key):
        found = self.keys(name).get(key)
        if found:
            del self.lines[found[0]]

    def remove_section(self, name):
        """The header and its lines up to the next header; one blank line before it goes too."""
        span = self.section(name)
        if span:
            start, end = span
            if start > 0 and not self.lines[start - 1].strip():
                start -= 1
            del self.lines[start:end]
            while self.lines and not self.lines[-1].strip():
                self.lines.pop()


# ---- northlight-tuning.ini ----

def parse_overrides(text):
    """{'enabled': bool, 'sections': {name: {key: value}}}, checked like the DLL; ValueError 'line N: why'."""
    enabled, sections, current, kind = True, {}, None, None
    seen_sections, seen_keys = set(), set()
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        try:
            if line.startswith('[') and line.endswith(']'):
                current = line[1:-1].strip()
                if current in seen_sections:
                    raise ValueError(f'duplicate section [{current}]')
                seen_sections.add(current)
                seen_keys = set()
                kind = section_kind(current)
                if kind != 'tuning':
                    sections[current] = {}
                continue
            if current is None or '=' not in line:
                raise ValueError('expected key = value in a section')
            key, value = (p.strip() for p in line.split('=', 1))
            if key in seen_keys:
                raise ValueError(f'duplicate key {key}')
            seen_keys.add(key)
            if kind == 'tuning':
                if key != 'enabled' or value not in ('0', '1'):
                    raise ValueError('[tuning] holds only enabled = 0 or 1')
                enabled = value == '1'
                continue
            if kind == 'default' and key in ZONE_ONLY:
                raise ValueError(f'{key} is a zone-only key, not allowed in [default]')
            sections[current][key] = parse_value(key, value)
        except ValueError as e:
            raise ValueError(f'line {number}: {e}') from None
    return {'enabled': enabled, 'sections': {k: v for k, v in sections.items() if v}}


def write_atomic(path, text):
    tmp = path.with_name(path.name + '.tmp')
    with open(tmp, 'w', encoding='utf-8', newline='') as f:
        f.write(text)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def read_text(path):
    try:
        return path.read_text(encoding='utf-8')
    except FileNotFoundError:
        return ''


def edit_overrides(text, enabled=None, section=None, values=None, names=None):
    """The new file text: [tuning] enabled set, and/or one section's keys replaced by `values`
    (an empty or None `values` removes the section). Checks before editing; ValueError."""
    if section is not None:
        values = values or {}
        check_section(section, values)
        if section == 'tuning':
            raise ValueError('[tuning] is edited through enabled')
    doc = IniLines(text)
    if not doc.lines:
        doc.lines = [HEADER, '[tuning]', 'enabled = 1']
    if doc.section('tuning') is None:
        first = next((i for i, line in enumerate(doc.lines) if SECTION_RE.match(line)), len(doc.lines))
        doc.lines[first:first] = ['[tuning]', 'enabled = 1', ''] if first < len(doc.lines) else ['[tuning]', 'enabled = 1']
    if enabled is not None:
        doc.set('tuning', 'enabled', '1' if enabled else '0')
    if section is not None:
        if not values:
            doc.remove_section(section)
        else:
            have = doc.keys(section)
            for key in have:
                if key not in values:
                    doc.remove(section, key)
            name = (names or {}).get(section)
            for key, v in values.items():
                doc.set(section, key, format_value(key, v), header_comment=f'# {name}' if name else None)
    new = doc.text()
    parse_overrides(new)   # never write a file the DLL would reject
    return new


# ---- zone names ----

def area_table_names(client):
    """{zone id: name} from the client's AreaTable.dbc (the highest-priority archive holding it)."""
    import client_archives
    import mpq   # needs StormLib (northlight_paths.stormlib)
    for path in reversed(client_archives.chain(client)):
        with mpq.Archive(path) as archive:
            if archive.has(AREA_TABLE):
                return parse_area_table(archive.read(AREA_TABLE))
    raise FileNotFoundError(f'{client}: no {AREA_TABLE} in the archive chain')


def parse_area_table(blob):
    import struct
    magic, n, fields, size, strings = struct.unpack_from('<4s4I', blob)
    if magic != b'WDBC' or size != fields * 4 or len(blob) != 20 + n * size + strings or fields < 27:
        raise ValueError('AreaTable.dbc layout')
    text = blob[20 + n * size:]
    out = {}
    for i in range(n):
        row = struct.unpack_from('<' + 'I' * fields, blob, 20 + i * size)
        # Name columns 11..26 are the locales; the client's own locale is the one filled in.
        names = [text[o:].split(b'\0', 1)[0].decode('utf-8', 'replace') for o in row[11:27] if 0 < o < len(text)]
        name = next((t for t in names if t), '')
        if name:
            out[row[0]] = name
    return out


def ini_comment_names(*paths):
    """{zone id: name} from `[Map:id] # Name` headers and `# Name` lines right above a header."""
    out = {}
    for path in paths:
        lines = read_text(path).splitlines()
        for i, line in enumerate(lines):
            m = SECTION_RE.match(line)
            z = re.fullmatch(r'[A-Za-z0-9]+:(\d+)', m.group(1).strip()) if m else None
            if not z:
                continue
            zone = int(z.group(1))
            if m.group(2) and m.group(2).lstrip('# ').strip():
                out.setdefault(zone, m.group(2).lstrip('# ').strip())
            elif i and lines[i - 1].startswith('#') and len(lines[i - 1]) < 48 and '=' not in lines[i - 1]:
                out.setdefault(zone, lines[i - 1].lstrip('# ').strip())
    return out


class ZoneNames:
    """Zone id -> name: AreaTable.dbc, else the client-config ini comments. Loaded once, on first use."""

    def __init__(self, client=None, ini_paths=(), table=None):
        self.client, self.ini_paths, self.table, self.lock = client, ini_paths, table, threading.Lock()
        self.source = 'given' if table is not None else None

    def load(self):
        with self.lock:
            if self.table is None:
                table = ini_comment_names(*self.ini_paths)
                self.source = 'ini comments'
                if self.client:
                    try:
                        table = {**table, **area_table_names(self.client)}
                        self.source = 'AreaTable.dbc'
                    except Exception as e:   # no StormLib, no client data: the ini comments still name zones
                        print(f'zone_tuner: zone names from ini comments only ({str(e).splitlines()[0]})', file=sys.stderr)
                self.table = table
            return self.table

    def get(self, zone):
        return self.load().get(int(zone), '')

    def section(self, name):
        """The zone name of a [Map:id] section, '' for others."""
        m = re.fullmatch(r'[A-Za-z0-9]+:(\d+)', name)
        return self.get(m.group(1)) if m else ''


# ---- base values (what the DLL loads from the game root's ini files) ----

def ini_values(path):
    """{section: {key: value}} of a shipped profile ini, parsed leniently (bad lines skipped)."""
    out, current = {}, None
    for raw in read_text(path).splitlines():
        line = raw.split('#', 1)[0].strip()
        if line.startswith('[') and line.endswith(']'):
            current = out.setdefault(line[1:-1].strip(), {})
        elif current is not None and '=' in line:
            key, value = (p.strip() for p in line.split('=', 1))
            try:
                current[key] = parse_value(key, value)
            except ValueError:
                pass
    return out


def base_values(section, celestial_path, shadow_path):
    """The section's values before overrides: built-in, [default], the zone's own sections."""
    celestial, shadow = ini_values(celestial_path), ini_values(shadow_path)
    values = dict(BUILTIN)
    values.update({k: v for k, v in celestial.get('default', {}).items() if k not in ZONE_ONLY})
    if section != 'default':
        values.update({k: v for k, v in celestial.get(section, {}).items() if k not in ZONE_ONLY})
        if 'terrain_radius' in shadow.get(section, {}):
            values['terrain_radius'] = shadow[section]['terrain_radius']
    else:
        for key in ZONE_ONLY:
            values.pop(key, None)
    return values


# ---- server ----

class Tuner:
    """The files of one game root, shared by the request threads."""

    def __init__(self, root, names, page=PAGE):
        self.root, self.names, self.page, self.lock = Path(root), names, page, threading.Lock()
        self.seqs = {}   # section -> the page's last applied PUT seq
        self.overrides_path, self.state_path = self.root / OVERRIDES, self.root / STATE

    def state(self):
        try:
            st = self.state_path.stat()
            state = json.loads(self.state_path.read_text(encoding='utf-8'))
        except FileNotFoundError:
            return {'status': 'missing', 'path': str(self.state_path)}
        except (OSError, ValueError) as e:
            return {'status': 'invalid', 'error': str(e), 'path': str(self.state_path)}
        age = max(0.0, time.time() - st.st_mtime)
        state.setdefault('groundFogActive', True)   # a DLL without the field: assume the zone has ground fog
        section = ''
        if state.get('map') in MAPS and int(state.get('zone') or 0) > 0:
            section = f'{state["map"]}:{int(state["zone"])}'
        return {'status': 'stale' if age > STALE_SECONDS or not state.get('inWorld', True) else 'live',
                'age': round(age, 1), 'section': section, 'zoneName': self.names.get(state.get('zone') or 0),
                'state': state}

    def overrides(self):
        text = read_text(self.overrides_path)
        try:
            parsed, error = parse_overrides(text), ''
        except ValueError as e:
            parsed, error = {'enabled': True, 'sections': {}}, str(e)
        return {**parsed, 'error': error, 'exists': self.overrides_path.is_file(), 'path': str(self.overrides_path),
                'names': {s: self.names.section(s) for s in parsed['sections']}}

    def put(self, body):
        if not isinstance(body, dict):
            raise ValueError('expected a JSON object')
        enabled, section, values, seq = body.get('enabled'), body.get('section'), body.get('values'), body.get('seq')
        if enabled is not None and not isinstance(enabled, bool):
            raise ValueError('enabled must be true or false')
        if section is not None and (not isinstance(section, str) or (values is not None and not isinstance(values, dict))):
            raise ValueError('section must be a name and values an object')
        if seq is not None and (isinstance(seq, bool) or not isinstance(seq, (int, float))):
            raise ValueError('seq must be a number')
        names = {section: self.names.section(section)} if section else None   # the first lookup loads AreaTable: outside the lock
        with self.lock:
            # Requests can overtake each other: an older write of a section must not undo a newer one.
            if section is not None and seq is not None:
                if seq < self.seqs.get(section, float('-inf')):
                    return {**self.overrides(), 'stale': True}
                self.seqs[section] = seq
            text = read_text(self.overrides_path)
            new = edit_overrides(text, enabled, section, values, names)
            if new != text:
                write_atomic(self.overrides_path, new)
        return self.overrides()

    def base(self, section):
        section_kind(section)
        return {'section': section, 'name': self.names.section(section),
                'values': base_values(section, self.root / 'celestial-profiles.ini', self.root / 'shadow-range-profiles.ini')}


def handler(tuner, port):
    allowed_hosts = {f'127.0.0.1:{port}', f'localhost:{port}'}

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def send(self, code, body, kind='application/json; charset=utf-8'):
            data = body if isinstance(body, bytes) else json.dumps(body).encode()
            self.send_response(code)
            self.send_header('Content-Type', kind)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(data)

        def local(self):
            # A page on another site must not reach the tuner through DNS rebinding.
            if self.headers.get('Host') not in allowed_hosts:
                self.send(403, {'error': 'only http://127.0.0.1 may use the tuner'})
                return False
            return True

        def do_GET(self):
            if not self.local():
                return
            url = urllib.parse.urlparse(self.path)
            try:
                if url.path in ('/', '/index.html'):
                    self.send(200, tuner.page.read_bytes(), 'text/html; charset=utf-8')
                elif url.path == '/api/state':
                    self.send(200, tuner.state())
                elif url.path == '/api/overrides':
                    self.send(200, tuner.overrides())
                elif url.path == '/api/base':
                    self.send(200, tuner.base(urllib.parse.parse_qs(url.query).get('section', [''])[0]))
                else:
                    self.send(404, {'error': 'not found'})
            except ValueError as e:
                self.send(400, {'error': str(e)})

        def do_PUT(self):
            if not self.local():
                return
            if urllib.parse.urlparse(self.path).path != '/api/overrides':
                return self.send(404, {'error': 'not found'})
            try:
                length = int(self.headers.get('Content-Length') or 0)
                if length > 65536:
                    raise ValueError('request too large')
                self.send(200, tuner.put(json.loads(self.rfile.read(length) or b'{}')))
            except ValueError as e:
                self.send(400, {'error': str(e)})
            except OSError as e:
                self.send(500, {'error': f'cannot write {OVERRIDES}: {e}'})

    return Handler


def make_server(tuner, port=0):
    """A threaded server on 127.0.0.1 (port 0: an ephemeral one); server.server_port is the port."""
    server = http.server.ThreadingHTTPServer(('127.0.0.1', port), None)
    server.RequestHandlerClass = handler(tuner, server.server_port)
    server.daemon_threads = True
    return server


# ---- export ----

def export(overrides_path, celestial_path, shadow_path, names, dry_run=False, out=sys.stdout):
    """Merge the overrides into the two profile inis; print the diff and the ground fog summary.
    Returns the list of files written (or that would be written)."""
    text = read_text(overrides_path)
    if not text:
        print(f'no overrides: {overrides_path} is missing or empty', file=out)
        return []
    model = parse_overrides(text)
    if not model['enabled']:
        print('note: [tuning] enabled = 0; exporting the overrides anyway', file=out)
    celestial_old, shadow_old = read_text(celestial_path), read_text(shadow_path)
    celestial, shadow = IniLines(celestial_old), IniLines(shadow_old)
    fog = []
    for section, values in model['sections'].items():
        zone_name = names.section(section)
        comment = f'# {zone_name}' if zone_name else (f'# {section.replace(":", " zone ")}' if section != 'default' else None)
        have = {k: v for k, (_, v) in celestial.keys(section).items()}
        for key, v in values.items():
            if key in ZONE_ONLY:
                continue
            try:
                if key in have and same(parse_value(key, have[key]), v):
                    continue   # same number, keep the line's own formatting
            except ValueError:
                pass
            celestial.set(section, key, format_value(key, v), comment_line=comment)
        if 'terrain_radius' in values:
            v = values['terrain_radius']
            old = shadow.keys(section).get('terrain_radius')
            try:
                unchanged = old is not None and same(parse_value('terrain_radius', old[1]), v)
            except ValueError:
                unchanged = False
            if not unchanged and (old is not None or not same(v, DEFAULT_RADIUS)):
                shadow.set(section, 'terrain_radius', format_value('terrain_radius', v), header_comment=comment)
        if any(k in values for k in FOG_KEYS):
            fog.append((section, zone_name, *(values.get(k, BUILTIN[k]) for k in FOG_KEYS)))
    written = []
    for path, old, doc in ((celestial_path, celestial_old, celestial), (shadow_path, shadow_old, shadow)):
        new = doc.text()
        if new == old:
            print(f'{path.name}: unchanged', file=out)
            continue
        diff = difflib.unified_diff(old.splitlines(), new.splitlines(), f'a/{path.name}', f'b/{path.name}', lineterm='')
        print('\n'.join(diff), file=out)
        written.append(path)
        if not dry_run:
            write_atomic(path, new)
    if fog:
        print('\nGround fog (no ini): paste each line into NorthlightRegionalFog::policy (src/sky/regional_fog.h)', file=out)
        print('AFTER the line `out.nightExtra*=1.2f;` and before `return out;`. The dev DLL applies scale and add to', file=out)
        print("policy()'s final values (night multiplier included) on outdoor cells, which is what these lines do", file=out)
        print('there (indoor cells have returned earlier), so they reproduce the tuned fog exactly. They work in a', file=out)
        print('zone without a branch of its own too: its zero density is only raised by the add.', file=out)
        for section, zone_name, day, night, day_add, night_add in fog:
            zone = section.split(':')[1]
            print(f'  {section:<18} {zone_name or "?":<24} day x{fmt(day)} +{fmt(day_add, places=6)}  '
                  f'night x{fmt(night)} +{fmt(night_add, places=6)}', file=out)
            print(f'    if(zone=={zone}){{out.day=out.day*{fmt(day)}f+{fmt(day_add, places=6)}f;'
                  f'out.nightExtra=out.nightExtra*{fmt(night)}f+{fmt(night_add, places=6)}f;}}', file=out)
    print(('\n(dry run: nothing written)' if dry_run else f'\nwritten: {", ".join(p.name for p in written) or "nothing"}'), file=out)
    return written


def check_client_config():
    """client-config/check_client_config.py compares the tracked copies with the client root's live files."""
    r = subprocess.run([sys.executable, str(CONFIG / 'check_client_config.py')], capture_output=True, text=True)
    return r.returncode, (r.stdout + r.stderr).strip()


# ---- command line ----

def client_arg(path):
    if path is not None:
        return path.expanduser().resolve()
    root = northlight_paths.client_root(required=False)
    if root is None:
        raise SystemExit('zone_tuner: pass --client <game root> (no client configured in northlight.local.ini)')
    return root


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    serve = sub.add_parser('serve', help='run the tuning page on 127.0.0.1')
    serve.add_argument('--client', type=Path, help='game root (default: the configured client)')
    serve.add_argument('--port', type=int, default=8765)
    exp = sub.add_parser('export', help='merge the overrides into client-config/')
    exp.add_argument('--client', type=Path, help='game root (default: the configured client)')
    exp.add_argument('--dry-run', action='store_true', help='print the diff, write nothing')
    exp.add_argument('--celestial', type=Path, default=CONFIG / 'celestial-profiles.ini')
    exp.add_argument('--shadow', type=Path, default=CONFIG / 'shadow-range-profiles.ini')
    args = parser.parse_args(argv)
    root = client_arg(args.client)
    if args.command == 'serve':
        names = ZoneNames(root, (root / 'shadow-range-profiles.ini', root / 'celestial-profiles.ini',
                                 CONFIG / 'shadow-range-profiles.ini', CONFIG / 'celestial-profiles.ini'))
        server = make_server(Tuner(root, names), args.port)
        print(f'zone tuner: http://127.0.0.1:{server.server_port}/  (game root {root}; Ctrl+C stops)')
        threading.Thread(target=names.load, daemon=True).start()
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        return 0
    names = ZoneNames(root, (args.shadow, args.celestial))
    try:
        export(root / OVERRIDES, args.celestial, args.shadow, names, args.dry_run)
    except ValueError as e:
        print(f'zone_tuner: {OVERRIDES}: {e}', file=sys.stderr)
        return 1
    if not args.dry_run and args.celestial.parent == CONFIG and args.shadow.parent == CONFIG:
        code, output = check_client_config()
        print('\ncheck_client_config.py:', output, sep='\n')
        if code:
            print('(expected after an export: the client root still has the old files. Copy client-config/ to the '
                  'game root when you install, or the next check_client_config.py --update would undo the export.)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
