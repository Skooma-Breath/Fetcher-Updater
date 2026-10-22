"""Opt-in native preparation of audited original archives in a new isolated root.

This tests all available source layouts, not a complete server preset/game join.
No API credentials or public downloads are used. Hardlinks avoid duplicating the
original compressed archives; only their new cache directory entries are managed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--audit', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--game-data', type=Path, required=True)
    parser.add_argument('--allow-incomplete-audit', action='store_true')
    parser.add_argument('--resume', action='store_true', help='Resume only an identical test manifest created by this driver')
    args = parser.parse_args()
    root = args.root.resolve()
    if root.exists() and not args.resume:
        raise ValueError('Choose a new test root; existing installations are never reused')
    audit = json.loads(args.audit.read_text(encoding='utf8'))
    if not audit['ready'] and not args.allow_incomplete_audit:
        raise ValueError('Audit still has curation issues; use the explicit incomplete source-layout test flag')
    for package in audit['packages']:
        if not package['definition']['dataDirs'] or not any(
                source in package['definition'] for source in ['nexus', 'url']):
            raise ValueError('Available package still lacks its source/layout identity')
    profile = {'schemaVersion': 1, 'id': 'full-cache-source-layout-test',
               'name': 'Fetcher source archive acceptance (not a server preset)', 'version': '1',
               'description': 'Source extraction/layout test only; no game join or full compatibility claim.',
               'host': 'source-layout.invalid', 'port': 25565,
               'content': ['Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm'],
               'packages': [package['definition'] for package in audit['packages']]}
    manifest = root / 'source-layout-test.json'
    if root.exists() and (not manifest.is_file() or json.loads(manifest.read_text(encoding='utf8')) != profile):
        raise ValueError('Resume requires the exact source test manifest, not an existing game installation')
    root.mkdir(parents=True, exist_ok=True)
    downloads = root / '_fetcher_launcher/downloads'
    downloads.mkdir(parents=True, exist_ok=True)
    for package in audit['packages']:
        source = Path(package['archive'])
        with source.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        if digest != package['definition']['sha256']:
            raise ValueError('Source changed since audit: ' + package['mod'])
        target = downloads / (digest + '.zip')
        if not target.exists():
            target.hardlink_to(source)
    manifest.write_text(json.dumps(profile, indent=2), encoding='utf8')
    def command(request, timeout=120):
        (root / 'request.json').write_text(json.dumps(request), encoding='utf8')
        result = root / 'result.json'
        process = subprocess.run([str(args.exe.resolve()), '--install-root', str(root),
            '--request-file', str(root / 'request.json'), '--log-file', str(result)], timeout=timeout)
        response = json.loads(result.read_text(encoding='utf8'))
        if process.returncode:
            raise RuntimeError(response.get('error', 'Native command failed'))
        return response
    command({'op': 'gameData', 'path': str(args.game_data.resolve())})
    reviewed = command({'op': 'reviewProfile', 'path': str(manifest)})
    accepted = command({'op': 'confirmProfile', 'token': reviewed['profileReview']['token']})
    print(f"Preparing {len(profile['packages'])} exact source packages with the native executable...", flush=True)
    start = time.monotonic()
    command({'op': 'prepare'}, timeout=1800)
    config = root / '_fetcher_launcher/profiles' / accepted['selectedProfile'] / 'openmw.cfg'
    assert config.is_file()
    report = {'sourcePackageCount': len(profile['packages']), 'sourceArchiveBytes': audit['totalArchiveBytes'],
              'elapsedSeconds': round(time.monotonic() - start, 1), 'config': str(config),
              'fullPreset': False, 'remainingAuditIssues': audit['issues']}
    (root / 'source-layout-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
