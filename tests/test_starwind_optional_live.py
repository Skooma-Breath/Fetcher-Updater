"""Opt-in plain-vs-experimental Starwind isolation using real cached releases.

Creates a new fixture. Plain Nexus Starwind must prepare without installing the
Fetcher compatibility archive or restoring game assets. Only the explicitly
reviewed experimental profile adds that separate layer; switching back preserves
the plain config and raw files. No game/server/public configuration is changed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['exe', 'root', 'source-root', 'audit', 'overrides', 'owned-recipe', 'archive', 'game-data']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    if root.exists():
        raise ValueError('Choose a new isolated fixture root')
    audited = json.loads(args.audit.read_text(encoding='utf8'))
    base = next(package for package in audited['packages'] if package['mod'] == 'Starwindv3AStarWarsConversion')
    recipe = json.loads(args.owned_recipe.read_text(encoding='utf8'))
    compatibility_hash = recipe['sourceArchiveSha256']
    assert digest(args.archive) == compatibility_hash
    root.mkdir(parents=True)
    managed = root / '_fetcher_launcher'
    source_raw = args.source_root.resolve() / '_fetcher_launcher/packages' / base['definition']['sha256']
    raw = managed / 'packages' / base['definition']['sha256']
    shutil.copytree(source_raw, raw, copy_function=os.link)
    downloads = managed / 'downloads'
    downloads.mkdir()
    (downloads / (base['definition']['sha256'] + '.zip')).hardlink_to(Path(base['archive']))
    marker = b'# optional Starwind fixture personal settings\n'
    (root / 'openmw.cfg').write_bytes(marker)
    game = args.game_data.resolve()
    stock = {name: digest(game / name) for name in ['Morrowind.bsa', 'Tribunal.bsa', 'Bloodmoon.bsa']}
    request_file, result_file = root / 'request.json', root / 'result.json'
    def command(request):
        request_file.write_text(json.dumps(request), encoding='utf8')
        process = subprocess.run([str(args.exe.resolve()), '--install-root', str(root),
            '--request-file', str(request_file), '--log-file', str(result_file)], timeout=1800)
        result = json.loads(result_file.read_text(encoding='utf8'))
        if process.returncode:
            raise RuntimeError(result.get('error', 'Native command failed'))
        return result
    def accept(profile, filename):
        path = root / filename
        path.write_text(json.dumps(profile, indent=2), encoding='utf8')
        review = command({'op': 'reviewProfile', 'path': str(path)})
        return command({'op': 'confirmProfile', 'token': review['profileReview']['token']})['selectedProfile']
    def config(identity):
        return managed / 'profiles' / identity / 'openmw.cfg'
    def roots(identity):
        return [Path(line.split('=', 1)[1].strip('"')) for line in config(identity).read_text(encoding='utf8').splitlines() if line.startswith('data=')]
    content = ['Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm',
               'StarwindRemasteredV1.15.esm', 'StarwindRemasteredPatch.esm']
    plain = {'schemaVersion': 1, 'id': 'plain-starwind', 'name': 'Plain Nexus Starwind isolation test',
             'version': '1', 'host': 'plain-starwind.invalid', 'port': 25565,
             'content': content, 'packages': [base['definition']]}
    command({'op': 'gameData', 'path': str(game)})
    plain_id = accept(plain, 'plain.json')
    print('Preparing plain Nexus Starwind with no Fetcher compatibility package...', flush=True)
    command({'op': 'prepare'})
    plain_config = config(plain_id).read_bytes()
    assert not (managed / 'packages' / compatibility_hash).exists()
    assert not (downloads / (compatibility_hash + '.zip')).exists()
    plain_root = raw / base['definition']['dataDirs'][0]
    assert plain_root in roots(plain_id)
    assert (plain_root / 'Music').is_dir()
    before = digest(plain_root / 'Textures/menu_morrowind.dds')
    experimental = json.loads(json.dumps(plain))
    experimental.update(id='experimental-fetcher-starwind', name='Fetcher experimental server compatibility (opt-in)',
                        host='experimental-starwind.invalid')
    removed = json.loads(args.overrides.read_text(encoding='utf8'))['absent']
    ui = [file['path'] for file in removed if file['package'] == base['definition']['id'] and '/Textures/' in file['path']]
    experimental['packages'][0]['actions'] = [
        {'action': 'remove', 'path': base['definition']['dataDirs'][0] + '/Music'},
        {'action': 'remove', 'paths': ui}]
    experimental['packages'].append({'id': 'fetcher-experimental-starwind-layer',
        'name': 'Fetcher experimental Starwind compatibility 2.2.6 (optional server layer)',
        'url': 'https://github.com/Skooma-Breath/Fetcher-Starwind/releases/download/fetcher-starwind-compat-patch-v2/fetcher-starwind-compat-patch-v2.zip',
        'sha256': compatibility_hash, 'archiveFormat': 'zip',
        'dataDirs': ['payload/Starwind Vanilla Compat', 'payload/Data Files'],
        'actions': [{'action': 'remove', 'path': 'Apply-Fetcher-Starwind-CompatibilityPatch.ps1'}] + recipe['actions']})
    experimental['content'] = content + ['StarwindVanillaCompat.omwscripts']
    experimental_id = accept(experimental, 'experimental.json')
    (downloads / (compatibility_hash + '.zip')).hardlink_to(args.archive.resolve())
    print(f"Preparing the explicitly reviewed experimental layer and {recipe['assetCount']} owned assets...", flush=True)
    command({'op': 'prepare'})
    active = roots(experimental_id)
    base_layout = next(directory for directory in active if (directory / 'StarwindRemasteredV1.15.esm').exists()
                       and not (directory / 'StarwindVanillaCompat.omwscripts').exists())
    assert base_layout != plain_root and not (base_layout / 'Music').exists()
    assert not (base_layout / 'Textures/menu_morrowind.dds').exists()
    layer = next(directory for directory in active if (directory / 'StarwindVanillaCompat.omwscripts').exists())
    count = 0
    for action in recipe['actions']:
        for entry in action['files']:
            relative = entry['patch']['path'].removeprefix('payload/Starwind Vanilla Compat/')
            assert digest(layer / relative) == entry['patch']['outputSha256'], relative
            count += 1
    assert (plain_root / 'Music').is_dir() and digest(plain_root / 'Textures/menu_morrowind.dds') == before
    command({'op': 'profile', 'id': plain_id})
    command({'op': 'prepare'})
    assert config(plain_id).read_bytes() == plain_config
    assert layer not in roots(plain_id) and plain_root in roots(plain_id)
    assert (root / 'openmw.cfg').read_bytes() == marker
    assert {name: digest(game / name) for name in stock} == stock
    report = {'plainProfileInstallsCompatibility': False, 'experimentalAssetsVerified': count,
              'plainRawFilesPreserved': True, 'plainConfigPreservedAfterSwitchBack': True,
              'ownedGameArchivesPreserved': True, 'gameLaunched': False,
              'plainProfile': plain_id, 'experimentalProfile': experimental_id}
    (root / 'optional-starwind-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
