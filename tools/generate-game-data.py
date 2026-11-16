"""Convert the independent minecraft-data 1.21.11 registry facts to wire packets.
Input files are downloaded into .tools/reference; generated C++ is checked in.
No Minecraft executable/source or assets are used.
"""
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / '.tools/reference'
TYPES = {'end': 0, 'byte': 1, 'short': 2, 'int': 3, 'long': 4, 'float': 5,
         'double': 6, 'byteArray': 7, 'string': 8, 'list': 9, 'compound': 10,
         'intArray': 11, 'longArray': 12}

def vi(v):
    b = bytearray()
    v &= 0xffffffff
    while True:
        b.append((v & 127) | (128 if v > 127 else 0))
        v >>= 7
        if not v:
            return bytes(b)

def string(v):
    b = v.encode('utf-8')
    return vi(len(b)) + b

def ns(v):
    b = v.encode('utf-8')
    return struct.pack('>H', len(b)) + b

def payload(t, v):
    if t in ('byte', 'short', 'int', 'float', 'double'):
        return struct.pack('>' + {'byte':'b','short':'h','int':'i','float':'f','double':'d'}[t], v)
    if t == 'long':
        value = ((v[0] & 0xffffffff) << 32) | (v[1] & 0xffffffff) if isinstance(v, list) else v
        return struct.pack('>Q', value & 0xffffffffffffffff)
    if t == 'string': return ns(v)
    if t == 'compound':
        return b''.join(bytes([TYPES[x['type']]]) + ns(k) + payload(x['type'], x['value']) for k,x in v.items()) + b'\0'
    if t == 'list':
        return bytes([TYPES[v['type']]]) + struct.pack('>i',len(v['value'])) + b''.join(payload(v['type'], x) for x in v['value'])
    if t.endswith('Array'):
        kind = {'byteArray':'byte','intArray':'int','longArray':'long'}[t]
        return struct.pack('>i',len(v)) + b''.join(payload(kind, x) for x in v)
    if t == 'end': return b''
    raise ValueError(t)

data = json.loads((REF / 'loginPacket.json').read_text(encoding='utf-8'))
registries = data['dimensionCodec']
packets = []
for name, registry in registries.items():
    b = b'\x07' + string(name) + vi(len(registry['entries']))
    for entry in registry['entries']:
        node = entry['value']
        b += string(entry['key']) + b'\1' + bytes([TYPES[node['type']]]) + payload(node['type'], node['value'])
    packets.append(b)

# The creative prototype supplies only tags referenced by its registry snapshot.
# Gameplay tags are deliberately empty until their associated mechanics exist.
references = sorted(set(re.findall(r'"#minecraft:([^"\\]+)"', json.dumps(registries))))
tags = {}
for name in references:
    if name.startswith('enchantable/'): registry = 'item'
    elif name.startswith('exclusive_set/'): registry = 'enchantment'
    elif name in ('in_overworld','in_nether','in_end'): registry = 'timeline'
    elif name in ('quick_actions','pause_screen_additions'): registry = 'dialog'
    elif name.startswith('sensitive_to_') or name == 'arrows': registry = 'entity_type'
    else: registry = 'block'
    tags.setdefault('minecraft:' + registry, {})['minecraft:' + name] = []
timeline = {e['key']:i for i,e in enumerate(registries['minecraft:timeline']['entries'])}
tags['minecraft:timeline']['minecraft:in_overworld'] = list(timeline.values())
tags['minecraft:timeline']['minecraft:in_nether'] = [timeline['minecraft:villager_schedule']]
tags['minecraft:timeline']['minecraft:in_end'] = [timeline['minecraft:villager_schedule']]
tags['minecraft:timeline']['minecraft:universal'] = [timeline['minecraft:villager_schedule']]
b = b'\x0d' + vi(len(tags))
for registry, entries in tags.items():
    b += string(registry) + vi(len(entries))
    for name, ids in entries.items():
        b += string(name) + vi(len(ids)) + b''.join(vi(i) for i in ids)
packets.append(b)

blocks = {b['name']:b for b in json.loads((REF/'blocks.json').read_text())}
items = {b['name']:b for b in json.loads((REF/'items.json').read_text())}
names = ['stone','dirt','grass_block','cobblestone','oak_planks','glass','bricks','gold_block',
         'granite','polished_granite','diorite','polished_diorite','andesite','polished_andesite',
         'deepslate','cobbled_deepslate','polished_deepslate','tuff','calcite','obsidian',
         'sandstone','smooth_sandstone','red_sandstone','smooth_stone','stone_bricks',
         'iron_block','diamond_block','emerald_block','lapis_block','coal_block','netherite_block',
         'spruce_planks','birch_planks','jungle_planks','acacia_planks','dark_oak_planks',
         'mangrove_planks','cherry_planks','bamboo_planks','pale_oak_planks','crimson_planks','warped_planks']
for color in ['white','orange','magenta','light_blue','yellow','lime','pink','gray','light_gray','cyan','purple','blue','brown','green','red','black']:
    names += [color+'_wool',color+'_concrete',color+'_terracotta']
names += ['sand','gravel','snow_block','oak_log','birch_log','spruce_log','oak_leaves',
          'birch_leaves','spruce_leaves','coal_ore','iron_ore','gold_ore','diamond_ore',
          'deepslate_coal_ore','deepslate_iron_ore','deepslate_gold_ore','deepslate_diamond_ore']
names += ['acacia_log','jungle_log','dark_oak_log','mangrove_log','cherry_log','pale_oak_log',
          'acacia_leaves','jungle_leaves','dark_oak_leaves','mangrove_leaves','cherry_leaves',
          'pale_oak_leaves','red_sand','terracotta','mycelium','podzol','mud','ice','moss_block']
header = ['// Generated by tools/generate-game-data.py. Minecraft Java 1.21.11 only.',
          '#pragma once', '#include "cudacraft/protocol.hpp"',
          'namespace cudacraft::game_data {',
          'inline const std::vector<protocol::Bytes>& configuration_packets() {',
          'static const std::vector<protocol::Bytes> packets{']
for packet in packets:
    header.append('{'+','.join(str(x) for x in packet)+'},')
header += ['}; return packets; }', 'struct BuildingBlock { int item; int state; const char* name; };',
           'inline constexpr BuildingBlock building_blocks[]{']
for name in names:
    header.append('{'+str(items[name]['id'])+','+str(blocks[name]['defaultState'])+',"'+name+'"},')
header += ['};','inline constexpr int plains_biome = '+str(next(i for i,e in enumerate(registries['minecraft:worldgen/biome']['entries']) if e['key']=='minecraft:plains'))+';', '}']
out = ROOT/'include/cudacraft/generated_game_data.hpp'
out.write_text('\n'.join(header)+'\n', encoding='utf-8')
print(f'{len(packets)} registry/tag packets; {sum(map(len,packets))} bytes; {len(names)} building blocks; largest packet {max(map(len,packets))}')
terrain_names = ['air','stone','dirt','grass_block','bedrock','deepslate','water','lava',
                 'sand','sandstone','gravel','snow_block','oak_log','birch_log','spruce_log',
                 'oak_leaves','birch_leaves','spruce_leaves','short_grass','dandelion','poppy',
                 'coal_ore','iron_ore','gold_ore','diamond_ore','deepslate_coal_ore',
                 'deepslate_iron_ore','deepslate_gold_ore','deepslate_diamond_ore',
                 'acacia_log','jungle_log','dark_oak_log','mangrove_log','cherry_log','pale_oak_log',
                 'acacia_leaves','jungle_leaves','dark_oak_leaves','mangrove_leaves','cherry_leaves',
                 'pale_oak_leaves','red_sand','terracotta','mycelium','podzol','mud','ice','moss_block']
constants = ['// Generated version-pinned identifiers, not generation rules.', '#pragma once',
             'namespace cudacraft::terrain_ids {']
for name in terrain_names:
    constants.append(f'inline constexpr int {name} = {blocks[name]["defaultState"]};')
for name in [e['key'].split(':')[1] for e in registries['minecraft:worldgen/biome']['entries']]:
    biome = next(i for i,e in enumerate(registries['minecraft:worldgen/biome']['entries']) if e['key']=='minecraft:'+name)
    constants.append(f'inline constexpr int biome_{name} = {biome};')
constants += ['}']
(ROOT/'include/cudacraft/terrain_ids.hpp').write_text('\n'.join(constants)+'\n',encoding='utf-8')
# Cubiomes uses stable historical IDs; the wire registry uses its 1.21.11 order.
mapping=['// Generated wire-registry mapping. Include after cubiomes/biomes.h.',
         '#pragma once','namespace cudacraft::biome_mapping {',
         'inline int protocol_id(int biome) { switch(biome) {']
for i,e in enumerate(registries['minecraft:worldgen/biome']['entries']):
    name=e['key'].split(':')[1]
    mapping.append(f'case ::{name}: return {i};')
mapping += ['default: return -1; } }','}']
(ROOT/'include/cudacraft/biome_mapping.hpp').write_text('\n'.join(mapping)+'\n',encoding='utf-8')
