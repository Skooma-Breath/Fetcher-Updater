"""Real native launcher: server-pinned XML FOMOD choices, repair and rollback."""
from pathlib import Path
from tempfile import TemporaryDirectory
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from threading import Thread
import argparse
import copy
import hashlib
import json
import struct
import subprocess
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.resolve()
    xml = (Path(__file__).parent / 'fixtures/fomod/ModuleConfig.xml').read_text(encoding='utf-8')
    with TemporaryDirectory(prefix='fetcher-fomod-') as temporary:
        base = Path(temporary)
        root = base / 'Player installation'; root.mkdir()
        data = base / 'Owned game'; data.mkdir()

        def plugin(master=None):
            record = b'' if master is None else b'MAST' + struct.pack('<I', len(master) + 1) + master.encode() + b'\0'
            return b'TES3' + struct.pack('<I', len(record)) + b'\0' * 8 + record

        for name in ['Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm']:
            (data / name).write_bytes(plugin())
        marker = b'# personal config stays untouched\n'
        (root / 'openmw.cfg').write_bytes(marker)

        def archive(name, config):
            path = base / name
            with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
                for relative, payload in {
                    'fomod/ModuleConfig.xml': config,
                    'core/Instrument.esp': plugin('Morrowind.esm'),
                    'core/textures/tone.dds': b'core',
                    'options/modern/tone.dds': b'modern',
                    'options/classic/tone.dds': b'classic',
                    'options/modern/é.dds': b'Unicode texture',
                    'patch/Patch.esp': plugin('Instrument.esp'),
                    'patch/final.dds': b'patched',
                    'docs/readme.txt': b'readme',
                    'docs/usable.txt': b'usable',
                    'docs/disabled.txt': b'disabled',
                }.items():
                    z.writestr('Wrapper/' + relative, payload)
            return hashlib.sha256(path.read_bytes()).hexdigest()

        sha = archive('instrument.zip', xml)
        requested = []

        class Handler(SimpleHTTPRequestHandler):
            def __init__(self, *a, **kw):
                super().__init__(*a, directory=str(base), **kw)

            def log_message(self, *_):
                pass

            def do_GET(self):
                requested.append(self.path)
                super().do_GET()

        server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = Thread(target=server.serve_forever, daemon=True); thread.start()
        origin = f'http://127.0.0.1:{server.server_port}'
        operations = 0

        def command(value, ok=True):
            nonlocal operations
            operations += 1
            request, result = base / 'request.json', base / 'result.json'
            request.write_text(json.dumps(value), encoding='utf-8')
            process = subprocess.run([str(exe), '--install-root', str(root), '--allow-local-fixtures',
                                      '--request-file', str(request), '--log-file', str(result)], timeout=60)
            response = json.loads(result.read_text(encoding='utf-8-sig'))
            assert (process.returncode == 0) == ok, (value, response)
            return response

        profile = {
            'schemaVersion': 1, 'id': 'band', 'name': 'FOMOD community', 'version': '1',
            'host': 'music.example', 'port': 25565,
            'content': ['Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm', 'Instrument.esp', 'Patch.esp'],
            'requiredFiles': ['textures/tone.dds', 'docs/readme.txt'],
            'packages': [{'id': 'instrument', 'url': origin + '/instrument.zip', 'sha256': sha,
                          'dataDirs': ['.'], 'fomod': {'config': 'Wrapper/fomod/ModuleConfig.xml', 'selections': [
                              {'step': 'Sound', 'group': 'Variant', 'plugins': ['Modern']},
                              {'step': 'Patches', 'group': 'Extras', 'plugins': ['Patch']}]},
                          'actions': [{'action': 'copy', 'src': 'docs/readme.txt', 'dst': 'notes/readme.txt'}]}],
        }

        def review(value, accept=True, name='server.json'):
            (base / name).write_text(json.dumps(value), encoding='utf-8')
            count = requested.count('/instrument.zip')
            state = command({'op': 'reviewProfile', 'url': origin + '/' + name})
            assert requested.count('/instrument.zip') == count, 'Review downloaded the archive'
            if accept:
                return command({'op': 'confirmProfile', 'token': state['profileReview']['token']})
            return state

        def layout(identity):
            cfg = root / '_fetcher_launcher/profiles' / identity / 'openmw.cfg'
            roots = [Path(line.split('=', 1)[1].strip('"')) for line in cfg.read_text(encoding='utf-8').splitlines() if line.startswith('data=')]
            return cfg, next(p for p in roots if (p / 'Instrument.esp').exists())

        try:
            command({'op': 'gameData', 'path': str(data)})
            reviewed = review(profile, accept=False)
            token = reviewed['profileReview']['token']
            assert reviewed['profileReview']['profile']['packages'][0]['fomod']['selections'][0]['step'] == 'Patches'
            accepted = command({'op': 'confirmProfile', 'token': token})
            identity = accepted['selectedProfile']
            command({'op': 'prepare'})
            cfg, modern = layout(identity); saved = cfg.read_bytes()
            assert (modern / 'textures/tone.dds').read_bytes() == b'patched'
            assert (modern / 'textures/é.dds').read_bytes() == b'Unicode texture'
            assert (modern / 'notes/readme.txt').read_bytes() == b'readme'
            assert not (modern / 'Wrapper').exists() and not (modern / 'disabled.txt').exists()
            assert [s.split('=', 1)[1] for s in cfg.read_text().splitlines() if s.startswith('content=')][-2:] == ['Instrument.esp', 'Patch.esp']
            raw = root / '_fetcher_launcher/packages' / sha
            assert (raw / 'Wrapper/options/classic/tone.dds').read_bytes() == b'classic'
            assert not (raw / 'notes').exists()
            count = requested.count('/instrument.zip')
            (modern / 'textures/tone.dds').write_bytes(b'CORRUPT')
            command({'op': 'prepare'})
            assert (modern / 'textures/tone.dds').read_bytes() == b'patched'
            assert requested.count('/instrument.zip') == count
            other = copy.deepcopy(profile)
            other['version'] = '2'; other['host'] = 'other.example'
            other['content'].remove('Patch.esp'); other['requiredFiles'] = ['textures/tone.dds']
            package = other['packages'][0]; package['actions'] = []
            package['fomod']['selections'] = [{'step': 'Sound', 'group': 'Variant', 'plugins': ['Classic']}]
            classic_id = review(other, name='other.json')['selectedProfile']; command({'op': 'prepare'})
            _, classic = layout(classic_id)
            assert classic != modern and (classic / 'textures/tone.dds').read_bytes() == b'classic'
            assert (modern / 'textures/tone.dds').read_bytes() == b'patched'
            assert requested.count('/instrument.zip') == count
            # Reimport changed choices at the same source changes the reviewed fingerprint.
            bad = copy.deepcopy(profile); bad['version'] = 'bad'
            bad['packages'][0]['fomod']['selections'][0]['plugins'] = ['Typo']
            changed = review(bad, accept=False)
            assert changed['profileReview']['token'] != token
            command({'op': 'confirmProfile', 'token': token}, ok=False)
            command({'op': 'confirmProfile', 'token': changed['profileReview']['token']})
            command({'op': 'prepare'}, ok=False)
            assert cfg.read_bytes() == saved and (modern / 'textures/tone.dds').read_bytes() == b'patched'
            command({'op': 'rollbackProfile'}); command({'op': 'prepare'})
            assert cfg.read_bytes() == saved
            # XML attempts never resolve an external entity, including local HTTP.
            hostile = copy.deepcopy(profile)
            hostile['packages'][0]['url'] = origin + '/hostile.zip'
            hostile['packages'][0]['sha256'] = archive('hostile.zip', xml.replace('<config ',
                '<!DOCTYPE config [<!ENTITY xxe SYSTEM "' + origin + '/PRIVATE">]><config '))
            review(hostile); command({'op': 'prepare'}, ok=False)
            assert '/PRIVATE' not in requested
            assert cfg.read_bytes() == saved
            # Selected file mappings cannot disguise a program as an ordinary source document.
            executable = copy.deepcopy(profile)
            executable['packages'][0]['url'] = origin + '/executable.zip'
            executable['packages'][0]['sha256'] = archive('executable.zip', xml.replace(
                'destination="docs/"', 'destination="installer.exe"'))
            review(executable); command({'op': 'prepare'}, ok=False)
            assert cfg.read_bytes() == saved
            invalid = copy.deepcopy(profile)
            invalid['packages'][0]['dataDirs'] = ['Wrapper']
            (base / 'invalid.json').write_text(json.dumps(invalid), encoding='utf-8')
            command({'op': 'reviewProfile', 'url': origin + '/invalid.json'}, ok=False)
            # UMO download extension carries the exact same server-owned choices.
            umo = {k: v for k, v in profile.items() if k != 'packages'}
            definition = profile['packages'][0]
            umo['umoMods'] = [{'name': 'FOMOD keyboard', 'plugins': ['Instrument.esp', 'Patch.esp'],
                              'data_paths': ['unused'], 'download_info': [{'pinned': True, 'extract_to': 'Instrument',
                              'direct_download': origin + '/instrument.zip', 'sha256': sha, 'dataDirs': ['.'],
                              'fomod': definition['fomod']}]}]
            accepted = review(umo); command({'op': 'prepare'})
            _, selected = layout(accepted['selectedProfile'])
            assert (selected / 'textures/tone.dds').read_bytes() == b'patched'
            assert not (selected / 'notes').exists()
            assert (root / 'openmw.cfg').read_bytes() == marker
            print(f'PASS FOMOD integration: {operations} executable operations; pinned choices, priorities/flags, isolated variants, repair, rollback, UMO, stale review and XML external-entity rejection')
        finally:
            server.shutdown(); server.server_close(); thread.join()


if __name__ == '__main__':
    main()
