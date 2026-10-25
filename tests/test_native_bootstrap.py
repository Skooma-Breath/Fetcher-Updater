"""Real native setup/launcher processes with isolated loopback release fixtures.

The test client is an argument-recording executable, not a real game join. No
PowerShell updater, game installation, registry or public release is changed.
"""
import argparse
import ctypes
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
from pathlib import Path
import struct
import subprocess
from tempfile import TemporaryDirectory
from threading import Thread
import time
from zipfile import ZipFile, ZIP_DEFLATED


def digest(data):
    return hashlib.sha256(data).hexdigest()


def zipped(files):
    target = io.BytesIO()
    with ZipFile(target, 'w', ZIP_DEFLATED) as archive:
        for name, content in files.items():
            archive.writestr(name, content)
    return target.getvalue()


def package(files, manifest):
    records = [{'path': name, 'size': len(data), 'sha256': digest(data)} for name, data in files.items()]
    return zipped({**files, manifest: json.dumps({'schemaVersion': 1, 'files': records}).encode()})


def plugin():
    return b'TES3' + struct.pack('<III', 0, 0, 0)


def wait_fixture(capture, root):
    deadline = time.monotonic() + 10
    while not capture.exists() and time.monotonic() < deadline:
        time.sleep(.05)
    result = json.loads(capture.read_text(encoding='utf8'))
    assert result['cwd'] == str(root)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.OpenProcess(0x100000, False, result['pid'])
    if handle:
        try:
            assert kernel.WaitForSingleObject(handle, 10000) == 0, 'Owned fixture did not exit'
        finally:
            kernel.CloseHandle(handle)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--setup', type=Path, required=True)
    parser.add_argument('--launcher', type=Path, required=True)
    parser.add_argument('--client-fixture', type=Path, required=True)
    options = parser.parse_args()
    setup, launcher, fixture = [path.resolve() for path in [options.setup, options.launcher, options.client_fixture]]
    with TemporaryDirectory(prefix='fetcher-native-setup-') as temporary:
        base = Path(temporary)
        root = base / 'New inställ with spaces'
        game = base / 'Owned game' / 'Data Files'
        game.mkdir(parents=True)
        for name in ['Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm']:
            (game / name).write_bytes(plugin())
        owned = {path.name: path.read_bytes() for path in game.iterdir()}
        tool_files = {name: (launcher.parent / name).read_bytes() for name in [
            'FetcherLauncher.exe', 'FetcherLauncher-THIRD-PARTY-NOTICES.txt', 'ui/index.html',
            'ui/assets/fetcher-float.gif', 'ui/assets/fetcher-float-right.gif', 'ui/assets/potm2504a.jpg']}
        tool_files['fetcher-update-channel.json'] = json.dumps({'schemaVersion': 1,
            'clientRepository': 'Fetcher-Simulator/Fetcher-Simulator', 'clientReleaseTag': 'Fetcher-Simulator',
            'clientAssetName': 'fetcher-simulator.zip'}).encode()
        engine_files = {'openmw.exe': fixture.read_bytes(), 'resources/vfs-mw/fixture.txt': b'test-only client resources'}
        engine_files.update({name: fixture.read_bytes() for name in ['openmw-wizard.exe', 'openmw-iniimporter.exe']})
        engine_files.update({name: b'test-only Qt dependency' for name in [
            'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Svg.dll', 'platforms/qwindows.dll',
            'imageformats/qsvg.dll', 'iconengines/qsvgicon.dll']})
        engine_files['qt.conf'] = b'[Paths]\nPlugins=.\nTranslations=resources/translations\n'
        tools_bytes = package(tool_files, 'fetcher-tester-tools.json')
        client_bytes = package(engine_files, 'fetcher-client-files.json')
        archive_bytes = {'tools': tools_bytes, 'client': client_bytes}
        served_bytes = archive_bytes.copy()
        metadata_modes = {}
        requested = []
        mod = zipped({'Data Files/Bootstrap.esp': plugin(), 'Data Files/meshes/fixture.nif': b'fixture'})

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_GET(self):
                requested.append(self.path)
                if self.path.startswith('/release/'):
                    kind = self.path.rsplit('/', 1)[1]
                    name = 'fetcher-tester-tools.zip' if kind == 'tools' else 'fetcher-simulator.zip'
                    entry = {'name': name, 'digest': 'sha256:' + digest(archive_bytes[kind]),
                             'size': len(archive_bytes[kind]), 'browser_download_url': origin + '/archive/' + kind}
                    if metadata_modes.get(kind) == 'missing-digest':
                        entry.pop('digest')
                    assets = [entry, entry] if metadata_modes.get(kind) == 'duplicate' else [entry]
                    data = json.dumps({'assets': assets}).encode()
                elif self.path.startswith('/archive/'):
                    data = served_bytes[self.path.rsplit('/', 1)[1]]
                elif self.path == '/mod.zip':
                    data = mod
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        origin = f'http://127.0.0.1:{server.server_port}'
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        operations = 0

        def install(target=root, success=True, crash=0, fixtures=True):
            nonlocal operations
            result_file = base / 'setup-result.json'
            result_file.unlink(missing_ok=True)
            command = [str(setup), '--install', '--install-root', str(target), '--log-file', str(result_file),
                       '--tools-release-url', origin + '/release/tools', '--client-release-url', origin + '/release/client']
            if fixtures:
                command.append('--allow-local-fixtures')
            if crash:
                command += ['--fixture-crash-after', str(crash)]
            process = subprocess.run(command, timeout=90)
            operations += 1
            if crash:
                assert process.returncode == 87
                return
            assert result_file.exists(), (f'Setup exited without a result: {process.returncode:#x}; ' +
                result_file.with_name(result_file.name + '.progress.txt').read_text(encoding='utf8'))
            result = json.loads(result_file.read_text(encoding='utf8'))
            assert (process.returncode == 0) == success, result
            return result

        def command(request, success=True):
            nonlocal operations
            request_file = base / 'request.json'
            result_file = base / 'launcher-result.json'
            request_file.write_text(json.dumps(request), encoding='utf8')
            process = subprocess.run([str(root / 'FetcherLauncher.exe'), '--install-root', str(root),
                '--allow-local-fixtures', '--request-file', str(request_file), '--log-file', str(result_file)], timeout=45)
            operations += 1
            result = json.loads(result_file.read_text(encoding='utf8'))
            assert (process.returncode == 0) == success, result
            return result

        def assert_files(files):
            for name, value in files.items():
                assert (root / name).read_bytes() == value, name

        try:
            denied = install(success=False, fixtures=False)
            assert 'loopback' in denied['error'] and requested == [] and not root.exists()
            report = install()
            assert report['installed'] and report['client']['sha256'] == digest(client_bytes)
            assert_files(tool_files | engine_files)
            assert not any(path == '/mod.zip' for path in requested)
            state = command({'op': 'init'})
            assert state['setup'] == {'clientInstalled': True, 'gameDataReady': False, 'profilePrepared': False}
            capture = root / 'launch-capture.json'
            command({'op': 'wizard'})
            assert wait_fixture(capture, root)['arguments'] == []
            capture.unlink()
            wizard = root / 'openmw-wizard.exe'
            wizard_bytes = wizard.read_bytes()
            wizard.unlink()
            assert 'Updates > Install updates' in command({'op': 'wizard'}, success=False)['error']
            wizard.write_bytes(wizard_bytes)
            command({'op': 'gameData', 'path': str(game)})
            profile = {'schemaVersion': 1, 'id': 'bootstrap-profile', 'name': 'Bootstrap profile fixture',
                'version': '1', 'host': '127.0.0.1', 'port': 25570,
                'content': ['Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm', 'Bootstrap.esp'],
                'packages': [{'id': 'content', 'url': origin + '/mod.zip', 'sha256': digest(mod), 'dataDirs': ['Data Files']}]}
            profile_file = base / 'profile.json'
            profile_file.write_text(json.dumps(profile), encoding='utf8')
            review = command({'op': 'reviewProfile', 'path': str(profile_file)})
            command({'op': 'confirmProfile', 'token': review['profileReview']['token']})
            assert not command({'op': 'init'})['setup']['profilePrepared']
            state = command({'op': 'prepare'})
            assert all(state['setup'].values())
            assert 'account' in command({'op': 'launch'}, success=False)['error']
            command({'op': 'identity', 'account': 'Fixture Account', 'character': 'Fixture Character', 'autoEnter': True})
            command({'op': 'identity', 'account': 'invalid\nname'}, success=False)
            command({'op': 'launch'})
            capture = root / 'launch-capture.json'
            handoff = wait_fixture(capture, root)
            arguments = handoff['arguments']
            assert '--connect=127.0.0.1:25570' in arguments
            assert '--mp-account=Fixture Account' in arguments and '--mp-character=Fixture Character' in arguments
            assert '--mp-auto-enter=1' in arguments
            assert '--mp-profile-root=' + str(root / 'profiles') in arguments
            config = Path(next(value.removeprefix('--config=') for value in arguments if value.startswith('--config=')))
            content = (config / 'openmw.cfg').read_text(encoding='utf8')
            assert content.index('content=Morrowind.esm') < content.index('content=Bootstrap.esp')
            assert str(root) == handoff['cwd']
            command({'op': 'selectServer', 'host': '127.0.0.1', 'port': 25571})
            assert command({'op': 'init'})['identity'] == {}
            command({'op': 'identity', 'account': 'Other Account', 'character': ''})
            capture.unlink()
            command({'op': 'launch'})
            other = wait_fixture(capture, root)
            assert not any(argument.startswith('--mp-profile-root=') for argument in other['arguments'])
            assert '--mp-account=Other Account' in other['arguments']
            command({'op': 'selectServer', 'host': '127.0.0.1', 'port': 25570})
            assert command({'op': 'init'})['identity']['character'] == 'Fixture Character'
            markers = {'openmw.cfg': b'# personal cfg', 'settings.cfg': b'personal video settings',
                'profiles/personal/keep.bin': b'character settings', 'Data Files/personal.dds': b'custom texture',
                'mp-keys/private.key': b'synthetic marker', 'FetcherSetup.exe': b'local setup marker'}
            for name, value in markers.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(value)
            count = len([path for path in requested if path.startswith('/archive/')])
            install()
            assert len([path for path in requested if path.startswith('/archive/')]) == count
            assert_files(markers)
            (root / '_fetcher_launcher/downloads' / (digest(tools_bytes) + '.zip')).write_bytes(b'damaged cache')
            install()
            assert requested.count('/archive/tools') == 2
            hold = root / 'hold-process'
            hold.touch()
            process = subprocess.Popen([str(root / 'openmw.exe')], cwd=root)
            try:
                time.sleep(.1)
                before = len(requested)
                denied = install(success=False)
                assert 'Close this installation' in denied['error'] and len(requested) == before
            finally:
                hold.unlink(missing_ok=True)
                process.wait(timeout=5)
            original = {name: (root / name).read_bytes() for name in tool_files | engine_files}
            metadata_modes['tools'] = 'duplicate'
            assert 'duplicate' in install(success=False)['error']
            metadata_modes['tools'] = 'missing-digest'
            install(success=False)
            metadata_modes.clear()
            for mode in ['bad-hash', 'missing-ui', 'bad-inventory', 'traversal']:
                changed = dict(tool_files)
                changed['ui/index.html'] += ('\n<!-- ' + mode + ' -->').encode()
                if mode == 'missing-ui':
                    changed.pop('ui/index.html')
                value = package(changed, 'fetcher-tester-tools.json')
                if mode == 'bad-inventory':
                    with ZipFile(io.BytesIO(value)) as z:
                        changed = {name: z.read(name) for name in z.namelist()}
                    inventory = json.loads(changed['fetcher-tester-tools.json'])
                    inventory['files'][0]['sha256'] = '0' * 64
                    changed['fetcher-tester-tools.json'] = json.dumps(inventory).encode()
                    value = zipped(changed)
                if mode == 'traversal':
                    value = zipped({'../escaped.txt': b'outside'})
                archive_bytes['tools'] = value
                served_bytes['tools'] = b'corrupted response' if mode == 'bad-hash' else value
                install(success=False)
                assert_files(original | markers)
                assert not (root / '_fetcher_launcher/staging/escaped.txt').exists()
            archive_bytes['tools'] = served_bytes['tools'] = tools_bytes
            for mode in ['protected', 'protected-setup', 'wrong-platform', 'wizard-platform',
                         'openmw-wizard.exe', 'openmw-iniimporter.exe', 'platforms/qwindows.dll', 'qt.conf']:
                changed = dict(engine_files)
                if mode == 'protected':
                    changed['settings.cfg'] = b'overwrite personal settings'
                elif mode == 'protected-setup':
                    changed['FetcherSetup.exe'] = b'overwrite setup executable'
                elif mode in ['wrong-platform', 'wizard-platform']:
                    name = 'openmw-wizard.exe' if mode == 'wizard-platform' else 'openmw.exe'
                    image = bytearray(changed[name])
                    pe = struct.unpack_from('<I', image, 60)[0]
                    image[pe + 4:pe + 6] = bytes([0x4c, 1])
                    changed[name] = image
                else:
                    changed.pop(mode)
                archive_bytes['client'] = served_bytes['client'] = package(changed, 'fetcher-client-files.json')
                install(success=False)
                assert_files(original | markers)
            archive_bytes['client'] = served_bytes['client'] = client_bytes
            changed = dict(tool_files)
            changed['ui/index.html'] += b'\n<!-- recovery candidate -->'
            archive_bytes['tools'] = served_bytes['tools'] = package(changed, 'fetcher-tester-tools.json')
            install(crash=2)
            install()
            assert_files(changed | engine_files | markers)
            assert all((game / name).read_bytes() == value for name, value in owned.items())
            print(f'PASS native bootstrap: {operations} process operations; fresh install, real launcher handoff, '
                  'owned game/profile readiness, cache repair, running-client guard, bad downloads/inventories, '
                  'protected settings, wizard handoff/missing runtime, x64 validation and interrupted-install recovery. '
                  'Recording executables only; no real Qt wizard or game join.')
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == '__main__':
    main()
