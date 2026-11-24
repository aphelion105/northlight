"""Flatten selected implementation units for fixtures that extract C++ scopes."""
import re


UNITS = {
    'device_draw_preparation.inl', 'device_draw_submission.inl', 'device_effect_resources.inl',
    'device_effect_targets.h', 'device_frame_effects.inl', 'renderer_backend_windows.h',
    'renderer_log.h', 'renderer_startup.h', 'stream_buffer_locks.inl', 'stream_image_locks.inl',
    'stream_image_proxies.inl', 'stream_proxy_lifetime.inl', 'stream_query_proxy.inl', 'world_atmosphere_frame.inl', 'world_build_pipeline.inl',
    'world_directional_cache_draw.inl', 'world_directional_cache_plan.inl', 'world_directional_pass.inl',
    'world_mesh_publication.inl', 'world_mesh_stage.h',
    'world_publication.h', 'world_region_build.inl', 'world_replay_upload.inl', 'world_screen_lighting.inl',
}


def source_text(path, encoding='utf-8'):
    def expand(match):
        name = match[1]
        return source_text(path.with_name(name), encoding) if name in UNITS else match[0]

    content = path.read_text(encoding=encoding)
    if path.name in UNITS:
        content = content.removeprefix('#pragma once\n')
    return re.sub(r'^#include "([^"\n]+)"$', expand, content, flags=re.M)


def world_render_path(world):
    """Follow the synchronous directional pass for assertions about frame order."""
    start = world.index('    bool renderDirectional(')
    end = world.index('\n    }\n', start)
    directional = world[world.index('{\n', start, end) + 2:end]
    render = world[world.index('    bool render(IDirect3DSurface9* targetSurface'):]
    render, calls = re.subn(r'^        if\(!renderDirectional\([^;]+?\)\)',
                           lambda call: directional + '\n' + call[0], render, flags=re.M)
    assert calls == 1, 'WorldRenderer must call the directional pass exactly once'
    return render
