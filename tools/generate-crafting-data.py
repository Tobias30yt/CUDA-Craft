"""Extract plain shaped/shapeless 1.21.11 recipe facts from pinned local inputs."""
import hashlib,json,re,subprocess,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
base=ROOT/'.tools/vanilla-1.21.11'
wrapper=base/'server.jar';archive=base/'versions/1.21.11/server-1.21.11.jar'
assert hashlib.sha1(wrapper.read_bytes()).hexdigest()=='64bb6d763bed0a9f1d632ec347938594144943ed'
with zipfile.ZipFile(wrapper) as bundle:expected,version,_=bundle.read('META-INF/versions.list').decode().strip().split('\t')
assert version=='1.21.11' and hashlib.sha256(archive.read_bytes()).hexdigest()==expected
item_path=ROOT/'.tools/reference/items.json'
assert hashlib.sha256(item_path.read_bytes()).hexdigest()==json.loads((ROOT/'tools/game-data-manifest.json').read_text())['files']['items.json']
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
groups=[()];group_ids={():0}
def ingredient(value):
    key=tuple(sorted(resolve(value)))
    if key not in group_ids:group_ids[key]=len(groups);groups.append(key)
    return group_ids[key]
recipes=[];excluded=[]
for path in sorted(jar.namelist()):
    if not path.startswith('data/minecraft/recipe/') or not path.endswith('.json'):continue
    data=json.loads(jar.read(path));name=Path(path).stem;kind=data['type']
    if kind not in ('minecraft:crafting_shaped','minecraft:crafting_shapeless'):
        if kind.startswith('minecraft:crafting_'):excluded.append({'name':name,'reason':kind})
        continue
    result=data['result']
    if set(result)-{'id','count'}:excluded.append({'name':name,'reason':'result components'});continue
    if kind.endswith('_shaped'):
        pattern=data['pattern'];occupied=[(x,y) for y,row in enumerate(pattern) for x,c in enumerate(row) if c!=' ']
        x0=min(x for x,y in occupied);x1=max(x for x,y in occupied);y0=min(y for x,y in occupied);y1=max(y for x,y in occupied)
        width=x1-x0+1;height=y1-y0+1
        slots=[0 if pattern[y][x]==' ' else ingredient(data['key'][pattern[y][x]]) for y in range(y0,y1+1) for x in range(x0,x1+1)]
    else:
        width=height=0;slots=[ingredient(v) for v in data['ingredients']]
    count=sum(i!=0 for i in slots);assert 1<=count<=9
    recipes.append((name,width,height,count,slots+[0]*(9-len(slots)),items[result['id'].removeprefix('minecraft:')],result.get('count',1)))
common=next((ROOT/'.tools/gradle-home/caches/fabric-loom/minecraftMaven/net/minecraft/minecraft-common').rglob('*.jar'))
bytecode=subprocess.check_output(['C:/Program Files/Java/jdk-23/bin/javap.exe','-classpath',str(common),'-c','-p','net.minecraft.world.item.Items'],text=True)
remainders={};previous=None;pending=None
for line in bytecode.splitlines():
    field=re.search(r'getstatic.*// Field ([A-Z_0-9]+):Lnet/minecraft/world/item/Item;',line)
    if field:previous=field[1].lower()
    if 'Properties.craftRemainder:' in line:assert previous in items;pending=previous
    field=re.search(r'putstatic.*// Field ([A-Z_0-9]+):Lnet/minecraft/world/item/Item;',line)
    if field and pending:remainders[items[field[1].lower()]]=items[pending];pending=None
assert len(remainders)==5 and remainders[items['milk_bucket']]==items['bucket'] and remainders[items['honey_bottle']]==items['glass_bottle']
out=['// Generated numerical 1.21.11 crafting facts; see tools/crafting-data-manifest.json.','#pragma once','#include <array>','namespace cudacraft::crafting {',
     'struct Ingredient {int offset,count;};','inline constexpr int ingredient_items[]={'+','.join(str(i) for group in groups for i in group)+'};','inline constexpr Ingredient ingredients[]={']
offset=0
for group in groups:out.append('{'+str(offset)+','+str(len(group))+'},');offset+=len(group)
out+=['};','struct Recipe {const char* name; int width,height,occupied;std::array<int,9> slots;int output,count;};','inline constexpr Recipe recipes[]={']
for name,w,h,n,slots,result,count in recipes:out.append('{"'+name+'",'+','.join(map(str,[w,h,n]))+',{'+','.join(map(str,slots))+'},'+str(result)+','+str(count)+'},')
out+=['};','inline int remainder(int item){switch(item){']
out+=['case '+str(i)+':return '+str(r)+';' for i,r in sorted(remainders.items())]
out+=['default:return 0;}}','}']
target=ROOT/'include/cudacraft/crafting_data.hpp';target.write_text('\n'.join(out)+'\n')
(ROOT/'tools/crafting-data-manifest.json').write_text(json.dumps({'version':version,'server_sha1':hashlib.sha1(wrapper.read_bytes()).hexdigest(),'inner_server_sha256':expected,
    'items_sha256':hashlib.sha256(item_path.read_bytes()).hexdigest(),'mapped_item_reference_sha256':hashlib.sha256(common.read_bytes()).hexdigest(),
    'recipe_count':len(recipes),'shaped':sum(r[1]>0 for r in recipes),'shapeless':sum(r[1]==0 for r in recipes),'ingredient_groups':len(groups)-1,'remainders':len(remainders),
    'excluded_crafting':excluded,'generated_sha256':hashlib.sha256(target.read_bytes()).hexdigest()},indent=2)+'\n')
print(len(recipes),'plain crafting recipes;',len(groups)-1,'ingredient groups;',len(excluded),'explicitly excluded crafting recipes')
