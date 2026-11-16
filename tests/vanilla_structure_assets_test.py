"""Optional integration: every imported template palette vs Mojang's independent block report."""
import gzip, importlib.util, json, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('nbt',ROOT/'tools/capture-vanilla-biomes.py');nbt=importlib.util.module_from_spec(spec);spec.loader.exec_module(nbt)
report=json.loads((ROOT/'.tools/vanilla-1.21.11/reports-1.21.11/reports/blocks.json').read_text())
states={(name,tuple(sorted(state.get('properties',{}).items()))):state['id'] for name,block in report.items() for state in block['states']}
count=checked=0
with zipfile.ZipFile(ROOT/'.tools/vanilla-1.21.11/versions/1.21.11/server-1.21.11.jar') as archive:
    for name in archive.namelist():
        if not name.startswith('data/minecraft/structure/') or not name.endswith('.nbt'):continue
        original=nbt.NBT(gzip.decompress(archive.read(name))).root()
        converted=json.loads((ROOT/'data/structures'/(name[len('data/minecraft/structure/'):-4]+'.json')).read_text())
        assert converted['size']==original['size']
        for source,target in zip(original.get('palettes',[original.get('palette',[])]),converted['palettes'],strict=True):
            for index,entry in enumerate(source):
                assert target[index]==states[entry['Name'],tuple(sorted(entry.get('Properties',{}).items()))], (name,entry,target[index])
                checked+=1
        count+=1
assert count==1202
print(count,'vanilla templates;',checked,'palette entries match the official 1.21.11 block report exactly')
