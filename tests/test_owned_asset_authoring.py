"""Owned-asset recipes embed changed texture names, never original asset bytes."""
import base64
from io import BytesIO
from pathlib import Path
import runpy
import struct

api = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/Build-FetcherOwnedAssetRecipe.py'))


def rejects(fn):
    try:
        fn()
    except (ValueError, KeyError):
        return
    raise AssertionError('Invalid owned-game input accepted')


def main():
    source = b'base-game-model-data' + struct.pack('<I', 7) + b'old.bmp' + b'unchanged-footer'
    replacement = {'from': 'old.bmp', 'to': 'textures\\compat\\new.dds', 'occurrences': 1}
    operations, output = api['texture_delta'](source, [replacement])
    reconstructed = b''.join(base64.b64decode(operation['data']) if 'data' in operation
                             else source[operation['copyOffset']:operation['copyOffset'] + operation['copyLength']]
                             for operation in operations)
    assert reconstructed == output
    assert output == b'base-game-model-data' + struct.pack('<I', len(replacement['to'])) + replacement['to'].encode() + b'unchanged-footer'
    literals = b''.join(base64.b64decode(operation['data']) for operation in operations if 'data' in operation)
    assert b'base-game-model-data' not in literals and b'unchanged-footer' not in literals
    rejects(lambda: api['texture_delta'](source, [dict(replacement, occurrences=2)]))
    rejects(lambda: api['texture_delta'](source, [replacement, replacement]))
    name = b'textures\\fixture.dds\0'
    data = b'owned bytes'
    blob = (struct.pack('<III', 0x100, 12 + len(name), 1) + struct.pack('<III', len(data), 0, 0)
            + name + bytes(8) + data)
    archive = api['Bsa'](BytesIO(blob))
    assert archive.read('Textures/FIXTURE.DDS') == data
    rejects(lambda: api['Bsa'](BytesIO(blob[:14])))
    malformed = bytearray(blob)
    struct.pack_into('<I', malformed, 16, 0xffffffff)
    rejects(lambda: api['Bsa'](BytesIO(malformed)))
    for path in ['../save.nif', '/Meshes/model.nif', 'settings.cfg', 'C:/secret.wav', 'Meshes//model.nif']:
        rejects(lambda: api['safe_path'](path))
    print('PASS owned-asset authoring: exact texture replacements, occurrence/overlap refusal, no stock payload literals, bounded BSA offsets and source paths')


if __name__ == '__main__':
    main()
