#!/usr/bin/env python3
"""Repack the two installed ModDB races without changing their plugin or asset bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install-root', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    repository = Path(__file__).resolve().parent.parent
    install = args.install_root.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    files = {}
    for mod, prefix, plugin in (
        ('MarioRace', 'Mario', 'Mario.ESP'),
        ('LegendOfZeldaBeta', 'Zelda', 'The Legend of Zelda.ESP'),
    ):
        source = install / 'Data Files/fetcher-simulator/Races' / mod / 'Data Files'
        if source.is_symlink() or not source.resolve().is_relative_to(install):
            raise ValueError('Source leaves the named installation')
        source = source.resolve()
        if not (source / plugin).is_file():
            raise ValueError('Required plugin missing: ' + plugin)
        for path in sorted(source.rglob('*')):
            if path.is_symlink() or not path.resolve().is_relative_to(source):
                raise ValueError('Source links are not supported')
            if not path.is_file():
                continue
            if path.suffix.lower() not in (
                '.esp', '.esm', '.nif', '.dds', '.tga', '.bmp', '.jpg', '.jpeg', '.png',
                '.txt', '.wav', '.mp3', '.ogg',
            ):
                raise ValueError('Unexpected payload: ' + str(path))
            files[prefix + '/Data Files/' + path.relative_to(source).as_posix()] = path.read_bytes()
    files['FetcherLegacyRaces/ATTRIBUTION.txt'] = (
        repository / 'docs/content-attribution/legacy-races.txt').read_text(encoding='utf-8-sig').encode('utf-8')
    records = [{'path': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
               for name, data in sorted(files.items())]
    files['FetcherLegacyRaces/FILES.json'] = (
        json.dumps({'schemaVersion': 1, 'files': records}, indent=2) + '\n').encode()
    archive = output / 'fetcher-legacy-races.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 23, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            bundle.writestr(info, data)
    with zipfile.ZipFile(archive) as bundle:
        if bundle.testzip() is not None:
            raise ValueError('ZIP integrity failure')
        for record in records:
            if hashlib.sha256(bundle.read(record['path'])).hexdigest() != record['sha256']:
                raise ValueError('Bundled file differs from its source')
    package = json.loads((repository / 'launcher/presets/fetcher-legacy-races.json').read_text())
    package['sha256'] = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output / 'package.json').write_text(json.dumps(package, indent=2) + '\n', encoding='utf-8')
    (output / 'build-report.json').write_text(json.dumps({
        'archiveBytes': archive.stat().st_size, 'sha256': package['sha256'],
        'originalFiles': len(records) - 1, 'records': records,
    }, indent=2), encoding='utf-8')
    print(f"Built {archive}: {archive.stat().st_size} bytes; SHA-256 {package['sha256']}")


if __name__ == '__main__':
    main()
