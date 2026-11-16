#!/usr/bin/env python3
"""Lossless compact captures: pack old/new sessions, or restore ordinary files.

    python scripts/capture_files.py pack SESSION_OR_ZIP compact.zip
    python scripts/capture_files.py unpack SESSION_OR_ZIP new-directory

No game/client access. Inputs are read-only; the output must not already exist.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import tempfile
import zipfile

MAGIC = b'NLCREF1\n'
BLOBS = '.capture-blobs'
BINARY = {'.fgr', '.bin', '.f32', '.bc'}
MAX_FILE = 256 * 1024 * 1024
REFERENCE = re.compile(rb'NLCREF1\n([0-9a-f]{64}) (0|[1-9][0-9]*)\n\Z')


def reference(data):
    if not data.startswith(MAGIC):
        return None
    match = REFERENCE.fullmatch(data)
    if not match or len(match[2]) > 10 or int(match[2]) > MAX_FILE:
        raise ValueError('Invalid compact capture reference')
    return match[1].decode('ascii'), int(match[2])


def checked_blob(data, digest, size):
    if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
        raise ValueError('Missing, truncated or corrupt capture blob: ' + digest)
    return data


def read_bytes(path):
    """Read either an ordinary capture file or a checked, sibling blob reference."""
    path = Path(path)
    if path.is_symlink() or path.stat().st_size > MAX_FILE:
        raise ValueError('Unsafe or oversized capture file')
    data = path.read_bytes()
    ref = reference(data)
    if ref is None:
        return data
    digest, size = ref
    blob = path.parent / BLOBS / digest
    if blob.is_symlink() or blob.parent.is_symlink() or blob.stat().st_size != size:
        raise ValueError('Unsafe or truncated capture blob')
    return checked_blob(blob.read_bytes(), digest, size)


def safe_name(name):
    parts = name.split('/')
    if not name or '\\' in name or ':' in name or any(p in ('', '.', '..') for p in parts):
        raise ValueError('Unsafe capture path: ' + name)
    return PurePosixPath(name)


class CaptureInput:
    def __init__(self, source):
        self.source = Path(source)
        self.archive = None
        if self.source.is_dir():
            entries = []
            for path in self.source.rglob('*'):
                if path.is_symlink():
                    raise ValueError('Capture contains a symbolic link')
                if path.is_file():
                    entries.append((path.relative_to(self.source).as_posix(), path.stat().st_size))
        else:
            self.archive = zipfile.ZipFile(self.source)
            entries = []
            try:
                for item in self.archive.infolist():
                    if stat.S_ISLNK(item.external_attr >> 16):
                        raise ValueError('Capture ZIP contains a symbolic link')
                    safe_name(item.filename.rstrip('/'))
                    if not item.is_dir():
                        entries.append((item.filename, item.file_size))
            except Exception:
                self.close()
                raise
        try:
            seen = set()
            for name, size in entries:
                safe_name(name)
                if name.casefold() in seen or size > MAX_FILE:
                    raise ValueError('Duplicate capture path or oversized file: ' + name)
                seen.add(name.casefold())
            if len(entries) > 100000 or sum(size for _, size in entries) > 16 * 1024**3:
                raise ValueError('Capture exceeds file/size limits')
            self.names = sorted(name for name, _ in entries if BLOBS not in PurePosixPath(name).parts)
            self.sizes = dict(entries)
        except Exception:
            self.close()
            raise

    def close(self):
        if self.archive:
            self.archive.close()

    def raw(self, name):
        if name not in self.sizes:
            raise ValueError('Missing capture file: ' + name)
        if self.archive:
            with self.archive.open(name) as stream:
                data = stream.read(MAX_FILE + 1)
        else:
            data = (self.source / name).read_bytes()
        if len(data) != self.sizes[name] or len(data) > MAX_FILE:
            raise ValueError('Capture file changed or exceeded its size limit: ' + name)
        return data

    def read(self, name):
        data = self.raw(name)
        ref = reference(data)
        if ref is None:
            return data
        digest, size = ref
        blob = (PurePosixPath(name).parent / BLOBS / digest).as_posix()
        return checked_blob(self.raw(blob), digest, size)


def destination(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    if output.exists() or source == output or (source.is_dir() and source in output.parents):
        raise ValueError('Output must be new and outside the source capture')
    output.parent.mkdir(parents=True, exist_ok=True)
    return output


def pack(source, output):
    output = destination(source, output)
    capture = CaptureInput(source)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, suffix='.zip.tmp', delete=False) as stream:
            temporary = Path(stream.name)
        blobs = set()
        logical = unique = repeats = 0
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            archive.comment = b'Northlight compact capture v1; restore with scripts/capture_files.py unpack'
            for name in capture.names:
                data = capture.read(name)
                logical += len(data)
                if PurePosixPath(name).suffix.lower() in BINARY:
                    digest = hashlib.sha256(data).hexdigest()
                    blob = (PurePosixPath(name).parent / BLOBS / digest).as_posix()
                    if blob not in blobs:
                        archive.writestr(blob, data)
                        unique += len(data)
                        blobs.add(blob)
                    else:
                        repeats += 1
                    archive.writestr(name, MAGIC + f'{digest} {len(data)}\n'.encode('ascii'))
                else:
                    archive.writestr(name, data)
        if output.exists():
            raise ValueError('Output appeared during packing')
        temporary.rename(output)
        return dict(files=len(capture.names), logical_bytes=logical, unique_binary_bytes=unique,
                    duplicate_binary_files=repeats, zip_bytes=output.stat().st_size)
    finally:
        capture.close()
        if temporary and temporary.exists():
            temporary.unlink()


def unpack(source, output):
    output = destination(source, output)
    capture = CaptureInput(source)
    temporary = Path(tempfile.mkdtemp(dir=output.parent, prefix='capture-unpack-'))
    try:
        # Fresh staging tree: no archive path can traverse pre-existing links.
        saved = {}
        total = 0
        for name in capture.names:
            data = capture.read(name)
            total += len(data)
            if total > 16 * 1024**3:
                raise ValueError('Expanded capture exceeds size limit')
            path = temporary / name
            path.parent.mkdir(parents=True, exist_ok=True)
            digest = hashlib.sha256(data).hexdigest()
            if digest in saved:
                try:
                    os.link(saved[digest], path)
                    continue
                except OSError:
                    pass  # Filesystems without hard links still get exact bytes.
            path.write_bytes(data)
            saved[digest] = path
        if output.exists():
            raise ValueError('Output appeared during unpacking')
        temporary.rename(output)
        return dict(files=len(capture.names), logical_bytes=total, unique_contents=len(saved))
    finally:
        capture.close()
        if temporary.exists():
            assert temporary.resolve().parent == output.parent and temporary.name.startswith('capture-unpack-')
            shutil.rmtree(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=('pack', 'unpack'))
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print(json.dumps((pack if args.command == 'pack' else unpack)(args.source, args.output), indent=2))


if __name__ == '__main__':
    main()
