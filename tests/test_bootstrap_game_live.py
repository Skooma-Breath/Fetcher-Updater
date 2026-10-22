"""Opt-in staged game join through native setup and the installed launcher.

Uses a new fixture root, explicit local engine/tools/content inputs, owned game
files and a read-only local test-account seed. The existing Synth fixture and
public releases/servers are never modified. Only this test's processes are closed.
"""
import argparse
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
from threading import Thread
import time
from zipfile import ZipFile, ZIP_DEFLATED


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def powershell(code):
    environment = os.environ.copy()
    environment['PSModulePath'] = str(Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/Modules')
    return subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', code],
        capture_output=True, text=True, encoding='utf-8-sig', errors='replace', timeout=30,
        creationflags=subprocess.CREATE_NO_WINDOW, env=environment)


def game_processes(executable):
    result = powershell("Get-CimInstance Win32_Process -Filter \"Name='openmw.exe'\" | "
        'Select-Object ProcessId,ExecutablePath | ConvertTo-Json -Compress')
    if not result.stdout.strip():
        return []
    value = json.loads(result.stdout)
    return [process for process in (value if isinstance(value, list) else [value])
            if (process.get('ExecutablePath') or '').lower() == str(executable).lower()]


def component(value):
    # Seed display-only settings for this known ASCII test identity, using the engine's profile convention.
    slug = re.sub('_+', '_', re.sub('[^A-Za-z0-9_.-]', '_', value))
    return slug if slug == value else slug + '-' + hashlib.sha256(value.encode()).hexdigest()[:12]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['setup', 'tools', 'runtime', 'bundle', 'game-data', 'source-fixture', 'seed-script', 'root']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--port', type=int, default=25570)
    parser.add_argument('--observe-seconds', type=int, default=0)
    args = parser.parse_args()
    assert args.port not in (25567, 25568), 'Preserve the established Synth test fixture'
    assert 0 <= args.observe_seconds <= 60
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(('127.0.0.1', args.port))
    base = args.root.resolve()
    base.mkdir(parents=True, exist_ok=False)
    installed = base / 'client'
    serving = base / 'release-fixture'
    serving.mkdir()
    runtime = args.runtime.resolve()
    game = args.game_data.resolve()
    owned = {name: digest(game / name) for name in ['Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm']}
    engine = serving / 'fetcher-simulator.zip'
    records = []
    paths = [runtime / 'openmw.exe', runtime / 'defaults.bin', *runtime.glob('*.dll')]
    for directory in ['resources', 'osgPlugins-3.6.5']:
        paths.extend(path for path in (runtime / directory).rglob('*') if path.is_file())
    print(f'Packaging {len(paths)} explicit local runtime files...', flush=True)
    with ZipFile(engine, 'w', ZIP_DEFLATED, compresslevel=1) as archive:
        for path in sorted(set(paths)):
            name = path.relative_to(runtime).as_posix()
            archive.write(path, name)
            records.append({'path': name, 'sha256': digest(path), 'size': path.stat().st_size})
        archive.writestr('fetcher-client-files.json', json.dumps({'schemaVersion': 1, 'files': records}))
    shutil.copyfile(args.tools, serving / 'fetcher-tester-tools.zip')
    shutil.copyfile(args.bundle, serving / 'content.zip')
    hashes = {name: digest(serving / name) for name in ['fetcher-tester-tools.zip', 'fetcher-simulator.zip', 'content.zip']}
    requested = []

    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *values, **kwargs):
            super().__init__(*values, directory=str(serving), **kwargs)

        def log_message(self, *_):
            pass

        def do_GET(self):
            requested.append(self.path)
            if self.path.startswith('/release/'):
                name = 'fetcher-tester-tools.zip' if self.path.endswith('/tools') else 'fetcher-simulator.zip'
                body = json.dumps({'assets': [{'name': name, 'digest': 'sha256:' + hashes[name],
                    'size': (serving / name).stat().st_size, 'browser_download_url': origin + '/' + name}]}).encode()
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            else:
                super().do_GET()

    http = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    origin = f'http://127.0.0.1:{http.server_port}'
    thread = Thread(target=http.serve_forever, daemon=True)
    thread.start()
    server_process = None
    source = args.source_fixture.resolve()
    source_database = source / 'playerdata.db'
    source_hash = digest(source_database)
    server = base / 'server'
    server.mkdir()
    request_file, result_file = base / 'request.json', base / 'result.json'

    def command(request):
        request_file.write_text(json.dumps(request), encoding='utf8')
        process = subprocess.run([str(installed / 'FetcherLauncher.exe'), '--install-root', str(installed),
            '--allow-local-fixtures', '--request-file', str(request_file), '--log-file', str(result_file)], timeout=180)
        value = json.loads(result_file.read_text(encoding='utf8'))
        assert process.returncode == 0, value
        return value

    try:
        print('Installing the complete client into an empty folder using FetcherSetup.exe...', flush=True)
        setup_result = base / 'setup-result.json'
        process = subprocess.run([str(args.setup.resolve()), '--install', '--install-root', str(installed),
            '--allow-local-fixtures', '--tools-release-url', origin + '/release/tools',
            '--client-release-url', origin + '/release/client', '--log-file', str(setup_result)], timeout=300)
        assert process.returncode == 0, setup_result.read_text(encoding='utf8')
        assert digest(installed / 'openmw.exe') == digest(runtime / 'openmw.exe')
        with ZipFile(args.bundle) as archive:
            package = json.loads(archive.read('openmw-client-package.json').decode('utf-8-sig'))
        profile = {'schemaVersion': 1, 'id': 'bootstrap-game-join', 'name': 'Isolated bootstrap game join',
            'version': '1', 'host': '127.0.0.1', 'port': args.port, 'content': package['content'],
            'packages': [{'id': 'content', 'url': origin + '/content.zip', 'sha256': hashes['content.zip'],
                          'dataDirs': package['dataDirs']}],
            'requiredFiles': ['meshes/fetcher/synth/keyboard-stand.nif', 'icons/fetcher/synth/keyboard.dds',
                              'FetcherStudio/ATTRIBUTION.txt', 'FetcherVehicles/CREDITS.txt']}
        profile_file = base / 'profile.json'
        profile_file.write_text(json.dumps(profile), encoding='utf8')
        command({'op': 'gameData', 'path': str(game)})
        review = command({'op': 'reviewProfile', 'path': str(profile_file)})
        state = command({'op': 'confirmProfile', 'token': review['profileReview']['token']})
        identity = state['selectedProfile']
        command({'op': 'prepare'})
        config = installed / '_fetcher_launcher/profiles' / identity
        config_text = (config / 'openmw.cfg').read_text(encoding='utf8')
        assert 'FetcherVehicles.omwaddon' in config_text and 'surf_mesa_mw.omwaddon' in config_text
        shutil.copytree(source / 'server-scripts', server / 'server-scripts')
        shutil.copytree(source / 'server-lua-packages', server / 'server-lua-packages')
        # A developer source-directory fallback otherwise masks missing deployed Lua libraries.
        shutil.copytree(installed / 'resources/lua_libs', server / 'resources/lua_libs')
        (server / 'server-scripts/server.omwscripts').write_text('GLOBAL: core.lua\nGLOBAL: procedural_synth.lua\n', encoding='utf8')
        seeded = subprocess.run(['python', str(args.seed_script.resolve()), str(source_database),
                                 str(server / 'playerdata.db')], capture_output=True, text=True)
        assert seeded.returncode == 0, seeded.stderr
        (server / 'logs').mkdir()
        (server / 'server.cfg').write_text(f'''[server]
port={args.port}
max_players=4
db_path={(server / 'playerdata.db').as_posix()}
[content]
openmw_cfg={(config / 'openmw.cfg').as_posix()}
resources={(installed / 'resources').as_posix()}
encoding=win1252
verify_determinism=true
[client_lua]
packages={(server / 'server-lua-packages').as_posix()}
[master]
public_listing=false
server_name=Native Bootstrap Local Acceptance
''', encoding='utf8')
        (installed / 'mp-keys').mkdir()
        key = source / 'mp-keys/127.0.0.1_25567.key'
        if not key.is_file():
            raise ValueError('Explicit source fixture is missing its local Synth test key')
        shutil.copyfile(key, installed / f'mp-keys/127.0.0.1_{args.port}.key')
        settings = (installed / 'profiles' / component(f'127.0.0.1:{args.port}') / 'ass' /
                    'characters' / component('Synth A') / 'userdata')
        settings.mkdir(parents=True)
        (settings / 'settings.cfg').write_text('[Video]\nresolution x = 1024\nresolution y = 768\nfullscreen = false\n[Sound]\nmaster volume = 0.3\nmusic volume = 0\n', encoding='utf8')
        log_file = server / 'stdout.log'
        with log_file.open('wb') as out, (server / 'stderr.log').open('wb') as error:
            server_process = subprocess.Popen([str(runtime / 'openmw-server.exe'), '--config', str(server / 'server.cfg'),
                '--log-dir', str(server / 'logs')], cwd=server, stdout=out, stderr=error,
                creationflags=subprocess.CREATE_NO_WINDOW)
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            assert server_process.poll() is None, log_file.read_text(errors='replace')[-4000:]
            startup = log_file.read_text(errors='replace')
            assert '[LuaServerContext] Startup failed:' not in startup, startup[-4000:]
            listening = powershell(f"Get-NetUDPEndpoint -LocalPort {args.port} -ErrorAction SilentlyContinue | "
                f"Where-Object OwningProcess -eq {server_process.pid} | Select-Object -ExpandProperty LocalPort")
            if str(args.port) in listening.stdout and 'Loaded 2 global server Lua script(s)' in startup:
                break
            time.sleep(.2)
        else:
            raise AssertionError('Staged server did not become ready')
        command({'op': 'identity', 'account': 'ass', 'character': 'Synth A', 'autoEnter': True})
        print('Launching the installed client through Fetcher Launcher into the staged server...', flush=True)
        command({'op': 'launch'})
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            text = log_file.read_text(errors='replace')
            if '[core] Connect: Synth A' in text and 'ActorList established complete cell roster from=Synth A' in text:
                break
            time.sleep(.25)
        else:
            raise AssertionError('Client did not enter the staged world; inspect retained client/server logs')
        assert digest(source_database) == source_hash
        assert all(digest(game / name) == value for name, value in owned.items())
        report = {'nativeSetupInstalledFreshClient': True, 'nativeLauncherJoinedServer': True,
            'serverPort': args.port, 'combinedContentPrepared': True, 'worldRosterReceived': True,
            'ownedGameFilesPreserved': True, 'sourceTestDatabasePreserved': True,
            'clientSha256': digest(installed / 'openmw.exe'), 'profile': identity,
            'releaseSources': 'explicit loopback fixtures with local runtime/content inputs',
            'cleanWindowsMachineTested': False, 'publicDeployment': False}
        (base / 'game-join-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
        print('PASS: installed launcher/client joined the local world with surf/truck/Studio content.', flush=True)
        if args.observe_seconds:
            time.sleep(args.observe_seconds)
    finally:
        # Scope cleanup to the exact executable installed by this test; never stop another game.
        for process in game_processes(installed / 'openmw.exe'):
            path = str(installed / 'openmw.exe').replace("'", "''")
            result = powershell(f"$p=Get-Process -Id {int(process['ProcessId'])} -ErrorAction SilentlyContinue; "
                f"if($p -and $p.Path -eq '{path}'){{Stop-Process -Id $p.Id -Force}}")
            assert result.returncode == 0, result.stderr
        if server_process is not None and server_process.poll() is None:
            server_process.terminate()
            server_process.wait(timeout=10)
        http.shutdown()
        http.server_close()
        thread.join()


if __name__ == '__main__':
    main()
