"""Launch the already installed, unmodified 1.21.11 client in an isolated test directory.
Uses a synthetic identity against the loopback-only fixture, never launcher credentials.
"""
import json
import os
import re
import subprocess
from pathlib import Path

root=Path(__file__).resolve().parents[1]
home=Path(os.environ['APPDATA'])
minecraft=home/'.minecraft'
version=json.loads((minecraft/'versions/1.21.11/1.21.11.json').read_text())
assert version['id']=='1.21.11' and version['mainClass']=='net.minecraft.client.main.Main'
game=root/'.tools/gui-client'
game.mkdir(parents=True,exist_ok=True)
natives=game/'natives'
natives.mkdir(exist_ok=True)
java=home/'ModrinthApp/meta/java_versions/zulu21.44.17-ca-jre21.0.8-win_x64/bin/java.exe'
if not java.is_file(): raise RuntimeError('The installed Java 21 runtime was not found')

def allowed(rules):
    if not rules:return True
    result=False
    for rule in rules:
        host=rule.get('os',{})
        matches=(host.get('name','windows')=='windows' and host.get('arch','amd64') in ('amd64','x86_64') and not rule.get('features'))
        if matches:result=rule['action']=='allow'
    return result

classpath=[]
for library in version['libraries']:
    if not allowed(library.get('rules')):continue
    artifact=library['downloads'].get('artifact')
    if not artifact:continue
    path=minecraft/'libraries'/artifact['path']
    if not path.is_file():raise RuntimeError('Missing installed client library: '+str(path))
    classpath.append(str(path))
classpath.append(str(minecraft/'versions/1.21.11/1.21.11.jar'))
values={'natives_directory':str(natives),'launcher_name':'CUDA-Craft-test','launcher_version':'0.8.0','classpath':os.pathsep.join(classpath)}
args=[str(java),'-Xmx2G','-Xms512M']
for item in version['arguments']['jvm']:
    if isinstance(item,dict):
        if not allowed(item.get('rules')):continue
        item=item['value']
    for arg in item if isinstance(item,list) else [item]:
        args.append(re.sub(r'\$\{([^}]+)\}',lambda m:values[m[1]],arg))
args += [version['mainClass'],'--username','GuiTester','--uuid','000102030405060708090a0b0c0d0e0f',
         '--accessToken','0','--version','1.21.11','--versionType','release',
         '--gameDir',str(game),'--assetsDir',str(minecraft/'assets'),'--assetIndex',version['assetIndex']['id'],
         '--width','1100','--height','700','--quickPlayPath',str(game/'quick-play.json'),
         '--quickPlayMultiplayer','127.0.0.1:25566']
with (game/'console.log').open('w',encoding='utf-8') as log:
    child=subprocess.Popen(args,cwd=game,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
(game/'client.pid').write_text(str(child.pid))
print('Launched isolated Minecraft 1.21.11 test client; PID',child.pid)
