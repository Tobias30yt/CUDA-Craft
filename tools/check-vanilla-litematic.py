"""Verify structural ingestion of a local Litematica fixture on Vanilla 1.21.11."""
import argparse
from collections import Counter
from contextlib import contextmanager
import gzip
import hashlib
from importlib import import_module
import json
import math
from pathlib import Path
import queue
import re
import subprocess
import tempfile
import threading
import time

from litematic_fixture import add, merge_regions, read_fixture, template_tiles
from litematic_anvil_ticks import chunk_tick_records, restore_ticks

capture = import_module("capture-vanilla-biomes")
terrain = import_module("capture-vanilla-terrain")
ROOT = capture.ROOT


@contextmanager
def vanilla_console(java, jar, directory, *, freeze_at_boot=False, buttons=()):
    (directory / "eula.txt").write_text("eula=true\n")
    settings = {
        "biome":"minecraft:plains","structure_overrides":[],
        "layers":[{"block":f"minecraft:{name}","height":height} for name,height in
                  (("bedrock",1),("stone",124),("dirt",2),("grass_block",1))],
    }
    (directory / "server.properties").write_text(
        "server-ip=127.0.0.1\nserver-port=0\nonline-mode=false\nlevel-type=minecraft:flat\n"
        "view-distance=2\nsimulation-distance=2\npause-when-empty-seconds=0\nspawn-monsters=false\n"
        "level-seed=0\nfunction-permission-level=4\ngenerate-structures=false\n"
        f"generator-settings={json.dumps(settings,separators=(',',':'))}\n"
    )
    output = queue.Queue()
    debugger_ports = queue.Queue()
    launch = [str(java), "-Xmx1G"]
    if freeze_at_boot:
        launch.append("-agentlib:jdwp=transport=dt_socket,server=y,suspend=y,address=127.0.0.1:0")
    with subprocess.Popen(launch+["-jar", str(jar), "nogui"], cwd=directory,
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, encoding="utf-8", errors="replace") as child:
        def reader():
            for line in child.stdout:
                if line.startswith("Listening for transport dt_socket at address:"):
                    debugger_ports.put(int(line.rsplit(":",1)[1].strip()))
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
                    return line
                recent.append(line)
            raise RuntimeError(f"Missing {marker!r}:\n" + "".join(recent[-12:]))

        worker = threading.Thread(target=reader, daemon=True)
        worker.start()

        def press_now(positions):
            try:
                port = debugger_ports.get(timeout=15)
            except queue.Empty as error:
                raise RuntimeError("The local reference debugger did not start listening") from error
            helper = Path(__file__).with_name("FreezeVanilla.java").resolve()
            arguments = [str(java),"-Xmx128M",str(helper),str(port)]
            for position in positions:
                arguments.extend(map(str,position))
            subprocess.run(arguments,cwd=directory,check=True,timeout=100)

        try:
            if freeze_at_boot:
                press_now(buttons)
            wait_for("Done (", 120)

            def command(instruction, marker):
                send(instruction)
                return wait_for(marker)

            yield command,press_now
        finally:
            if child.poll() is None:
                send("stop")
                try:
                    child.wait(timeout=35)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=5)
            worker.join(timeout=5)


def step_to(command, target):
    def clock():
        response = command("time query gametime", "The time is ")
        return int(re.search(r"The time is (\d+)", response).group(1))

    remaining = target - clock()
    if remaining <= 0:
        raise ValueError(f"Step target {target} must be after the current tick")
    command(f"tick step {remaining}", "Stepping")
    deadline = time.monotonic() + max(15,remaining/20+5)
    while time.monotonic() < deadline:
        observed = clock()
        if observed == target:
            return
        if observed > target:
            raise RuntimeError(f"Tick step overshot {target}, got {observed}")
        time.sleep(.05)
    raise TimeoutError(f"Tick step did not reach {target}")


def place_in_vanilla(java, jar, directory, placements, exact_clock, observe):
    with vanilla_console(java,jar,directory,freeze_at_boot=exact_clock) as (command,_):
        command("tick freeze", "frozen")
        command("time set 6000", "Set the time to 6000")
        for dimension,templates in placements.items():
            prefix=f"execute in minecraft:{dimension} run "
            xs, _, zs = zip(*(p for _,p in templates))
            bounds = (min(xs),min(zs),max(xs)+16,max(zs)+16)
            command(prefix+"forceload add " + " ".join(map(str,bounds)), "to be force loaded")
            # Forceload returns before chunk loading finishes.
            deadline = time.monotonic()+60
            corners = [(x,z) for x in range(bounds[0]//16,bounds[2]//16+1)
                       for z in range(bounds[1]//16,bounds[3]//16+1)]
            if dimension == "overworld":
                observed_chunks = {(x//16,z//16) for x,_,z in observe}
                for cx,cz in sorted(observed_chunks-set(corners)):
                    command(prefix+f"forceload add {cx*16} {cz*16}","to be force loaded")
                    corners.append((cx,cz))
            dimension_save = directory/"world"
            if dimension == "the_nether":
                dimension_save /= "DIM-1"
            while True:
                command("save-all flush", "Saved the game")
                if all(capture.chunk_from_region(dimension_save,x,z) is not None for x,z in corners):
                    break
                if time.monotonic() > deadline:
                    raise TimeoutError(f"Template chunks did not load in {dimension}")
            for name,(x,y,z) in templates:
                command(prefix+f"place template fixture:{name} {x} {y} {z} none none 1 0 strict",
                        f'Loaded template "fixture:{name}"')
        command("save-all flush", "Saved the game")


def stage_fixture(java, jar, directory, merged, version, offset, nether=None, panel=None, *,
                  exact_clock=False, observe=()):
    pack = directory/"world/datapacks/fixture"
    structures = pack/"data/fixture/structure"
    structures.mkdir(parents=True)
    (pack/"pack.mcmeta").write_text(json.dumps({"pack": {
        "description":"Local fixture verification","min_format":[94,1],"max_format":[94,1],
    }}))
    def stage_tiles(prefix, records, data_version, origin):
        blocks,block_entities,entities,_,_ = records
        tiles = []
        for i,(position,nbt) in enumerate(template_tiles(blocks,block_entities,entities,data_version)):
            name = f"{prefix}_{i:03d}"
            (structures/(name+".nbt")).write_bytes(gzip.compress(nbt,mtime=0))
            tiles.append((name,add(position,origin)))
        return tiles

    placements = {"overworld":stage_tiles("tile",merged,version,offset)}
    if panel:
        panel_records, panel_version, panel_origin = panel
        placements["overworld"].extend(stage_tiles("panel",panel_records,panel_version,panel_origin))
    if nether:
        nether_records, nether_version, nether_origin = nether
        placements["the_nether"] = stage_tiles("nether",nether_records,nether_version,nether_origin)
    place_in_vanilla(java,jar,directory,placements,exact_clock,observe)
    _,_,_,block_ticks,fluid_ticks = merged
    restore_ticks(directory/"world",block_ticks,fluid_ticks,offset)
    if nether:
        _,_,_,block_ticks,fluid_ticks = nether_records
        restore_ticks(directory/"world/DIM-1",block_ticks,fluid_ticks,nether_origin)
    if panel:
        _,_,_,block_ticks,fluid_ticks = panel_records
        restore_ticks(directory/"world",block_ticks,fluid_ticks,panel_origin)
    return sum(map(len,placements.values()))


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
    merged = merge_regions(regions)
    blocks, block_entities, entities, block_ticks, fluid_ticks = merged
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
        tile_count = stage_fixture(args.java,jar,directory,merged,version,offset)
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
        print(f"{tile_count} Vanilla tiles placed. {len(expected)-len(mismatches)}/{len(expected)} block states matched.")
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
