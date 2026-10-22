"""Upgrade a real published legacy launcher to a candidate through its existing updater.

Only test-owned installations and a loopback GitHub fixture are used. A small entry-point
adapter supplies documented endpoint/skip arguments to the pristine legacy updater;
the launcher executable, updater and bootstrap implementation are never patched.
"""
from pathlib import Path, PurePosixPath
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from threading import Thread
import argparse
import hashlib
import io
import json
import os
import subprocess
import time
import zipfile

# A PowerShell 7 development host may prepend PS7-only modules to child PS5.1 paths.
# Use the Windows PowerShell inbox modules for this process tree, as a normal Explorer
# launch does. This is test-local and never modifies user/machine environment settings.
LEGACY_ENV = os.environ.copy()
LEGACY_ENV['PSModulePath'] = str(Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/Modules')


def digest(value):
    return hashlib.sha256(value).hexdigest()


def powershell(script, timeout=30):
    return subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
                           '[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); ' + script],
                          capture_output=True, text=True, encoding='utf-8-sig', errors='replace',
                          timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW, env=LEGACY_ENV)


def launcher_processes():
    result = powershell("Get-CimInstance Win32_Process -Filter \"Name='FetcherLauncher.exe'\" | "
                        "Select-Object ProcessId,ExecutablePath,CommandLine | ConvertTo-Json -Compress")
    assert result.returncode == 0, result.stderr
    if not result.stdout.strip():
        return []
    value = json.loads(result.stdout)
    return value if isinstance(value, list) else [value]


def staged_process(root):
    install_argument = f'--install-root "{root}"'.lower()
    for _ in range(60):
        found = [p for p in launcher_processes() if '--staged' in (p['CommandLine'] or '')
                 and install_argument in p['CommandLine'].lower()]
        if len(found) == 1:
            return found[0]
        assert len(found) < 2, 'Multiple owned staged launchers'
        time.sleep(.2)
    raise AssertionError('Launcher did not stage itself')


def stop_owned(process, root):
    current = next((p for p in launcher_processes() if p['ProcessId'] == process['ProcessId']), None)
    if current:
        assert f'--install-root "{root}"'.lower() in current['CommandLine'].lower()
        assert '--staged' in current['CommandLine'] and current['ExecutablePath'] == process['ExecutablePath']
        result = powershell(f"Stop-Process -Id {int(current['ProcessId'])} -Force -ErrorAction Stop")
        assert result.returncode == 0, result.stderr


def read_package(path):
    payload = path.read_bytes()
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        files = {p.filename: archive.read(p) for p in archive.infolist() if not p.is_dir()}
    for name in files:
        path_name = PurePosixPath(name)
        assert not path_name.is_absolute() and not set(path_name.parts) & {'..', '.'}
        assert ':' not in name and '\\' not in name, 'Unsafe fixture archive path'
    manifest = json.loads(files['fetcher-tester-tools.json'].decode('utf-8-sig'))
    assert manifest['schemaVersion'] == 1
    for record in manifest['files']:
        content = files[record['path']]
        assert len(content) == record['size'] and digest(content) == record['sha256'], record['path']
    return payload, files, manifest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--legacy-archive', type=Path, required=True)
    parser.add_argument('--legacy-sha256', required=True)
    parser.add_argument('--candidate-archive', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True, help='New, retained test directory')
    parser.add_argument('--gui', action='store_true', help='Check real staged old/new GUI processes')
    parser.add_argument('--leave-preview', action='store_true')
    args = parser.parse_args()
    assert not args.leave_preview or args.gui
    base = args.root.resolve()
    assert not base.exists(), 'Use a fresh directory; this test never deletes an existing installation'
    legacy_bytes, legacy_files, legacy = read_package(args.legacy_archive)
    assert digest(legacy_bytes) == args.legacy_sha256.lower(), 'Legacy public download SHA-256 mismatch'
    assert b'fetcherStartUpdate' in legacy_files['ui/index.html'], (
        'Baseline is not the legacy PowerShell launcher; supply a retained legacy ZIP and its hash')
    candidate_bytes, candidate_files, candidate = read_package(args.candidate_archive)
    assert legacy_files['FetcherLauncher.exe'] != candidate_files['FetcherLauncher.exe']
    base.mkdir(parents=True)
    harness = base / 'harness'; harness.mkdir()
    runner = harness / 'legacy-runner'; runner.mkdir()
    for name in ['FetcherLauncher.exe', 'ui/index.html']:
        path = runner / name; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(legacy_files[name])
    pristine = harness / 'pristine-legacy-updater.ps1'
    pristine.write_bytes(legacy_files['Update-Fetcher-Simulator.ps1'])
    # The same adapter is staged by the legacy binary's real RunUpdaterProcess implementation.
    adapter = r'''param([string]$InstallRoot, [switch]$QuickCheck, [switch]$StatusOnly)
$ErrorActionPreference = 'Stop'
$fixture = Get-Content -Raw -LiteralPath (Join-Path $InstallRoot 'upgrade-fixture.json') | ConvertFrom-Json
$parameters = @{
    InstallRoot = $InstallRoot
    GitHubApiBaseUrl = $fixture.origin
    GitHubDownloadBaseUrl = $fixture.origin
    PatchCatalogPath = (Join-Path $InstallRoot 'fetcher-client-patches.json')
    SkipClientUpdate = $true
    SkipClientModBundle = $true
    SkipUmoMods = $true
    SkipModPatches = $true
    QuickCheck = [bool]$QuickCheck
    StatusOnly = [bool]$StatusOnly
}
& $fixture.legacyUpdater @parameters
if (-not $?) { exit 1 }
'''
    requested = []
    response_bytes = candidate_bytes
    expected_digest = digest(candidate_bytes)
    tools_path = '/Skooma-Breath/Fetcher-Updater/releases/download/fetcher-tester-tools/fetcher-tester-tools.zip'

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            requested.append(self.path)
            if self.path == tools_path:
                body = response_bytes; mime = 'application/zip'
            elif '/releases/tags/' in self.path:
                tag = self.path.rsplit('/', 1)[1]
                tools = tag == 'fetcher-tester-tools'
                name = 'fetcher-tester-tools.zip' if tools else 'fetcher-simulator.zip' if 'Fetcher-Simulator' in tag else 'openmw-client-mods.zip'
                body = json.dumps({'tag_name': tag, 'target_commitish': candidate['sourceCommit'] if tools else 'b' * 40,
                                   'assets': [{'name': name, 'digest': 'sha256:' + (expected_digest if tools else 'b' * 64),
                                               'size': len(response_bytes) if tools else 1,
                                               'browser_download_url': origin + (tools_path if tools else '/never-download')}]}).encode()
                mime = 'application/json'
            else:
                self.send_error(404); return
            self.send_response(200); self.send_header('Content-Type', mime)
            self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    origin = f'http://127.0.0.1:{server.server_port}'
    thread = Thread(target=server.serve_forever, daemon=True); thread.start()
    old_gui = new_gui = None
    success_root = base / 'Player installation'
    markers = {'openmw.cfg': b'# personal config\ncontent=Morrowind.esm\n', 'settings.cfg': b'[Video]\nresolution x = 2560\n',
               'userdata/saves/personal.omwsave': b'personal save', 'Data Files/personal.dds': b'personal texture',
               'mp-keys/private.key': b'synthetic private marker, never uploaded', 'ui/personal.css': b'/* locally owned */'}

    def seed(root):
        root.mkdir()
        for name, content in {**legacy_files, **markers, 'openmw.exe': b'not-a-real-game-fixture'}.items():
            path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(content)
        (root / 'upgrade-fixture.json').write_text(json.dumps({'origin': origin, 'legacyUpdater': str(pristine)}), encoding='utf-8')
        (root / 'Update-Fetcher-Simulator.ps1').write_text(adapter, encoding='utf-8-sig')

    def unchanged(root, files):
        for name, content in files.items():
            assert (root / name).read_bytes() == content, 'Unexpected change: ' + name

    def run(root, status=False, ok=True):
        log = base / (root.name + ('-status.log' if status else '-update.log'))
        command = [str(runner / 'FetcherLauncher.exe'), '--run-updater', '--quick-check', '--install-root', str(root), '--log-file', str(log)]
        if status:
            command.append('--status-only')
        result = subprocess.run(command, timeout=90, env=LEGACY_ENV)
        text = log.read_text(encoding='utf-8-sig', errors='replace')
        assert (result.returncode == 0) == ok, (result.returncode, text[-5000:])
        return text

    report = {'legacySourceCommit': legacy['sourceCommit'], 'legacyArchiveSha256': digest(legacy_bytes),
              'candidateSourceCommit': candidate['sourceCommit'], 'candidateArchiveSha256': digest(candidate_bytes),
              'oldLauncherSha256': digest(legacy_files['FetcherLauncher.exe']),
              'newLauncherSha256': digest(candidate_files['FetcherLauncher.exe']), 'checks': []}
    try:
        seed(success_root)
        status = json.loads(next(line for line in reversed(run(success_root, status=True).splitlines()) if line.startswith('{')))
        assert status['status'] == 'updateAvailable' and not status['testerTools']['current']
        assert status['testerTools']['availableCommit'] == candidate['sourceCommit']
        assert tools_path not in requested, 'Status check installed an update'
        report['checks'].append('Pristine legacy status detects the candidate without downloading it')
        if args.gui:
            subprocess.run([str(success_root / 'FetcherLauncher.exe')], timeout=30, env=LEGACY_ENV)
            old_gui = staged_process(success_root)
            assert digest(Path(old_gui['ExecutablePath']).read_bytes()) == report['oldLauncherSha256']
        run(success_root)
        unchanged(success_root, candidate_files)
        unchanged(success_root, markers)
        assert tools_path in requested, 'Legacy bootstrap did not download the candidate'
        installed = json.loads((success_root / 'fetcher-tester-tools.json').read_text(encoding='utf-8-sig'))
        assert installed['sourceCommit'] == candidate['sourceCommit']
        report['checks'].append('Pristine legacy updater/bootstrap installs exact candidate files and preserves personal markers')
        if old_gui:
            assert any(p['ProcessId'] == old_gui['ProcessId'] for p in launcher_processes())
            assert digest(Path(old_gui['ExecutablePath']).read_bytes()) == report['oldLauncherSha256']
            assert (Path(old_gui['ExecutablePath']).parent / 'ui/index.html').read_bytes() == legacy_files['ui/index.html']
            report['checks'].append('Old staged GUI remains running while installed executable/UI are replaced')
            stop_owned(old_gui, success_root); old_gui = None
        exe = success_root / 'FetcherLauncher.exe'
        assert subprocess.run([str(exe), '--self-test', '--install-root', str(success_root)], timeout=30).returncode == 0
        request, result = harness / 'request.json', harness / 'state.json'
        request.write_text('{"op":"init"}', encoding='utf-8')
        assert subprocess.run([str(exe), '--install-root', str(success_root), '--request-file', str(request), '--log-file', str(result)], timeout=30).returncode == 0
        state = json.loads(result.read_text(encoding='utf-8-sig'))
        assert state['selectedProfile'] == 'installed', 'Upgrade changed the existing game/mod setup'
        unchanged(success_root, markers)
        report['checks'].append('Installed native executable passes self-test and opens existing-installation mode')
        # The legacy validator must fail before replacing any files on corrupt or incomplete payloads.
        for scenario in ['bad-archive-hash', 'missing-required-ui']:
            root = base / scenario; seed(root)
            if scenario == 'bad-archive-hash':
                response_bytes = b'corrupt download'; expected_digest = digest(candidate_bytes)
            else:
                buffer = io.BytesIO()
                with zipfile.ZipFile(buffer, 'w', zipfile.ZIP_DEFLATED) as archive:
                    for name, content in candidate_files.items():
                        if name != 'ui/index.html':
                            archive.writestr(name, content)
                response_bytes = buffer.getvalue(); expected_digest = digest(response_bytes)
            before = {str(p.relative_to(root)): p.read_bytes() for p in root.rglob('*') if p.is_file()}
            run(root, ok=False)
            unchanged(root, before)
            report['checks'].append(scenario + ' rejected before replacing legacy files')
        if args.gui:
            subprocess.run([str(exe)], timeout=30, env=LEGACY_ENV)
            new_gui = staged_process(success_root)
            assert digest(Path(new_gui['ExecutablePath']).read_bytes()) == report['newLauncherSha256']
            assert (Path(new_gui['ExecutablePath']).parent / 'ui/index.html').read_bytes() == candidate_files['ui/index.html']
            report['checks'].append('Reopening starts the new native executable with the matching new UI')
            report['previewProcessId'] = new_gui['ProcessId']
        report.update({'gameLaunched': False, 'publicReleaseChanged': False, 'publicServerChanged': False,
                       'fixtureAdapter': 'Only documented local endpoints/skip switches supplied; pristine legacy script is recorded in harness',
                       'installRoot': str(success_root)})
        (base / 'upgrade-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print('PASS legacy-to-native upgrade: ' + '; '.join(report['checks']), flush=True)
        print('Report: ' + str(base / 'upgrade-report.json'), flush=True)
    finally:
        server.shutdown(); server.server_close(); thread.join()
        if old_gui:
            stop_owned(old_gui, success_root)
        if new_gui and not args.leave_preview:
            stop_owned(new_gui, success_root)


if __name__ == '__main__':
    main()
