"""Opt-in official 1.21.11 reference capture. Never used by the native server.
Run with --java PATH; requires SHA1-verified .tools/vanilla-1.21.11/server.jar.
Reference binaries/worlds stay ignored. Only sampled biome facts are exported.
"""
import argparse
import gzip
import hashlib
import io
import json
import math
import queue
import struct
import subprocess
import threading
import time
import zlib
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SHA1='64bb6d763bed0a9f1d632ec347938594144943ed'

class NBT:
    def __init__(self,data):self.f=io.BytesIO(data)
    def read(self,n):
        value=self.f.read(n)
        if len(value)!=n:raise ValueError('Truncated NBT')
        return value
    def number(self,fmt):return struct.unpack('>'+fmt,self.read(struct.calcsize(fmt)))[0]
    def string(self):return self.read(self.number('H')).decode('utf-8')
    def payload(self,tag,depth=0):
        if depth>64:raise ValueError('NBT depth exceeded')
        if tag in range(1,7):return self.number({1:'b',2:'h',3:'i',4:'q',5:'f',6:'d'}[tag])
        if tag==8:return self.string()
        if tag==10:
            out={}
            while True:
                t=self.number('B')
                if t==0:return out
                name=self.string();out[name]=self.payload(t,depth+1)
        if tag==9:
            kind=self.number('B');length=self.number('i')
            if not 0<=length<=1000000:raise ValueError('Bad list length')
            return [self.payload(kind,depth+1) for _ in range(length)]
        if tag in (7,11,12):
            length=self.number('i')
            if not 0<=length<=1000000:raise ValueError('Bad array length')
            return self.read(length) if tag==7 else [self.number('i' if tag==11 else 'q') for _ in range(length)]
        raise ValueError('Unknown NBT tag '+str(tag))
    def root(self):
        tag=self.number('B');self.string();return self.payload(tag)

def chunk_from_region(folder,cx,cz,storage='region'):
    path=folder/storage/f'r.{cx//32}.{cz//32}.mca'
    if not path.exists():return None
    with path.open('rb') as stream:
        stream.seek(4*((cx&31)+(cz&31)*32));entry=stream.read(4)
        if len(entry)!=4:return None
        location=int.from_bytes(entry,'big')
        if not location:return None
        stream.seek((location>>8)*4096)
        length=struct.unpack('>I',stream.read(4))[0]
        if not 1<=length<=16*1024*1024:raise ValueError('Bad region chunk size')
        compression=stream.read(1)[0];raw=stream.read(length-1)
    decoded=zlib.decompress(raw) if compression==2 else gzip.decompress(raw) if compression==1 else raw if compression==3 else None
    if decoded is None:raise ValueError('Unsupported reference compression')
    return NBT(decoded).root()

def biomes_from_chunk(data,names):
    result=[]
    sections={s['Y']:s for s in data['sections']}
    for sy in range(-4,20):
        section=sections[sy]['biomes'];palette=[names[s] for s in section['palette']]
        if len(palette)==1:result.extend(palette*64);continue
        bits=(len(palette)-1).bit_length();per_word=64//bits;words=section['data']
        result.extend(palette[((words[i//per_word]&0xffffffffffffffff)>>((i%per_word)*bits))&((1<<bits)-1)] for i in range(64))
    return result

def rle(values):
    out=[]
    for v in values:
        if out and out[-1][0]==v:out[-1][1]+=1
        else:out.append([v,1])
    return out

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--java',required=True);parser.add_argument('--seed',type=int,default=20261113)
    parser.add_argument('--cx',type=int,default=0);parser.add_argument('--cz',type=int,default=0)
    parser.add_argument('--terrain-only',action='store_true',help='Isolated reference with only placed features/structures disabled')
    parser.add_argument('--no-carvers',action='store_true',help='For terrain reference: isolate noise/surface stages')
    args=parser.parse_args();jar=ROOT/'.tools/vanilla-1.21.11/server.jar'
    if args.no_carvers and not args.terrain_only:parser.error('--no-carvers requires --terrain-only')
    if hashlib.sha1(jar.read_bytes()).hexdigest()!=SHA1:raise ValueError('Wrong reference JAR')
    prefix='surface-reference' if args.no_carvers else 'terrain-reference' if args.terrain_only else 'biome-reference'
    folder=jar.parent/f'{prefix}-{args.seed}';folder.mkdir(exist_ok=True)
    (folder/'eula.txt').write_text('eula=true\n')
    (folder/'server.properties').write_text('server-ip=127.0.0.1\nserver-port=0\nonline-mode=false\nenable-rcon=false\nenable-query=false\nlevel-name=world\nlevel-seed='+str(args.seed)+'\nview-distance=2\nsimulation-distance=2\nmax-players=1\ndifficulty=peaceful\nspawn-protection=0\npause-when-empty-seconds=0\n')
    if args.terrain_only:
        with (folder/'server.properties').open('a') as properties:properties.write('generate-structures=false\n')
        pack=folder/'world/datapacks/terrain-reference'
        pack.mkdir(parents=True,exist_ok=True)
        (pack/'pack.mcmeta').write_text(json.dumps({'pack':{'description':'CUDA Craft terrain-stage test only: no placed features','min_format':[94,1],'max_format':[94,1]}}))
        with zipfile.ZipFile(jar.parent/'versions/1.21.11/server-1.21.11.jar') as archive:
            for name in archive.namelist():
                if name.startswith('data/minecraft/worldgen/biome/') and name.endswith('.json'):
                    biome=json.loads(archive.read(name));biome['features']=[[] for _ in biome['features']]
                    if args.no_carvers:biome['carvers']=[]
                    target=pack/name;target.parent.mkdir(parents=True,exist_ok=True)
                    target.write_text(json.dumps(biome))
    lines=queue.Queue();log=(folder/'capture.log').open('w',encoding='utf-8')
    child=subprocess.Popen([args.java,'-Xms512M','-Xmx2G','-jar',str(jar),'nogui'],cwd=folder,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
    def reader():
        for line in child.stdout:log.write(line);log.flush();lines.put(line)
        lines.put(None)
    worker=threading.Thread(target=reader,daemon=True);worker.start()
    def wait_for(text,timeout=180):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            line=lines.get(timeout=max(.01,deadline-time.monotonic()))
            if line is None:raise RuntimeError('Reference exited; see '+str(folder/'capture.log'))
            if text in line:return
        raise TimeoutError(text)
    def command(text):child.stdin.write(text+'\n');child.stdin.flush()
    try:
        wait_for('Done (');print('Reference ready',flush=True)
        command('forceload remove all')
        force=f'forceload add {(args.cx-2)*16} {(args.cz-2)*16} {(args.cx+2)*16-1} {(args.cz+2)*16-1}'
        command(force)
        chunks=[(x,z) for z in range(args.cz-2,args.cz+2) for x in range(args.cx-2,args.cx+2)]
        deadline=time.monotonic()+120
        while True:
            command('save-all flush');wait_for('Saved the game',60)
            data=[chunk_from_region(folder/'world',x,z) for x,z in chunks]
            if all(c and c.get('Status')=='minecraft:full' for c in data):break
            if time.monotonic()>deadline:raise TimeoutError('Reference chunks not ready')
            time.sleep(2)
        registries=json.loads((ROOT/'.tools/reference/loginPacket.json').read_text())['dimensionCodec']
        names={e['key']:i for i,e in enumerate(registries['minecraft:worldgen/biome']['entries'])}
        if args.terrain_only:
            import importlib.util
            spec=importlib.util.spec_from_file_location('terrain_capture',ROOT/'tools/capture-vanilla-terrain.py')
            terrain_capture=importlib.util.module_from_spec(spec);spec.loader.exec_module(terrain_capture)
            registry={}
            for name,block in json.loads((jar.parent/'reports-1.21.11/reports/blocks.json').read_text()).items():
                for state in block['states']:
                    registry[terrain_capture.state_key({'Name':name,'Properties':state.get('properties',{})})]=state['id']
            fixture={'version':'1.21.11','server_sha1':SHA1,'seed':args.seed,'status':'minecraft:full',
                     'source':'Unmodified official binary; default overworld noise/surface; generate-structures=false; biome features empty; carvers '+('disabled' if args.no_carvers else 'unchanged'),
                     'features_disabled':True,'structures_disabled':True,'carvers_disabled':args.no_carvers,'chunks':[]}
            output=ROOT/'tests/fixtures'
            if not args.no_carvers:output=output/'postprocess'
            output.mkdir(parents=True,exist_ok=True)
            for (x,z),chunk in zip(chunks,data):
                raw=struct.pack('<98304I',*terrain_capture.states_from_chunk(chunk,registry))
                name=f'vanilla-density-{"surface-" if args.no_carvers else ""}{args.seed}-{x}_{z}.u32le.zlib'
                (output/name).write_bytes(zlib.compress(raw,9))
                fixture['chunks'].append({'x':x,'z':z,'file':name,'sha256':hashlib.sha256(raw).hexdigest()})
            target=output/f'vanilla-density-{"surface-" if args.no_carvers else ""}{args.seed}-{args.cx}_{args.cz}.json'
            target.write_text(json.dumps(fixture,indent=2)+'\n')
            print('Captured 16 terrain-reference chunks: '+str(target),flush=True)
            return
        fixture={'version':'1.21.11','server_sha1':SHA1,'seed':args.seed,'source':'Unmodified official server; default overworld; '+force,'chunks':[]}
        for (x,z),chunk in zip(chunks,data):fixture['chunks'].append({'x':x,'z':z,'biomes_rle':rle(biomes_from_chunk(chunk,names))})
        suffix=f'-{args.cx}_{args.cz}' if args.cx or args.cz else ''
        target=ROOT/'tests/fixtures'/f'vanilla-biomes-{args.seed}{suffix}.json';target.parent.mkdir(exist_ok=True)
        target.write_text(json.dumps(fixture,indent=2)+'\n')
        print('Captured 16 chunks / 24576 biome cells: '+str(target),flush=True)
    finally:
        if child.poll() is None:
            command('stop')
            try:child.wait(timeout=60)
            except subprocess.TimeoutExpired:child.kill();child.wait()
        worker.join(timeout=2);log.close()

if __name__=='__main__':main()
