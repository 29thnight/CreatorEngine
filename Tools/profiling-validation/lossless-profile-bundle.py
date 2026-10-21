"""Standard-library lossless profiling archives, split below Git's file limit.

pack --spec files.json --output NEW_DIRECTORY
verify --manifest DIRECTORY/manifest.json
unpack --manifest DIRECTORY/manifest.json --output NEW_DIRECTORY [--json]

The spec is a list of {source, path} entries. Source bytes are never filtered or
re-encoded. --json additionally expands every embedded CPU/GPU timeline blob.
"""
import argparse
import base64
import gzip
import hashlib
import io
import json
import lzma
from pathlib import Path, PurePosixPath
import re
import tarfile
import time


def digest_file(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1 << 20):
            digest.update(block)
    return digest.hexdigest()


def safe_name(name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
        raise ValueError(f'Invalid archive path: {name}')
    return path


class PartWriter(io.RawIOBase):
    def __init__(self, directory, limit):
        self.directory, self.limit = directory, limit
        self.file = None
        self.parts = []
        self.size = 0
        self.hash = hashlib.sha256()
        self.archive_hash = hashlib.sha256()

    def writable(self):
        return True

    def finish_part(self):
        if self.file is not None:
            name = Path(self.file.name).name
            self.file.close()
            self.parts.append(dict(path=name, bytes=self.size, sha256=self.hash.hexdigest()))
            self.file = None

    def write(self, data):
        count = len(data)
        self.archive_hash.update(data)
        while data:
            if self.file is None:
                self.file = (self.directory/f'data.tar.xz.part{len(self.parts)+1:03d}').open('xb')
                self.size = 0
                self.hash = hashlib.sha256()
            block = data[:self.limit-self.size]
            self.file.write(block)
            self.hash.update(block)
            self.size += len(block)
            data = data[len(block):]
            if self.size == self.limit:
                self.finish_part()
        return count


class PartReader(io.RawIOBase):
    def __init__(self, directory, parts):
        self.paths = iter(directory/safe_name(part['path']) for part in parts)
        self.file = None

    def readable(self):
        return True

    def read(self, size=-1):
        if size < 0:
            return b''.join(iter(lambda: self.read(1 << 20), b''))
        output = bytearray()
        while len(output) < size:
            if self.file is None:
                path = next(self.paths, None)
                if path is None:
                    break
                self.file = path.open('rb')
            block = self.file.read(size-len(output))
            if block:
                output.extend(block)
            else:
                self.file.close()
                self.file = None
        return bytes(output)

    def close(self):
        if self.file is not None:
            self.file.close()
        super().close()


class HashedSource:
    def __init__(self, stream, name):
        self.stream, self.name = stream, name
        self.hash = hashlib.sha256()
        self.bytes = 0
        self.last = time.monotonic()

    def read(self, count):
        data = self.stream.read(count)
        self.hash.update(data)
        self.bytes += len(data)
        if time.monotonic()-self.last > 20:
            print(self.name, self.bytes, 'source bytes', flush=True)
            self.last = time.monotonic()
        return data


def pack(spec, output, part_mib=24):
    output.mkdir(parents=True, exist_ok=False)
    entries = json.loads(spec.read_text('utf-8-sig'))
    names = [entry['path'] for entry in entries]
    if len(names) != len(set(names)):
        raise ValueError('Duplicate archive paths')
    manifest = dict(format='creator-profile-bundle-v1', compression='xz-lzma2-preset6',
                    lossless=True, files=[], parts=[])
    writer = PartWriter(output, part_mib << 20)
    with lzma.LZMAFile(writer, 'w', preset=6) as compressed:
        with tarfile.open(fileobj=compressed, mode='w|') as archive:
            for entry in entries:
                name = str(safe_name(entry['path']))
                source = Path(entry['source'])
                before = source.stat()
                print('pack', name, before.st_size, flush=True)
                info = tarfile.TarInfo(name)
                info.size = before.st_size
                info.mode = 0o644
                with source.open('rb') as stream:
                    hashed = HashedSource(stream, name)
                    archive.addfile(info, hashed)
                after = source.stat()
                if hashed.bytes != before.st_size or (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                    raise ValueError('Source changed while archiving: '+str(source))
                manifest['files'].append(dict(path=name, source=entry['source'], bytes=hashed.bytes,
                                               sha256=hashed.hash.hexdigest()))
    writer.finish_part()
    manifest['parts'] = writer.parts
    manifest['archiveSha256'] = writer.archive_hash.hexdigest()
    manifest['sourceBytes'] = sum(item['bytes'] for item in manifest['files'])
    manifest['compressedBytes'] = sum(item['bytes'] for item in manifest['parts'])
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2), 'utf-8')
    print('PACKED', manifest['sourceBytes'], '->', manifest['compressedBytes'], flush=True)
    return manifest


def verify(manifest_path, output=None, expand_json=False):
    manifest = json.loads(manifest_path.read_text('utf-8-sig'))
    if manifest['format'] != 'creator-profile-bundle-v1':
        raise ValueError('Unknown bundle format')
    directory = manifest_path.parent
    archive_hash = hashlib.sha256()
    for part in manifest['parts']:
        path = directory/safe_name(part['path'])
        if path.stat().st_size != part['bytes'] or digest_file(path) != part['sha256']:
            raise ValueError('Archive part differs: '+part['path'])
        with path.open('rb') as stream:
            while block := stream.read(1 << 20):
                archive_hash.update(block)
    if archive_hash.hexdigest() != manifest['archiveSha256']:
        raise ValueError('Archive stream SHA256 differs')
    if output is not None:
        output.mkdir(parents=True, exist_ok=False)
    expected = {item['path']: item for item in manifest['files']}
    seen = set()
    with PartReader(directory, manifest['parts']) as parts:
        with lzma.LZMAFile(parts, 'r') as compressed:
            with tarfile.open(fileobj=compressed, mode='r|') as archive:
                for info in archive:
                    safe_name(info.name)
                    if not info.isfile() or info.name not in expected or info.name in seen:
                        raise ValueError('Unexpected archive member: '+info.name)
                    seen.add(info.name)
                    digest = hashlib.sha256()
                    count = 0
                    destination = None
                    if output is not None:
                        path = output/safe_name(info.name)
                        path.parent.mkdir(parents=True, exist_ok=True)
                        destination = path.open('xb')
                    try:
                        with archive.extractfile(info) as stream:
                            while block := stream.read(1 << 20):
                                digest.update(block)
                                count += len(block)
                                if destination:
                                    destination.write(block)
                    finally:
                        if destination:
                            destination.close()
                    if count != expected[info.name]['bytes'] or digest.hexdigest() != expected[info.name]['sha256']:
                        raise ValueError('Restored file SHA256 differs: '+info.name)
                    print('verified', info.name, count, flush=True)
            while compressed.read(1 << 20):
                pass
    if seen != set(expected):
        raise ValueError('Archive members missing')
    if expand_json:
        if output is None:
            raise ValueError('--json needs an unpack output directory')
        for timeline in output.glob('timeline*.html'):
            text = timeline.read_text('utf-8')
            for name, payload in re.findall(r'<script type="application/gzip" id="((?:gpu-)?blob-[^"]+)">([A-Za-z0-9+/=]+)</script>', text):
                target = output/'analysis-json'/f'{timeline.stem}-{name}.json'
                target.parent.mkdir(exist_ok=True)
                target.write_bytes(gzip.decompress(base64.b64decode(payload)))
    verdict = dict(passed=True, files=len(seen), allRestoredSha256Match=True,
                   sourceBytes=manifest['sourceBytes'], compressedBytes=manifest['compressedBytes'])
    print(json.dumps(verdict), flush=True)
    return verdict


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    packing = commands.add_parser('pack')
    packing.add_argument('--spec', required=True, type=Path)
    packing.add_argument('--output', required=True, type=Path)
    packing.add_argument('--part-mib', type=int, default=24, choices=range(1, 65))
    for command in ['verify', 'unpack']:
        action = commands.add_parser(command)
        action.add_argument('--manifest', required=True, type=Path)
        if command == 'unpack':
            action.add_argument('--output', required=True, type=Path)
            action.add_argument('--json', action='store_true')
    args = parser.parse_args()
    if args.command == 'pack':
        pack(args.spec, args.output, args.part_mib)
    else:
        verify(args.manifest, getattr(args, 'output', None), getattr(args, 'json', False))


if __name__ == '__main__':
    main()
