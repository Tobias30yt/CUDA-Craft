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


def build_snapshot(blocks, records, block_ticks, reports, item_ids, offset):
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
        elif entry["id"] in ("minecraft:chest","minecraft:barrel","minecraft:hopper","minecraft:dropper"):
            is_hopper = entry["id"] == "minecraft:hopper"
            limit = 5 if is_hopper else 9 if entry["id"] == "minecraft:dropper" else 27
            stacks = [[0,0,0] for _ in range(27)]
            for item in entry.get("Items",()):
                slot = item["Slot"]
                if item.get("components") or not 0 <= slot < limit or stacks[slot][0]:
                    raise ValueError(f"Unrepresentable inventory at {position}")
                stacks[slot] = [item_ids[item["id"]],item["count"],0]
            block = reports[blocks[position]["Name"]]
            inventory = dict(position=add(position,offset),type=min(s["id"] for s in block["states"]),
                             items=stacks,burn_left=0,burn_total=0,cook_progress=0,cook_total=200,cook_input=0)
            if is_hopper:
                inventory["transfer_cooldown"] = entry["TransferCooldown"]
            inventories.append(inventory)
    for position, entry in sorted(block_ticks,key=lambda record:record[0]):
        state = blocks.get(position)
        if entry["Block"] in ("minecraft:comparator","minecraft:dropper") and state and state["Name"] == entry["Block"]:
            pending.append([*add(position,offset),entry["Time"],resolve_state(state,reports)])
    simulation = dict(version=1,tick=0,time=6000,water=[],devices=pending,mobs=[],players={},
                      comparators=outputs,containers=inventories)
    assertions.append(dict(check_saved={key:simulation[key] for key in ("comparators","containers","devices")}))
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
    document, assertions = build_snapshot(blocks,records,ticks,reports,item_ids,(0,96,0))
    with tempfile.TemporaryDirectory(prefix="cuda-fixture-") as folder:
        world = Path(folder)
        (world/"blocks.json").write_text(json.dumps(document,separators=(",",":")),encoding="utf-8")
        subprocess.run([str(args.probe.resolve()),str(world)],input=json.dumps(assertions),
                       encoding="utf-8",check=True,timeout=60)
    simulation = document["simulation"]
    print(f"Native save reopened and matched all {len(blocks)} block states.")
    print(f"Verified {len(simulation['comparators'])} nonzero comparator outputs, "
          f"{len(simulation['containers'])} chest/barrel/hopper/dropper inventories and "
          f"{len(simulation['devices'])} supported block tick delays after reload.")
    print(f"Structural check only. Unapplied: {len(entities)} entity records, "
          f"{len(ticks)-len(simulation['devices'])} other block ticks, {len(fluid_ticks)} fluid ticks "
          "and remaining block-entity data. Native tick priorities are not represented.")


if __name__ == "__main__":
    main()
