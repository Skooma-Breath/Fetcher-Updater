"""Assemble a local release review. This script never publishes or deploys anything."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
from zipfile import ZipFile, ZIP_DEFLATED


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def verify_zip(path, inventory):
    with ZipFile(path) as archive:
        names = [entry.filename for entry in archive.infolist() if not entry.is_dir()]
        assert len(names) == len(set(name.lower() for name in names)), 'Duplicate ZIP path'
        for name in names:
            parts = PurePosixPath(name).parts
            assert not name.startswith(('/', '\\')) and '\\' not in name and ':' not in name
            assert '..' not in parts and not any(part.endswith((' ', '.')) for part in parts)
        value = json.loads(archive.read(inventory).decode('utf-8-sig'))
        for entry in value['files']:
            data = archive.read(entry['path'])
            assert len(data) == entry['size']
            assert hashlib.sha256(data).hexdigest() == entry['sha256'], entry['path']
        return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('engine-source', 'runtime', 'tools-dir', 'content', 'profile', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    root = args.output.resolve()
    publish = root / 'publish'
    operator = root / 'operator'
    assert publish.is_dir() and operator.is_dir(), 'Create a dedicated review root first'
    assert not (root / 'release-review.json').exists(), 'Review already assembled'
    engine = args.engine_source.resolve()
    runtime = args.runtime.resolve()
    commit = subprocess.check_output(['git', '-C', str(engine), 'rev-parse', 'HEAD'], text=True).strip()
    # The live engine worktree can contain unrelated Lua edits; copy server source from Git,
    # and refuse native source modifications when assigning this commit to its compiled runtime.
    changed = subprocess.check_output(['git', '-C', str(engine), 'diff', '--name-only', 'HEAD'], text=True).splitlines()
    assert all(name.startswith('files/server/server-lua-packages/') for name in changed), changed
    inventory = verify_zip(args.tools_dir / 'fetcher-tester-tools.zip', 'fetcher-tester-tools.json')
    assets = []
    for name in ('fetcher-tester-tools.zip', 'FetcherSetup.exe', 'Install-Fetcher-Tester-Tools.ps1',
                 'Setup-Fetcher-Updater.bat', 'fetcher-simulator-umo.json'):
        target = publish / name
        assert not target.exists(), target
        shutil.copyfile(args.tools_dir / name, target)
        assets.append({'file': name, 'kind': 'launcher', 'sha256': digest(target), 'bytes': target.stat().st_size})
    client = publish / 'fetcher-simulator.zip'
    assert not client.exists()
    paths = [runtime / 'openmw.exe', runtime / 'defaults.bin', *runtime.glob('*.dll')]
    for folder in ('resources', 'osgPlugins-3.6.5'):
        paths.extend(path for path in (runtime / folder).rglob('*') if path.is_file())
    records = []
    with ZipFile(client, 'w', ZIP_DEFLATED, compresslevel=1) as archive:
        for path in sorted(set(paths)):
            assert not path.is_symlink(), path
            name = path.relative_to(runtime).as_posix()
            assert path.suffix.lower() not in ('.esm', '.bsa', '.key', '.db', '.dmp'), name
            archive.write(path, name)
            records.append({'path': name, 'size': path.stat().st_size, 'sha256': digest(path)})
        for name in ('COPYING', 'AUTHORS.md'):
            path = engine / name
            if path.is_file():
                data = subprocess.check_output(['git', '-C', str(engine), 'show', commit + ':' + name])
                archive.writestr(name, data)
                records.append({'path': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        archive.writestr('fetcher-client-files.json', json.dumps({'schemaVersion': 1,
            'clientCommit': commit, 'files': records}, indent=2) + '\n')
    verify_zip(client, 'fetcher-client-files.json')
    assets.append({'file': client.name, 'kind': 'studio-client', 'sha256': digest(client), 'bytes': client.stat().st_size})
    content = publish / 'openmw-client-mods-studio-candidate.zip'
    assert not content.exists()
    shutil.copyfile(args.content, content)
    profile = json.loads(args.profile.read_text(encoding='utf-8-sig'))
    assert profile['packages'][0]['sha256'] == digest(content), 'Content differs from the tested profile'
    profile.update(id='fetcher-vanilla-studio', name='Fetcher Vanilla + Studio (cutover candidate)',
                   version='2026-10-22-review', host='163.192.106.225', port=25569)
    write_json(publish / 'fetcher-vanilla-studio.json', profile)
    with ZipFile(content) as archive:
        names = archive.namelist()
        assert not any(PurePosixPath(name).suffix.lower() in ('.esm', '.bsa', '.key', '.db') for name in names)
        for suffix in ('FetcherStudio/ATTRIBUTION.txt', 'FetcherVehicles/CREDITS.txt',
                       'meshes/fetcher/synth/keyboard-stand.nif', 'icons/fetcher/synth/keyboard.dds'):
            assert any(name.endswith(suffix) for name in names), suffix
    for path in (content, publish / 'fetcher-vanilla-studio.json'):
        assets.append({'file': path.name, 'kind': 'studio-content', 'sha256': digest(path), 'bytes': path.stat().st_size})
    # Keep clean server source in the private operator review, not the GitHub asset list.
    server_source = operator / 'matching-server-source.zip'
    subprocess.run(['git', '-C', str(engine), 'archive', '--format=zip', '-o', str(server_source), commit,
                    'apps/openmw-server/scripts', 'files/server/server-lua-packages/fetcher.procedural-synth',
                    'scripts/vps-post-receive-hook.sh', 'scripts/vps-deploy-server.sh',
                    'scripts/build-server.sh', 'components/openmw-mp/MasterServerProtocol.hpp'], check=True)
    # Extracted into this private review only; the authoritative runtime is built by the VPS hook.
    with ZipFile(server_source) as archive:
        archive.extractall(operator / 'matching-server-source')
    manifest = {'schemaVersion': 1, 'status': 'local-review-only', 'published': False,
        'vpsDeployedByThisScript': False, 'armServerGitHubAssetRequired': False,
        'launcherSourceCommit': inventory['sourceCommit'], 'engineSourceCommit': commit,
        'clientExecutableSha256': digest(runtime / 'openmw.exe'),
        'serverWindowsExecutableSha256': digest(runtime / 'openmw-server.exe'),
        'serverDelivery': 'Existing VPS Git post-receive build/deploy workflow; no ARM release asset',
        'launcherReleaseAssets': [item['file'] for item in assets if item['kind'] == 'launcher'],
        'studioFollowupAssets': [item['file'] for item in assets if item['kind'] != 'launcher'],
        'launcherClientChannel': 'Existing published Fetcher-Simulator-Vehicles; unchanged in the launcher-only release',
        'assets': assets,
        'activationGates': ['Re-test this client/tools pair; previous evidence identifies older exact hashes',
            'Publish matching Windows client and channel metadata together',
            'Preserve operator configs, player databases and server Lua storage during VPS deployment',
            'Prepare Vanilla content authority and Studio Lua before activating the new server profile',
            'Choose the first public keyboard placement; do not copy test mpNum=1 into live databases',
            'Keep the current-public Vanilla profile until the server cutover succeeds',
            'Verify released URLs/digests and old-launcher upgrade after publication']}
    write_json(root / 'release-review.json', manifest)
    (root / 'SHA256SUMS.txt').write_text(''.join(f"{item['sha256']}  publish/{item['file']}\n" for item in assets), encoding='utf-8')
    table = '\n'.join(f"| {item['file']} | {item['kind']} | {item['bytes']:,} | `{item['sha256']}` |" for item in assets)
    (root / 'REVIEW.md').write_text(f'''# Fetcher release review — 2026-10-22

Local staging only. No GitHub upload or VPS deployment is performed by the staging tool.

Launcher source: `{inventory['sourceCommit']}`. Studio engine/server source: `{commit}`.

## Launcher release

The five `launcher` assets below form the independent launcher release. Its update channel still
uses the existing published Vehicles client. It can be tested/released against the existing public
servers, without publishing an ARM server or activating Studio content. Retained old release assets
and their GitHub metadata are under `rollback/` and `evidence/`.

## Studio cutover

The other three assets are the matching Windows Studio client, content bundle and proposed Vanilla
profile. They are staged separately for a coordinated follow-up. The proposed profile targets the
real Vanilla endpoint, so do not activate it against today's stock-content server. The launcher-only
package deliberately retains its current-public Vanilla profile and existing client channel.

The VPS builds its own ARM64 server through the existing Git push hook. No ARM GitHub asset is needed.
`operator/matching-server-source/` contains exact committed server scripts and deployment scripts for
review. It is not a live-config overlay: its generic config/manifest must not overwrite operator settings.
Use `launcher/RELEASE-REVIEW.md` in the source repository for the cutover and rollback procedure.

## Assets

| File | Release role | Bytes | SHA-256 |
| --- | --- | ---: | --- |
{table}

## Acceptance and activation

Existing evidence is retained with exact source/hash identities; an older test result does not certify
the newly assembled client ZIP. Record the final candidate's package and fresh-install/upgrade checks
in `evidence/` before promotion. Review `release-review.json` for the Studio activation gates.
Only the five launcher assets belong in the launcher stable release. Preserve the old release target
and all four prior assets. Reopen the launcher after upgrading. Do not run the stable publisher against
this entire folder: the client/content assets belong to their own releases.
''', encoding='utf-8')
    print(json.dumps({'ok': True, 'root': str(root), 'assets': len(assets), 'engineCommit': commit,
                      'launcherCommit': inventory['sourceCommit'], 'armServerPublished': False}, indent=2))


if __name__ == '__main__':
    main()
