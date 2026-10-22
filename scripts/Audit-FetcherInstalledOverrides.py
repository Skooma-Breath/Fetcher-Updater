#!/usr/bin/env python3
"""Compare authoritative installed mod bytes with verified native source inventories.

Produces an authoring report only. No compatibility recipe is inferred/applied,
no original files are changed and no third-party payload bytes enter the report.
"""
import argparse
import hashlib
import json
from pathlib import Path


def fingerprint(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def audit_sources(audit, cache_root, install):
    expected = {}
    roots = {}
    missing = []
    for package in audit['packages']:
        definition = package['definition']
        raw = cache_root / '_fetcher_launcher/packages' / definition['sha256']
        receipt_path = raw / '.fetcher-package.json'
        if not receipt_path.is_file():
            missing.append(package['mod'] + '/' + package['download'])
            continue
        receipt = json.loads(receipt_path.read_text(encoding='utf8'))
        if receipt.get('sha256') != definition['sha256']:
            raise ValueError('Raw source receipt has a different archive identity')
        installed_root = install / 'Data Files/fetcher-simulator' / package['category'] / package['extractTo']
        for directory in definition['dataDirs']:
            path = installed_root if directory == '.' else installed_root / directory
            roots[str(path).casefold()] = path
        for record in receipt['files']:
            path = record['path']
            if not any(directory == '.' or path.casefold().startswith(directory.casefold() + '/')
                       for directory in definition['dataDirs']):
                continue
            target = installed_root / path
            expected[str(target).casefold()] = (target, package, record, raw / path)
    if missing:
        raise ValueError('Prepare all available sources first; missing inventories: ' + ', '.join(missing))
    changed, absent, added = [], [], []
    for target, package, record, raw in expected.values():
        if not target.is_file():
            absent.append({'package': package['definition']['id'], 'path': record['path']})
            continue
        actual = fingerprint(target)
        if actual != record['sha256']:
            if fingerprint(raw) != record['sha256']:
                raise ValueError('Source cache file changed: ' + record['path'])
            changed.append({'package': package['definition']['id'], 'path': record['path'],
                            'sourceSize': record['size'], 'sourceSha256': record['sha256'],
                            'outputSize': target.stat().st_size, 'outputSha256': actual,
                            'installedPath': str(target)})
    seen = set()
    for root in roots.values():
        if not root.is_dir():
            continue
        for path in root.rglob('*'):
            key = str(path).casefold()
            if path.is_file() and key not in expected and key not in seen:
                seen.add(key)
                added.append({'installedPath': str(path), 'size': path.stat().st_size,
                              'sha256': fingerprint(path)})
    return {'schemaVersion': 1, 'sourcePackageCount': len(audit['packages']),
            'vfsSourceFileCount': len(expected), 'changed': changed, 'absent': absent, 'added': added,
            'note': 'Review these differences before authoring compatibility deltas. This report does not modify/install files.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit', type=Path, required=True)
    parser.add_argument('--cache-root', type=Path, required=True)
    parser.add_argument('--install-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = json.loads(args.audit.read_text(encoding='utf8'))
    report = audit_sources(source, args.cache_root.resolve(), args.install_root.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(f"Compared {report['vfsSourceFileCount']} source files; "
          f"changed={len(report['changed'])}, absent={len(report['absent'])}, added={len(report['added'])}. "
          f"Report: {args.output}")


if __name__ == '__main__':
    main()
