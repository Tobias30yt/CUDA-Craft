"""Pinned 1.21.11 entity IDs, dimensions and health metadata slots.

Movement/combat profiles below are explicitly approximate engine defaults, not
extracted vanilla AI. The manifest separates registry coverage from behavior.
"""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
source=ROOT/'.tools/reference/entities.json'
entities=json.loads(source.read_text())
flying=set('allay bat bee blaze breeze ender_dragon ghast happy_ghast parrot phantom vex wither'.split())
aquatic=set('axolotl cod dolphin drowned elder_guardian glow_squid guardian nautilus pufferfish salmon squid tadpole tropical_fish zombie_nautilus'.split())
neutral=set('bee enderman iron_golem llama panda piglin polar_bear trader_llama wolf zombified_piglin'.split())
health={'pig':10,'cow':10,'mooshroom':10,'chicken':4,'sheep':8,'rabbit':3,'bat':6,'cod':3,'salmon':3,'tropical_fish':3,'pufferfish':3,'tadpole':6,'squid':10,'glow_squid':10,'dolphin':10,'axolotl':14,'cat':10,'ocelot':10,'parrot':6,'wolf':8,'fox':10,'bee':10,'spider':16,'cave_spider':12,'silverfish':8,'endermite':8,'enderman':40,'iron_golem':100,'snow_golem':4,'wither':300,'ender_dragon':200,'warden':500,'ravager':100,'elder_guardian':80,'guardian':30,'hoglin':40,'zoglin':40,'piglin_brute':50,'ghast':10,'happy_ghast':20,'allay':20,'horse':30,'donkey':30,'mule':30,'camel':32,'camel_husk':32,'turtle':30,'shulker':30,'sniffer':14,'goat':10,'frog':10,'slime':4,'magma_cube':4,'pillager':24,'vindicator':24,'evoker':24,'witch':26,'illusioner':32,'vex':14,'giant':100}
drops={'pig':'porkchop','cow':'beef','mooshroom':'beef','chicken':'chicken','sheep':'mutton','rabbit':'rabbit','zombie':'rotten_flesh','husk':'rotten_flesh','drowned':'rotten_flesh','zombie_villager':'rotten_flesh','zombified_piglin':'rotten_flesh','skeleton':'bone','stray':'bone','bogged':'bone','parched':'bone','wither_skeleton':'bone','spider':'string','cave_spider':'string','creeper':'gunpowder','blaze':'blaze_rod','enderman':'ender_pearl','slime':'slime_ball','magma_cube':'magma_cream','ghast':'ghast_tear','squid':'ink_sac','glow_squid':'glow_ink_sac','cod':'cod','salmon':'salmon','pufferfish':'pufferfish','tropical_fish':'tropical_fish','hoglin':'porkchop','zoglin':'rotten_flesh'}
mobs=[e for e in entities if 'health' in e['metadataKeys'] and e['name'] not in ('player','armor_stand','mannequin')]
lines=['// Generated registry facts and approximate behavior profiles; see docs/MOBS.md.','#pragma once','#include <string_view>','namespace cudacraft::mobdata {','struct Type {int id; const char* name; double width,height; float health; int health_index; bool hostile,flying,aquatic,neutral; const char* drop;};','inline constexpr Type types[]={']
for e in mobs:
    n=e['name'];flags=[e['type']=='hostile' and n not in neutral,n in flying,n in aquatic,n in neutral]
    lines.append('    {'+f'{e["id"]},"{n}",{e["width"]},{e["height"]},{health.get(n,20)},'+str(e['metadataKeys'].index('health'))+','+','.join(str(v).lower() for v in flags)+f',"{drops.get(n,"")}"'+'},')
lines+=['};','inline const Type* find(int id){for(const auto& t:types)if(t.id==id)return &t;return nullptr;}','inline const Type* find(std::string_view name){if(name.starts_with("minecraft:"))name.remove_prefix(10);for(const auto& t:types)if(name==t.name)return &t;return nullptr;}','}']
output=ROOT/'include/cudacraft/mob_data.hpp';output.write_text('\n'.join(lines)+'\n')
(ROOT/'fabric-client/src/client/java/dev/cudacraft/client/MobSmokeData.java').write_text('package dev.cudacraft.client;\n// Generated mob names for the opt-in integration scenario.\nfinal class MobSmokeData {\n    static final String[] NAMES = {\n'+',\n'.join('        '+json.dumps(e['name']) for e in mobs)+'\n    };\n}\n')
(ROOT/'tools/mob-data-manifest.json').write_text(json.dumps({'minecraft':'1.21.11','registry_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'mobs':len(mobs),'generated_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'behavior':'Approximate profiles; no claim of vanilla AI or complete loot tables.'},indent=2)+'\n')
print('Generated',len(mobs),'mob types')
