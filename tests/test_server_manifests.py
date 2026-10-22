"""End-to-end server-owner manifests through the real native executable."""
from pathlib import Path
from tempfile import TemporaryDirectory
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from threading import Thread
from zipfile import ZipFile
import argparse
import hashlib
import json
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--bundle', type=Path)
    args = parser.parse_args()
    exe = args.exe.resolve()
    with TemporaryDirectory(prefix='fetcher-server-manifest-') as temp:
        base = Path(temp)
        root = base / 'independent player'; root.mkdir()
        data = base / 'owned data'; data.mkdir()
        for name in ['Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm']:
            (data/name).write_bytes(b'TES3' + struct.pack('<III', 0, 0, 0))
        personal = b'content=personal.esp\n'
        (root/'openmw.cfg').write_bytes(personal)
        archive = base/'ordinary.zip'
        with ZipFile(archive, 'w') as z:
            z.writestr('Data Files/Test.esp', b'TES3' + struct.pack('<III', 0, 0, 0))
            z.writestr('Data Files/Grass.esp', b'TES3' + struct.pack('<III', 0, 0, 0))
            z.writestr('Data Files/Extra.bsa', b'archive fixture')
            z.writestr('Data Files/meshes/test.nif', b'ordinary mod, no Fetcher package manifest')
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        seen = []
        api_headers = []
        archive_headers = []
        automatic = base/'automatic.zip'
        with ZipFile(automatic,'w') as z:
            z.writestr('Data Files/Test.esp', b'TES3' + struct.pack('<III',0,0,0))
            z.writestr('Data Files/Grass.esp', b'TES3' + struct.pack('<III',0,0,0))
            z.writestr('Data Files/automatic.txt', 'automatic Nexus fixture')
        automatic_hash = hashlib.sha256(automatic.read_bytes()).hexdigest()
        class Handler(SimpleHTTPRequestHandler):
            def __init__(self, *a, **kw):
                super().__init__(*a, directory=str(base), **kw)
            def log_message(self, *_): pass
            def do_GET(self):
                seen.append(self.path)
                if self.path.startswith('/v1/'):
                    api_headers.append(dict(self.headers))
                    if '/123458/' in self.path:
                        self.send_response(403); self.end_headers(); return
                    if '/123459/' in self.path:
                        self.send_response(302); self.send_header('Location', '/automatic.zip'); self.end_headers(); return
                    body=json.dumps([{'URI':f'http://127.0.0.1:{self.server.server_port}/automatic.zip'}]).encode()
                    self.send_response(200); self.send_header('Content-Length',str(len(body))); self.end_headers(); self.wfile.write(body)
                    return
                if self.path == '/automatic.zip': archive_headers.append(dict(self.headers))
                if self.path == '/downgrade':
                    self.send_response(302); self.send_header('Location', 'http://example.org/archive.zip'); self.end_headers()
                elif self.path == '/redirect':
                    self.send_response(302); self.send_header('Location', '/ordinary.zip'); self.end_headers()
                else: super().do_GET()
        server = ThreadingHTTPServer(('127.0.0.1',0), Handler)
        worker = Thread(target=server.serve_forever,daemon=True); worker.start()
        origin = f'http://127.0.0.1:{server.server_port}'
        count = 0
        def command(request, ok=True, fixtures=True, nexus_fixture=False):
            nonlocal count
            count += 1
            req=base/'request.json'; result=base/'result.json'
            req.write_text(json.dumps(request),encoding='utf8')
            cmd=[str(exe),'--install-root',str(root),'--request-file',str(req),'--log-file',str(result)]
            if fixtures: cmd += ['--allow-local-fixtures']
            if nexus_fixture: cmd += ['--nexus-fixture-url',origin]
            process=subprocess.run(cmd,timeout=60)
            value=json.loads(result.read_text(encoding='utf8'))
            assert (process.returncode == 0) == ok, (request.get('op'),value)
            return value
        manifest = {'schemaVersion':1,'id':'community','name':'Independent community','version':'1.0',
            'host':'community.example','port':25565,'content':['Morrowind.esm','Tribunal.esm','Bloodmoon.esm','Test.esp'],
            'packages':[{'id':'mod','url':origin+'/ordinary.zip','sha256':digest,'dataDirs':['Data Files']}]}
        def publish(value, name='owner.json'):
            (base/name).write_text(json.dumps(value),encoding='utf8')
            return origin+'/'+name
        def accept(url):
            value=command({'op':'reviewProfile','url':url})
            return command({'op':'confirmProfile','token':value['profileReview']['token']})
        try:
            command({'op':'gameData','path':str(data)})
            url=publish(manifest)
            command({'op':'reviewProfile','url':url},ok=False,fixtures=False)
            assert not seen, 'Production mode contacted a local service'
            reviewed=command({'op':'reviewProfile','url':url})
            assert reviewed['selectedProfile']=='installed' and '/ordinary.zip' not in seen
            token=reviewed['profileReview']['token']
            command({'op':'confirmProfile','token':'stale'},ok=False)
            accepted=command({'op':'confirmProfile','token':token})
            identity=accepted['selectedProfile']
            assert identity.startswith('remote-') and accepted['host']=='community.example'
            command({'op':'confirmProfile','token':token},ok=False)
            command({'op':'prepare'})
            config=root/'_fetcher_launcher/profiles'/identity/'openmw.cfg'
            good=config.read_bytes()
            assert b'content=Test.esp' in good
            assert (root/'openmw.cfg').read_bytes()==personal
            requests=len(seen); command({'op':'prepare'}); assert len(seen)==requests
            # Separate owners choosing the same friendly ID cannot overwrite one another.
            other=json.loads(json.dumps(manifest)); other['host']='second.example'
            second=accept(publish(other,'other-owner.json'))['selectedProfile']
            assert second != identity
            assert len([p for p in command({'op':'init'})['profiles'] if p['id'] in (identity,second)])==2
            # A version update remains only a preview until accepted; rollback restores the old plan.
            manifest['version']='2.0'; manifest['content'].append('Missing.esp')
            preview=command({'op':'reviewProfile','url':publish(manifest)})
            assert next(p for p in preview['profiles'] if p['id']==identity)['version']=='1.0'
            command({'op':'confirmProfile','token':preview['profileReview']['token']})
            command({'op':'prepare'},ok=False); assert config.read_bytes()==good
            rolled=command({'op':'rollbackProfile'}); assert next(p for p in rolled['profiles'] if p['id']==identity)['version']=='1.0'
            command({'op':'prepare'}); assert config.read_bytes()==good
            manifest['content'].remove('Missing.esp')
            # Changed review invalidates an earlier acceptance token.
            old=command({'op':'reviewProfile','url':publish(manifest)})['profileReview']['token']
            manifest['version']='2.1'
            new=command({'op':'reviewProfile','url':publish(manifest)})['profileReview']['token']
            assert old != new
            command({'op':'confirmProfile','token':old},ok=False)
            command({'op':'dismissProfile'})
            # Import real UMO fields without running actions or requiring custom archive metadata.
            umo={'name':'UMO example','url':'https://www.nexusmods.com/morrowind/mods/123',
                 'data_paths':['Example/Data Files'],'plugins':['Test.esp'],'groundcover':['Grass.esp'],
                 'download_info':[{'pinned':True,'extract_to':'Example','direct_download':origin+'/ordinary.zip',
                                  'sha256':digest,'actions':[]}]}
            wrapped={'id':'umo-community','name':'UMO community','version':'1','host':'umo.example','port':25566,'umoMods':[umo]}
            reviewed=command({'op':'reviewProfile','url':publish(wrapped,'umo.json')})
            converted=reviewed['profileReview']['profile']
            assert converted['packages'][0]['dataDirs']==['Data Files'] and converted['content'][-1]=='Test.esp'
            assert converted['groundcover']==['Grass.esp']
            command({'op':'confirmProfile','token':reviewed['profileReview']['token']}); command({'op':'prepare'})
            for broken in ['action','unpinned','hash','layout']:
                bad=json.loads(json.dumps(wrapped)); d=bad['umoMods'][0]['download_info'][0]
                if broken=='action': d['actions']=[{'action':'run','command':'unsafe'}]
                if broken=='unpinned': d['pinned']=False
                if broken=='hash': del d['sha256']
                if broken=='layout': bad['umoMods'][0]['data_paths']=['../escape']
                command({'op':'reviewProfile','url':publish(bad,'bad.json')},ok=False)
            # Pinned external UMO exports use the same normalizer.
            list_text=json.dumps([umo]); (base/'list.json').write_text(list_text,encoding='utf8')
            reference={k:v for k,v in wrapped.items() if k!='umoMods'}
            reference['umo']={'url':origin+'/list.json','sha256':hashlib.sha256(list_text.encode()).hexdigest()}
            accept(publish(reference,'reference.json')); command({'op':'prepare'})
            reference['umo']['sha256']='0'*64
            command({'op':'reviewProfile','url':publish(reference,'reference.json')},ok=False)
            # Nexus manual fallback uses the exact hash and needs no credentials or API request.
            umo['download_info'][0]['direct_download']=None
            umo['download_info'][0]['nexus_file_id']=123456
            reviewed=command({'op':'reviewProfile','url':publish(wrapped,'nexus.json')})
            package=reviewed['profileReview']['profile']['packages'][0]
            assert package['nexus']=={'game':'morrowind','modId':123,'fileId':123456}
            command({'op':'confirmProfile','token':reviewed['profileReview']['token']})
            command({'op':'importArchive','id':package['id'],'path':str(base/'owner.json')},ok=False)
            command({'op':'importArchive','id':package['id'],'path':str(archive)})
            command({'op':'prepare'})
            fake='fixture-only-not-a-real-api-key'
            assert command({'op':'nexusKey','key':fake})['nexusKeySaved']
            encrypted=(root/'_fetcher_launcher/nexus-key.bin').read_bytes()
            assert fake.encode() not in encrypted and fake not in json.dumps(command({'op':'init'}))
            # Eligible Nexus route resolves an API link and sends credentials only to the API.
            umo['download_info'][0].update(nexus_file_id=123457,sha256=automatic_hash)
            accept(publish(wrapped,'nexus-auto.json'))
            command({'op':'prepare'},nexus_fixture=True)
            assert api_headers[-1].get('apikey')==fake
            assert api_headers[-1].get('Application-Name')=='Fetcher Launcher'
            assert archive_headers and all('apikey' not in headers for headers in archive_headers)
            for file_id in [123458,123459]:
                umo['download_info'][0].update(nexus_file_id=file_id,sha256=str(file_id%10)*64)
                accept(publish(wrapped,f'nexus-{file_id}.json'))
                response=command({'op':'prepare'},ok=False,nexus_fixture=True)
                assert fake not in json.dumps(response)
                assert 'manual' in response['error'].lower()
            assert not command({'op':'nexusKey','key':''})['nexusKeySaved']
            # Refuse desktop executables and bad redirects before activating their content.
            evil=base/'evil.zip'
            with ZipFile(evil,'w') as z: z.writestr('Data Files/installer.exe',b'never execute')
            bad=json.loads(json.dumps(manifest)); bad['content']=manifest['content'][:-1]
            bad['packages'][0].update(url=origin+'/evil.zip',sha256=hashlib.sha256(evil.read_bytes()).hexdigest())
            accepted=accept(publish(bad,'evil.json'))
            command({'op':'prepare'},ok=False)
            assert not (root/'_fetcher_launcher/profiles'/accepted['selectedProfile']/'openmw.cfg').exists()
            redirect=json.loads(json.dumps(manifest)); redirect['packages'][0]['url']=origin+'/downgrade'
            # Use a different expected digest so verified cached downloads cannot mask a network request.
            redirect['packages'][0]['sha256']='1'*64
            accept(publish(redirect,'redirect.json')); command({'op':'prepare'},ok=False)
            assert seen[-1]=='/downgrade'
            if args.bundle:
                bundle=args.bundle.resolve()
                with ZipFile(bundle) as z: bundle_manifest=json.loads(z.read('openmw-client-package.json').decode('utf-8-sig'))
                target=base/'combined.zip'; target.write_bytes(bundle.read_bytes())
                candidate={'id':'fetcher-vanilla-candidate','name':'Vanilla combined candidate','version':'1',
                    'host':'163.192.106.225','port':25569,'content':bundle_manifest['content'],
                    'requiredFiles':['FetcherStudio/ATTRIBUTION.txt','FetcherVehicles/CREDITS.txt','meshes/fetcher/synth/keyboard-stand.nif'],
                    'packages':[{'id':'combined','url':origin+'/combined.zip','sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'dataDirs':['Data Files']}]}
                accepted=accept(publish(candidate,'vanilla.json')); command({'op':'prepare'})
                config=root/'_fetcher_launcher/profiles'/accepted['selectedProfile']/'openmw.cfg'
                assert 'content=FetcherVehicles.omwaddon' in config.read_text()
                print('PASS combined Vanilla candidate: manifest review/accept, real archive download, exact hash, assets and isolated plugin order')
            assert (root/'openmw.cfg').read_bytes()==personal
            print(f'PASS server manifests: {count} real executable operations; review isolation, distinct owners, version rollback, UMO, Nexus automatic/manual routes, API credential isolation, protected keys and hostile inputs')
        finally:
            server.shutdown(); server.server_close(); worker.join()

if __name__=='__main__': main()
