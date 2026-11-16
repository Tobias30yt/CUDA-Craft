"""Isolated Fabric 1.21.11 launcher using the user's existing Minecraft assets.
Local profiles only: no account credentials are read. Use a normal launcher for online play.
"""
import argparse, hashlib, json, os, re, shutil, subprocess, time, urllib.request, uuid, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MC='1.21.11'; LOADER='0.20.1'; API='0.141.6+1.21.11'

def fetch(url):
    with urllib.request.urlopen(url, timeout=60) as response: return response.read()

def prepare(game):
    if not game.resolve().is_relative_to(ROOT.resolve()):raise RuntimeError('Client instance must stay in this workspace')
    cache=ROOT/'.tools/fabric-launcher'; cache.mkdir(parents=True,exist_ok=True)
    profile_path=cache/f'fabric-{MC}-{LOADER}.json'
    if not profile_path.exists(): profile_path.write_bytes(fetch(f'https://meta.fabricmc.net/v2/versions/loader/{MC}/{LOADER}/profile/json'))
    profile=json.loads(profile_path.read_text())
    assert profile['inheritsFrom']==MC and profile['mainClass']=='net.fabricmc.loader.impl.launch.knot.KnotClient'
    libraries=[]
    for library in profile['libraries']:
        group,name,version=library['name'].split(':')
        relative='/'.join([group.replace('.','/'),name,version,f'{name}-{version}.jar'])
        path=cache/'libraries'/relative; url=library['url']+relative
        hash_path=path.with_suffix('.jar.sha1')
        sha=library.get('sha1') or (hash_path.read_text().strip() if hash_path.exists() else fetch(url+'.sha1').decode().strip().split()[0])
        if not re.fullmatch('[0-9a-fA-F]{40}',sha):raise RuntimeError('Invalid Fabric library checksum: '+name)
        if not path.exists() or hashlib.sha1(path.read_bytes()).hexdigest()!=sha:
            data=fetch(url)
            if hashlib.sha1(data).hexdigest()!=sha: raise RuntimeError('Fabric library checksum mismatch: '+name)
            path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        hash_path.write_text(sha)
        libraries.append(str(path))
    mods=game/'mods'; mods.mkdir(parents=True,exist_ok=True)
    mod=ROOT/'fabric-client/build/libs/cuda-craft-client-0.10.0.jar'
    if not mod.exists(): raise RuntimeError('Build the Fabric mod first: tools/build-fabric-client.cmd')
    destination=mods/'cuda-craft-client.jar'
    for old in mods.glob('cuda-craft-client*.jar'):
        if old==destination:continue
        if not old.resolve().is_relative_to(ROOT.resolve()):raise RuntimeError('Mod path points outside this workspace')
        try:
            with zipfile.ZipFile(old) as jar:is_ours=json.loads(jar.read('fabric.mod.json')).get('id')=='cudacraft_client'
        except (KeyError,ValueError,zipfile.BadZipFile):is_ours=False
        if is_ours:
            backup=game/'cudacraft/mod-backups';backup.mkdir(parents=True,exist_ok=True)
            target=backup/(old.stem+'-'+hashlib.sha256(old.read_bytes()).hexdigest()[:12]+'.jar')
            if not target.resolve().is_relative_to(ROOT.resolve()):raise RuntimeError('Backup path points outside this workspace')
            old.replace(target)
    shutil.copy2(mod,destination)
    api_name=f'fabric-api-{API}.jar'; api_path=cache/api_name
    api_hash=api_path.with_suffix('.jar.sha1')
    url=f'https://maven.fabricmc.net/net/fabricmc/fabric-api/fabric-api/{API}/{api_name}'
    sha=api_hash.read_text().strip() if api_hash.exists() else fetch(url+'.sha1').decode().strip().split()[0]
    if not re.fullmatch('[0-9a-fA-F]{40}',sha):raise RuntimeError('Invalid Fabric API checksum')
    if not api_path.exists() or hashlib.sha1(api_path.read_bytes()).hexdigest()!=sha:
        data=fetch(url)
        if hashlib.sha1(data).hexdigest()!=sha: raise RuntimeError('Fabric API checksum mismatch')
        api_path.write_bytes(data)
    api_hash.write_text(sha)
    shutil.copy2(api_path,mods/api_name)
    native=game/'cudacraft/native';native.mkdir(parents=True,exist_ok=True)
    shutil.copy2(ROOT/'build/cuda/cuda-craft.exe',native/'cuda-craft.exe')
    structures=ROOT/'data/structures'
    if structures.is_dir(): shutil.copytree(structures,native/'data/structures',dirs_exist_ok=True)
    return profile,libraries

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--name',default='CudaPlayer');parser.add_argument('--smoke',action='store_true');parser.add_argument('--smoke-containers',action='store_true');parser.add_argument('--smoke-generator',choices=['flat','density'],default='flat');parser.add_argument('--prepare-only',action='store_true')
    args=parser.parse_args()
    if args.smoke_containers and (not args.smoke or args.smoke_generator!='flat'):parser.error('--smoke-containers requires --smoke and the flat generator')
    if not re.fullmatch('[A-Za-z0-9_]{1,16}',args.name):parser.error('Profile name: 1..16 letters, numbers or underscores')
    game=ROOT/('.tools/fabric-smoke' if args.smoke else 'client-instance');game.mkdir(parents=True,exist_ok=True)
    profile,extra=prepare(game)
    if args.prepare_only:print('Prepared Fabric instance:',game);return
    mc=Path(os.environ['APPDATA'])/'.minecraft'; version=json.loads((mc/f'versions/{MC}/{MC}.json').read_text())
    java=Path(os.environ['APPDATA'])/'ModrinthApp/meta/java_versions/zulu21.44.17-ca-jre21.0.8-win_x64/bin/java.exe'
    if not java.is_file():raise RuntimeError('Java 21 runtime not found. Install Java 21 or update the runtime path in this script.')
    def allowed(rules):
        if not rules:return True
        result=False
        for rule in rules:
            host=rule.get('os',{})
            if host.get('name','windows')=='windows' and host.get('arch','amd64') in ('amd64','x86_64') and not rule.get('features'): result=rule['action']=='allow'
        return result
    paths=extra[:]
    for library in version['libraries']:
        artifact=library.get('downloads',{}).get('artifact')
        if artifact and allowed(library.get('rules')):
            path=mc/'libraries'/artifact['path']
            if not path.is_file():raise RuntimeError('Missing Minecraft library: '+str(path))
            paths.append(str(path))
    paths.append(str(mc/f'versions/{MC}/{MC}.jar'))
    natives=game/'natives';natives.mkdir(exist_ok=True)
    values={'natives_directory':str(natives),'launcher_name':'CUDA-Craft','launcher_version':'0.10.0','classpath':os.pathsep.join(paths)}
    command=[str(java),'-Xmx2G','-Xms512M']
    for item in version['arguments']['jvm']:
        if isinstance(item,dict):
            if not allowed(item.get('rules')):continue
            item=item['value']
        for arg in item if isinstance(item,list) else [item]:command.append(re.sub(r'\$\{([^}]+)\}',lambda match:values[match[1]],arg))
    if args.smoke:
        world_id=str(uuid.uuid4());world=game/'cuda-worlds'/world_id;world.mkdir(parents=True)
        (world/'cuda-world.properties').write_text('format=1\nname=Fabric integration test\nseed=20261115\ngenerator='+args.smoke_generator+'\ncreative='+('false' if args.smoke_containers else 'true')+'\n')
        command += ['-Dcudacraft.quickPlayWorld='+world_id,'-Dcudacraft.smokeSeconds='+('30' if args.smoke_containers else '20')]
        if args.smoke_containers:
            blocks={b['name']:b for b in json.loads((ROOT/'.tools/reference/blocks.json').read_text())}
            chest=[[0,0,0] for _ in range(27)];chest[0]=[898,17,0];chest[1]=[36,8,0]
            furnace=[[0,0,0] for _ in range(27)];furnace[0]=[903,1,0];furnace[1]=[896,1,0]
            containers=[{'position':[2,64,2],'type':blocks['chest']['minStateId'],'items':chest}, {'position':[4,64,2],'type':blocks['furnace']['minStateId'],'items':furnace}]
            (world/'blocks.json').write_text(json.dumps({'format':2,'protocol':774,'generator':'cuda-craft-flat-v1','seed':20261115,
                'edits':[[2,64,2,blocks['chest']['defaultState']],[3,64,2,blocks['crafting_table']['defaultState']],[4,64,2,blocks['furnace']['defaultState']]],
                'simulation':{'version':1,'tick':0,'time':6000,'water':[],'devices':[],'players':{},'mobs':[],'containers':containers}}))
            command += ['-Dcudacraft.smokeContainers=true']
        (game/'smoke-world.path').write_text(str(world))
    identity=uuid.UUID(bytes=hashlib.md5(('OfflinePlayer:'+args.name).encode()).digest(),version=3)
    command += [profile['mainClass'],'--username',args.name,'--uuid',identity.hex,'--accessToken','0','--version',MC,'--versionType','release',
        '--gameDir',str(game),'--assetsDir',str(mc/'assets'),'--assetIndex',version['assetIndex']['id'],'--width','1100','--height','700']
    with (game/'console.log').open('w',encoding='utf-8') as log:
        child=subprocess.Popen(command,cwd=game,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
    (game/'client.pid').write_text(str(child.pid))
    print('Launched CUDA Craft Fabric client, local profile',args.name,'PID',child.pid)
    if args.smoke:
        code=child.wait(timeout=150); text=(game/'console.log').read_text(errors='replace')
        if code or 'CUDA Craft smoke: world received' not in text:raise RuntimeError('Fabric integration failed; see '+str(game/'console.log'))
        deadline=time.monotonic()+10
        while 'CUDA Craft stopped.' not in (world/'native-server.log').read_text(errors='replace'):
            if time.monotonic()>deadline:raise RuntimeError('Native backend did not shut down cleanly')
            time.sleep(.1)
        saved=json.loads((world/'blocks.json').read_text())
        if identity.hex not in saved['simulation']['players']:raise RuntimeError('Player state was not saved')
        if args.smoke_containers:
            if 'CUDA Craft smoke: chest and furnace menus verified' not in text:raise RuntimeError('Real container menu verification did not finish')
            if 'CUDA Craft smoke: crafting menu verified' not in text:raise RuntimeError('Real crafting menu verification did not finish')
            inventory=saved['simulation']['players'][identity.hex]['items']
            if sum(s[1] for s in inventory if s[0]==898)!=17 or sum(s[1] for s in inventory if s[0]==904)!=1:raise RuntimeError('Container transfers were not committed by the server')
            if sum(s[1] for s in inventory if s[0]==331)!=1 or any(s[0]==36 for s in inventory):raise RuntimeError('Chest crafting was not committed by the server')
            for c in saved['simulation']['containers']:
                if c['position']==[2,64,2] and any(s[0] for s in c['items']):raise RuntimeError('Chest contents duplicated')
                if c['position']==[4,64,2] and c['items'][2][0]:raise RuntimeError('Furnace output duplicated')
            print('Real vanilla chest/furnace/crafting menus and item transfers verified against the server save.')
        print('Real Fabric client received the native world and shut down cleanly.')

if __name__=='__main__':main()
