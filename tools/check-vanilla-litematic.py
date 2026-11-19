"""Verify structural ingestion of a local Litematica fixture on Vanilla 1.21.11."""
import argparse
from collections import Counter
import gzip
import hashlib
from importlib import import_module
import json
import math
from pathlib import Path
import queue
import subprocess
import tempfile
import threading
import time

from litematic_fixture import add, merge_regions, read_fixture, template_tiles
from litematic_anvil_ticks import chunk_tick_records, restore_ticks

capture = import_module("capture-vanilla-biomes")
terrain = import_module("capture-vanilla-terrain")
ROOT = capture.ROOT


def place_in_vanilla(java, jar, directory, templates, bounds):
    (directory / "eula.txt").write_text("eula=true\n")
    (directory / "server.properties").write_text(
        "server-ip=127.0.0.1\nserver-port=0\nonline-mode=false\nlevel-type=minecraft:flat\n"
        "view-distance=2\nsimulation-distance=2\npause-when-empty-seconds=0\nspawn-monsters=false\n"
    )
    output = queue.Queue()
    with subprocess.Popen([str(java), "-Xmx1G", "-jar", str(jar), "nogui"], cwd=directory,
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, encoding="utf-8", errors="replace") as child:
        def reader():
            for line in child.stdout:
                output.put(line)
            output.put(None)

        def send(command):
            child.stdin.write(command + "\n")
            child.stdin.flush()

        def wait_for(marker, timeout=30):
            deadline = time.monotonic() + timeout
            recent = []
            while time.monotonic() < deadline:
                try:
                    line = output.get(timeout=max(.01, deadline-time.monotonic()))
                except queue.Empty:
                    break
                if line is None:
                    break
                if marker.lower() in line.lower():
                    return
                recent.append(line)
            raise RuntimeError(f"Missing {marker!r}:\n" + "".join(recent[-12:]))

        worker = threading.Thread(target=reader, daemon=True)
        worker.start()
        try:
            wait_for("Done (", 120)
            send("tick freeze")
            wait_for("frozen")
            send(f"forceload add {bounds[0]} {bounds[1]} {bounds[2]} {bounds[3]}")
            wait_for("to be force loaded")
            # Forceload returns before chunk loading finishes. Save until every
            # template's bounding area is available to the place command.
            deadline = time.monotonic()+60
            corners = [(x, z) for x in range(bounds[0]//16,bounds[2]//16+1)
                       for z in range(bounds[1]//16,bounds[3]//16+1)]
            while True:
                send("save-all flush")
                wait_for("Saved the game")
                if all(capture.chunk_from_region(directory/"world",x,z) is not None for x,z in corners):
                    break
                if time.monotonic() > deadline:
                    raise TimeoutError("Template chunks did not load")
            for name, (x,y,z) in templates:
                send(f"place template fixture:{name} {x} {y} {z} none none 1 0 strict")
                wait_for(f'Loaded template "fixture:{name}"')
            send("save-all flush")
            wait_for("Saved the game")
        finally:
            if child.poll() is None:
                send("stop")
                try:
                    child.wait(timeout=35)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=5)
            worker.join(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--java", required=True, type=Path)
    parser.add_argument("--sha256")
    args = parser.parse_args()
    jar = ROOT/".tools/vanilla-1.21.11/server.jar"
    if hashlib.sha1(jar.read_bytes()).hexdigest() != capture.SHA1:
        raise ValueError("Wrong reference server JAR")
    version, regions = read_fixture(args.fixture, args.sha256)
    blocks, block_entities, entities, block_ticks, fluid_ticks = merge_regions(regions)
    offset = (0,96,0)
    expected = {add(pos,offset): terrain.state_key(state) for pos,state in blocks.items()}
    registry = {}
    reference = jar.parent/"reports-1.21.11/reports/blocks.json"
    for name, definition in json.loads(reference.read_text(encoding="utf-8")).items():
        for state in definition["states"]:
            key = terrain.state_key({"Name": name,"Properties": state.get("properties",{})})
            registry[key] = key
    registry[0] = "minecraft:air"
    with tempfile.TemporaryDirectory(prefix="vanilla-litematic-",dir=jar.parent) as temporary:
        directory = Path(temporary)
        pack = directory/"world/datapacks/fixture"
        structures = pack/"data/fixture/structure"
        structures.mkdir(parents=True)
        (pack/"pack.mcmeta").write_text(json.dumps({"pack": {
            "description":"Local fixture verification","min_format":[94,1],"max_format":[94,1],
        }}))
        placements = []
        for i, (position, nbt) in enumerate(template_tiles(blocks,block_entities,entities,version)):
            name = f"tile_{i:03d}"
            (structures/(name+".nbt")).write_bytes(gzip.compress(nbt,mtime=0))
            placements.append((name,add(position,offset)))
        xs, _, zs = zip(*(p for _,p in placements))
        bounds = (min(xs), min(zs), max(xs)+16, max(zs)+16)
        place_in_vanilla(args.java,jar,directory,placements,bounds)
        restore_ticks(directory/"world",block_ticks,fluid_ticks,offset)
        chunks = {}
        loaded_block_entities = {}
        mismatches = []
        for (x,y,z), state in expected.items():
            key = x//16,z//16
            if key not in chunks:
                chunk = capture.chunk_from_region(directory/"world",*key)
                chunks[key] = terrain.states_from_chunk(chunk,registry)
                loaded_block_entities.update(((e["x"],e["y"],e["z"]),e)
                                             for e in chunk.get("block_entities",[]))
            actual = chunks[key][(y+64)*256+(z&15)*16+(x&15)]
            if actual != state:
                mismatches.append(((x,y,z),state,actual))
        failed_records = []
        for position, record in block_entities.values():
            world_pos = add(position,offset)
            loaded = loaded_block_entities.get(world_pos,{})
            keys = ("id", "OutputSignal", "TransferCooldown", "Items")
            for key in keys:
                if key in record and loaded.get(key) != record[key]:
                    failed_records.append((world_pos,key))
        chunks_with_entities = {(math.floor((p[0]+offset[0])/16),math.floor((p[2]+offset[2])/16)) for p,_ in entities}
        loaded_entities = Counter()
        for cx,cz in chunks_with_entities:
            data = capture.chunk_from_region(directory/"world",cx,cz,"entities")
            loaded_entities.update((entity["id"],tuple(entity["Pos"])) for entity in (data or {}).get("Entities",[]))
        missing_entities = Counter((entity["id"],add(p,offset)) for p,entity in entities)-loaded_entities
        print(f"{len(placements)} Vanilla tiles placed. {len(expected)-len(mismatches)}/{len(expected)} block states matched.")
        print(f"{len(block_entities)-len({position for position,_ in failed_records})}/{len(block_entities)} block-entity records matched.")
        print(f"{len(entities)-sum(missing_entities.values())}/{len(entities)} entity types and positions matched.")
        unmatched_ticks = []
        for field, kind, records in (("block_ticks", "Block", block_ticks), ("fluid_ticks", "Fluid", fluid_ticks)):
            for (cx,cz), wanted in chunk_tick_records(records,offset,kind).items():
                chunk = capture.chunk_from_region(directory/"world",cx,cz)
                actual = chunk.get(field,[])
                positions = {(tick["i"],tick["x"],tick["y"],tick["z"]) for tick in wanted}
                matching = [tick for tick in actual if (tick["i"],tick["x"],tick["y"],tick["z"]) in positions]
                if matching != wanted:
                    unmatched_ticks.append((cx,cz,field))
        print(f"Restored {len(block_ticks)} block ticks and {len(fluid_ticks)} fluid ticks into the reference save.")
        for mismatch in mismatches[:12]:
            print("Block state mismatch",*mismatch)
        for position,key in failed_records[:12]:
            print("Block-entity field mismatch",position,key)
        for key,count in missing_entities.items():
            print("Entity missing from saved chunk",*key,count)
        for cx,cz,field in unmatched_ticks:
            print("Scheduled tick mismatch",cx,cz,field)
        return int(bool(mismatches or failed_records or missing_entities or unmatched_ticks))


if __name__ == "__main__":
    raise SystemExit(main())
