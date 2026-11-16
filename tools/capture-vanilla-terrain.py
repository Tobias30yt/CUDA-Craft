"""Extract block-state facts from existing private official reference worlds.

Prefer chunks saved at the surface stage: unlike FULL chunks these do not yet
contain later carvers, structures or features, so terrain can be compared exactly.
No server/client is launched and no world file is changed.
"""
import argparse
import collections
import hashlib
import importlib.util
import json
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('capture', ROOT/'tools/capture-vanilla-biomes.py')
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)

def state_key(state):
    props = state.get('Properties', {})
    return state['Name'] + ('[' + ','.join(k+'='+v for k,v in sorted(props.items())) + ']' if props else '')

def states_from_chunk(chunk, registry):
    sections = {s['Y']:s for s in chunk['sections']}
    result = []
    for sy in range(-4,20):
        section = sections.get(sy, {}).get('block_states')
        if not section:
            result.extend([0]*4096)
            continue
        palette = [registry[state_key(s)] for s in section['palette']]
        if len(palette)==1:
            result.extend(palette*4096)
            continue
        bits=max(4,(len(palette)-1).bit_length()); per_word=64//bits
        words=section['data']; mask=(1<<bits)-1
        result.extend(palette[((words[i//per_word]&0xffffffffffffffff)>>((i%per_word)*bits))&mask] for i in range(4096))
    return result

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--seed',type=int,default=20261113)
    parser.add_argument('--status',default='minecraft:carvers')
    parser.add_argument('--limit',type=int,default=8)
    parser.add_argument('--list',action='store_true')
    parser.add_argument('--controlled',action='store_true',help='Use test world without structures or features')
    parser.add_argument('--cx',type=int,default=0);parser.add_argument('--cz',type=int,default=0)
    args=parser.parse_args()
    reference=ROOT/'.tools/vanilla-1.21.11'
    if hashlib.sha1((reference/'server.jar').read_bytes()).hexdigest()!=capture.SHA1:
        raise ValueError('Wrong reference server')
    registry={}
    for name,block in json.loads((reference/'reports-1.21.11/reports/blocks.json').read_text()).items():
        for s in block['states']:
            registry[state_key({'Name':name,'Properties':s.get('properties',{})})]=s['id']
    world=reference/f'{"terrain-reference" if args.controlled else "biome-reference"}-{args.seed}'/'world'
    chunks=[]; counts=collections.Counter()
    for region in sorted((world/'region').glob('r.*.*.mca')):
        _,rx,rz,_=region.name.split('.')
        with region.open('rb') as f: header=f.read(4096)
        for i in range(1024):
            if not any(header[i*4:i*4+4]):continue
            cx=int(rx)*32+(i%32); cz=int(rz)*32+(i//32)
            chunk=capture.chunk_from_region(world,cx,cz)
            counts[chunk['Status']]+=1
            if chunk['Status']==args.status: chunks.append((cx,cz,chunk))
    print(dict(counts))
    if args.list:return
    chunks.sort(key=lambda c:(max(abs(c[0]-args.cx),abs(c[1]-args.cz)),c[1],c[0]))
    output=ROOT/'tests/fixtures'
    records=[]
    for cx,cz,chunk in chunks[:args.limit]:
        values=states_from_chunk(chunk,registry)
        raw=struct.pack('<98304I',*values)
        prefix='vanilla-density-carvers' if args.controlled else 'vanilla-terrain'
        name=f'{prefix}-{args.seed}-{cx}_{cz}.u32le.zlib'
        (output/name).write_bytes(zlib.compress(raw,9))
        records.append({'x':cx,'z':cz,'file':name,'sha256':hashlib.sha256(raw).hexdigest(),
                        'structures':list(chunk.get('structures',{}).get('References',{}))})
    manifest={'version':'1.21.11','server_sha1':capture.SHA1,'seed':args.seed,'status':args.status,'chunks':records}
    if args.controlled:
        manifest.update(features_disabled=True,structures_disabled=True,carvers_disabled=False,
                        source='Official 1.21.11 binary; features empty, generate-structures=false; saved BEFORE chunk promotion and fluid postprocessing')
    target=output/f'{"vanilla-density-carvers" if args.controlled else "vanilla-terrain"}-{args.seed}.json'
    target.write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'Exported {len(records)} chunks: {target}')

if __name__=='__main__':main()
