"""Convert locally installed, verified 1.21.11 structure assets for native placement.
Generated Minecraft assets remain local and ignored; they are not bundled in source releases.
"""
import gzip, hashlib, importlib.util, json, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('nbt',ROOT/'tools/capture-vanilla-biomes.py');nbt=importlib.util.module_from_spec(spec);spec.loader.exec_module(nbt)

def main():
    reference=ROOT/'.tools/vanilla-1.21.11'
    wrapper=reference/'server.jar'
    if hashlib.sha1(wrapper.read_bytes()).hexdigest()!=nbt.SHA1:raise ValueError('Expected official 1.21.11 server bundle')
    archive=reference/'versions/1.21.11/server-1.21.11.jar'
    with zipfile.ZipFile(wrapper) as bundle:
        sha,version,path=bundle.read('META-INF/versions.list').decode().strip().split('\t')
        if version!='1.21.11' or hashlib.sha256(archive.read_bytes()).hexdigest()!=sha:raise ValueError('Inner server archive checksum mismatch')
    blocks=json.loads((ROOT/'.tools/reference/blocks.json').read_text());byname={b['name']:b for b in blocks}
    def state(entry):
        name=entry['Name'].removeprefix('minecraft:');b=byname[name];props=entry.get('Properties',{})
        value=b['minStateId'];stride=b['maxStateId']-b['minStateId']+1
        for p in b['states']:
            stride//=p['num_values'];values=p.get('values',['true','false'] if p['type']=='bool' else [str(i) for i in range(p['num_values'])])
            # A missing property uses this block's vanilla default, not the first enum value.
            index=values.index(props[p['name']]) if p['name'] in props else (b['defaultState']-b['minStateId'])//stride%p['num_values']
            value+=index*stride
        return value
    output=ROOT/'data/structures';output.mkdir(parents=True,exist_ok=True);manifest=[]
    with zipfile.ZipFile(archive) as jar:
        for path in sorted(jar.namelist()):
            if not path.startswith('data/minecraft/structure/') or not path.endswith('.nbt'):continue
            template=nbt.NBT(gzip.decompress(jar.read(path))).root()
            palettes=template.get('palettes',[template.get('palette',[])])
            numeric=[[state(entry) for entry in palette] for palette in palettes]
            records=[];block_entities=0;markers=0
            for block in template.get('blocks',[]):
                names={palette[block['state']]['Name'] for palette in palettes}
                if 'minecraft:structure_block' in names or 'minecraft:structure_void' in names:markers+=1;continue
                if 'minecraft:jigsaw' in names:
                    final=block.get('nbt',{}).get('final_state','minecraft:air')
                    name,_,prop=final.partition('[');properties=dict(p.split('=',1) for p in prop.rstrip(']').split(',') if p)
                    replacement=state({'Name':name,'Properties':properties})
                    index=len(numeric[0])
                    for palette in numeric:palette.append(replacement)
                    records.append([*block['pos'],index]);markers+=1;continue
                records.append([*block['pos'],block['state']])
                block_entities+=int('nbt' in block)
            name=path[len('data/minecraft/structure/'):-4]
            result={'format':1,'protocol':774,'size':template['size'],'palettes':numeric,'blocks':records,
                'unsupported_block_entities':block_entities,'unsupported_entities':len(template.get('entities',[])),'markers':markers}
            target=output/(name+'.json');target.parent.mkdir(parents=True,exist_ok=True);target.write_text(json.dumps(result,separators=(',',':')))
            manifest.append({'id':name,'blocks':len(records),'variants':len(numeric),'block_entities':block_entities,'entities':result['unsupported_entities']})
    (output/'manifest.json').write_text(json.dumps({'version':'1.21.11','templates':manifest},indent=2)+'\n')
    print('Imported',len(manifest),'vanilla structure templates to',output)

if __name__=='__main__':main()
