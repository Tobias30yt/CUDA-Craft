"""Check observer or copper bulb fixtures on the local official 1.21.11 server."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import queue
import re
import subprocess
import tempfile
import threading
import time

from importlib import import_module

reference = import_module("capture-vanilla-biomes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--java", required=True)
    parser.add_argument("--copper-bulb", action="store_true")
    parser.add_argument("--fixture", type=Path, help="Run shared native/vanilla fixture actions")
    args = parser.parse_args()
    jar = reference.ROOT / ".tools/vanilla-1.21.11/server.jar"
    if hashlib.sha1(jar.read_bytes()).hexdigest() != reference.SHA1:
        raise ValueError("Wrong reference server JAR")
    with tempfile.TemporaryDirectory(prefix="observer-reference-", dir=jar.parent) as directory:
        folder = Path(directory)
        (folder / "eula.txt").write_text("eula=true\n")
        (folder / "server.properties").write_text(
            "server-ip=127.0.0.1\nserver-port=0\nonline-mode=false\n"
            "level-type=minecraft:flat\nview-distance=2\nsimulation-distance=2\n"
            "pause-when-empty-seconds=0\n"
        )
        with subprocess.Popen(
            [args.java, "-Xmx1G", "-jar", str(jar), "nogui"], cwd=folder,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace",
        ) as server:
            lines = queue.Queue()

            def read():
                for line in server.stdout:
                    lines.put(line)
                lines.put(None)

            def wait_for(marker, timeout=20):
                deadline = time.monotonic() + timeout
                recent = []
                while time.monotonic() < deadline:
                    try:
                        line = lines.get(timeout=max(.01, deadline - time.monotonic()))
                    except queue.Empty:
                        break
                    if line is None:
                        break
                    if marker.lower() in line.lower():
                        return line
                    recent.append(line)
                raise RuntimeError(f"Missing server response {marker!r}:\n" + "".join(recent[-15:]))

            def send(command):
                server.stdin.write(command + "\n")
                server.stdin.flush()

            def command(instruction, marker):
                send(instruction)
                return wait_for(marker)

            def expect(position, state, label):
                command(f"execute if block {position} {state} run say {label}", f"[Server] {label}")
                print(label, flush=True)

            def place(position, block):
                command(f"setblock {position} {block}", "Changed the block")

            def step(ticks):
                def game_time():
                    line = command("time query gametime", "The time is ")
                    return int(re.search(r"The time is (\d+)", line).group(1))

                target = game_time() + ticks
                command(f"tick step {ticks}", "Stepping")
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    observed = game_time()
                    if observed == target:
                        return
                    if observed > target:
                        raise RuntimeError(f"Tick step overshot {target}, got {observed}")
                    time.sleep(.05)
                raise RuntimeError(f"Tick step did not reach {target}")

            def bulb_fixture():
                place("16 64 0", "waxed_copper_bulb")
                place("17 63 0", "stone")
                place("18 63 0", "stone")
                place("17 64 0", "comparator[facing=west]")
                place("18 64 0", "redstone_wire")
                place("15 64 0", "redstone_block")
                expect("16 64 0", "waxed_copper_bulb[lit=true,powered=true]", "rise lights bulb")
                step(8)
                expect("16 64 0", "waxed_copper_bulb[lit=true,powered=true]", "held signal preserves light")
                expect("18 64 0", "redstone_wire[power=15]", "comparator reads 15 from lit bulb")
                place("15 64 0", "air")
                expect("16 64 0", "waxed_copper_bulb[lit=true,powered=false]", "fall preserves light")
                step(4)
                expect("18 64 0", "redstone_wire[power=15]", "comparator still reads 15 after power loss")
                place("15 64 0", "redstone_block")
                expect("16 64 0", "waxed_copper_bulb[lit=false,powered=true]", "second rise darkens bulb")
                step(4)
                expect("18 64 0", "redstone_wire[power=0]", "comparator reads 0 from dark bulb")

            def shared_fixture(actions):
                for action in actions:
                    if "next_tick_multiple" in action:
                        multiple=action["next_tick_multiple"]
                        line=command("time query gametime","The time is ")
                        current=int(re.search(r"The time is (\d+)",line).group(1))
                        step(multiple-current%multiple)
                        continue
                    if "ticks" in action:
                        step(action["ticks"])
                        continue
                    if "time" in action:
                        value=action["time"]
                        command(f"time set {value}",f"Set the time to {value}")
                        continue
                    if "remove_vehicle" in action:
                        command(f'kill @e[tag={action["remove_vehicle"]},limit=1]',"Killed")
                        continue
                    position = " ".join(map(str, action["pos"]))
                    if "item_probe" in action:
                        key = action["item_probe"]
                        if "check" not in action:
                            motion = ",".join(f"{value}d" for value in action["velocity"])
                            command(f'summon item {position} {{Tags:["{key}"],Motion:[{motion}],'
                                    'Item:{id:"minecraft:stone",count:1}}',"Summoned new")
                        else:
                            label = action["check"]
                            for field,expected in (("Pos",action["pos"]),("Motion",action["velocity"])):
                                response = command(f"data get entity @e[tag={key},limit=1] {field}","has the following entity data:")
                                raw = response.rsplit("[",1)[1].split("]",1)[0]
                                actual = [float(value.strip().rstrip("dDfF")) for value in raw.split(",")]
                                if len(actual)!=3 or any(abs(a-b)>1e-8 for a,b in zip(actual,expected)):
                                    raise AssertionError(f"{label} {field}: expected {expected}, got {actual}")
                            print(label,flush=True)
                        continue
                    if "spawn_resting_item" in action:
                        command(f'summon item {position} {{Tags:["fixture_resting_item"],OnGround:1b,'
                                'Motion:[0d,0d,0d],Item:{id:"minecraft:stone",count:1}}',"Summoned new")
                        continue
                    if "falling_item_count" in action:
                        result = command("execute if entity @e[type=item,tag=fixture_resting_item,nbt={OnGround:0b}]",
                                         "Test passed. Count:")
                        observed = int(result.rsplit(":",1)[1])
                        if observed!=action["falling_item_count"]:
                            raise AssertionError(f"{action['check']}: found {observed} falling items")
                        print(action["check"],flush=True)
                        continue
                    if "tnt" in action:
                        key = action["tnt"]
                        selector=f"@e[type=minecraft:tnt,tag={key},limit=1]"
                        if action.get("remove"):
                            command(f"kill {selector}","Killed")
                            continue
                        if "check" not in action:
                            motion=",".join(f"{value}d" for value in action["velocity"])
                            command(f'summon tnt {position} {{Tags:["{key}"],Motion:[{motion}],fuse:{action["fuse"]}s}}',
                                    "Summoned new")
                            continue
                        label=action["check"]
                        if "fuse" not in action:
                            command(f"execute unless entity {selector} run say {label}",f"[Server] {label}")
                        else:
                            for field,expected in (("Pos",action["pos"]),("Motion",action["velocity"])):
                                response=command(f"data get entity {selector} {field}","has the following entity data:")
                                raw=response.rsplit("[",1)[1].split("]",1)[0]
                                actual=[float(value.strip().rstrip("dDfF")) for value in raw.split(",")]
                                if len(actual)!=3 or any(abs(a-b)>1e-8 for a,b in zip(actual,expected)):
                                    raise AssertionError(f"{label} {field}: expected {expected}, got {actual}")
                            response=command(f"data get entity {selector} fuse","has the following entity data:")
                            fuse=int(response.rsplit(": ",1)[1].strip().rstrip("sS"))
                            if fuse!=action["fuse"]:
                                raise AssertionError(f"{label}: expected fuse {action['fuse']}, got {fuse}")
                        print(label,flush=True)
                        continue
                    if "vehicle_present" in action:
                        mode = "if" if action["expected"] else "unless"
                        kind = action["vehicle_present"]
                        label = action["check"]
                        command(f"execute positioned {position} {mode} entity @e[type=minecraft:{kind},distance=..1.5] run say {label}",
                                f"[Server] {label}")
                        print(label,flush=True)
                        continue
                    if "vehicle" in action:
                        key = action["vehicle"]
                        if "vehicle_item" in action:
                            item=action["vehicle_item"]
                            label=action["check"]
                            command(f'execute if data entity @e[tag={key},limit=1] '
                                    f'Items[{{Slot:0b,id:"minecraft:{item}",count:1}}] run say {label}',f"[Server] {label}")
                            print(label,flush=True)
                        elif "check" in action:
                            for field, expected in (("Pos",action["pos"]),("Motion",action["velocity"])):
                                response = command(f"data get entity @e[tag={key},limit=1] {field}",
                                                   "has the following entity data:")
                                raw = response.rsplit("[",1)[1].split("]",1)[0]
                                actual = [float(value.strip().rstrip("dDfF")) for value in raw.split(",")]
                                if len(actual)!=3 or any(abs(a-b)>1e-8 for a,b in zip(actual,expected)):
                                    raise AssertionError(f"{action['check']} {field}: expected {expected}, got {actual}")
                            print(action["check"],flush=True)
                        else:
                            motion = ",".join(f"{value}d" for value in action["velocity"])
                            command(f'summon {action["kind"]} {position} {{Tags:["{key}"],Motion:[{motion}]}}',
                                    "Summoned new")
                        continue
                    if "inventory" in action:
                        item = action["inventory"]
                        if "check" in action:
                            slot = f"Slot:{item['slot']}b"
                            count = item["count"]
                            match = slot + (f',id:"minecraft:{item["item"]}",count:{count}' if count else "")
                            mode = "if" if count else "unless"
                            label = action["check"]
                            command(f"execute {mode} data block {position} Items[{{{match}}}] run say {label}",
                                    f"[Server] {label}")
                            print(label, flush=True)
                            continue
                        stack = item["item"] if item["count"] else "air"
                        count = f" {item['count']}" if item["count"] else ""
                        command(f"item replace block {position} container.{item['slot']} with {stack}{count}",
                                "Replaced a slot")
                        continue
                    properties = action.get("properties", {})
                    block = action["block"]
                    if properties:
                        block += "[" + ",".join(f"{key}={value}" for key, value in properties.items()) + "]"
                    if "check" in action:
                        expect(position, block, action["check"])
                    else:
                        place(position, block)

            worker = threading.Thread(target=read, daemon=True)
            worker.start()
            try:
                wait_for("Done (", 120)
                if args.fixture:
                    actions = json.loads(args.fixture.read_text(encoding="utf-8"))
                    positions = [action["pos"] for action in actions if "pos" in action]
                    xs, _, zs = zip(*positions)
                    bounds=tuple(map(math.floor,(min(xs),min(zs),max(xs),max(zs))))
                    command("tick freeze", "frozen")
                    command("forceload add " + " ".join(map(str,bounds)), "to be force loaded")
                    chunks = [(x,z) for x in range(bounds[0]//16,bounds[2]//16+1)
                              for z in range(bounds[1]//16,bounds[3]//16+1)]
                    deadline = time.monotonic()+60
                    while True:
                        command("save-all flush","Saved the game")
                        if all((reference.chunk_from_region(folder/"world",x,z) or {}).get("Status")=="minecraft:full"
                               for x,z in chunks):
                            break
                        if time.monotonic()>deadline:
                            raise TimeoutError("Reference fixture chunks did not finish loading")
                    shared_fixture(actions)
                    return
                command("forceload add 0 0 31 15", "to be force loaded")
                command("tick freeze", "frozen")
                if args.copper_bulb:
                    bulb_fixture()
                    return
                for position, block in [
                    ("15 64 0", "observer[facing=east,powered=false]"),
                    ("14 64 0", "redstone_lamp"),
                    ("15 64 1", "redstone_lamp"),
                    ("16 64 0", "stone"),
                ]:
                    place(position, block)
                for tick, powered in enumerate(("false", "true", "true", "false"), 1):
                    step(1)
                    expect("15 64 0", f"observer[powered={powered}]", f"tick {tick}: powered={powered}")
                    if tick == 2:
                        expect("14 64 0", "redstone_lamp[lit=true]", "rear lamp on")
                        expect("15 64 1", "redstone_lamp[lit=false]", "side lamp off")
            finally:
                if server.poll() is None:
                    send("stop")
                    try:
                        server.wait(timeout=35)
                    except subprocess.TimeoutExpired:
                        server.kill()
                        server.wait(timeout=5)
                worker.join(timeout=5)


if __name__ == "__main__":
    main()
