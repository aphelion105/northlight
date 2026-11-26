"""Report the graphics path and Northlight settings observed in a gameplay run."""

import ctypes
from ctypes import wintypes
import re


WINDOW_RENDERERS = {'GxWindowClassD3d': 'D3D9', 'GxWindowClassD3d9Ex': 'D3D9Ex',
                    'GxWindowClassVulkan': 'Vulkan'}
D3D9_EFFECTS = ('GI', 'WorldShadows', 'ActorShadows', 'PointShadows', 'WorldFog', 'FogClouds',
                'LocalLights', 'EquipmentLights', 'ContactAO', 'AutoBump', 'Weather', 'RainFog',
                'CelestialEffects', 'Godrays', 'RainLens', 'SnowLens', 'SandLens')


def d3d9on12_loaded(pid):
    """Observe the owned client's loader after timing. None means no reliable snapshot."""
    class Module(ctypes.Structure):
        _fields_ = [('size', wintypes.DWORD), ('module', wintypes.DWORD), ('pid', wintypes.DWORD),
                    ('global_usage', wintypes.DWORD), ('process_usage', wintypes.DWORD),
                    ('base', ctypes.c_void_p), ('bytes', wintypes.DWORD), ('handle', wintypes.HMODULE),
                    ('name', wintypes.WCHAR * 256), ('path', wintypes.WCHAR * 260)]

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    snapshot = kernel.CreateToolhelp32Snapshot
    snapshot.argtypes = (wintypes.DWORD, wintypes.DWORD)
    snapshot.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    first, advance = kernel.Module32FirstW, kernel.Module32NextW
    for function in (first, advance):
        function.argtypes = (wintypes.HANDLE, ctypes.POINTER(Module))
        function.restype = wintypes.BOOL
    # TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32 covers both game architectures.
    for _ in range(3):
        handle = snapshot(0x18, pid)
        if handle != ctypes.c_void_p(-1).value:
            break
        if ctypes.get_last_error() != 24:  # ERROR_BAD_LENGTH: the loader was changing.
            return None
    else:
        return None
    try:
        entry = Module(size=ctypes.sizeof(Module))
        if not first(handle, ctypes.byref(entry)):
            return None
        while True:
            if entry.name.casefold() == 'd3d9on12.dll':
                return True
            if not advance(handle, ctypes.byref(entry)):
                return False if ctypes.get_last_error() == 18 else None  # ERROR_NO_MORE_FILES
    finally:
        kernel.CloseHandle(handle)


def d3d9_quality(lines):
    preset = None
    settings = {}
    switches = {}
    master = True
    fault = False
    for line in lines:
        if match := re.match(r'^QUALITY preset=(\w+) ', line):
            preset = match[1]
            settings = {key: int(value) for key, value in re.findall(r'\b(\w+)=(-?\d+)\(', line)}
            switches = {}
            master = True
            fault = False
        elif line.startswith('SETTINGS applied '):
            if match := re.search(r'\bkey=(\w+) previous=-?\d+ value=(-?\d+)\b', line):
                settings[match[1]] = int(match[2])
        elif line.startswith('Effects components '):
            switches.update({key: state == 'ON' for key, state in re.findall(r'\b(GI|shadows|fog)=(ON|OFF)\b', line)})
        elif line.startswith(('Effects ON ', 'Effects OFF ')):
            master = line.startswith('Effects ON ')
        elif line.startswith('EXTENSION fault '):
            fault = True
    return preset, settings, switches, master and not fault


def d3d9_effects(settings, switches, master):
    enabled = {key: bool(settings[key]) if key in settings else None for key in D3D9_EFFECTS}
    for effect, switch, gate in (('GI', 'GI', 'GIStrength'), ('WorldShadows', 'shadows', None),
                                 ('WorldFog', 'fog', None), ('LocalLights', None, 'LocalLightLimit'),
                                 ('PointShadows', 'shadows', 'PointShadowLimit')):
        if switches.get(switch) is False or (gate is not None and settings.get(gate) == 0):
            enabled[effect] = False
    for effect, parent in (('ActorShadows', 'WorldShadows'), ('PointShadows', 'WorldShadows'),
                           ('PointShadows', 'LocalLights'), ('EquipmentLights', 'LocalLights')):
        if enabled[parent] is False:
            enabled[effect] = False
    if not master or settings.get('Effects') == 0:
        return dict.fromkeys(enabled, False)
    return enabled


def evidence(observed, northlight_log, gx_log, vulkan_log, dxvk_log, fixture_log):
    runtime = dict(observed)
    renderer = WINDOW_RENDERERS.get(runtime.get('window_class'), 'unknown')
    if renderer == 'unknown':
        if any('vulkan: frame: first frame presented' in line for line in vulkan_log):
            renderer = 'Vulkan'
        elif any('CGxDeviceD3d::DeviceSetFormat()' in line for line in gx_log):
            renderer = 'D3D9'
    runtime['renderer'] = renderer
    selected = next((match[1] for line in reversed(northlight_log) if
                     (match := re.search(r'^BACKEND selected=.* runtime=(.+?) rules=\S+ ', line))), None)
    driver_version = next((match[1] for line in reversed(dxvk_log) if
                           (match := re.fullmatch(r'info:\s+DXVK:\s+(v\d+\.\d+[\w.+-]*)', line))), None)
    dxvk = selected != 'non-DXVK' if selected else bool(driver_version)
    version = selected if selected and selected.startswith('v') else driver_version
    expected_proxy = runtime.get('case', '').endswith('-dxvk')
    runtime['dxvk'] = ('active' if dxvk else 'off' if selected or (renderer != 'unknown' and not expected_proxy)
                       else 'unknown')
    runtime['dxvk_version'] = version if dxvk else None
    hdr_row = next((line for line in reversed(northlight_log) if line.startswith('HDR experimental ')), '')
    swapchain = next((line for line in reversed(vulkan_log) if 'swapchain created' in line), '')
    native_present = any(line.startswith('HDR native DXGI Present ') and 'transfer=D3D9On12 GPU' in line
                         for line in northlight_log)
    loaded = runtime.get('d3d9on12_loaded')
    runtime['d3d9on12'] = ('active' if native_present else 'loaded' if loaded else
                           'off' if loaded is False else 'unknown')
    hdr = 'EXTENDED_SRGB_LINEAR' in swapchain if renderer == 'Vulkan' else ' active=1 ' in hdr_row
    if renderer == 'unknown' or (renderer == 'Vulkan' and not swapchain):
        runtime['presentation'] = 'unknown'
    else:
        runtime['presentation'] = 'HDR scRGB' if hdr else 'SDR'
    if renderer == 'Vulkan':
        status = {}
        for line in gx_log:
            if match := re.search(r'Northlight Vulkan (contact lighting|bloom): (.+)$', line):
                status[match[1]] = match[2]
        for line in fixture_log:
            if match := re.fullmatch(r'BENCH_EFFECTS ([^:]+):([^:]+)', line):
                for key, value in zip(('contact lighting', 'bloom'), match.groups()):
                    if value != 'unavailable' or key not in status:
                        status[key] = value
        contact, bloom = (status.get(key, 'unknown') for key in ('contact lighting', 'bloom'))
        runtime['northlight'] = ('active' if 'active' in status.values() else 'unavailable' if contact == bloom == 'unavailable'
                                 else 'inactive' if status else 'unknown')
        runtime['effects'] = {'ContactAO': contact, 'LocalColorBounce': contact, 'Bloom': bloom}
    elif any(line.startswith('D3D9 device wrapped.') for line in northlight_log):
        preset, settings, switches, master = d3d9_quality(northlight_log)
        runtime['northlight'] = 'effects-enabled' if master and settings.get('Effects') == 1 else (
            'effects-disabled' if not master or settings.get('Effects') == 0 else 'unknown')
        runtime['quality_preset'] = preset
        runtime['quality_settings'] = settings
        runtime['effects'] = d3d9_effects(settings, switches, master)
        runtime['effects']['Tideglass'] = any(line == 'TIDEGLASS cooperative integration active.' for line in northlight_log)
    else:
        passthrough = any(line.startswith(('X64 D3D9 passthrough:', 'Northlight effects disabled:')) for line in northlight_log)
        stock = runtime.get('case', '').startswith('stock-')
        runtime['northlight'] = 'passthrough' if passthrough else 'off' if stock and renderer == 'D3D9' else 'unknown'
        runtime['effects'] = {}
    return runtime
