#!/usr/bin/env python3
"""Port Fetcher-owned legacy byte deltas into bounded native per-package recipes.

Verifies original cached source and authoritative installed output. Engine-resource
fixes remain outside mod manifests. Does not run compatibility scripts or modify
either installation. A failing record is reported, never guessed or force-applied.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
from zipfile import ZipFile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def bardcraft_records(archive, expected_sha256):
    if digest(archive.read_bytes()) != expected_sha256:
        raise ValueError('Bardcraft patch archive differs from its pinned SHA-256')
    with ZipFile(archive) as zipped:
        info = zipped.getinfo('fetcher-bardcraft-mp-patch.json')
        if info.file_size > 8 * 1024 * 1024:
            raise ValueError('Bardcraft patch metadata exceeds limit')
        patch = json.loads(zipped.read(info).decode('utf-8-sig'))
    if patch['formatVersion'] != 2 or patch['targetSubdirectory'] != 'scripts/Bardcraft':
        raise ValueError('Unsupported Bardcraft compatibility format')
    prefix = 'Data Files/fetcher-simulator/Gameplay/BardcraftOpenMW/'
    records = []
    for entry in patch['files']:
        if entry.get('targetBase') not in (None, 'data'):
            raise ValueError('Unsupported Bardcraft target base')
        relative = entry['path']
        if not relative or '\\' in relative or ':' in relative or any(part in ('', '.', '..') for part in relative.split('/')):
            raise ValueError('Unsafe Bardcraft compatibility path')
        record = {key: entry[key] for key in ['outputSha256', 'outputSize', 'operations']}
        record['path'] = prefix + (relative if entry.get('targetBase') == 'data' else 'scripts/Bardcraft/' + relative)
        record['sourceSha256'] = entry.get('sourceSha256') or digest(b'')
        record['sourceSize'] = entry.get('sourceSize') or 0
        if entry.get('sourceSha256') is None:
            record['createIfMissing'] = True
            record['requiresPath'] = prefix + 'Bardcraft.omwscripts'
        records.append(record)
    return records


def build(audit, cache_root, installed, legacy):
    packages = []
    for entry in audit['packages']:
        raw = cache_root / '_fetcher_launcher/packages' / entry['definition']['sha256']
        receipt = raw / '.fetcher-package.json'
        if not receipt.is_file():
            raise ValueError('Prepare source inventories before authoring compatibility: ' + entry['mod'])
        files = json.loads(receipt.read_text(encoding='utf8'))['files']
        prefix = 'Data Files/fetcher-simulator/' + entry['category'] + '/' + entry['extractTo'] + '/'
        packages.append((entry, raw, files, prefix))
    actions, failures, excluded, planned, seen = {}, [], [], set(), set()
    for record in legacy['files']:
        target = record['path'].replace('\\', '/')
        if target.casefold() in seen:
            raise ValueError('Duplicate compatibility target: ' + target)
        seen.add(target.casefold())
        if not target.startswith('Data Files/fetcher-simulator/'):
            excluded.append({'path': target, 'reason': 'Engine/client resource belongs to the client release, not a mod profile'})
            continue
        candidates = []
        for entry, raw, files, prefix in packages:
            if not target.casefold().startswith(prefix.casefold()):
                continue
            relative = target[len(prefix):]
            if record['sourceSize'] == 0:
                if record.get('createIfMissing'):
                    candidates.append((entry, raw, relative, None, prefix))
                continue
            # A renamed Clean plugin can derive from the exact verified original.
            matches = [file for file in files if file['sha256'] == record['sourceSha256']
                       and file['size'] == record['sourceSize']]
            exact = [file for file in matches if file['path'].casefold() == relative.casefold()]
            for source in exact or matches:
                candidates.append((entry, raw, relative, source['path'], prefix))
        if len(candidates) != 1:
            failures.append({'path': target, 'reason': 'Known source fingerprint is missing or ambiguous'})
            continue
        entry, raw, relative, source_path, prefix = candidates[0]
        source = b'' if source_path is None else (raw / source_path).read_bytes()
        if len(source) != record['sourceSize'] or digest(source) != record['sourceSha256']:
            failures.append({'path': target, 'reason': 'Original source bytes changed'})
            continue
        output = b''.join(base64.b64decode(operation['data'], validate=True) if 'data' in operation
                          else source[operation['copyOffset']:operation['copyOffset'] + operation['copyLength']]
                          for operation in record['operations'])
        if len(output) != record['outputSize'] or digest(output) != record['outputSha256']:
            failures.append({'path': target, 'reason': 'Legacy recipe failed output verification'})
            continue
        destination = installed / target
        if not destination.is_file() or destination.read_bytes() != output:
            failures.append({'path': target, 'reason': 'Recipe output differs from the current authoritative installation'})
            continue
        prerequisite = record.get('requiresPath')
        if prerequisite:
            if not prerequisite.startswith(prefix) or not (
                    (raw / prerequisite[len(prefix):]).is_file() or prerequisite.casefold() in planned):
                failures.append({'path': target, 'reason': 'Creation prerequisite is absent from this package recipe'})
                continue
        recipe = actions.setdefault(entry['definition']['id'], [])
        if source_path and source_path.casefold() != relative.casefold():
            recipe.append({'action': 'copy', 'src': source_path, 'dst': relative})
        patch = {key: record[key] for key in ['sourceSha256', 'sourceSize', 'outputSha256', 'outputSize', 'operations']}
        patch.update(action='patch', path=relative)
        if source_path is None:
            patch['create'] = True
        recipe.append(patch)
        planned.add(target.casefold())
    return {'schemaVersion': 1, 'patchVersion': legacy['patchVersion'], 'packages': actions,
            'sourceArchives': {entry['definition']['id']: entry['definition']['sha256']
                               for entry in audit['packages'] if entry['definition']['id'] in actions},
            'portedPatchCount': sum(action['action'] == 'patch' for values in actions.values() for action in values),
            'failures': failures, 'excludedClientResources': excluded,
            'note': 'Native action fragment, not a complete profile. Add these after each package\'s existing curated actions.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit', type=Path, required=True)
    parser.add_argument('--cache-root', type=Path, required=True)
    parser.add_argument('--installed-root', type=Path, required=True)
    parser.add_argument('--legacy-patches', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--bardcraft-archive', type=Path)
    parser.add_argument('--bardcraft-sha256')
    parser.add_argument('--additional-patches', type=Path, action='append', default=[])
    args = parser.parse_args()
    legacy = json.loads(args.legacy_patches.read_text(encoding='utf-8-sig'))
    for path in args.additional_patches:
        additional = json.loads(path.read_text(encoding='utf-8-sig'))
        if additional['formatVersion'] != 1:
            raise ValueError('Unsupported additional patch format')
        legacy['files'].extend(additional['files'])
    if args.bardcraft_archive or args.bardcraft_sha256:
        if not args.bardcraft_archive or not args.bardcraft_sha256:
            raise ValueError('Bardcraft archive and pinned SHA-256 are both required')
        legacy['files'].extend(bardcraft_records(args.bardcraft_archive, args.bardcraft_sha256))
    report = build(json.loads(args.audit.read_text(encoding='utf8')), args.cache_root.resolve(),
                   args.installed_root.resolve(), legacy)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(f"Ported {report['portedPatchCount']} hash-gated fixes; failures={len(report['failures'])}; "
          f"client resources excluded={len(report['excludedClientResources'])}. Report: {args.output}")


if __name__ == '__main__':
    main()
