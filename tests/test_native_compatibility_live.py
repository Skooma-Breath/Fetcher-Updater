"""Opt-in real-executable application of verified Fetcher compatibility recipes.

Reuses only a source-layout fixture created by test_umo_cache_live.py. Does not
touch the authoritative installed mods, connect to a server or publish anything.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--recipes', type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    source_manifest = root / 'source-layout-test.json'
    source = json.loads(source_manifest.read_text(encoding='utf8'))
    if source['id'] != 'full-cache-source-layout-test' or source['host'] != 'source-layout.invalid':
        raise ValueError('Choose an existing isolated source-layout fixture, not a game installation')
    recipes = json.loads(args.recipes.read_text(encoding='utf8'))
    if recipes['failures']:
        raise ValueError('Compatibility authoring still reports failures')
    available = {package['id'] for package in source['packages']}
    if set(recipes['packages']) - available:
        raise ValueError('Recipe package is absent from the source fixture')
    source.update(id='native-compatibility-test', name='Fetcher compatibility acceptance', version='1')
    for package in source['packages']:
        if package['id'] in recipes.get('sourceArchives', {}) and package['sha256'] != recipes['sourceArchives'][package['id']]:
            raise ValueError('Compatibility fragment belongs to a different source archive')
        package.setdefault('actions', []).extend(recipes['packages'].get(package['id'], []))
    manifest = root / 'compatibility-test.json'
    manifest.write_text(json.dumps(source, indent=2), encoding='utf8')
    original_config = root / 'openmw.cfg'
    original_config.write_bytes(b'# compatibility fixture personal config marker\n')
    def command(request):
        (root / 'compat-request.json').write_text(json.dumps(request), encoding='utf8')
        result = root / 'compat-result.json'
        process = subprocess.run([str(args.exe.resolve()), '--install-root', str(root),
            '--request-file', str(root / 'compat-request.json'), '--log-file', str(result)], timeout=1800)
        response = json.loads(result.read_text(encoding='utf8'))
        if process.returncode:
            raise RuntimeError(response.get('error', 'Native command failed'))
        return response
    review = command({'op': 'reviewProfile', 'path': str(manifest)})
    accepted = command({'op': 'confirmProfile', 'token': review['profileReview']['token']})
    print(f"Applying {recipes['portedPatchCount']} known compatibility fixes to isolated native layouts...", flush=True)
    command({'op': 'prepare'})
    layouts = {}
    for directory in (root / '_fetcher_launcher/layouts').iterdir():
        receipt = directory / '.fetcher-package.json'
        if receipt.is_file():
            metadata = json.loads(receipt.read_text(encoding='utf8'))
            layouts.setdefault(metadata['sourceSha256'], []).append(directory)
    verified = 0
    for package in source['packages']:
        patches = [action for action in recipes['packages'].get(package['id'], []) if action['action'] == 'patch']
        if not patches:
            continue
        def matches(directory):
            for patch in patches:
                target = directory / patch['path']
                if not target.is_file() or hashlib.sha256(target.read_bytes()).hexdigest() != patch['outputSha256']:
                    return False
            return True
        assert any(matches(directory) for directory in layouts.get(package['sha256'], [])), package['id']
        verified += len(patches)
    assert original_config.read_bytes() == b'# compatibility fixture personal config marker\n'
    report = {'portedPatchCount': verified, 'profile': accepted['selectedProfile'],
              'clientResourcesWritten': False, 'fullPreset': False, 'gameLaunched': False}
    (root / 'compatibility-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
