"""Opt-in real staged GUI restart after a hard-killed native tools update."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
import time
sys.dont_write_bytecode = True
from test_launcher_upgrade import read_package, launcher_processes, stop_owned, LEGACY_ENV


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--old-tools', type=Path, required=True)
    parser.add_argument('--new-launcher', type=Path, required=True)
    parser.add_argument('--transaction-helper', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--leave-preview', action='store_true')
    args = parser.parse_args()
    base = args.root.resolve()
    assert not base.exists(), 'Use a fresh test directory'
    base.mkdir(parents=True)
    root, stage = base / 'Player installation é', base / 'staged update'
    root.mkdir(); stage.mkdir()
    _, old_files, old_manifest = read_package(args.old_tools)
    for name, content in old_files.items():
        target = root / name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(content)
    markers = {'openmw.cfg': b'# personal configuration\n', 'settings.cfg': b'# personal settings\n',
               'userdata/save.omwsave': b'personal save', 'openmw.exe': b'not-a-game-test-fixture'}
    for name, content in markers.items():
        target = root / name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(content)
    names = ['FetcherLauncher.exe', 'ui/index.html', 'ui/assets/fetcher-float.gif',
             'ui/assets/fetcher-float-right.gif', 'ui/assets/potm2504a.jpg', 'FetcherLauncher-THIRD-PARTY-NOTICES.txt']
    records = []
    for name in names:
        content = (args.new_launcher / name).read_bytes()
        target = stage / name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(content)
        records.append({'path': name, 'size': len(content), 'sha256': hashlib.sha256(content).hexdigest()})
    (stage / 'fetcher-tester-tools.json').write_text(json.dumps({'schemaVersion': 1, 'files': records}), encoding='utf-8')
    result = subprocess.run([str(args.transaction_helper.resolve()), '--crash', str(root), str(stage), 'tools', '2'], timeout=30)
    assert result.returncode == 87, result.returncode
    assert (root / 'FetcherLauncher.exe').read_bytes() != old_files['FetcherLauncher.exe']
    process = None
    try:
        assert subprocess.run([str(root / 'FetcherLauncher.exe')], timeout=30, env=LEGACY_ENV).returncode == 0
        expected = hashlib.sha256(old_files['FetcherLauncher.exe']).hexdigest()
        install_arg = f'--install-root "{root}"'.lower()
        for _ in range(60):
            matches = [p for p in launcher_processes() if '--staged' in (p['CommandLine'] or '')
                       and install_arg in p['CommandLine'].lower()]
            for candidate in matches:
                if hashlib.sha256(Path(candidate['ExecutablePath']).read_bytes()).hexdigest() == expected:
                    process = candidate
                    break
            if process:
                break
            time.sleep(.2)
        assert process, 'Recovery failed to restart the previous matching launcher'
        for name, content in {**old_files, **markers}.items():
            assert (root / name).read_bytes() == content, name
        staged_ui = Path(process['ExecutablePath']).parent / 'ui/index.html'
        assert staged_ui.read_bytes() == old_files['ui/index.html']
        snapshots = list((root / '_fetcher_launcher/rollback').glob('tx-tools-*'))
        assert len(snapshots) == 1 and (snapshots[0] / 'rolled-back.json').exists()
        report = {'oldSourceCommit': old_manifest['sourceCommit'], 'previousExecutableSha256': expected,
                  'interruptedFiles': 2, 'restoredOriginalInventory': True, 'personalFilesPreserved': True,
                  'matchingPreviousGuiRestarted': True, 'previewProcessId': process['ProcessId'],
                  'installRoot': str(root), 'gameLaunched': False, 'serverChanged': False}
        (base / 'gui-recovery-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print('PASS real GUI recovery: hard-killed tools update restores original files and reopens the matching previous executable/UI')
        print('Report: ' + str(base / 'gui-recovery-report.json'))
    finally:
        if process and not args.leave_preview:
            stop_owned(process, root)


if __name__ == '__main__':
    main()
