#!/usr/bin/env python3
"""Read-only authoring audit: bind a UMO list to exact installed cache identities.

Reads JSON/SQLite data, never imports or executes UMO code or installer actions.
Does not read API credentials or persist cached download URLs.
"""
import argparse
from contextlib import closing
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sqlite3
import subprocess
from urllib.parse import urlsplit

BLOCKED = {'.exe', '.dll', '.com', '.bat', '.cmd', '.ps1', '.psm1', '.psd1',
           '.msi', '.msp', '.scr', '.cpl', '.lnk', '.url', '.hta', '.vbs', '.vbe',
           '.wsf', '.wsh', '.pif', '.reg', '.py', '.jar'}


def fingerprint(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def relative(value):
    value = value.replace('\\', '/')
    if value == '.':
        return value
    if not value or ':' in value or value.startswith('/') or any(
            part in ('', '.', '..') for part in value.split('/')):
        raise ValueError('Unsafe relative path: ' + value)
    return value


def inventory(executable, path):
    result = subprocess.run([str(executable), 'l', '-slt', '-ba', '-sccUTF-8', str(path)],
                            capture_output=True, encoding='utf8', timeout=180)
    if result.returncode:
        raise ValueError('Archive inventory failed: ' + path.name)
    entries = []
    for block in re.split(r'\r?\n\r?\n', result.stdout.strip()):
        fields = dict(line.split(' = ', 1) for line in block.splitlines() if ' = ' in line)
        if 'Path' not in fields:
            continue
        name = relative(fields['Path'])
        entries.append({'path': name, 'size': int(fields.get('Size', 0)),
                        'directory': fields.get('Folder') == '+' or 'D' in fields.get('Attributes', '')[:1]})
    return entries


def audit(umo_list, install, seven_zip, curation=None):
    curation = curation or {}
    mods = json.loads(umo_list.read_text(encoding='utf-8-sig'))
    requested_mod_count = len(mods)
    omitted = curation.get('omittedMods', {})
    if set(omitted) - {mod['dir'] for mod in mods}:
        raise ValueError('Curation omits a mod absent from the source list')
    mods = [mod for mod in mods if mod['dir'] not in omitted]
    cache = install / '_fetcher_umo/cache'
    with closing(sqlite3.connect((cache / 'cache.db').resolve().as_uri() + '?mode=ro', uri=True)) as db:
        rows = {key: json.loads(value).get('fetcher-simulator', {})
                for key, value in db.execute('SELECT id,data FROM cache WHERE id IN (3,5,6)')}
    installed = {key.casefold(): value for key, value in rows[5].items()}
    synced = {key.casefold(): value for key, value in rows[6].items()}
    report = {'schemaVersion': 1, 'ready': False, 'modCount': len(mods),
              'requestedModCount': requested_mod_count, 'omittedMods': omitted,
              'packages': [], 'issues': [], 'totalArchiveBytes': 0}
    issues = report['issues']
    for mod in mods:
        identity = relative(mod['dir'])
        downloads = mod['download_info']
        for index, download in enumerate(downloads):
            label = identity + '/' + download['file_name']
            matches = [(directory, key, value) for directory, records in installed.items()
                       for key, value in records.items()
                       if value.get('mod_data', {}).get('file_name') == download['file_name']
                       and value.get('mod_data', {}).get('extract_to') == download['extract_to']]
            if len(matches) != 1:
                issues.append({'package': label, 'issue': 'Installed identity is missing or ambiguous'})
                continue
            directory, archive_key, record = matches[0]
            # UMO's cache directory can differ from the curated list's dir. Bind by
            # the complete stored download identity, then verify Nexus mod/file membership.
            cache_directory = next(key for key in rows[5] if key.casefold() == directory)
            generic = record['generic_data']
            version = str(generic.get('version') or '')
            candidates = [cache / 'archive-cache' / cache_directory / (archive_key + '_' + version + '.archive'),
                          cache / 'archive-cache' / cache_directory / (archive_key + '.archive')]
            candidates = list(dict.fromkeys(path for path in candidates if path.is_file()))
            if len(candidates) != 1:
                issues.append({'package': label, 'issue': 'Exact installed archive is missing or ambiguous'})
                continue
            archive = candidates[0]
            file_data = synced.get(directory, {}).get(archive_key, {}).get('nexus_data', {})
            file_id = file_data.get('file_id') or record['mod_data'].get('nexus_file_id')
            pinned_id = download.get('nexus_file_id') if download.get('pinned') else None
            local_issues = []
            if pinned_id and file_id != pinned_id:
                local_issues.append('Installed Nexus file differs from the list pin')
            size = archive.stat().st_size
            if generic.get('size_in_bytes') and size != generic['size_in_bytes']:
                local_issues.append('Archive size differs from installed metadata')
            digest = fingerprint(archive)
            verified = curation.get('verifiedArchives', {}).get(digest, {})
            cached_hash = generic.get('hash', {})
            if cached_hash.get('type') == 'sha256' and cached_hash.get('value', '').lower() != digest and not verified:
                local_issues.append('Archive SHA-256 differs from installed metadata')
            raw = archive.read_bytes()[:8] if size < 8 else None
            if raw is None:
                with archive.open('rb') as stream:
                    raw = stream.read(8)
            form = ('zip' if raw.startswith(b'PK') else '7z' if raw.startswith(b'7z\xbc\xaf\x27\x1c')
                    else 'rar' if raw.startswith(b'Rar!') else 'unknown')
            if form == 'unknown':
                local_issues.append('Unsupported archive format')
            entries = inventory(seven_zip, archive)
            names = {entry['path'].casefold() for entry in entries}
            dirs = set()
            for entry in entries:
                path = PurePosixPath(entry['path'])
                if entry['directory']:
                    dirs.add(str(path).casefold())
                dirs.update(str(parent).casefold() for parent in path.parents)
            prefix = relative(download['extract_to'])
            roots = []
            for target in mod.get('data_paths', []):
                target = relative(target)
                if target == prefix or target.startswith(prefix + '/'):
                    sub = '.' if target == prefix else target[len(prefix) + 1:]
                    if sub == '.' or sub.casefold() in dirs:
                        roots.append(sub)
                    elif len(downloads) == 1:
                        local_issues.append('Declared data directory missing from archive: ' + sub)
            if not roots:
                local_issues.append('No declared data directory maps to this archive')
            blocked = [entry['path'] for entry in entries if not entry['directory']
                       and PurePosixPath(entry['path']).suffix.casefold() in BLOCKED]
            actions = download.get('actions', [])
            if actions or mod.get('actions'):
                local_issues.append('Layout actions need explicit native review')
            package = {'id': 'umo-' + identity.lower() + '-' + str(index),
                       'name': mod['name'], 'author': re.sub('<[^>]+>', '', mod.get('author', '')),
                       'sha256': digest, 'archiveFormat': form, 'dataDirs': roots}
            if len(package['id']) > 64:
                package['id'] = ('umo-' + identity.lower()[:44] + '-' +
                                 hashlib.sha256(identity.encode()).hexdigest()[:8] + '-' + str(index))
            curated = curation.get('packages', {}).get(package['id'], {})
            if curated:
                if set(curated) - {'actions', 'dataDirs'}:
                    raise ValueError('Curation supports only explicit dataDirs/actions')
                package.update(curated)
            removed = {str(action['path']).casefold() for action in package.get('actions', [])
                       if action.get('action') == 'remove' and 'path' in action}
            if any(path.casefold() not in removed for path in blocked):
                local_issues.append('Archive includes programs/scripts without exact reviewed removals')
            direct = download.get('direct_download')
            nexus = re.fullmatch(r'https://(?:www\.)?nexusmods\.com/([a-z0-9-]+)/mods/([0-9]+)/?', mod.get('url', ''))
            if direct:
                url = urlsplit(direct)
                if url.scheme != 'https' or url.username or url.password or url.query or url.fragment:
                    local_issues.append('Direct source requires reviewed permanent HTTPS URL')
                else:
                    package['url'] = direct
            elif nexus and isinstance(file_id, int) and file_id > 0:
                package['nexus'] = {'game': nexus[1], 'modId': int(nexus[2]), 'fileId': file_id}
                if verified and verified.get('nexus') != package['nexus']:
                    raise ValueError('Verified archive digest/source identity mismatch')
                known_files = rows[3].get(nexus[2], {}).get('files', [])
                if not any(file.get('file_id') == file_id for file in known_files) and not verified:
                    local_issues.append('Nexus file identity is not corroborated by cached mod metadata')
            else:
                local_issues.append('Exact public download identity is unavailable')
            for issue in local_issues:
                issues.append({'package': label, 'issue': issue})
            report['packages'].append({'mod': identity, 'download': download['file_name'],
                'archive': str(archive), 'archiveBytes': size, 'installedVersion': version,
                'extractTo': prefix, 'category': mod['category'], 'blockedFiles': blocked,
                'entryCount': len(entries), 'plugins': mod.get('plugins', []),
                'definition': package})
            report['totalArchiveBytes'] += size
    report['ready'] = not issues and len(report['packages']) == sum(len(m['download_info']) for m in mods)
    report['note'] = ('Authoring audit only. Installed-tree compatibility, merged multi-archive precedence, '
                      'client resources and effective server content still require validation. '
                      'This report is not a launchable server manifest.')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--umo-list', type=Path, required=True)
    parser.add_argument('--install-root', type=Path, required=True)
    parser.add_argument('--seven-zip', type=Path, default=Path(r'C:\Program Files\7-Zip\7z.exe'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--curation', type=Path)
    args = parser.parse_args()
    curation = json.loads(args.curation.read_text(encoding='utf8')) if args.curation else None
    report = audit(args.umo_list.resolve(), args.install_root.resolve(), args.seven_zip.resolve(), curation)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(f"Audited {report['modCount']} mods / {len(report['packages'])} exact archives; "
          f"{report['totalArchiveBytes']} bytes; {len(report['issues'])} curation issues. Report: {args.output}")


if __name__ == '__main__':
    main()
