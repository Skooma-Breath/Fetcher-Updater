"""Native compatibility authoring refuses guesses and separates client resources."""
import base64
import hashlib
import json
from pathlib import Path
import runpy
from tempfile import TemporaryDirectory
from zipfile import ZipFile

api = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/Build-FetcherNativeCompatibility.py'))


def main():
    with TemporaryDirectory(prefix='fetcher-compat-authoring-') as temporary:
        root = Path(temporary)
        cached = root / 'cache'
        installed = root / 'installed'
        archive_sha = 'a' * 64
        raw = cached / '_fetcher_launcher/packages' / archive_sha
        raw.mkdir(parents=True)
        (raw / 'Original.esp').write_bytes(b'upstream')
        sha = lambda value: hashlib.sha256(value).hexdigest()
        (raw / '.fetcher-package.json').write_text(json.dumps({'sha256': archive_sha, 'files': [
            {'path': 'Original.esp', 'sha256': sha(b'upstream'), 'size': 8}]}), encoding='utf8')
        prefix = 'Data Files/fetcher-simulator/Test/Mod/'
        destination = installed / prefix
        destination.mkdir(parents=True)
        (destination / 'Clean Original.esp').write_bytes(b'corrected')
        (destination / 'added.lua').write_bytes(b'new')
        audit = {'packages': [{'mod': 'Mod', 'category': 'Test', 'extractTo': 'Mod',
                              'definition': {'id': 'mod', 'sha256': archive_sha}}]}
        patch = {'path': prefix + 'Clean Original.esp', 'sourceSize': 8, 'sourceSha256': sha(b'upstream'),
                 'outputSize': 9, 'outputSha256': sha(b'corrected'),
                 'operations': [{'data': base64.b64encode(b'corrected').decode()}]}
        created = {'path': prefix + 'added.lua', 'sourceSize': 0, 'sourceSha256': sha(b''),
                   'outputSize': 3, 'outputSha256': sha(b'new'), 'createIfMissing': True,
                   'requiresPath': prefix + 'Clean Original.esp', 'operations': [{'data': 'bmV3'}]}
        engine = dict(patch, path='resources/vfs/client.lua')
        legacy = {'patchVersion': 'test', 'files': [patch, created, engine]}
        report = api['build'](audit, cached, installed, legacy)
        assert report['portedPatchCount'] == 2 and not report['failures']
        assert report['packages']['mod'][0] == {'action': 'copy', 'src': 'Original.esp', 'dst': 'Clean Original.esp'}
        assert report['packages']['mod'][2]['create']
        assert len(report['excludedClientResources']) == 1
        assert (raw / 'Original.esp').read_bytes() == b'upstream'
        assert not (raw / 'Clean Original.esp').exists()
        (destination / 'Clean Original.esp').write_bytes(b'local edit')
        different = api['build'](audit, cached, installed, legacy)
        assert different['portedPatchCount'] == 0
        assert any('authoritative' in issue['reason'] for issue in different['failures'])
        assert any('prerequisite' in issue['reason'] for issue in different['failures'])
        patch['sourceSha256'] = '0' * 64
        missing = api['build'](audit, cached, installed, legacy)
        assert any('fingerprint' in issue['reason'] for issue in missing['failures'])
        bardcraft = root / 'bardcraft.zip'
        with ZipFile(bardcraft, 'w') as archive:
            archive.writestr('fetcher-bardcraft-mp-patch.json', json.dumps({
                'formatVersion': 2, 'targetSubdirectory': 'scripts/Bardcraft', 'files': [
                    {'path': 'player.lua', 'sourceSha256': None, 'sourceSize': None,
                     'outputSha256': sha(b'new'), 'outputSize': 3, 'operations': [{'data': 'bmV3'}]}]}))
        converted = api['bardcraft_records'](bardcraft, sha(bardcraft.read_bytes()))
        assert converted[0]['path'].endswith('/scripts/Bardcraft/player.lua')
        assert converted[0]['createIfMissing'] and converted[0]['sourceSha256'] == sha(b'')
        try:
            api['bardcraft_records'](bardcraft, '0' * 64)
            raise AssertionError('Unpinned compatibility archive accepted')
        except ValueError:
            pass
        print('PASS native compatibility authoring: exact source/output gates, renamed plugin derivation, explicit creation prerequisites, client-resource exclusion, immutable originals')


if __name__ == '__main__':
    main()
