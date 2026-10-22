#!/usr/bin/env python3
"""Author native owned-game restoration from a SHA-pinned Fetcher Starwind release.

The output contains source fingerprints, byte ranges and changed texture names;
no unmodified base-game asset bytes are embedded or redistributed.
"""
import argparse
from contextlib import ExitStack
import base64
import hashlib
import json
from pathlib import Path
import struct
from zipfile import ZipFile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_path(value):
    value = value.replace('\\', '/')
    if not value or ':' in value or any(part in ('', '.', '..') for part in value.split('/')):
        raise ValueError('Unsafe game asset path')
    if Path(value).suffix.lower() not in {'.nif', '.dds', '.kf', '.tga', '.wav'}:
        raise ValueError('Only model/texture/animation/sound game assets are supported')
    return value


class Bsa:
    def __init__(self, stream):
        self.stream = stream
        stream.seek(0, 2)
        length = stream.tell()
        stream.seek(0)
        header = stream.read(12)
        if len(header) != 12:
            raise ValueError('Truncated game BSA header')
        magic, directory_size, count = struct.unpack('<III', header)
        data_start = 12 + directory_size + count * 8
        if magic != 0x100 or not 0 < count <= 200000 or not count * 12 <= directory_size <= 64 * 1024**2 or data_start > length:
            raise ValueError('Unsupported or malformed Morrowind BSA')
        directory = stream.read(directory_size)
        if len(directory) != directory_size:
            raise ValueError('Truncated game BSA directory')
        self.entries = {}
        for index in range(count):
            size, offset = struct.unpack_from('<II', directory, index * 8)
            name_offset = struct.unpack_from('<I', directory, count * 8 + index * 4)[0]
            start = count * 12 + name_offset
            if not count * 12 <= start < directory_size or data_start + offset + size > length:
                raise ValueError('BSA entry exceeds archive bounds')
            end = directory.find(b'\0', start)
            if not start < end < min(directory_size, start + 1024):
                raise ValueError('Invalid BSA asset name')
            name = directory[start:end].decode('cp1252').replace('\\', '/').lower()
            if name in self.entries:
                raise ValueError('Duplicate BSA asset name')
            self.entries[name] = (data_start + offset, size)

    def read(self, name):
        offset, size = self.entries[name.lower()]
        if size > 64 * 1024**2:
            raise ValueError('Game asset exceeds reconstruction size limit')
        self.stream.seek(offset)
        result = self.stream.read(size)
        if len(result) != size:
            raise ValueError('Truncated BSA asset')
        return result


def texture_delta(source, replacements):
    changes = []
    for replacement in replacements:
        old = replacement['from'].encode('cp1252')
        new = replacement['to'].encode('cp1252')
        before = struct.pack('<I', len(old)) + old
        after = struct.pack('<I', len(new)) + new
        count = 0
        cursor = 0
        while (position := source.find(before, cursor)) >= 0:
            changes.append((position, len(before), after))
            cursor = position + len(before)
            count += 1
        if count != replacement['occurrences']:
            raise ValueError('NIF texture replacement occurrence count differs from the pinned recipe')
    cursor = 0
    operations, output = [], bytearray()
    for position, length, literal in sorted(changes):
        if position < cursor:
            raise ValueError('Overlapping NIF texture replacements')
        if position > cursor:
            operations.append({'copyOffset': cursor, 'copyLength': position - cursor})
            output.extend(source[cursor:position])
        operations.append({'data': base64.b64encode(literal).decode('ascii')})
        output.extend(literal)
        cursor = position + length
    if cursor < len(source):
        operations.append({'copyOffset': cursor, 'copyLength': len(source) - cursor})
        output.extend(source[cursor:])
    return operations, bytes(output)


def build(archive, expected_sha256, game_data):
    with archive.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != expected_sha256:
            raise ValueError('Starwind release archive differs from its pinned SHA-256')
    with ZipFile(archive) as zipped:
        info = zipped.getinfo('fetcher-starwind-morrowind-assets.json')
        if info.file_size > 8 * 1024**2:
            raise ValueError('Starwind asset metadata exceeds limit')
        metadata = json.loads(zipped.read(info).decode('utf-8-sig'))
    action = {'action': 'restoreGameAssets', 'files': []}
    if not 0 < len(metadata['files']) <= 4096:
        raise ValueError('Owned-game asset manifest file count exceeds limit')
    archives = {}
    with ExitStack() as stack:
        for file in metadata['files']:
            original = file['source']
            archive_name = original.get('archive') or ''
            name = safe_path(original['archivePath'] if archive_name else original['path'])
            if archive_name:
                if archive_name not in {'Morrowind.bsa', 'Tribunal.bsa', 'Bloodmoon.bsa'}:
                    raise ValueError('Only the three base-game BSA archives are supported')
                if archive_name not in archives:
                    archives[archive_name] = Bsa(stack.enter_context((game_data / archive_name).open('rb')))
                source = archives[archive_name].read(name)
            else:
                path = game_data / name
                if path.stat().st_size > 64 * 1024**2:
                    raise ValueError('Owned loose asset exceeds reconstruction size limit')
                source = path.read_bytes()
            if digest(source) != original['sha256']:
                raise ValueError('Owned game asset differs from the published source fingerprint: ' + name)
            if file['operation'] == 'copy':
                operations, output = [{'copyOffset': 0, 'copyLength': len(source)}], source
            elif file['operation'] == 'rewriteNifTexturePaths':
                operations, output = texture_delta(source, file['replacements'])
            else:
                raise ValueError('Unsupported owned-game asset operation')
            if digest(output) != file['resultSha256']:
                raise ValueError('Reconstructed owned game asset differs from the published output: ' + file['destinationPath'])
            action['files'].append({'archive': archive_name, 'sourcePath': name,
                'patch': {'action': 'patch', 'path': 'payload/Starwind Vanilla Compat/' + safe_path(file['destinationPath']),
                          'sourceSha256': original['sha256'], 'sourceSize': len(source),
                          'outputSha256': file['resultSha256'], 'outputSize': len(output),
                          'operations': operations}})
    return {'schemaVersion': 1, 'sourceArchiveSha256': expected_sha256,
            'assetCount': len(action['files']), 'actions': [action]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--game-data', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = build(args.archive.resolve(), args.sha256, args.game_data.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(f"Verified {report['assetCount']} owned-game asset reconstructions; recipe: {args.output}")


if __name__ == '__main__':
    main()
