"""Run the same tick-by-tick block fixtures on the native and official servers."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def place(pos, block, **properties):
    return dict(pos=pos, block=block, properties=properties)


def check(label, pos, block, **properties):
    return dict(place(pos, block, **properties), check=label)


def step(ticks=1):
    return dict(ticks=ticks)


def floor(*positions):
    return [place((x, y - 1, z), "stone") for x, y, z in positions]


def bulb_actions(kind):
    bulb, signal = (16, 80, 0), (15, 80, 0)
    return [
        place(bulb, kind), place(signal, "redstone_block"),
        check("first rise", bulb, kind, lit="true", powered="true"),
        step(4), check("held input", bulb, kind, lit="true", powered="true"),
        place(signal, "air"), check("fall keeps light", bulb, kind, lit="true", powered="false"),
        place(signal, "redstone_block"),
        check("second rise", bulb, kind, lit="false", powered="true"),
        place(signal, "air"), check("dark after fall", bulb, kind, lit="false", powered="false"),
    ]


def observer_actions():
    observer, source, rear, side = (15, 80, 0), (16, 80, 0), (14, 80, 0), (15, 80, 1)
    return [
        place(observer, "observer", facing="east"), place(rear, "redstone_lamp"),
        place(side, "redstone_lamp"), step(4),
        check("side and rear changes ignored", observer, "observer", powered="false"),
        place(source, "stone"), step(),
        check("first tick", observer, "observer", powered="false"), step(),
        check("second tick", observer, "observer", powered="true"),
        check("rear lights", rear, "redstone_lamp", lit="true"),
        check("side remains dark", side, "redstone_lamp", lit="false"), step(),
        check("third tick", observer, "observer", powered="true"), step(),
        check("fourth tick", observer, "observer", powered="false"),
    ]


def comparator_bulb_actions():
    source, bulb, device, output = [(x, 80, 0) for x in range(15, 19)]
    return [
        *floor(device, output), place(bulb, "waxed_copper_bulb"),
        place(device, "comparator", facing="west"), place(output, "redstone_wire"),
        step(4), place(source, "redstone_block"), step(),
        check("no output one tick after input", output, "redstone_wire", power="0"),
        step(), check("lit bulb output after two ticks", output, "redstone_wire", power="15"),
        place(source, "air"), step(4),
        check("signal persists after input removal", output, "redstone_wire", power="15"),
        place(source, "redstone_block"), step(),
        check("old output held for one tick", output, "redstone_wire", power="15"),
        step(), check("dark bulb output after two ticks", output, "redstone_wire", power="0"),
    ]


def comparator_mode_actions(mode):
    rear, device, out, side, side_source = (15, 80, 0), (16, 80, 0), (17, 80, 0), (16, 80, 1), (16, 80, 2)
    return [
        *floor(device, out, side), place(rear, "composter", level="7"),
        place(device, "comparator", facing="west", mode=mode), place(out, "redstone_wire"),
        place(side, "redstone_wire"), step(6),
        check("middle-strength analog input", out, "redstone_wire", power="7"),
        place(side_source, "redstone_block"), step(6),
        check("stronger side suppresses output", out, "redstone_wire", power="0"),
        place(side_source, "air"), step(6),
        check("removing side restores output", out, "redstone_wire", power="7"),
    ]


def comparator_unequal_actions(mode):
    rear, device, out, side, second, source = (15,80,0), (16,80,0), (17,80,0), (16,80,1), (16,80,2), (16,80,3)
    return [
        *floor(device, out, side, second),
        place(rear, "redstone_block"), place(device, "comparator", facing="west", mode=mode),
        place(out, "redstone_wire"), place(side, "redstone_wire"), place(second, "redstone_wire"),
        place(source, "redstone_block"), step(6),
        check("side input is fourteen", side, "redstone_wire", power="14"),
        check("weaker side gives mode-specific output", out, "redstone_wire", power="15" if mode=="compare" else "1"),
    ]


def inventory_edit(position, slot, item, count):
    return dict(pos=position,inventory=dict(slot=slot,item=item,count=count))


def comparator_inventory_actions():
    barrel, conductor, device, out = [(x,80,0) for x in range(14,18)]
    return [
        *floor(device,out), place(barrel,"barrel"), place(conductor,"stone"),
        place(device,"comparator",facing="west"), place(out,"redstone_wire"),
        step(3), check("empty barrel emits zero",out,"redstone_wire",power="0"),
        inventory_edit(barrel,0,"stone",1), step(),
        check("inventory change waits a second tick",out,"redstone_wire",power="0"), step(),
        check("reads barrel through one solid block",out,"redstone_wire",power="1"),
        *[inventory_edit(barrel,slot,"stone",64) for slot in range(14)], step(2),
        check("fourteen full slots emit eight",out,"redstone_wire",power="8"),
        *[inventory_edit(barrel,slot,"air",0) for slot in range(14)], step(2),
        check("emptying inventory clears output",out,"redstone_wire",power="0"),
    ]


def hopper_transfer_actions():
    source, hopper, target = (16,81,0),(16,80,0),(16,79,0)
    signal = (15,80,0)

    def slot(label, position, count):
        return dict(inventory_edit(position,0,"stone",count),check=label)

    return [
        place(source,"barrel"),place(hopper,"hopper",facing="down"),place(target,"barrel"),
        place(signal,"redstone_block"),inventory_edit(source,0,"stone",2), step(3),
        check("powered hopper locks",hopper,"hopper",enabled="false"),
        slot("locked hopper leaves both items",source,2),
        place(signal,"air"),step(),
        slot("unlocked hopper pulls one item",hopper,1),
        slot("source retains one item",source,1),
        step(7),slot("target stays empty during cooldown",target,0),
        step(),slot("pushes after eight ticks",target,1),
        slot("push and pull can occur on the same tick",hopper,1),
        slot("source is now empty",source,0),
        step(8),slot("target receives final item",target,2),
        slot("hopper is empty after final push",hopper,0),
    ]


def dropper_transfer_actions():
    power, dropper, target = (15,80,0),(16,80,0),(17,80,0)

    def slot(label, position, count):
        return dict(inventory_edit(position,0,"stone",count),check=label)

    return [
        place(dropper,"dropper",facing="east"),place(target,"barrel"),
        inventory_edit(dropper,0,"stone",3), place(power,"redstone_block"),
        step(3),slot("dropper waits four ticks",target,0),
        step(),slot("first rising edge transfers one",target,1),
        step(8),slot("held input does not repeat",target,1),
        place(power,"air"),place(power,"redstone_block"),place(power,"air"),
        step(3),slot("brief pulse still waits until due",target,1),
        step(),slot("brief pulse transfers exactly one",target,2),
        slot("one item remains in dropper",dropper,1),
    ]


def piston_actions(sticky):
    signal, piston, front, moved = [(x, 80, 0) for x in range(14, 18)]
    kind = "sticky_piston" if sticky else "piston"
    return [
        place(piston, kind, facing="east"), place(front, "stone"),
        place(signal, "redstone_block"), step(8),
        check("extends across chunk edge", piston, kind, extended="true"),
        check("head occupies former stone cell", front, "piston_head", facing="east"),
        check("pushes stone", moved, "stone"),
        place(signal, "air"), step(8),
        check("retracts", piston, kind, extended="false"),
        check("front after retraction", front, "stone" if sticky else "air"),
        check("moved cell after retraction", moved, "air" if sticky else "stone"),
    ]


def piston_limit_actions(length):
    piston, signal = (1,80,0), (0,80,0)
    actions = [
        place(piston, "piston", facing="east"),
        *[place((x,80,0), "stone") for x in range(2, 2+length)],
        place(signal, "redstone_block"), step(8),
        check("push limit controls extension", piston, "piston", extended="true" if length==12 else "false"),
        check("last cell changes only for legal push", (2+length,80,0), "stone" if length==12 else "air"),
    ]
    if length == 13:
        # A server that never moves pistons must also fail the negative case.
        actions += [
            place(signal, "air"), place((14,80,0), "air"),
            place(signal, "redstone_block"), step(8),
            check("same piston moves after removing thirteenth block", piston, "piston", extended="true"),
            check("remaining twelve blocks move", (14,80,0), "stone"),
        ]
    return actions


def piston_adhesion_actions(material, neighbor, sticks):
    piston, signal, front = (15,80,0), (14,80,0), (16,80,0)
    side, shifted = (16,80,1), (17,80,1)
    return [
        place(piston, "piston", facing="east"), place(front, material), place(side, neighbor),
        place(signal, "redstone_block"), step(8),
        check("moves adhesive", (17,80,0), material),
        check("original neighbor cell", side, "air" if sticks else neighbor),
        check("destination neighbor cell", shifted, neighbor if sticks else "air"),
    ]


def piston_quasi_connectivity_actions():
    piston, face, source, update = (15,80,0), (16,80,0), (14,81,0), (15,80,1)
    return [
        place(piston, "piston", facing="east"), place(face, "stone"), step(4),
        place(source, "redstone_block"), step(4),
        check("diagonal power alone does not cause update", piston, "piston", extended="false"),
        place(update, "stone"), step(8),
        check("neighbor update reveals quasi-power", piston, "piston", extended="true"),
        check("push occurs after neighbor update", (17,80,0), "stone"),
    ]


def repeater_short_pulse_actions():
    signal, device, out = (15,80,0), (16,80,0), (17,80,0)
    return [
        *floor(device, out), place(device, "repeater", facing="west", delay="2"),
        place(out, "redstone_wire"), step(4), place(signal, "redstone_block"),
        step(), place(signal, "air"), step(2),
        check("output waits full four ticks", out, "redstone_wire", power="0"), step(),
        check("short pulse still reaches output", out, "redstone_wire", power="15"),
        step(3), check("short pulse is stretched to four ticks", out, "redstone_wire", power="15"),
        step(), check("output clears after stretch", out, "redstone_wire", power="0"),
    ]


CASES = {"observer": observer_actions()}
for oxidation in ("", "exposed_", "weathered_", "oxidized_"):
    for wax in ("", "waxed_"):
        name = f"{wax}{oxidation}copper_bulb"
        CASES[name] = bulb_actions(name)
SMOKE = tuple(CASES)
CASES.update({
    "comparator_bulb": comparator_bulb_actions(),
    "comparator_compare": comparator_mode_actions("compare"),
    "comparator_subtract": comparator_mode_actions("subtract"),
    "comparator_weaker_side_compare": comparator_unequal_actions("compare"),
    "comparator_weaker_side_subtract": comparator_unequal_actions("subtract"),
    "comparator_inventory": comparator_inventory_actions(),
    "hopper_transfer": hopper_transfer_actions(),
    "dropper_transfer": dropper_transfer_actions(),
    "piston_push": piston_actions(False),
    "sticky_piston_pull": piston_actions(True),
    "piston_limit_12": piston_limit_actions(12),
    "piston_limit_13": piston_limit_actions(13),
    "piston_slime_adhesion": piston_adhesion_actions("slime_block", "stone", True),
    "piston_honey_adhesion": piston_adhesion_actions("honey_block", "stone", True),
    "piston_slime_honey_separation": piston_adhesion_actions("slime_block", "honey_block", False),
    "piston_quasi_connectivity": piston_quasi_connectivity_actions(),
    "repeater_short_pulse": repeater_short_pulse_actions(),
})


def vanilla_actions(names):
    # Offset by whole chunks to preserve the same boundary crossings.
    for i, name in enumerate(names):
        for action in CASES[name]:
            if "pos" not in action:
                yield action
                continue
            x, y, z = action["pos"]
            moved = dict(action, pos=(x + 32 * (i % 4), y, z + 32 * (i // 4)))
            if "check" in moved:
                moved["check"] = name + " / " + moved["check"]
            yield moved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", nargs="?", type=Path)
    parser.add_argument("--case", action="append", choices=CASES)
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--java", type=Path, help="Run selected fixtures on the official server")
    args = parser.parse_args()
    if not args.probe and not args.java:
        parser.error("Supply a native probe or --java")
    names = args.case or (tuple(CASES) if args.all else SMOKE)
    vanilla_failed = False
    if args.java:
        with tempfile.TemporaryDirectory(prefix="cuda-redstone-fixture-") as folder:
            path = Path(folder) / "fixture.json"
            path.write_text(json.dumps(list(vanilla_actions(names))), encoding="utf-8")
            reference = Path(__file__).resolve().parents[1] / "tools/check-vanilla-observer.py"
            vanilla = subprocess.run([sys.executable, str(reference), "--java", str(args.java),
                                      "--fixture", str(path)], timeout=180)
            vanilla_failed = bool(vanilla.returncode)
            if not vanilla_failed:
                print(f"{len(names)}/{len(names)} vanilla fixtures passed", flush=True)
    failures = []
    if args.probe:
        for name in names:
            result = subprocess.run([str(args.probe.resolve())], input=json.dumps(CASES[name]),
                                    text=True, capture_output=True, timeout=10)
            if result.returncode:
                failures.append(name)
                print(f"FAIL {name}\n{result.stderr.rstrip()}", flush=True)
            else:
                print(f"PASS {name}", flush=True)
        print(f"{len(names)-len(failures)}/{len(names)} native fixtures passed")
    return int(vanilla_failed or bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
