"""Version-pinned 1.21.11 block/item facts for the native simulation."""
import json
import hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[1];ref=root/'.tools/reference'
manifest=json.loads((root/'tools/game-data-manifest.json').read_text())
for name in ('blocks.json','items.json'):
    if hashlib.sha256((ref/name).read_bytes()).hexdigest()!=manifest['files'][name]:
        raise RuntimeError('Pinned simulation data hash mismatch: '+name)
blocks=json.loads((ref/'blocks.json').read_text());items=json.loads((ref/'items.json').read_text())
byitem={i['name']:i for i in items}
out=['// Generated 1.21.11 registry facts. See tools/game-data-manifest.json.', '#pragma once',
     '#include <algorithm>','#include <string_view>','namespace cudacraft::simdata {',
     'struct Block { int first,last,state,item,drop; float hardness; bool solid; int tool; const char* name; };',
     'inline constexpr Block blocks[]={']
for b in blocks:
    tool={'mineable/pickaxe':1,'mineable/axe':2,'mineable/shovel':3}.get(b.get('material'),0)
    out.append('{'+','.join(map(str,[b['minStateId'],b['maxStateId'],b['defaultState'],byitem.get(b['name'],{}).get('id',0),b.get('drops',[0])[0] if b.get('drops') else 0]))+','+str(float(b.get('hardness') if b.get('hardness') is not None else -1))+'f,'+str(b['boundingBox']=='block').lower()+','+str(tool)+','+json.dumps(b['name'])+'},')
out += ['};','inline constexpr float block_resistance[]={']
for start in range(0,len(blocks),16):
    out.append(','.join(f"{float(b['resistance'])}f" for b in blocks[start:start+16])+',')
out+=['};','inline const Block& block(int state) { auto i=std::lower_bound(std::begin(blocks),std::end(blocks),state,[](const Block& b,int s){return b.last<s;}); return i!=std::end(blocks) && state>=i->first?*i:blocks[0]; }',
      'inline float blast_resistance(int state) { return block_resistance[&block(state)-blocks]; }',
      'inline int default_state(std::string_view name) { for(const auto& b:blocks) if(name==b.name)return b.state; return 0; }',
      'inline bool valid_state(int state) { return state>=0 && state<=blocks[std::size(blocks)-1].last; }',
      'struct Item { int id,stack,durability,tool,tier; const char* name; };','inline constexpr Item items[]={']
for i in items:
    n=i['name'];tool=next((v for suffix,v in [('_pickaxe',1),('_axe',2),('_shovel',3),('_sword',4)] if n.endswith(suffix)),0)
    tier=next((v for prefix,v in [('wooden_',1),('stone_',2),('iron_',3),('diamond_',4),('netherite_',5),('golden_',1),('copper_',2)] if n.startswith(prefix)),0)
    out.append('{'+','.join(map(str,[i['id'],i['stackSize'],i.get('maxDurability',0),tool,tier]))+','+json.dumps(n)+'},')
out+=['};','inline const Item* item(int id) { auto i=std::lower_bound(std::begin(items),std::end(items),id,[](const Item& a,int b){return a.id<b;}); return i!=std::end(items) && i->id==id?i:nullptr; }',
      'inline int item_id(std::string_view name) { for(const auto& i:items) if(name==i.name) return i.id; return 0; }']
out += ['struct Property { int first,last,stride,count; const char* name; const char* values; };', 'inline constexpr Property properties[]={']
for b in blocks:
    stride=b['maxStateId']-b['minStateId']+1
    for prop in b['states']:
        stride//=prop['num_values']
        values=prop.get('values', ['true','false'] if prop['type']=='bool' else [str(i) for i in range(prop['num_values'])])
        out.append('{'+','.join(map(str,[b['minStateId'],b['maxStateId'],stride,prop['num_values']]))+','+json.dumps(prop['name'])+','+json.dumps('|'.join(values))+'},')
out += ['};',
    'inline const Property* property(int state,std::string_view name) { auto i=std::lower_bound(std::begin(properties),std::end(properties),state,[](const Property& p,int s){return p.last<s;}); for(;i!=std::end(properties)&&i->first<=state;++i)if(name==i->name)return i;return nullptr; }',
    'inline std::string_view property_value(int state,std::string_view name) { const auto* p=property(state,name);if(!p)return {};int index=(state-p->first)/p->stride%p->count;std::string_view values=p->values;while(index-->0)values.remove_prefix(values.find(\'|\')+1);return values.substr(0,values.find(\'|\')); }',
    'inline int with_property(int state,std::string_view name,std::string_view value) { const auto* p=property(state,name);if(!p)return state;std::string_view values=p->values;for(int i=0;i<p->count;++i){auto end=values.find(\'|\');if(values.substr(0,end)==value)return state+(i-(state-p->first)/p->stride%p->count)*p->stride;if(end==values.npos)break;values.remove_prefix(end+1);}return state; }']
for n in ['redstone_wire','lever','redstone_torch','redstone_wall_torch','repeater','redstone_lamp','redstone_block','crafting_table','cobblestone','obsidian','water','lava','bedrock']:
    b=next(b for b in blocks if b['name']==n)
    for key in ['minStateId','maxStateId','defaultState']:
        out.append(f'inline constexpr int {n}_{dict(minStateId="first",maxStateId="last",defaultState="state")[key]}={b[key]};')
for n in ['bucket','water_bucket','redstone','lever','redstone_torch','repeater','redstone_lamp','redstone_block','bread','apple','cooked_beef','porkchop','rotten_flesh','pig_spawn_egg','zombie_spawn_egg','crafting_table','stick']:
    out.append(f'inline constexpr int {n}_item={byitem[n]["id"]};')
out+=['inline bool water(int s) { return s>=86 && s<=101; }','inline bool lava(int s) { return s>=102 && s<=117; }',
      'inline bool wire(int s) { return s>=redstone_wire_first && s<=redstone_wire_last; }',
      'inline int wire_power(int s) { return ((s-redstone_wire_first)/9)%16; }',
      'inline bool lever(int s) { return s>=lever_first && s<=lever_last; }',
      'inline bool repeater(int s) { return s>=repeater_first && s<=repeater_last; }',
      'inline bool torch(int s) { return s>=redstone_torch_first && s<=redstone_wall_torch_last; }',
      'inline bool lamp(int s) { return s>=redstone_lamp_first && s<=redstone_lamp_last; }',
      'inline bool circuit(int s) { return wire(s)||lever(s)||repeater(s)||torch(s)||lamp(s)||s==redstone_block_state; }',
      'inline bool copper_bulb(int state) {',
      '    return state>=26861 && state<=26892; // Eight bulb types, four states each in 1.21.11.',
      '}',
      'inline int copper_bulb_light(int state) {',
      '    if(!copper_bulb(state)||property_value(state,"lit")!="true")return 0;',
      '    constexpr int levels[]={15,12,8,4};',
      '    return levels[((state-26861)/4)%4];',
      '}',
      'inline bool replaceable(int s) { auto n=std::string_view(block(s).name); return s==0||water(s)||n=="short_grass"||n=="dandelion"||n=="poppy"||wire(s)||torch(s); }','}']
(root/'include/cudacraft/simulation_data.hpp').write_text('\n'.join(out)+'\n')
print('Generated simulation facts:',len(blocks),'blocks,',len(items),'items')
