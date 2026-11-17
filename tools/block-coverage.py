"""Inventory block implementation areas without claiming vanilla behavior parity."""
import csv
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
blocks=json.loads((ROOT/'.tools/reference/blocks.json').read_text())
def area(name):
    if name in ('chest','trapped_chest','barrel','furnace','blast_furnace','smoker'):return 'containers_partial'
    if name in ('sand','red_sand','gravel','anvil','chipped_anvil','damaged_anvil','suspicious_sand','suspicious_gravel') or name.endswith('_concrete_powder'):return 'gravity_partial'
    if name in ('farmland','wheat','carrots','potatoes','beetroots','nether_wart','sweet_berry_bush','cocoa','torchflower_crop','pitcher_crop'):return 'growth_partial'
    if name.endswith(('_button','_pressure_plate')) or name in ('lever','redstone_wire','redstone_torch','redstone_wall_torch','repeater','redstone_lamp','redstone_block'):return 'redstone_partial'
    if name in ('nether_portal','end_portal','end_portal_frame','obsidian'):return 'portal_partial'
    if name in ('water','lava') or 'waterlogged' in [s['name'] for s in name_to_block[name].get('states',[])]:return 'fluid_partial'
    if name.endswith(('_door','_trapdoor','_fence','_fence_gate','_pane','_slab','_stairs','_bed')) or name=='iron_bars':return 'placement_shapes_partial'
    return 'generic_registry_only_not_audited'
name_to_block={b['name']:b for b in blocks}
path=ROOT/'docs/block-coverage.csv'
with path.open('w',newline='',encoding='utf-8') as stream:
    writer=csv.writer(stream);writer.writerow(['block','states','implementation_area','full_vanilla_behavior_verified'])
    for block in blocks:writer.writerow([block['name'],block['maxStateId']-block['minStateId']+1,area(block['name']),'no'])
print(f'Inventoried {len(blocks)} block types; complete vanilla behavior percentage remains unverified.')
