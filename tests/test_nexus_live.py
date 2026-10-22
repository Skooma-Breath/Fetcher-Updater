"""Opt-in live Nexus/native download acceptance. No credential values enter requests/log files.

Uses an explicitly selected local UMO config only to seed a test-owned DPAPI key file.
Account metadata is reduced to eligibility booleans; API redirects are prohibited.
"""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import subprocess
import urllib.request
import urllib.error


def protect(text):
    class Blob(ctypes.Structure):
        _fields_ = [('size', wintypes.DWORD), ('data', ctypes.POINTER(ctypes.c_ubyte))]
    buffer = ctypes.create_string_buffer(text.encode('utf-8'))
    source = Blob(len(text.encode('utf-8')), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_ubyte)))
    output = Blob()
    crypt = ctypes.WinDLL('crypt32', use_last_error=True)
    crypt.CryptProtectData.argtypes = [ctypes.POINTER(Blob), wintypes.LPCWSTR, ctypes.c_void_p,
                                      ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Blob)]
    crypt.CryptProtectData.restype = wintypes.BOOL
    if not crypt.CryptProtectData(ctypes.byref(source), 'Fetcher Nexus access', None, None, None, 1, ctypes.byref(output)):
        raise RuntimeError('Could not protect Nexus key for this Windows user')
    try:
        return ctypes.string_at(output.data, output.size)
    finally:
        ctypes.WinDLL('kernel32').LocalFree(ctypes.cast(output.data, ctypes.c_void_p))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--game-data', type=Path, required=True)
    parser.add_argument('--umo-config', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--profile', type=Path, default=Path(__file__).resolve().parents[1] / 'launcher/examples/fetcher-vanilla-hackle-lo.json')
    args = parser.parse_args()
    root = args.root.resolve()
    assert not root.exists(), 'Choose a fresh test installation; no existing installation is overwritten'
    profile = json.loads(args.profile.read_text(encoding='utf-8-sig'))
    definition = profile['packages'][0]
    assert hashlib.sha256(args.archive.read_bytes()).hexdigest() == definition['sha256']
    key = json.loads(args.umo_config.read_text(encoding='utf-8-sig'))['NEXUS_API_KEY']
    assert isinstance(key, str) and key

    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *args, **kwargs):
            return None

    opener = urllib.request.build_opener(NoRedirect())
    headers = {'apikey': key, 'Application-Name': 'Fetcher Launcher', 'Application-Version': '0.5.0',
               'User-Agent': 'FetcherLauncher/0.5', 'Accept': 'application/json'}

    def api(path):
        request = urllib.request.Request('https://api.nexusmods.com' + path, headers=headers)
        with opener.open(request, timeout=30) as response:
            return json.load(response)

    account = api('/v1/users/validate.json')
    n = definition['nexus']
    metadata = api(f"/v1/games/{n['game']}/mods/{n['modId']}/files/{n['fileId']}.json")
    assert metadata['file_id'] == n['fileId']
    root.mkdir(parents=True)
    managed = root / '_fetcher_launcher'; managed.mkdir()
    protected = protect(key)
    assert key.encode() not in protected
    (managed / 'nexus-key.bin').write_bytes(protected)
    # Credentials remain in memory or Windows-protected storage only.
    marker = b'# personal configuration remains unchanged\n'
    (root / 'openmw.cfg').write_bytes(marker)
    operations = 0

    def command(value, allow_failure=False):
        nonlocal operations
        operations += 1
        request, result = root / 'request.json', root / 'result.json'
        request.write_text(json.dumps(value), encoding='utf-8')
        process = subprocess.run([str(args.exe.resolve()), '--install-root', str(root), '--request-file', str(request),
                                  '--log-file', str(result)], timeout=90)
        response = json.loads(result.read_text(encoding='utf-8-sig'))
        assert key not in json.dumps(response) and key not in result.read_text(encoding='utf-8-sig')
        if not allow_failure:
            assert process.returncode == 0, {'operation': value['op'], 'error': response.get('error')}
        return process.returncode == 0, response

    command({'op': 'gameData', 'path': str(args.game_data.resolve())})
    _, reviewed = command({'op': 'reviewProfile', 'path': str(args.profile.resolve())})
    _, accepted = command({'op': 'confirmProfile', 'token': reviewed['profileReview']['token']})
    automatic, response = command({'op': 'prepare'}, allow_failure=True)
    report = {'nexus': n, 'fileName': metadata.get('file_name'), 'fileVersion': metadata.get('version'),
              'fileCategory': metadata.get('category_name'), 'archiveSha256': definition['sha256'],
              'accountValid': True, 'premium': bool(account.get('is_premium')), 'automaticDownload': automatic}
    if not automatic:
        message = response.get('error', '')
        assert 'manual' in message.lower() or 'import' in message.lower(), 'Missing manual route on Nexus API refusal'
        report['automaticFailure'] = message
        command({'op': 'importArchive', 'id': definition['id'], 'path': str(args.archive.resolve())})
        command({'op': 'prepare'})
    config = managed / 'profiles' / accepted['selectedProfile'] / 'openmw.cfg'
    content = [line.split('=', 1)[1] for line in config.read_text(encoding='utf-8').splitlines() if line.startswith('content=')]
    assert content == profile['content']
    extracted = managed / 'packages' / definition['sha256']
    for name in profile['requiredFiles']:
        assert (extracted / name).is_file(), name
    assert (root / 'openmw.cfg').read_bytes() == marker
    assert not list(managed.glob('downloads/*.part'))
    command({'op': 'nexusKey', 'key': ''})
    assert not (managed / 'nexus-key.bin').exists()
    report.update({'manualCachedImport': not automatic, 'generatedContent': content, 'operations': operations,
                   'personalConfigurationPreserved': True, 'gameLaunched': False, 'serverChanged': False,
                   'credentialValuesLogged': False, 'testCredentialRemoved': True})
    (root / 'nexus-live-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS live Nexus acceptance: ' + json.dumps(report))


if __name__ == '__main__':
    main()
