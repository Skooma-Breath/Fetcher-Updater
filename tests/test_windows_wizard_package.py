"""Start the real packaged Qt wizard through a freshly installed native launcher.

Windows release acceptance, separate from the argument-recording bootstrap tests.
Serves supplied release ZIPs on loopback, installs with the real native Setup, starts
the wizard without developer Qt paths, checks its ready log and loaded local plugin,
and exercises the INI importer. Does not click through or finish the wizard.
"""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
from threading import Thread
import time


def powershell(source):
    shell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    return subprocess.check_output([str(shell), '-NoProfile', '-NonInteractive', '-Command', source], text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--setup', type=Path, required=True)
    parser.add_argument('--tools-archive', type=Path, required=True)
    parser.add_argument('--client-archive', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    options = parser.parse_args()
    work = options.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=False)
    root = work / 'Fresh install with spaces'
    payloads = {'tools': options.tools_archive.read_bytes(), 'client': options.client_archive.read_bytes()}
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in payloads.items()}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            kind = self.path.rsplit('/', 1)[-1]
            if kind not in payloads:
                self.send_error(404)
                return
            if self.path.startswith('/release/'):
                name = 'fetcher-tester-tools.zip' if kind == 'tools' else 'fetcher-simulator.zip'
                data = json.dumps({'assets': [{'name': name, 'digest': 'sha256:' + hashes[kind],
                    'browser_download_url': origin + '/archive/' + kind}]}).encode()
            elif self.path.startswith('/archive/'):
                data = payloads[kind]
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
    environment = {key: value for key, value in os.environ.items()
                   if not key.upper().startswith(('QT_', 'QML_')) and key.upper() != 'PATH'}
    windows = Path(os.environ['SystemRoot'])
    environment['PATH'] = str(windows / 'System32') + os.pathsep + str(windows)
    environment['QT_QPA_PLATFORM'] = 'windows'
    owned_pid = None
    try:
        with (work / 'setup-console.log').open('wb') as output:
            process = subprocess.run([str(options.setup.resolve()), '--install', '--install-root', str(root),
                '--log-file', str(work / 'setup.json'), '--allow-local-fixtures',
                '--tools-release-url', origin + '/release/tools',
                '--client-release-url', origin + '/release/client'], stdout=output, stderr=subprocess.STDOUT,
                env=environment, timeout=180)
        result = json.loads((work / 'setup.json').read_text(encoding='utf-8-sig'))
        assert process.returncode == 0 and result['installed'], result
        request = work / 'wizard-request.json'
        request.write_text(json.dumps({'op': 'wizard'}))
        with (work / 'launcher-console.log').open('wb') as output:
            process = subprocess.run([str(root / 'FetcherLauncher.exe'), '--install-root', str(root),
                '--request-file', str(request), '--log-file', str(work / 'launcher.json')],
                cwd=root, env=environment, stdout=output, stderr=subprocess.STDOUT, timeout=30)
        result = json.loads((work / 'launcher.json').read_text(encoding='utf-8-sig'))
        assert process.returncode == 0 and 'error' not in result, result.get('error')
        expected = str(root / 'openmw-wizard.exe').replace("'", "''")
        rows = json.loads(powershell(
            "@{items=@(Get-CimInstance Win32_Process -Filter \"Name='openmw-wizard.exe'\" | "
            f"Where-Object {{$_.ExecutablePath -eq '{expected}'}} | "
            'Select-Object ProcessId,ParentProcessId,ExecutablePath)} | ConvertTo-Json -Compress'))['items']
        identifiers = {row['ProcessId'] for row in rows}
        main_processes = [row for row in rows if row['ParentProcessId'] not in identifiers]
        assert len(main_processes) == 1, rows  # The engine also starts its own crash monitor.
        owned_pid = main_processes[0]['ProcessId']
        deadline = time.monotonic() + 30
        ready = []
        while time.monotonic() < deadline:
            ready = [path for path in root.rglob('*.log')
                     if 'OpenMW Wizard ready' in path.read_text(encoding='utf-8-sig', errors='replace')]
            if ready:
                break
            time.sleep(.2)
        assert ready, 'Real Qt wizard never reached its event loop; inspect Wizard.log'
        modules = json.loads(powershell(
            f'@{{items=@((Get-Process -Id {owned_pid}).Modules | '
            "Where-Object {$_.ModuleName -eq 'qwindows.dll'} | "
            'Select-Object -ExpandProperty FileName)} | ConvertTo-Json -Compress'))['items']
        assert len(modules) == 1 and Path(modules[0]).resolve() == (root / 'platforms/qwindows.dll').resolve(), modules
        help_result = subprocess.run([str(root / 'openmw-iniimporter.exe'), '--help'], cwd=root,
            env=environment, capture_output=True, timeout=15)
        assert help_result.returncode == 0 and b'Syntax: openmw-iniimporter' in help_result.stdout, help_result.stdout + help_result.stderr
        (work / 'importer-help.txt').write_bytes(help_result.stdout + help_result.stderr)
        report = {'ok': True, 'toolsSha256': hashes['tools'], 'clientSha256': hashes['client'],
            'installedRoot': str(root), 'wizardReadyLog': str(ready[0]), 'localWindowsPlugin': modules[0],
            'developerQtPathsRemoved': True, 'iniImporterHelpPassed': True,
            'scope': 'Real native setup and launcher action, Qt wizard startup and importer; wizard not finished.'}
        (work / 'report.json').write_text(json.dumps(report, indent=2))
        print(json.dumps(report), flush=True)
    finally:
        expected = str(root / 'openmw-wizard.exe').replace("'", "''")
        # This installation did not exist before the test. Stop only its own wizard
        # and crash monitor, including when an assertion failed before PID selection.
        powershell("Get-CimInstance Win32_Process -Filter \"Name='openmw-wizard.exe'\" | "
                   f"Where-Object {{$_.ExecutablePath -eq '{expected}'}} | "
                   'ForEach-Object {Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue}')
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == '__main__':
    main()
