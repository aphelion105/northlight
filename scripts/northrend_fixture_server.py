"""Loopback-only WoW 12340 fixture for rendering scenes from local client data.

Packet layouts and SRP parameters follow the 3.3.5 branch of TrinityCore.
The fixed test credentials are local to this fixture. No world DB is needed.
"""
import argparse
import ctypes
from hashlib import sha1, sha256
import hmac
import math
from pathlib import Path
import secrets
import select
import socket
import struct
import threading
import time


ACCOUNT = b'NORTHLIGHT'
PASSWORD = b'NORTHLIGHT'
PLAYER = b'Northlight\0'
REALM = b'Northlight Render Fixture\0'
SPAWN = (5846.67, 604.13, 657.0, 0.778)
# Dry terrain at the edge of ADT Northrend_30_19, facing its visible river cells.
SHORELINE_SPAWN = (6902.08, 585.42, 425.99, math.pi)
# Near the player position inferred from the retained inn capture's view.
INTERIOR_SPAWN = (3648.70, -719.65, 215.50, .945)
# Camera region of the retained heavy terrain QA run.
TERRAIN_SPAWN = (6132.55, -2047.18, 416.50, 3.741)
# Ratchet (AreaTable 392), on the road above the harbor in Kalimdor_39_33.
RATCHET_SPAWN = (-948.0, -3749.0, 6.2, 1.5 * math.pi)
RATCHET_PAN = 'ratchet-pan'
RATCHET_CROWD_SIZE = 32
RATCHET_PHASES = ('idle', 'left', 'right', 'done')
# Valgarde (AreaTable 3981), beside the inn facing the cliffs across Daggercap Bay.
VALGARDE_SPAWN = (567.0, -4946.0, 18.40, math.tau - .94)
VALGARDE = 'valgarde'
VALGARDE_CROWD_SIZE = 32
VALGARDE_TILE = '41_30'
MAP = 571
ZONE = 65  # Dragonblight. The client derives the local area from its own ADT.
BUILD = 12340
WORLD_MINUTE = 12 * 60
PRIME = 0x894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7
GENERATOR = 7
SERVER_KEY = bytes.fromhex('cc98ae04e897eaca12ddc09342915357')
CLIENT_KEY = bytes.fromhex('c2b3723cc6aed9b5343c53ee2f4367ce')
SCENES = ('solo', 'crowd', 'm2-stress', 'rain', 'shoreline', 'interior', 'terrain', RATCHET_PAN, VALGARDE)
RAIN_STATE = 5  # Heavy rain in the 3.3.5 SMSG_WEATHER state table.
CROWD_SIZES = range(50, 129)
SMALL_BENCHMARK_CROWD = 8
DEFAULT_CROWD_SIZE = 128
M2_STRESS_SIZE = 512
CROWD_BATCH_SIZE = 32
FLIGHT_FLAGS = 0x03000400  # Can fly, flying and gravity disabled.

# Race, sex, native display, class, clothed CreatureDisplayInfo, main hand,
# off hand, permanent enchant. The native MPQs supply each armored model,
# weapon mesh and light. The stock display remains the race's native form.
OUTFITS = (
    (1, 0, 49, 1, 31286, 49623, 0, 3789),
    (4, 1, 56, 8, 29770, 45234, 0, 3854),
    (7, 0, 1563, 4, 31713, 34331, 34203, 2673),
    (1, 1, 50, 6, 31953, 50730, 0, 3370),
    (11, 0, 16125, 7, 31194, 46017, 34346, 2674),
    (3, 1, 54, 1, 31708, 50415, 0, 3870),
    (4, 0, 55, 11, 30883, 50603, 0, 1899),
    (3, 0, 53, 6, 31127, 50737, 34329, 3225),
)
LOCAL_APPEARANCE = (1, 0, 49, 1, 49)


def crowd_roster(count=DEFAULT_CROWD_SIZE, *, stress=False):
    if (count != M2_STRESS_SIZE if stress else count != SMALL_BENCHMARK_CROWD and count not in CROWD_SIZES):
        raise ValueError(f'The M2 stress scene needs {M2_STRESS_SIZE} players' if stress else 'The crowd needs 8 or 50–128 players')
    forward_x, forward_y = math.cos(SPAWN[3]), math.sin(SPAWN[3])
    roster = {}
    columns = 32 if stress else 12
    for index in range(count):
        row, column = divmod(index, columns)
        outfit = OUTFITS[(column * 3 + row) % len(OUTFITS)]
        if stress:
            depth = 8 + 1.35 * row + .28 * (column % 3)
            side = ((column - (columns - 1) / 2) / columns) * depth * 1.2 + (row % 2 - .5) * .28
            name_row, name_column = divmod(index, 26)
        else:
            depth = 5.5 + 2.1 * row + .28 * (column % 3)
            side = (column - (columns - 1) / 2) * 1.3 + (row % 2 - .5) * .65
            name_row, name_column = row, column
        position = (SPAWN[0] + depth * forward_x + side * forward_y,
                    SPAWN[1] + depth * forward_y - side * forward_x,
                    SPAWN[2], SPAWN[3] + math.pi + .18 * (column % 3 - 1))
        name = f'Fixture{chr(65 + name_row)}{chr(97 + name_column)}'.encode('ascii') + b'\0'
        roster[index + 2] = (name, position, outfit[:5], outfit[5:])
    return roster


def scene_roster(scene, crowd_size=DEFAULT_CROWD_SIZE):
    if scene in (RATCHET_PAN, VALGARDE):
        # Ground heights sampled from the stock ADTs. Each ring alternates an
        # enchanted character and one without a weapon or enchant.
        if scene == RATCHET_PAN:
            prefix = 'Ratchet'
            heights = ((7.95, 8.21, 7.90, 6.95, 6.15, 5.73, 5.58, 5.52, 5.41, 5.37, 5.53, 5.61, 5.46, 5.63, 6.44, 7.32),
                       (9.34, 9.33, 8.57, 6.92, 5.68, 5.65, 5.70, 5.32, 4.97, 4.82, 4.72, 4.19, 3.96, 4.87, 6.93, 8.76))
        else:
            prefix = 'Valgarde'
            heights = ((17.77, 17.66, 17.67, 17.82, 18.13, 18.54, 18.93, 19.23, 19.40, 19.50, 19.40, 19.33, 18.98, 18.73, 18.33, 18.00),
                       (17.36, 17.15, 16.97, 17.95, 18.67, 18.35, 17.21, 16.08, 15.34, 13.90, 12.50, 11.51, 12.39, 15.47, 18.31, 17.67))
        x, y, _, _ = spawn_for(scene)
        roster = {}
        for ring, (radius, levels) in enumerate(zip((9., 17.), heights)):
            for i, height in enumerate(levels):
                angle = (i + ring * .5) * math.tau / len(levels)
                appearance = OUTFITS[(i // 2 + 3 * ring) % len(OUTFITS)]
                position = (x + radius * math.cos(angle), y + radius * math.sin(angle),
                            height + .05, (angle + math.pi) % math.tau)
                name = f'{prefix}{chr(65 + ring)}{chr(97 + i)}'.encode('ascii') + b'\0'
                roster[2 + len(roster)] = (name, position, appearance[:5], appearance[5:] if (i + ring) % 2 == 0 else ())
        return roster
    if scene == 'm2-stress':
        return crowd_roster(M2_STRESS_SIZE, stress=True)
    return crowd_roster(crowd_size) if scene == 'crowd' else {}


def outfit_count(roster):
    return len({(player[2], player[3]) for player in roster.values()})


def spawn_record(roster, packet):
    return f'CROWD_SPAWN players={len(roster)} outfits={outfit_count(roster)} sha256={sha256(packet).hexdigest()}'


def rain_packet():
    return struct.pack('<IfB', RAIN_STATE, .85, 1)


def login_clock():
    hour, minute = divmod(WORLD_MINUTE, 60)
    return struct.pack('<IfI', (26 << 24) | (10 << 20) | (18 << 14) | (3 << 11) | (hour << 6) | minute, 0, 0)


def hash_bytes(*parts):
    return sha1(b''.join(parts)).digest()


def little(data):
    return int.from_bytes(data, 'little')


def integer(value):
    return value.to_bytes(32, 'little')


def session_key(secret):
    while secret.startswith(b'\0\0'):
        secret = secret[2:]
    if secret.startswith(b'\0'):
        secret = secret[2:]
    return bytes(value for pair in zip(hash_bytes(secret[::2]), hash_bytes(secret[1::2])) for value in pair)


def login_proof(salt, public_a, public_b, key):
    group = bytes(a ^ b for a, b in zip(hash_bytes(integer(PRIME)), hash_bytes(bytes([GENERATOR]))))
    return hash_bytes(group, hash_bytes(ACCOUNT), salt, public_a, public_b, key)


def cipher(key, direction):
    key = hmac.digest(direction, key, 'sha1')
    state = list(range(256))
    j = 0
    for i in range(256):
        j = (j + state[i] + key[i % len(key)]) & 255
        state[i], state[j] = state[j], state[i]
    i = j = 0

    def crypt(data):
        nonlocal i, j
        result = bytearray()
        for byte in data:
            i = (i + 1) & 255
            j = (j + state[i]) & 255
            state[i], state[j] = state[j], state[i]
            result.append(byte ^ state[(state[i] + state[j]) & 255])
        return bytes(result)

    crypt(bytes(1024))
    return crypt


def login(sock, world_port, session):
    with sock.makefile('rb') as incoming:
        header = incoming.read(4)
        if not header:
            return
        if len(header) != 4 or header[0] != 0 or not 30 <= little(header[2:]) <= 70:
            raise ValueError('Invalid login challenge')
        hello = incoming.read(little(header[2:]))
        if len(hello) < 30 or little(hello[7:9]) != BUILD or hello[29] != len(hello[30:]) or hello[30:].upper() != ACCOUNT:
            sock.sendall(b'\x00\x00\x04')
            return
        salt, private_b = secrets.token_bytes(32), secrets.randbelow(PRIME - 1) + 1
        x = little(hash_bytes(salt, hash_bytes(ACCOUNT, b':', PASSWORD)))
        verifier = pow(GENERATOR, x, PRIME)
        public_b = integer((3 * verifier + pow(GENERATOR, private_b, PRIME)) % PRIME)
        sock.sendall(b'\0\0\0' + public_b + bytes([1, GENERATOR, 32]) + integer(PRIME) + salt + bytes(17))
        response = incoming.read(75)
        if len(response) != 75 or response[0] != 1:
            raise ValueError('Invalid SRP proof packet')
        public_a, proof = response[1:33], response[33:53]
        a = little(public_a)
        if not 0 < a < PRIME:
            raise ValueError('Invalid SRP public value')
        u = little(hash_bytes(public_a, public_b))
        key = session_key(integer(pow(a * pow(verifier, u, PRIME), private_b, PRIME)))
        if not hmac.compare_digest(login_proof(salt, public_a, public_b, key), proof):
            sock.sendall(b'\x01\x04\x00\x00')
            return
        session['key'] = key
        sock.sendall(b'\x01\x00' + hash_bytes(public_a, proof, key) + bytes(10))
        print('LOGIN_OK', flush=True)
        send_realm_lists(incoming, sock, world_port)


def send_realm_lists(incoming, sock, world_port):
    entry = b'\0\0\0' + REALM + f'127.0.0.1:{world_port}'.encode('ascii') + b'\0'
    entry += struct.pack('<fBBB', 0.0, 1, 1, 1)
    payload = struct.pack('<IH', 0, 1) + entry + b'\x10\0'
    response = b'\x10' + struct.pack('<H', len(payload)) + payload
    while request := incoming.read(5):
        # The authenticated CMSG_REALM_LIST has four unused bytes after its opcode.
        if len(request) != 5 or request[0] != 0x10:
            raise ValueError('Invalid realm-list request')
        sock.sendall(response)
        print('REALM_LIST_SENT', flush=True)


def spawn_for(scene):
    return {'shoreline': SHORELINE_SPAWN, 'interior': INTERIOR_SPAWN,
            'terrain': TERRAIN_SPAWN, RATCHET_PAN: RATCHET_SPAWN, VALGARDE: VALGARDE_SPAWN}.get(scene, SPAWN)


def map_for(scene):
    if scene == VALGARDE:
        return (MAP, 495, 'Northrend')
    return (1, 392, 'Kalimdor') if scene == RATCHET_PAN else (MAP, ZONE, 'Northrend')


def ratchet_camera_marker(body):
    # CMSG_MESSAGECHAT: 32-bit type and language, followed by the SAY text.
    if body[:4] == struct.pack('<I', 1):
        return next((phase for phase in RATCHET_PHASES
                     if body[8:] == f'NorthlightRatchet:{phase}\0'.encode('ascii')), None)
    return None


def gameplay_message(body):
    if body[:4] != struct.pack('<I', 1) or not body.endswith(b'\0') or len(body) > 264:
        return None
    prefix = b'NorthlightBench:'
    if body[8:].startswith(prefix):
        message = body[8 + len(prefix):-1].decode('ascii', errors='replace')
        if message.isascii() and message.isprintable():
            return message
    return None


def character_list(spawn=SPAWN, *, map_id=MAP, zone=ZONE):
    appearance = bytes([1, 1, 0, 0, 0, 0, 0, 0, 80])  # Human male warrior.
    position = struct.pack('<II3f', zone, map_id, *spawn[:3])
    return b'\x01' + struct.pack('<Q', 1) + PLAYER + appearance + position + bytes(25 + 23 * 9)


def as_field_float(value):
    return little(struct.pack('<f', value))


def field_values(data):
    mask = [0] * (max(data) // 32 + 1)
    for field in data:
        mask[field // 32] |= 1 << (field % 32)
    return bytes([len(mask)]) + struct.pack(f'<{len(mask)}I', *mask) + struct.pack(
        f'<{len(data)}I', *(data[index] for index in sorted(data)))


def packed_guid(guid):
    raw = struct.pack('<Q', guid)
    return bytes((sum(1 << i for i, byte in enumerate(raw) if byte),)) + bytes(byte for byte in raw if byte)


def player_block(guid, position, appearance, outfit=(), *, flying=False):
    # OBJECT, UNIT and PLAYER fields from the 12340 update table.
    race, sex, native_display, player_class, display = appearance
    data = {
        0x00: guid, 0x02: 0x19, 0x04: as_field_float(1),
        0x17: race | (player_class << 8) | (sex << 16), 0x18: 1000, 0x20: 1000,
        0x36: 80, 0x37: 1, 0x41: as_field_float(0.389),
        0x42: as_field_float(1.5), 0x43: display, 0x44: native_display,
        0x50: as_field_float(1.0),
    }
    if outfit:
        data[0x7A] = 1  # UNIT_FIELD_BYTES_2: draw melee weapons in hand.
        data[0x9B] = sex  # PLAYER_BYTES_3: native gender.
        main_hand, off_hand, enchant = outfit
        data.update({0x139: main_hand, 0x13A: enchant})
        if off_hand:
            data[0x13B] = off_hand
    flight_flags = FLIGHT_FLAGS if flying else 0
    movement = struct.pack('<IHI4f', flight_flags, 0, 0, *position)
    if flying:
        # Build 12340 includes pitch between position and fall time while flying.
        movement += struct.pack('<f', 0.)
    movement += struct.pack('<I9f', 0, 2.5, 7, 4.5, 4.7, 2.5, 7, 4.5, 3.14, 3.14)
    return b'\x03' + packed_guid(guid) + struct.pack('<BH', 4, 0x21 if guid == 1 else 0x20) + movement + field_values(data)


def player_create(spawn=SPAWN, *, flying=False):
    return struct.pack('<I', 1) + player_block(1, spawn, LOCAL_APPEARANCE, flying=flying)


def gameobject_block(counter, display, position, *, scale=1.):
    entry = 930001  # Local fixture entry. The client resolves the supplied DBC display.
    guid = (0xf110 << 48) | (entry << 24) | counter
    fields = {0: guid & 0xffffffff, 1: guid >> 32, 2: 0x21, 3: entry,
              4: as_field_float(scale), 8: display, 13: as_field_float(1),
              17: int.from_bytes(bytes((1, 5, 0, 255)), 'little')}
    # Stationary GameObject, zero rotation, no transport.
    movement = struct.pack('<HB8fIQ', 0x350, 0, *position, *position, 0., 0., counter, 0)
    block = b'\x03' + packed_guid(guid) + b'\x05' + movement + field_values(fields)
    return guid, block


def gameobject_updates(previous_guid, block=b''):
    packets = []
    if previous_guid is not None:
        packets.append((0x0AA, struct.pack('<QB', previous_guid, 0)))
    if block:
        packets.append((0x0A9, struct.pack('<I', 1) + block))
    return packets


def crowd_create(roster):
    return struct.pack('<I', len(roster)) + b''.join(
        player_block(guid, position, appearance, outfit)
        for guid, (_, position, appearance, outfit) in roster.items())


def crowd_packets(roster):
    players = list(roster.items())
    return [crowd_create(dict(players[start:start + CROWD_BATCH_SIZE]))
            for start in range(0, len(players), CROWD_BATCH_SIZE)]


def teleport_packet(position, move_time, sequence, *, flying=False):
    # MSG_MOVE_TELEPORT_ACK server message: packed GUID, sequence and movement status.
    flags = FLIGHT_FLAGS if flying else 0
    status = struct.pack('<IHI4f', flags, 0, move_time, *position)
    if flying:
        status += struct.pack('<f', 0.)
    return packed_guid(1) + struct.pack('<I', sequence) + status + struct.pack('<I', 0)


def turn_packet(spawn, move_time, *, flying=False):
    position = (*spawn[:3], (spawn[3] + math.pi / 3) % math.tau)
    return teleport_packet(position, move_time, 1, flying=flying)


def world(sock, session, scene, crowd_size=DEFAULT_CROWD_SIZE, return_to_menu=None, camera_turn=None, *,
          flying=False, camera_route=None, camera_updates=None, gameplay_benchmark=False):
    roster = scene_roster(scene, crowd_size)
    spawn = spawn_for(scene)
    map_id, zone, _ = map_for(scene)
    outgoing = incoming_cipher = lambda data: data
    with sock.makefile('rb') as incoming:
        def receive():
            header = incoming.read(6)
            if not header:
                return None, b''
            if len(header) != 6:
                raise ValueError('Incomplete world header')
            wire_header = header
            header = incoming_cipher(header)
            size, opcode = int.from_bytes(header[:2], 'big'), little(header[2:])
            if not 4 <= size <= 8192:
                raise ValueError(f'Unexpected world header {wire_header.hex()} decoded={header.hex()}')
            body = incoming.read(size - 4)
            if len(body) != size - 4:
                raise ValueError('Incomplete world body')
            return opcode, body

        def send(opcode, payload):
            header = struct.pack('>H', len(payload) + 2) + struct.pack('<H', opcode)
            sock.sendall(outgoing(header) + payload)

        seed = secrets.token_bytes(4)
        send(0x1EC, struct.pack('<I', 1) + seed + secrets.token_bytes(32))
        opcode, payload = receive()
        if opcode is None:
            return
        end = payload.find(b'\0', 8)
        key = session.get('key')
        if (opcode != 0x1ED or not key or end < 8 or len(payload) < end + 49 or
                little(payload[:4]) != BUILD or payload[8:end].upper() != ACCOUNT):
            raise ValueError('Invalid world authentication message')
        credentials = payload[end + 1:]
        expected = hash_bytes(payload[8:end], bytes(4), credentials[4:8], seed, key)
        if not hmac.compare_digest(expected, credentials[28:48]):
            raise ValueError('World authentication proof mismatch')
        outgoing, incoming_cipher = cipher(key, SERVER_KEY), cipher(key, CLIENT_KEY)
        send(0x1EE, b'\x0c' + bytes(9) + b'\x02')
        send(0x4AB, bytes(4))  # Client cache version.
        send(0x0FD, bytes(32))  # Tutorial flags.
        print('WORLD_AUTH_OK', flush=True)
        in_world = False
        named = set()
        weather_triggered = set()
        pending_camera = None
        route_sequence = 1
        benchmark_requested = benchmark_armed = False

        def arm_benchmark():
            nonlocal benchmark_armed
            if benchmark_requested and not benchmark_armed and len(named) == len(roster):
                text = b'NorthlightBenchReady\0'
                send(0x096, struct.pack('<BIQIQI', 0, 0, 0, 0, 0, len(text)) + text + b'\0')
                benchmark_armed = True
                print(f'BENCH_ARM actors={len(named)}', flush=True)

        def send_camera_updates(sequence):
            if camera_updates:
                for opcode, payload in camera_updates.get(sequence, ()):
                    send(opcode, payload)

        def end_world():
            nonlocal benchmark_requested, benchmark_armed
            send(0x04C, bytes(4) + b'\x01')
            send(0x04D, b'')
            named.clear()
            weather_triggered.clear()
            benchmark_requested = benchmark_armed = False
            print('PLAYER_LOGOUT', flush=True)
        while True:
            if in_world and (return_to_menu is not None or camera_turn is not None or camera_route is not None):
                if return_to_menu is not None and return_to_menu.is_file():
                    return_to_menu.unlink()
                    return_to_menu = None
                    end_world()
                    in_world = False
                    continue
                if pending_camera is None and camera_turn is not None and camera_turn.is_file():
                    camera_turn.unlink()
                    camera_turn = None
                    now = (time.monotonic_ns() // 1_000_000) & 0xFFFFFFFF
                    send(0x0C7, turn_packet(spawn, now, flying=flying))
                    pending_camera = 1
                    print('CAMERA_TURN_SENT sequence=1', flush=True)
                if camera_route is not None and pending_camera is None and not camera_route.empty():
                    route_sequence += 1
                    position = camera_route.get_nowait()
                    now = (time.monotonic_ns() // 1_000_000) & 0xFFFFFFFF
                    send(0x0C7, teleport_packet(position, now, route_sequence, flying=flying))
                    send_camera_updates(route_sequence)
                    pending_camera = route_sequence
                    print(f'CAMERA_ROUTE_SENT sequence={route_sequence}', flush=True)
                if not select.select([sock], [], [], .25)[0]:
                    continue
            opcode, body = receive()
            if opcode is None:
                return
            if opcode == 0x037:
                send(0x03B, character_list(spawn, map_id=map_id, zone=zone))
                print('CHAR_LIST', flush=True)
            elif opcode == 0x03D and body == struct.pack('<Q', 1) and not in_world:
                send(0x236, struct.pack('<I4f', map_id, *spawn))
                send(0x042, login_clock())
                send(0x12A, bytes(5))  # Initial spell and cooldown counts.
                send(0x0A9, player_create(spawn, flying=flying))
                send_camera_updates(1)
                if flying:
                    send(0x343, packed_guid(1) + struct.pack('<I', 0))  # SMSG_MOVE_SET_CAN_FLY
                    print('PLAYER_FLIGHT_ENABLED', flush=True)
                if roster:
                    crowd = crowd_packets(roster)
                    for index, packet in enumerate(crowd):
                        # Give the client an update interval between bursts while
                        # the loading UI and first scene resources are active.
                        if index:
                            time.sleep(.2)
                        send(0x0A9, packet)
                    print(spawn_record(roster, b''.join(crowd)), flush=True)
                send(0x390, bytes(4))  # Time sync request.
                print(f'PLAYER_SPAWN map={map_id} position={spawn}', flush=True)
                in_world = True
            elif opcode == 0x1DC and len(body) == 8:
                send(0x1DD, body[:4])
            elif opcode == 0x095 and in_world:
                if gameplay_benchmark and (message := gameplay_message(body)):
                    if message == 'arm':
                        benchmark_requested = True
                        arm_benchmark()
                    elif benchmark_armed and message in ('start', 'right', 'done'):
                        print(f'BENCH phase={message} tick_ns={time.monotonic_ns()}', flush=True)
                    elif benchmark_armed and message.startswith('effects:'):
                        print(f'BENCH_EFFECTS {message.removeprefix("effects:")}', flush=True)
                    elif benchmark_armed and (message.startswith(('header:', 'data:')) or message == 'complete'):
                        print(f'BENCH_PAYLOAD {message}', flush=True)
                elif scene == RATCHET_PAN and (phase := ratchet_camera_marker(body)):
                    tick = ctypes.windll.kernel32.GetTickCount() & 0xffffffff
                    print(f'RATCHET_CAMERA phase={phase} tick={tick}', flush=True)
            elif (opcode == 0x0C7 and pending_camera is not None and len(body) == 10 and
                  body[:6] == packed_guid(1) + struct.pack('<I', pending_camera)):
                label = 'CAMERA_TURN_ACK' if pending_camera == 1 else 'CAMERA_ROUTE_ACK'
                print(f'{label} sequence={pending_camera}', flush=True)
                pending_camera = None
            elif scene in ('rain', 'shoreline') and in_world and (opcode, len(body)) in ((0x1F4, 4), (0x391, 8)):
                if opcode not in weather_triggered:
                    body = rain_packet()
                    send(0x2F4, body)
                    weather_triggered.add(opcode)
                    print(f'WEATHER_SENT kind=rain source={opcode:03x} packet={body.hex()}', flush=True)
            elif opcode == 0x050 and len(body) == 8:
                guid = little(body)
                if guid == 1 or guid in roster:
                    name, _, (race, sex, _, player_class, _), _ = roster.get(guid, (PLAYER, spawn, LOCAL_APPEARANCE, ()))
                    send(0x051, packed_guid(guid) + b'\0' + name + bytes((0, race, sex, player_class, 0)))
                    if guid != 1 and guid not in named:
                        named.add(guid)
                        print(f'CROWD_NAME guid={guid}', flush=True)
                        arm_benchmark()
            elif opcode == 0x04B and in_world:
                end_world()
                in_world = False


def listen(port):
    server = socket.socket()
    server.bind(('127.0.0.1', port))
    server.listen(1)
    return server


def serve(server, handler):
    while True:
        connection, _ = server.accept()
        with connection:
            connection.settimeout(90)
            try:
                handler(connection)
            except (ValueError, OSError) as error:
                print(f'CONNECTION_ENDED {error}', flush=True)


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--auth-port', type=int, default=3724)
    parser.add_argument('--scene', choices=SCENES, default='solo')
    parser.add_argument('--return-to-menu', type=Path, help='Private runner marker for a server-initiated logout')
    parser.add_argument('--camera-turn', type=Path, help='Private runner marker for a same-position heading change')
    parser.add_argument('--flying', action='store_true', help='Spawn the local player in flight with gravity disabled')
    parser.add_argument('--crowd-size', type=int, choices=(SMALL_BENCHMARK_CROWD, *CROWD_SIZES),
                        default=DEFAULT_CROWD_SIZE, metavar='8,50..128')
    parser.add_argument('--gameplay-benchmark', action='store_true', help='Authorize the in-world timing add-on')
    args = parser.parse_args()
    if args.crowd_size == SMALL_BENCHMARK_CROWD and not args.gameplay_benchmark:
        parser.error('The 8-player crowd requires --gameplay-benchmark')
    session = {}
    with listen(args.auth_port) as auth, listen(0) as game:
        port = game.getsockname()[1]
        thread = threading.Thread(target=serve, args=(auth, lambda sock: login(sock, port, session)), daemon=True)
        thread.start()
        print(f'READY auth={auth.getsockname()[1]} world={port}', flush=True)
        serve(game, lambda sock: world(sock, session, args.scene, args.crowd_size, args.return_to_menu,
                                       args.camera_turn, flying=args.flying, gameplay_benchmark=args.gameplay_benchmark))


if __name__ == '__main__':
    main()
