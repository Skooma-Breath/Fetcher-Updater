"""Exact-version and credential-boundary checks for the read-only preset authoring audit."""
import hashlib
from contextlib import closing
import json
from pathlib import Path
import runpy
import sqlite3
from tempfile import TemporaryDirectory
from zipfile import ZipFile

api = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/Audit-FetcherUmoPreset.py'))


def main():
    with TemporaryDirectory(prefix='fetcher-audit-') as temporary:
        root = Path(temporary)
        cache = root / '_fetcher_umo/cache'
        archive_dir = cache / 'archive-cache/Stored Name'
        archive_dir.mkdir(parents=True)
        archive = archive_dir / 'Main_1.archive'
        with ZipFile(archive, 'w') as output:
            output.writestr('Data/Mod.esp', b'TES3')
        # A newer filename must never substitute for the installed identity.
        (archive_dir / 'Main_999.archive').write_bytes(b'latest is wrong')
        mod = {'dir': 'CuratedName', 'name': 'Sample', 'author': '<b>Author</b>', 'category': 'Test',
               'url': 'https://www.nexusmods.com/morrowind/mods/123',
               'data_paths': ['Target/Data'], 'plugins': ['Mod.esp'], 'download_info': [
                   {'file_name': 'Main archive', 'extract_to': 'Target', 'pinned': True,
                    'nexus_file_id': 456, 'actions': []}]}
        umo = root / 'list.json'
        umo.write_text(json.dumps([mod]), encoding='utf8')
        record = {'mod_data': mod['download_info'][0], 'generic_data': {
            'version': '1', 'size_in_bytes': archive.stat().st_size,
            'hash': {'type': 'sha256', 'value': hashlib.sha256(archive.read_bytes()).hexdigest()},
            'url': 'https://secret.invalid/?key=DO_NOT_COPY'}}
        rows = {3: {'123': {'files': [{'file_id': 456}]}},
                5: {'Stored Name': {'Main': record}},
                6: {'Stored Name': {'Main': {'nexus_data': {'file_id': 456}}}}}
        def save():
            with closing(sqlite3.connect(cache / 'cache.db')) as db:
                db.execute('CREATE TABLE IF NOT EXISTS cache (id INTEGER PRIMARY KEY,data TEXT)')
                for key, value in rows.items():
                    db.execute('INSERT OR REPLACE INTO cache VALUES (?,?)',
                               (key, json.dumps({'fetcher-simulator': value})))
                db.commit()
        save()
        db_before = (cache / 'cache.db').read_bytes()
        report = api['audit'](umo, root, Path(r'C:\Program Files\7-Zip\7z.exe'))
        assert report['ready'] and len(report['packages']) == 1, report['issues']
        package = report['packages'][0]
        assert Path(package['archive']) == archive
        assert package['definition']['nexus']['fileId'] == 456
        assert package['definition']['dataDirs'] == ['Data']
        assert package['definition']['author'] == 'Author'
        assert 'DO_NOT_COPY' not in json.dumps(report)
        assert (cache / 'cache.db').read_bytes() == db_before, 'Audit modified the source database'
        archive.rename(archive.with_suffix('.saved'))
        missing = api['audit'](umo, root, Path(r'C:\Program Files\7-Zip\7z.exe'))
        assert not missing['ready'] and not missing['packages']
        archive.with_suffix('.saved').rename(archive)
        rows[5]['Stored Name']['Main']['generic_data']['hash']['value'] = '0' * 64
        save()
        rejected = api['audit'](umo, root, Path(r'C:\Program Files\7-Zip\7z.exe'))
        assert not rejected['ready'] and any('SHA-256' in issue['issue'] for issue in rejected['issues'])
        mod['download_info'][0]['direct_download'] = 'https://example.invalid/mod.zip?key=DO_NOT_COPY'
        umo.write_text(json.dumps([mod]), encoding='utf8')
        private = api['audit'](umo, root, Path(r'C:\Program Files\7-Zip\7z.exe'))
        assert 'DO_NOT_COPY' not in json.dumps(private)
        assert any('permanent HTTPS' in issue['issue'] for issue in private['issues'])
        rows[5]['Stored Name']['Main']['generic_data']['hash']['value'] = hashlib.sha256(archive.read_bytes()).hexdigest()
        rows[6]['Stored Name']['Main']['nexus_data']['file_id'] = 789
        del mod['download_info'][0]['direct_download']
        umo.write_text(json.dumps([mod]), encoding='utf8')
        save()
        mismatch = api['audit'](umo, root, Path(r'C:\Program Files\7-Zip\7z.exe'))
        assert any('differs from the list pin' in issue['issue'] for issue in mismatch['issues'])
        assert any('not corroborated' in issue['issue'] for issue in mismatch['issues'])
        print('PASS preset audit: exact archive binding, cache aliases, missing-version refusal, source hash and Nexus pin checks, read-only DB, credential URL redaction')


if __name__ == '__main__':
    main()
