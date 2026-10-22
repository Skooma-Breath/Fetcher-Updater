"""Opt-in public HTTPS test: download/prepare one manifest in a new disposable root.
Never launches a game, updates native binaries, or changes server configuration.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--game-data',type=Path,required=True)
    parser.add_argument('--url',required=True)
    args=parser.parse_args()
    root=args.root.resolve(); exe=args.exe.resolve()
    if root.exists(): raise SystemExit('Use a new directory so existing files/cache cannot mask this test.')
    if not args.url.startswith('https://'): raise SystemExit('This test requires a public HTTPS manifest.')
    root.mkdir(parents=True)
    personal=b'# untouched personal configuration marker\n'
    (root/'openmw.cfg').write_bytes(personal)
    def command(request):
        path=root/'request.json'; result=root/'result.json'
        path.write_text(json.dumps(request),encoding='utf8')
        process=subprocess.run([str(exe),'--install-root',str(root),'--request-file',str(path),'--log-file',str(result)],timeout=180)
        value=json.loads(result.read_text(encoding='utf8'))
        assert process.returncode==0,value
        return value
    command({'op':'gameData','path':str(args.game_data.resolve())})
    reviewed=command({'op':'reviewProfile','url':args.url})
    assert reviewed['selectedProfile']=='installed'
    assert not (root/'_fetcher_launcher/downloads').exists(), 'Review downloaded an archive'
    plan=reviewed['profileReview']['profile']
    accepted=command({'op':'confirmProfile','token':reviewed['profileReview']['token']})
    command({'op':'prepare'})
    managed=root/'_fetcher_launcher'
    config=managed/'profiles'/accepted['selectedProfile']/'openmw.cfg'
    plugins=[line.removeprefix('content=') for line in config.read_text().splitlines() if line.startswith('content=')]
    assert plugins==plan['content']
    archives=[]
    for package in plan['packages']:
        digest=package['sha256']
        archive=managed/'downloads'/(digest+'.zip')
        assert hashlib.sha256(archive.read_bytes()).hexdigest()==digest
        cache=managed/'packages'/digest
        receipt=json.loads((cache/'.fetcher-package.json').read_text())
        assert receipt['sha256']==digest
        for name in plan.get('requiredFiles',[]):
            assert any((cache/path/name).is_file() for path in package['dataDirs'])
        archives.append({'id':package['id'],'sha256':digest,'bytes':archive.stat().st_size,'files':len(receipt['files'])})
    assert (root/'openmw.cfg').read_bytes()==personal
    report={'manifestUrl':args.url,'selectedProfile':accepted['selectedProfile'],'version':plan['version'],
        'reviewDigest':reviewed['profileReview']['token'],'archives':archives,'content':plugins,
        'generatedConfig':str(config),'personalConfigurationPreserved':True,'gameLaunched':False}
    (root/'live-test-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print('PASS public HTTPS manifest import and fresh native archive download/preparation')
    print(json.dumps(report,indent=2))

if __name__=='__main__': main()
