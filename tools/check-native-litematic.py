"""Verify block placement via a temporary native CUDA Craft save."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from litematic_fixture import add, merge_regions, read_fixture

ROOT = Path(__file__).resolve().parents[1]


def resolve_state(state, reports):
    block = reports[state["Name"]]
    properties = state.get("Properties", {})
    defaults = next(value for value in block["states"] if value.get("default")).get("properties", {})
    expected = defaults | properties
    try:
        return next(value["id"] for value in block["states"] if value.get("properties", {}) == expected)
    except StopIteration as error:
        raise ValueError(f"Unknown block state {state['Name']} {properties}") from error


def decode_items(entries,slots,item_ids,position):
    stacks = [[0,0,0] for _ in range(slots)]
    for item in entries:
        slot = item["Slot"]
        if item.get("components") or not 0 <= slot < slots or stacks[slot][0]:
            raise ValueError(f"Unrepresentable inventory at {position}")
        stacks[slot] = [item_ids[item["id"]],item["count"],0]
    return stacks


def build_snapshot(blocks, records, entities, block_ticks, reports, item_ids, offset, fluid_ticks=()):
    states, edits, assertions, outputs, inventories, pending = {}, [], [], [], [], []
    for position, state in sorted(blocks.items()):
        key = state["Name"], tuple(sorted(state.get("Properties", {}).items()))
        if key not in states:
            states[key] = resolve_state(state,reports)
        world_pos = add(position,offset)
        edits.append([*world_pos,states[key]])
        assertions.append(dict(pos=world_pos,block=state["Name"].removeprefix("minecraft:"),
                               properties=state.get("Properties",{}),state_id=states[key],check=str(world_pos)))
    for position, entry in sorted(records.values(),key=lambda record:record[0]):
        if entry["id"] == "minecraft:comparator" and entry.get("OutputSignal",0):
            outputs.append([*add(position,offset),entry["OutputSignal"]])
        elif entry["id"] in ("minecraft:chest","minecraft:barrel","minecraft:hopper","minecraft:dropper","minecraft:dispenser"):
            is_hopper = entry["id"] == "minecraft:hopper"
            limit = 5 if is_hopper else 9 if entry["id"] in ("minecraft:dropper","minecraft:dispenser") else 27
            stacks = decode_items(entry.get("Items",()),limit,item_ids,position)+[[0,0,0] for _ in range(27-limit)]
            block = reports[blocks[position]["Name"]]
            inventory = dict(position=add(position,offset),type=min(s["id"] for s in block["states"]),
                             items=stacks,burn_left=0,burn_total=0,cook_progress=0,cook_total=200,cook_input=0)
            if is_hopper:
                inventory["transfer_cooldown"] = entry["TransferCooldown"]
            inventories.append(inventory)
    ordered_ticks = sorted(block_ticks,key=lambda record:record[1].get("SubTick",0))
    sequences = {position:index for index,(position,_) in enumerate(ordered_ticks)}
    supported_ticks = {"minecraft:comparator","minecraft:dropper","minecraft:dispenser",
                       "minecraft:repeater","minecraft:observer","minecraft:redstone_lamp"}
    for position, entry in sorted(block_ticks,key=lambda record:record[0]):
        state = blocks.get(position)
        if not state or state["Name"] != entry["Block"] or entry["Block"] not in supported_ticks:
            continue
        if state["Name"] in ("minecraft:observer","minecraft:repeater"):
            properties = state["Properties"]
            state = dict(state, Properties=properties | {"powered":"false" if properties["powered"]=="true" else "true"})
        elif state["Name"] == "minecraft:redstone_lamp":
            state = dict(state, Properties={"lit":"false"})
        pending.append([*add(position,offset),entry["Time"],resolve_state(state,reports),
                        entry["Priority"],sequences[position]])
    water = [[*add(position,offset),entry["Time"]]
             for position,entry in sorted(fluid_ticks,key=lambda record:record[0]) if entry["Fluid"] == "minecraft:water"]
    simulation = dict(version=1,tick=0,time=6000,water=water,devices=pending,mobs=[],players={},
                      comparators=outputs,containers=inventories,redstone_dirty=False,
                      piston_checks=[],shape_updates=[])
    vehicles = []
    vehicle_types = {"minecraft:chest_minecart":(25,27),"minecraft:hopper_minecart":(65,5),"minecraft:oak_boat":(89,0)}
    for position, entity in entities:
        if entity["id"] not in vehicle_types or entity.get("Passengers"):
            continue
        type_id, slots = vehicle_types[entity["id"]]
        vehicles.append(dict(id=len(vehicles)+1,type=type_id,position=add(position,offset),
                             velocity=entity.get("Motion",[0,0,0]),rotation=entity.get("Rotation",[0,0]),
                             portal_cooldown=entity.get("PortalCooldown",0),enabled=bool(entity.get("Enabled",1)),
                             items=decode_items(entity.get("Items",()),slots,item_ids,position)))
    if vehicles:
        simulation["vehicles"]=vehicles
    assertions.append(dict(check_saved={key:simulation[key] for key in
                                        ("comparators","containers","devices","vehicles","water") if key in simulation}))
    return dict(format=2,protocol=774,generator="cuda-craft-flat-v1",seed=0,edits=edits,
                simulation=simulation), assertions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--sha256")
    args = parser.parse_args()
    _,regions = read_fixture(args.fixture,args.sha256)
    blocks, records, entities, ticks, fluid_ticks = merge_regions(regions)
    reports = json.loads((ROOT/".tools/vanilla-1.21.11/reports-1.21.11/reports/blocks.json").read_text(encoding="utf-8"))
    items = json.loads((ROOT/".tools/reference/items.json").read_text(encoding="utf-8"))
    item_ids = {"minecraft:"+item["name"]:item["id"] for item in items}
    document, assertions = build_snapshot(blocks,records,entities,ticks,reports,item_ids,(0,96,0),fluid_ticks)
    with tempfile.TemporaryDirectory(prefix="cuda-fixture-") as folder:
        world = Path(folder)
        (world/"blocks.json").write_text(json.dumps(document,separators=(",",":")),encoding="utf-8")
        subprocess.run([str(args.probe.resolve()),str(world)],input=json.dumps(assertions),
                       encoding="utf-8",check=True,timeout=60)
    simulation = document["simulation"]
    print(f"Native save reopened and matched all {len(blocks)} block states.")
    print(f"Verified {len(simulation['comparators'])} nonzero comparator outputs, "
          f"{len(simulation['containers'])} supported block inventories and "
          f"{len(simulation['devices'])} supported block tick delays after reload.")
    vehicles = len(simulation.get("vehicles",[]))
    print(f"Verified position, motion, rotation, inventory and portal cooldown of {vehicles} saved vehicles.")
    print(f"Structural check only. Unapplied: {len(entities)-vehicles} entity records, "
          f"{len(ticks)-len(simulation['devices'])} other block ticks, "
          f"{len(fluid_ticks)-len(simulation['water'])} unsupported fluid ticks and remaining block-entity data.")


if __name__ == "__main__":
    main()
