"""Resolve local HLSL dependencies and materialize the split world source for fixtures."""
import hashlib
from pathlib import Path
import re
import shutil
import subprocess


INCLUDE = re.compile(rb'^[ \t]*#[ \t]*include[ \t]+"([^"\r\n]+)"', re.M)
WORLD_PART = re.compile(rb'^#include "(world_effects_[a-z_]+\.hlsl)"\r?\n', re.M)


def source_files(entry):
    entry = Path(entry).resolve()
    directory = entry.parent
    files = {}
    pending = [entry]
    while pending:
        path = pending.pop().resolve()
        if not path.is_relative_to(directory):
            raise ValueError(f'Shader include is outside {directory}: {path}')
        if path in files:
            continue
        files[path] = None
        pending.extend(path.parent / name.decode('utf-8') for name in INCLUDE.findall(path.read_bytes()))
    return tuple(files)


def source_hashes(entry):
    directory = Path(entry).resolve().parent
    return {path.relative_to(directory).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in source_files(entry)}


def copy_sources(entry, directory):
    parent = Path(entry).resolve().parent
    for path in source_files(entry):
        destination = directory / path.relative_to(parent)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)


def world_source_bytes(path):
    """Expand the world pass units, leaving shared library includes for the compiler."""
    path = Path(path)
    return WORLD_PART.sub(lambda match: path.with_name(match[1].decode('ascii')).read_bytes(), path.read_bytes())


def world_source_text(path):
    return world_source_bytes(path).decode('utf-8').replace('\r\n', '\n')


def world_source_at_ref(repo, ref, name='world_effects.hlsl'):
    """Read archived shader fixtures, including revisions that use pass units."""
    def read(filename):
        return subprocess.check_output(['git', 'show', f'{ref}:shaders/{filename}'], cwd=repo)

    return WORLD_PART.sub(lambda match: read(match[1].decode('ascii')), read(name))
