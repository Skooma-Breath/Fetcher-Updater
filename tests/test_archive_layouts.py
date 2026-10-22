"""Real-executable mixed 7z/RAR UMO installation and independent derived-layout caches."""
from pathlib import Path
from tempfile import TemporaryDirectory
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from threading import Thread
import argparse
import base64
import hashlib
import json
import struct
import subprocess


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--exe',type=Path,required=True)
    args=parser.parse_args();exe=args.exe.resolve()
    fixtures=Path(__file__).resolve().parent/'fixtures/archives'
    with TemporaryDirectory(prefix='fetcher-archive-layout-') as temporary:
        base=Path(temporary);root=base/'player';root.mkdir()
        data=base/'owned data';data.mkdir()
        for name in ['Morrowind.esm','Tribunal.esm','Bloodmoon.esm']:
            (data/name).write_bytes(b'TES3'+struct.pack('<III',0,0,0))
        personal=b'# player configuration\n';(root/'openmw.cfg').write_bytes(personal)
        names={'mod.7z':'layout-mod.7z','resource.rar':'test_read_format_rar5_compressed.rar'}
        for name,fixture in names.items(): (base/name).write_bytes((fixtures/fixture).read_bytes())
        def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
        requests=[]
        class Handler(SimpleHTTPRequestHandler):
            def __init__(self,*a,**kw):super().__init__(*a,directory=str(base),**kw)
            def log_message(self,*_):pass
            def do_GET(self): requests.append(self.path);super().do_GET()
        server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
        thread=Thread(target=server.serve_forever,daemon=True);thread.start()
        origin=f'http://127.0.0.1:{server.server_port}'
        operations=0
        def command(q,ok=True,install=root):
            nonlocal operations
            operations+=1
            request=base/'request.json';result=base/'result.json'
            request.write_text(json.dumps(q),encoding='utf8')
            process=subprocess.run([str(exe),'--install-root',str(install),'--allow-local-fixtures',
                '--request-file',str(request),'--log-file',str(result)],timeout=60)
            response=json.loads(result.read_text(encoding='utf8'))
            assert (process.returncode==0)==ok,(q,response)
            return response
        mod={'name':'Layout mod','url':'https://www.nexusmods.com/morrowind/mods/123',
            'plugins':['Variant.esp'],'data_paths':['Instrument/Data Files'],'download_info':[{
                'pinned':True,'direct_download':origin+'/mod.7z','sha256':digest(base/'mod.7z'),
                'extract_to':'Instrument','archiveFormat':'7z','actions':[
                    {'action':'copy','src':'Instrument/Options/Modern','dst':'Instrument/Data Files/textures'},
                    {'action':'rename','src':'Instrument/Data Files/Original.esp','dst':'Instrument/Data Files/Variant.esp'},
                    {'action':'remove','paths':['Instrument/Docs']}]}]}
        resource={'name':'RAR resource','plugins':[],'data_paths':['Samples'],'download_info':[{
            'pinned':True,'direct_download':origin+'/resource.rar','sha256':digest(base/'resource.rar'),
            'extract_to':'Samples','archiveFormat':'rar','actions':[
                {'action':'rename','src':'Samples/test.bin','dst':'Samples/samples/drum.bin'}]}]}
        profile={'schemaVersion':1,'id':'band','name':'Mixed archive community','version':'1',
            'host':'music.example','port':25565,'umoMods':[mod,resource],
            'requiredFiles':['textures/texture.dds','samples/drum.bin']}
        def review(value,name='server.json',install=root):
            (base/name).write_text(json.dumps(value),encoding='utf8')
            state=command({'op':'reviewProfile','url':origin+'/'+name},install=install)
            return command({'op':'confirmProfile','token':state['profileReview']['token']},install=install)
        def config(identity):return root/'_fetcher_launcher/profiles'/identity/'openmw.cfg'
        def data_roots(cfg):
            return [Path(line.split('=',1)[1].strip('"')) for line in cfg.read_text().splitlines() if line.startswith('data=')]
        try:
            command({'op':'gameData','path':str(data)})
            accepted=review(profile);first=accepted['selectedProfile']
            command({'op':'prepare'})
            cfg=config(first);original_config=cfg.read_bytes()
            assert [line.split('=',1)[1] for line in cfg.read_text().splitlines() if line.startswith('content=')] == ['Morrowind.esm','Tribunal.esm','Bloodmoon.esm','Variant.esp']
            roots=data_roots(cfg);modern=next(p for p in roots if (p/'textures/texture.dds').exists())
            sample=next(p for p in roots if (p/'samples/drum.bin').exists())
            assert (modern/'Variant.esp').exists() and not (modern/'Original.esp').exists()
            assert (modern/'textures/texture.dds').read_bytes()==b'modern texture fixture'
            assert not (modern/'Docs').exists()
            expected=next(r for r in json.loads((fixtures/'upstream.json').read_text())['fixtures'] if r['file']==names['resource.rar'])['expectedFiles'][0]
            assert digest(sample/'samples/drum.bin')==expected['sha256']
            raw=root/'_fetcher_launcher/packages'/mod['download_info'][0]['sha256']
            assert (raw/'Data Files/Original.esp').exists() and (raw/'Docs/optional.txt').exists()
            assert not (raw/'Data Files/Variant.esp').exists(), 'Layout changed the shared raw cache'
            # A second owner chooses a different variant from identical source bytes.
            second_profile=json.loads(json.dumps(profile));second_profile['host']='other.example'
            second_profile['umoMods'][0]['download_info'][0]['actions'][0]['src']='Instrument/Options/Classic'
            second=review(second_profile,'other.json')['selectedProfile'];command({'op':'prepare'})
            classic=next(p for p in data_roots(config(second)) if (p/'textures/texture.dds').exists())
            assert classic!=modern and (classic/'textures/texture.dds').read_bytes()==b'classic texture fixture'
            assert (modern/'textures/texture.dds').read_bytes()==b'modern texture fixture'
            count=len(requests);command({'op':'prepare'});assert len(requests)==count
            (classic/'textures/texture.dds').write_bytes(b'X'*(classic/'textures/texture.dds').stat().st_size)
            command({'op':'prepare'});assert (classic/'textures/texture.dds').read_bytes()==b'classic texture fixture'
            assert len(requests)==count, 'Derived repair redownloaded verified source bytes'
            # Failure halfway through a new recipe leaves the previous config and cache valid.
            failing=json.loads(json.dumps(profile));failing['version']='broken'
            failing['umoMods'][0]['download_info'][0]['actions'].append({'action':'remove','path':'Instrument/missing'})
            review(failing);command({'op':'prepare'},ok=False)
            assert cfg.read_bytes()==original_config and (modern/'textures/texture.dds').read_bytes()==b'modern texture fixture'
            command({'op':'rollbackProfile'});command({'op':'prepare'})
            assert cfg.read_bytes()==original_config
            # Bare dataDirs can select a variant without copying or mutating the raw archive.
            selection={'id':'select','name':'Folder selection','version':'1','host':'select.example','port':25565,
                'content':['Morrowind.esm','Tribunal.esm','Bloodmoon.esm'],'requiredFiles':['texture.dds'],
                'packages':[{'id':'variant','url':origin+'/mod.7z','sha256':digest(base/'mod.7z'),'dataDirs':['Options/Classic']}]}
            selected=review(selection,'select.json')['selectedProfile'];command({'op':'prepare'})
            assert any(p==raw/'Options/Classic' for p in data_roots(config(selected)))
            # Compatibility bytes are reconstructed inside an independent derived cache.
            source=b'modern texture fixture';output=b'modern texture corrected'
            patch={'action':'patch','path':'Options/Modern/texture.dds',
                'sourceSha256':hashlib.sha256(source).hexdigest(),'sourceSize':len(source),
                'outputSha256':hashlib.sha256(output).hexdigest(),'outputSize':len(output),
                'operations':[{'copyOffset':0,'copyLength':15},
                    {'data':base64.b64encode(b'corrected').decode()}]}
            patched=json.loads(json.dumps(selection));patched['id']='patched';patched['host']='patched.example'
            patched['packages'][0]['dataDirs']=['Options/Modern'];patched['packages'][0]['actions']=[patch]
            accepted=review(patched,'patched.json');patched_id=accepted['selectedProfile']
            command({'op':'prepare'});patched_cfg=config(patched_id);saved_cfg=patched_cfg.read_bytes()
            target=next(p for p in data_roots(patched_cfg) if (p/'texture.dds').exists())
            assert (target/'texture.dds').read_bytes()==output
            assert (raw/'Options/Modern/texture.dds').read_bytes()==source
            count=len(requests);command({'op':'prepare'});assert len(requests)==count
            (target/'texture.dds').write_bytes(b'locally changed')
            command({'op':'prepare'});assert (target/'texture.dds').read_bytes()==output
            assert len(requests)==count, 'Delta repair redownloaded immutable source'
            for field in ['sourceSha256','outputSha256']:
                invalid=json.loads(json.dumps(patched));invalid['version']=field
                invalid['packages'][0]['actions'][0][field]='0'*64
                review(invalid,'patched.json');command({'op':'prepare'},ok=False)
                assert patched_cfg.read_bytes()==saved_cfg and (target/'texture.dds').read_bytes()==output
                command({'op':'rollbackProfile'})
            # Exact manual Nexus archive import accepts 7z by signature, despite cache suffixes.
            manual_root=base/'manual player';manual_root.mkdir()
            command({'op':'gameData','path':str(data)},install=manual_root)
            manual=json.loads(json.dumps(profile));manual['umoMods']=manual['umoMods'][:1];manual['requiredFiles']=['textures/texture.dds']
            download=manual['umoMods'][0]['download_info'][0];download['direct_download']=None;download['nexus_file_id']=123456
            accepted=review(manual,'nexus.json',install=manual_root)
            command({'op':'prepare'},ok=False,install=manual_root)
            command({'op':'importArchive','id':'umo-0-0','path':str(base/'mod.7z')},install=manual_root)
            count=len(requests);command({'op':'prepare'},install=manual_root);assert len(requests)==count
            assert not (manual_root/'_fetcher_launcher/nexus-key.bin').exists()
            assert (root/'openmw.cfg').read_bytes()==personal
            print(f'PASS native archive/layout integration: {operations} executable operations; mixed 7z/RAR UMO list, exact plugin order, independent recipes, raw-cache preservation, hash-gated byte patches, repair, rollback, folder selection and Nexus manual 7z import')
        finally:
            server.shutdown();server.server_close();thread.join()

if __name__=='__main__':main()
