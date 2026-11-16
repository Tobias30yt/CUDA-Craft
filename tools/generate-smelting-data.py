"""Extract numerical smelting/fuel facts from the local pinned official 1.21.11 archive."""
import hashlib,json,re,subprocess,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
reference=ROOT/'.tools/vanilla-1.21.11'
wrapper=reference/'server.jar';archive=reference/'versions/1.21.11/server-1.21.11.jar'
assert hashlib.sha1(wrapper.read_bytes()).hexdigest()=='64bb6d763bed0a9f1d632ec347938594144943ed'
with zipfile.ZipFile(wrapper) as bundle:expected,version,_=bundle.read('META-INF/versions.list').decode().strip().split('\t')
assert version=='1.21.11' and hashlib.sha256(archive.read_bytes()).hexdigest()==expected
item_path=ROOT/'.tools/reference/items.json';manifest=json.loads((ROOT/'tools/game-data-manifest.json').read_text())
assert hashlib.sha256(item_path.read_bytes()).hexdigest()==manifest['files']['items.json']
items={i['name']:i['id'] for i in json.loads(item_path.read_text())}
jar=zipfile.ZipFile(archive)
def resolve(value,depth=0):
    if depth>32:raise ValueError('Recursive ingredient tag')
    if isinstance(value,list):return set().union(*(resolve(v,depth+1) for v in value))
    if isinstance(value,dict):value=value['id']
    if value.startswith('#'):
        namespace,name=value[1:].split(':');assert namespace=='minecraft'
        return resolve(json.loads(jar.read(f'data/minecraft/tags/item/{name}.json'))['values'],depth+1)
    return {items[value.removeprefix('minecraft:')]}
recipes={};source_count=0
for path in sorted(jar.namelist()):
    if not path.startswith('data/minecraft/recipe/') or not path.endswith('.json'):continue
    data=json.loads(jar.read(path));kind={'minecraft:smelting':1,'minecraft:blasting':2,'minecraft:smoking':3}.get(data['type'])
    if not kind:continue
    source_count+=1
    for ingredient in resolve(data['ingredient']):
        value=(items[data['result']['id'].removeprefix('minecraft:')],data['result'].get('count',1),data['cookingtime'])
        assert (kind,ingredient) not in recipes or recipes[kind,ingredient]==value
        recipes[kind,ingredient]=value
common=next((ROOT/'.tools/gradle-home/caches/fabric-loom/minecraftMaven/net/minecraft/minecraft-common').rglob('*.jar'))
bytecode=subprocess.check_output(['C:/Program Files/Java/jdk-23/bin/javap.exe','-classpath',str(common),'-c','net.minecraft.world.level.block.entity.FuelValues'],text=True)
fuels={};current=None;stack=[]
for line in bytecode.splitlines():
    field=re.search(r'// Field (net/minecraft/(?:tags/ItemTags|world/item/Items|world/level/block/Blocks))\.([A-Z_0-9]+):',line)
    if field:current=('#minecraft:' if field[1].endswith('ItemTags') else 'minecraft:')+field[2].lower();stack=[];continue
    if not current:continue
    op=re.search(r'^\s*\d+: (\w+)(?:\s+(-?\d+))?',line)
    if not op:continue
    name,value=op.groups()
    if name=='iload_2':stack.append(200)
    elif name in ('bipush','sipush'):stack.append(int(value))
    elif name.startswith('iconst_'):stack.append(int(name[-1]))
    elif name in ('imul','idiv','iadd'):
        right=stack.pop();left=stack.pop();stack.append(left*right if name=='imul' else left//right if name=='idiv' else left+right)
    elif name=='invokevirtual' and 'FuelValues$Builder.add:' in line:
        assert len(stack)==1,(current,stack)
        for item in resolve(current):fuels[item]=stack[0]
        current=None
    elif name=='invokevirtual' and 'FuelValues$Builder.remove:' in line:
        for item in resolve(current):fuels.pop(item,None)
        current=None
assert fuels[items['coal']]==1600 and fuels[items['oak_log']]==300 and fuels[items['stick']]==100
assert fuels[items['lava_bucket']]==20000 and items['warped_planks'] not in fuels
out=['// Generated numerical 1.21.11 recipe/fuel facts from the pinned official archive.','#pragma once','#include <algorithm>',
     'namespace cudacraft::smelting {','struct Recipe {int kind,input,output,count,ticks;};','inline constexpr Recipe recipes[]={']
out += ['{'+','.join(map(str,(*key,*value)))+'},' for key,value in sorted(recipes.items())]
out += ['};','inline const Recipe* recipe(int kind,int item){for(const auto& r:recipes)if(r.kind==kind&&r.input==item)return &r;return nullptr;}',
        'struct Fuel {int item,ticks;};','inline constexpr Fuel fuels[]={']
out += ['{'+str(item)+','+str(ticks)+'},' for item,ticks in sorted(fuels.items())]
out += ['};','inline int fuel(int item){auto i=std::lower_bound(std::begin(fuels),std::end(fuels),item,[](const Fuel& f,int id){return f.item<id;});return i!=std::end(fuels)&&i->item==item?i->ticks:0;}','}']
target=ROOT/'include/cudacraft/smelting_data.hpp';target.write_text('\n'.join(out)+'\n')
(ROOT/'tools/smelting-data-manifest.json').write_text(json.dumps({'version':'1.21.11','server_sha1':hashlib.sha1(wrapper.read_bytes()).hexdigest(),
    'inner_server_sha256':expected,'items_sha256':hashlib.sha256(item_path.read_bytes()).hexdigest(),'mapped_fuel_reference_sha256':hashlib.sha256(common.read_bytes()).hexdigest(),
    'recipe_count':source_count,'expanded_recipes':len(recipes),'fuel_items':len(fuels),'generated_sha256':hashlib.sha256(target.read_bytes()).hexdigest()},indent=2)+'\n')
print(source_count,'official cooking recipes;',len(recipes),'expanded input mappings;',len(fuels),'fuel items')
