"""Compare the first ticks of the original Mk.6.1 main assembly after loading."""
import argparse
from collections import Counter
from dataclasses import replace
import gzip
import hashlib
from importlib import import_module
import json
import math
from pathlib import Path
import subprocess
import tempfile

from litematic_fixture import add, merge_regions, read_fixture

native = import_module("check-native-litematic")
vanilla = import_module("check-vanilla-litematic")
SHA256 = "8c2d8bf4dc8059cb083a28368da2efa1be2bd2dd4999d8b8062d115c4c758632"
NETHER_SHA256 = "c829e9b9c3053451bf97dc8293cf8cebb3d91f0c8b281344769fd3e3c72c3e1a"
PANEL_SHA256 = "9bda32b9fbfc9c3e7ee6378e2d5370adec75938dbe0aba251727824f85732514"
PANEL_ORIGIN = (192,128,0)
REMOTE_PLATE = add((7.5,6.0625,-1.5),PANEL_ORIGIN)
REMOTE_HOLD = 20
VEHICLES = {"minecraft:chest_minecart","minecraft:hopper_minecart","minecraft:oak_boat"}
TRACKED_ENTITIES = sorted(VEHICLES | {"minecraft:tnt","minecraft:item"})
DIMENSIONS = ("overworld","the_nether")
LOADER_RETURN_OFFSETS = {(15,12,-17):(15,0,-14),(-17,12,15):(-14,0,15)}


def native_frames(probe, directory, snapshot, positions, hoppers, event_ticks, presses, nether_snapshot, grid,
                  remote, portal_only, target_positions):
    directory.mkdir()
    (directory/"blocks.json").write_text(json.dumps(snapshot, separators=(",",":")), encoding="utf-8")
    if nether_snapshot is not None:
        nether = directory/"DIM-1"
        nether.mkdir()
        (nether/"blocks.json").write_text(json.dumps(nether_snapshot,separators=(",",":")),encoding="utf-8")
    sample = dict(capture=positions,capture_hoppers=hoppers,capture_nether=bool(presses or nether_snapshot or grid))
    actions = []
    previous = 0
    for tick in event_ticks:
        if tick>previous:
            actions.append({"ticks":tick-previous})
        actions.extend(dict(pos=button,interact=True) for button in presses.get(tick,()))
        if grid and tick==grid[0]:
            actions.extend(dict(pos=button,indicator=lamp,lit=bit,interact=True)
                           for button,lamp,bit in grid[1])
        if tick==portal_only:
            actions.append(dict(enable_chunk_tickets=sorted({(x//16,z//16) for x,_,z in target_positions})))
        if remote and tick==remote:
            actions.append(dict(remote_weight="drop",pos=REMOTE_PLATE))
        if remote and tick==remote+REMOTE_HOLD:
            actions.append(dict(remote_weight="remove"))
        actions.append(sample)
        previous = tick
    result = subprocess.run([str(probe.resolve()),str(directory)], input=json.dumps(actions),
                            encoding="utf-8", capture_output=True, check=True, timeout=120)
    frames = [json.loads(line) for line in result.stdout.splitlines()]
    if len(frames)!=len(event_ticks):
        raise RuntimeError(f"Expected {len(event_ticks)} CUDA Craft samples, got {len(frames)}")
    return dict(zip(event_ticks,frames))


def vanilla_frame(world, positions, hoppers, chunks, registry, item_ids, live=None):
    loaded, inventories = {}, {}
    loaded_entities = []
    for cx,cz in chunks:
        chunk = vanilla.capture.chunk_from_region(world,cx,cz)
        if chunk is None:
            raise ValueError(f"Missing Vanilla chunk {(cx,cz)}")
        loaded[cx,cz] = vanilla.terrain.states_from_chunk(chunk,registry)
        if live is None:
            for hopper in chunk.get("block_entities",[]):
                if hopper["id"] == "minecraft:hopper":
                    inventories[hopper["x"],hopper["y"],hopper["z"]] = hopper
            entities = vanilla.capture.chunk_from_region(world,cx,cz,"entities")
            loaded_entities.extend(dict(entity,dimension="minecraft:overworld")
                                   for entity in (entities or {}).get("Entities",[]))
    states = [loaded[x//16,z//16][(y+64)*256+(z&15)*16+(x&15)] for x,y,z in positions]
    if live is not None:
        inventories = {(hopper["x"],hopper["y"],hopper["z"]):hopper for hopper in live["hoppers"]
                       if "TransferCooldown" in hopper}
        loaded_entities = [dict(entity,id=kind,dimension=f"minecraft:{dimension}")
                           for dimension in DIMENSIONS for kind in TRACKED_ENTITIES
                           for entity in live[f"{dimension}_{kind.removeprefix('minecraft:')}"]]
    tnt = []
    vehicles = []
    items = []
    for entity in loaded_entities:
        if entity["id"] == "minecraft:tnt":
            tnt.append(dict(position=entity["Pos"],velocity=entity["Motion"],fuse=entity["fuse"],
                            dimension=entity["dimension"]))
        elif entity["id"] in VEHICLES:
            vehicles.append(dict(kind=entity["id"].removeprefix("minecraft:"),position=entity["Pos"],
                                 velocity=entity["Motion"],portal_cooldown=entity.get("PortalCooldown",0)))
            vehicles[-1]["dimension"] = entity["dimension"]
        elif entity["id"] == "minecraft:item":
            items.append(dict(dimension=entity["dimension"],item=item_ids[entity["Item"]["id"]],
                              count=entity["Item"]["count"],position=entity["Pos"],velocity=entity["Motion"]))
    snapshots = []
    for pos in hoppers:
        hopper = inventories.get(pos)
        snapshots.append(None if hopper is None else dict(
            cooldown=hopper["TransferCooldown"],
            items=sorted([stack["Slot"],item_ids[stack["id"]],stack["count"]]
                         for stack in hopper.get("Items",[]))))
    return dict(states=states,tnt=tnt,hoppers=snapshots,vehicles=vehicles,items=items)


def read_nbt(path):
    return vanilla.capture.NBT(gzip.decompress(path.read_bytes())).root()


def vanilla_frames(java, jar, directory, merged, version, positions, hoppers, registry, item_ids,
                   event_ticks, presses, origin, nether, panel, target_positions, grid, remote, portal_only, extra_watch):
    directory.mkdir()
    vanilla.stage_fixture(java,jar,directory,merged,version,origin,nether,panel,
                          exact_clock=True,observe=(*target_positions,*extra_watch))
    world = directory/"world"
    chunks = sorted({(x//16,z//16) for x,_,z in positions})
    frames = {0:vanilla_frame(world,positions,hoppers,chunks,registry,item_ids)}
    indices = {tuple(position):index for index,position in enumerate(positions)}
    lit_states = {state for name,state in registry.items()
                  if isinstance(name,str) and name.startswith("minecraft:waxed_copper_bulb[") and "lit=true" in name}
    bulb_states = {state for name,state in registry.items()
                   if isinstance(name,str) and name.startswith("minecraft:waxed_copper_bulb[")}

    def grid_presses(frame):
        out = []
        for button,lamp,bit in grid[1]:
            state = frame["states"][indices[lamp]]
            if state not in bulb_states:
                raise RuntimeError(f"Original grid lamp is absent at {lamp}")
            if bit != (state in lit_states):
                out.append(button)
        return out

    pack = world/"datapacks/fixture/data"
    functions = pack/"fixture/function"
    functions.mkdir()
    capture = ["data modify storage fixture:sample hoppers set value []"]
    capture.extend(f"data modify storage fixture:sample hoppers append from block {x} {y} {z}" for x,y,z in hoppers)
    for dimension in DIMENSIONS:
        for kind in TRACKED_ENTITIES:
            field = f"{dimension}_{kind.removeprefix('minecraft:')}"
            capture.append(f"data modify storage fixture:sample {field} set value []")
            capture.append(f"execute in minecraft:{dimension} as @e[type={kind},distance=0..] "
                           f"run data modify storage fixture:sample {field} append from entity @s")
    (functions/"capture.mcfunction").write_text("\n".join(capture)+"\n", encoding="utf-8")
    first_tick = read_nbt(world/"level.dat")["Data"]["Time"]
    if first_tick!=0:
        raise RuntimeError(f"Fixture preparation advanced the Vanilla clock to tick {first_tick}")

    def sample(command):
        command("function fixture:capture", "Running function")
        command("save-all flush","Saved the game")
        storage = read_nbt(world/"data/command_storage_fixture.dat")["data"]["contents"]
        return vanilla_frame(world,positions,hoppers,chunks,registry,item_ids,storage["sample"])

    initial_presses = list(presses.get(0,()))
    if grid and grid[0]==0:
        initial_presses.extend(grid_presses(frames[0]))
    with vanilla.vanilla_console(java,jar,directory,freeze_at_boot=True,buttons=initial_presses) as (command,press):
        command("tick freeze","frozen")
        response = command("time query gametime","The time is ")
        resumed_tick = int(response.rsplit(" ",1)[-1]) - first_tick
        if resumed_tick:
            raise RuntimeError(f"Vanilla advanced {resumed_tick} ticks before the first measurement")
        if initial_presses:
            frames[0] = sample(command)
        for tick in event_ticks[1:]:
            vanilla.step_to(command,first_tick+tick)
            actions = list(presses.get(tick,()))
            if grid and tick==grid[0]:
                actions.extend(grid_presses(sample(command)))
            if actions:
                press(actions)
            if tick==portal_only:
                for dimension in DIMENSIONS:
                    command(f"execute in minecraft:{dimension} run forceload remove all", "force loaded chunks")
                active_chunks = {(p[0]//16,p[2]//16) for p in target_positions}
                for cx,cz in sorted(active_chunks):
                    command(f"forceload add {cx*16} {cz*16}","to be force loaded")
            if remote and tick==remote:
                coords = " ".join(map(str,REMOTE_PLATE))
                command(f'summon minecraft:item {coords} '
                        '{Tags:["fixture_remote_weight"],Motion:[0d,0d,0d],PickupDelay:32767s,'
                        'Item:{id:"minecraft:stone",count:1}}',"Summoned new")
            if remote and tick==remote+REMOTE_HOLD:
                command("kill @e[type=minecraft:item,tag=fixture_remote_weight,limit=1]","Killed")
            frames[tick] = sample(command)
    return frames


def normalize_charges(charges):
    return sorted((charge["dimension"],tuple(round(value,9) for value in charge["position"]),
                   tuple(round(value,9) for value in charge["velocity"]),charge["fuse"])
                  for charge in charges)


def normalize_vehicles(vehicles):
    return sorted((vehicle["dimension"],vehicle["kind"],tuple(round(value,9) for value in vehicle["position"]),
                   tuple(round(value,9) for value in vehicle["velocity"]),vehicle["portal_cooldown"])
                  for vehicle in vehicles)


def normalize_items(items):
    return sorted((item["dimension"],item["item"],item["count"],
                   tuple(round(value,9) for value in item["position"]),
                   tuple(round(value,9) for value in item["velocity"])) for item in items)


def matching_loader_drop(trace, displacement, boat_item):
    # ItemEntity chooses two independent horizontal velocities in [-0.1,0.1].
    # Verify each run's resulting flight before comparing the game events.
    frames = {frame["tick"]:frame for frame in trace}
    if 6 not in frames:
        return None
    def close(left,right):
        return all(math.isclose(a,b,rel_tol=0,abs_tol=2e-9) for a,b in zip(left,right))

    for key in ("vanilla_items","native_items"):
        tracked = [frames[tick][key] for tick in range(6,9) if tick in frames]
        if any(len(entities)!=1 for entities in tracked):
            return False
        spawn, *following = [entities[0] for entities in tracked]
        dimension,item,count,position,speed = spawn
        if (dimension,item,count)!=("minecraft:the_nether",boat_item,1):
            return False
        if abs(speed[0])>.1 or abs(speed[2])>.1 or not close((speed[1],),(.2,)):
            return False
        if following:
            _,_,_,point,motion = following[0]
            delta = (speed[0],speed[1]-.04,speed[2])
            if (following[0][:3]!=spawn[:3] or not close(point,add(position,delta))
                    or not close(motion,(delta[0]*0.9800000190734863,delta[1]*.98,delta[2]*0.9800000190734863))):
                return False
            if len(following)==2:
                arrival = following[1]
                if (arrival[:3]!=("minecraft:overworld",item,1) or not close(arrival[3],add(point,displacement))
                        or not close(arrival[4],motion)):
                    return False
    return close(frames[6]["vanilla_items"][0][3],frames[6]["native_items"][0][3])


def panel_buttons(grid, blocks):
    if not isinstance(grid,list) or len(grid)!=5 or any(not isinstance(row,list) or len(row)!=13 or
                           any(type(bit) is not int or bit not in (0,1) for bit in row) for row in grid):
        raise ValueError("Expected a 5x13 grid of integer bits from the Mk.6 fire control")
    for j,row in enumerate(grid):
        for i,bit in enumerate(row):
            local = 9,9-j,20-i
            indicator = blocks[7,local[1],local[2]]
            if indicator["Name"]!="minecraft:waxed_copper_bulb" or not blocks[local]["Name"].endswith("_button"):
                raise ValueError(f"Original control panel input is missing at {local}")
            yield add(local,PANEL_ORIGIN),add((7,local[1],local[2]),PANEL_ORIGIN),bool(bit)


def add_panel(snapshot, panel_snapshot):
    snapshot["edits"].extend(panel_snapshot["edits"])
    simulation, panel = snapshot["simulation"], panel_snapshot["simulation"]
    next_entity_id = max((v["id"] for v in simulation.get("vehicles",[])),default=0)
    for vehicle in panel.get("vehicles",[]):
        simulation.setdefault("vehicles",[]).append(dict(vehicle,id=vehicle["id"]+next_entity_id))
    next_tick_sequence = 1+max((device[6] for device in simulation["devices"]),default=-1)
    simulation["devices"].extend([*event[:6],event[6]+next_tick_sequence] for event in panel["devices"])
    for field in ("containers","comparators","water"):
        simulation[field].extend(panel[field])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture",type=Path)
    parser.add_argument("--java",type=Path,required=True)
    parser.add_argument("--probe",type=Path,required=True)
    parser.add_argument("--ticks",type=int,default=4,metavar="1..4096")
    parser.add_argument("--sample-every",type=int,default=1,help="Game ticks between samples")
    parser.add_argument("--sample-window",type=int,nargs=2,metavar=("START","END"),
                        help="Capture every tick inside an inclusive interval in addition to regular samples")
    parser.add_argument("--button",type=int,nargs=3,action="append",default=[],metavar=("X","Y","Z"),
                        help="Press an original button at this schematic-relative position before tick one")
    parser.add_argument("--control-at",type=int,nargs=4,action="append",default=[],metavar=("TICK","X","Y","Z"),
                        help="Use an original lever or button at this absolute position and tick")
    parser.add_argument("--portal-only-at",type=int,metavar="TICK",
                        help="Switch both engines from always-ticking setup to portal tickets, with the target forced")
    parser.add_argument("--watch",type=int,nargs=3,action="append",default=[],metavar=("X","Y","Z"),
                        help="Include the Vanilla and CUDA Craft states of an absolute block position in each trace frame")
    parser.add_argument("--nether",type=Path,help="Install the original OSC 6 Nether counterpart")
    parser.add_argument("--panel",type=Path,help="Install the original OSC 6 control panel")
    parser.add_argument("--grid",type=Path,help="Replay a 5x13 JSON grid from Cubicmetre's Mk.6 fire control")
    parser.add_argument("--grid-at",type=int,default=0,metavar="TICK",help="Enter the grid after this many startup ticks")
    parser.add_argument("--submit-at",type=int,metavar="TICK",help="Press the original Input Settings button at this tick")
    parser.add_argument("--remote-at",type=int,metavar="TICK",help="Weight the original fishing-rod pressure plate after submitting")
    parser.add_argument("--target",type=int,nargs=3,metavar=("X","Y","Z"),
                        help="Require TNT arrival and matching block damage around this Overworld target")
    parser.add_argument("--reference-cache",type=Path,
                        help="Record official frames on the first run and reuse them for matching test inputs")
    parser.add_argument("--output",type=Path,required=True)
    args = parser.parse_args()
    if not 1<=args.ticks<=4096 or args.sample_every<=0 or args.ticks//args.sample_every>120:
        parser.error("Choose 1 through 4096 ticks, with a positive sampling interval and at most 120 regular samples")
    if args.sample_window:
        first,last = args.sample_window
        if not 0<=first<=last<=args.ticks or last-first>=80:
            parser.error("--sample-window must cover at most 80 ticks inside the measured range")
    if not 0<=args.grid_at<=args.ticks or (args.grid_at and not args.grid):
        parser.error("--grid-at requires --grid and must be inside the measured tick range")
    if args.submit_at is not None and (not args.grid or not args.grid_at<args.submit_at<=args.ticks):
        parser.error("--submit-at must follow --grid-at and be within --ticks")
    if args.remote_at is not None and (not args.submit_at or
                                       not args.submit_at<args.remote_at<=args.ticks-REMOTE_HOLD):
        parser.error("--remote-at must follow --submit-at and allow the pressure plate to release before --ticks")
    if args.portal_only_at is not None and (not args.nether or not 0<args.portal_only_at<=args.ticks):
        parser.error("--portal-only-at requires --nether and a tick after fixture loading")
    target_positions = []
    if args.target:
        x,y,z = args.target
        if not args.submit_at or not args.nether:
            parser.error("--target requires the connected fixture with --nether and --submit-at")
        if abs(x)>1020 or abs(z)>1020 or not -61<=y<=316:
            parser.error("--target and the observation volume must fit inside the current native world")
        target_positions = [(a,b,c) for a in range(x-3,x+4) for b in range(y-3,y+4)
                            for c in range(z-3,z+4)]
    version, regions = read_fixture(args.fixture,SHA256)
    merged = merge_regions(regions)
    blocks,records,entities,block_ticks,fluid_ticks = merged
    origin = (0,128,0) if args.nether or args.panel else (0,96,0)
    nether = None
    if args.nether:
        version_nether, regions_nether = read_fixture(args.nether,NETHER_SHA256)
        if args.panel:
            # The standalone panel includes only its Overworld portal. Use
            # the unchanged Z-axis portal module for the other end, above the roof.
            portal = next(region for region in regions_nether if region.name=="Southwest Portal")
            portal = replace(portal,name="Panel Portal",anchor=add(portal.anchor,(29,-9,0)))
            regions_nether = (*regions_nether,portal)
        nether = merge_regions(regions_nether),version_nether,add(origin,(0,14,0))
    presses = {}
    for original_button in args.button:
        state = blocks.get(tuple(original_button),{})
        if not state.get("Name","").endswith("_button") or state["Properties"]["powered"] != "false":
            parser.error("--button must point to an unpressed button in the original schematic")
        presses.setdefault(0,[]).append(add(original_button,origin))
    panel = None
    grid_plan = None
    if args.grid and not args.panel:
        parser.error("--grid requires the original --panel file")
    if args.panel:
        version_panel,regions_panel = read_fixture(args.panel,PANEL_SHA256)
        panel = merge_regions(regions_panel),version_panel,PANEL_ORIGIN
        if args.grid:
            grid = json.loads(args.grid.read_text(encoding="utf-8"))
            grid_plan = args.grid_at,list(panel_buttons(grid,panel[0][0]))
    if args.submit_at is not None:
        presses.setdefault(args.submit_at,[]).append(add((9,7,5),PANEL_ORIGIN))
    for tick,x,y,z in args.control_at:
        pos = (x,y,z)
        state = blocks.get(tuple(value-base for value,base in zip(pos,origin)),{})
        if not state and panel:
            state = panel[0][0].get(tuple(value-base for value,base in zip(pos,PANEL_ORIGIN)),{})
        name = state.get("Name","")
        if not 0<=tick<=args.ticks or (name!="minecraft:lever" and not name.endswith("_button")):
            parser.error("--control-at requires a fixture tick and an original lever or button")
        presses.setdefault(tick,[]).append(pos)
    event_ticks = {args.ticks,*range(0,args.ticks+1,args.sample_every),*presses}
    if args.sample_window:
        event_ticks.update(range(first,last+1))
    if args.portal_only_at is not None:
        event_ticks.add(args.portal_only_at)
    if grid_plan:
        event_ticks.add(args.grid_at)
    if args.remote_at:
        event_ticks.update((args.remote_at,args.remote_at+REMOTE_HOLD))
    loader_offset = LOADER_RETURN_OFFSETS.get(tuple(args.button[0])) if nether and not panel and len(args.button)==1 else None
    if loader_offset:
        event_ticks.update(range(4,min(args.ticks,9)+1))
    event_ticks = sorted(event_ticks)
    jar = vanilla.ROOT/".tools/vanilla-1.21.11/server.jar"
    if hashlib.sha1(jar.read_bytes()).hexdigest() != vanilla.capture.SHA1:
        raise ValueError("Wrong reference server JAR")
    reports = json.loads((jar.parent/"reports-1.21.11/reports/blocks.json").read_text(encoding="utf-8"))
    items = json.loads((native.ROOT/".tools/reference/items.json").read_text(encoding="utf-8"))
    item_ids = {"minecraft:"+item["name"]:item["id"] for item in items}
    registry, names = {}, {}
    properties = {}
    for name, definition in reports.items():
        for state in definition["states"]:
            registry[vanilla.terrain.state_key({"Name":name,"Properties":state.get("properties",{})})] = state["id"]
            names[state["id"]] = name
            properties[state["id"]] = state.get("properties",{})
    snapshot,_ = native.build_snapshot(blocks,records,entities,block_ticks,reports,item_ids,origin,fluid_ticks)
    nether_snapshot = None
    if nether:
        (nether_blocks,nether_records,nether_entities,nether_ticks,_),_,nether_origin = nether
        nether_snapshot,_ = native.build_snapshot(nether_blocks,nether_records,nether_entities,nether_ticks,
                                                  reports,item_ids,nether_origin)
        nether_snapshot["generator"] = "cuda-craft-nether-v1"
    positions = [entry[:3] for entry in snapshot["edits"]]
    hoppers = sorted(add(position,origin) for position,record in records.values()
                     if record["id"] == "minecraft:hopper")
    if panel:
        (panel_blocks,panel_records,panel_entities,panel_ticks,panel_fluids),_,panel_origin = panel
        panel_snapshot,_ = native.build_snapshot(panel_blocks,panel_records,panel_entities,panel_ticks,reports,
                                                 item_ids,panel_origin,panel_fluids)
        add_panel(snapshot,panel_snapshot)
        positions.extend(entry[:3] for entry in panel_snapshot["edits"])
        hoppers.extend(sorted(add(position,panel_origin) for position,record in panel_records.values()
                              if record["id"]=="minecraft:hopper"))
    observed_positions = set(map(tuple,positions))
    extra_watch = set(map(tuple,args.watch))-observed_positions-set(target_positions)
    if any(not (-1024<=x<1024 and -64<=y<320 and -1024<=z<1024) for x,y,z in extra_watch):
        parser.error("--watch must be inside the native world")
    positions.extend(sorted(extra_watch))
    target_begin = len(positions)
    positions.extend(target_positions)
    indices = {tuple(position):index for index,position in enumerate(positions)}
    cache_key = None
    if args.reference_cache:
        cache_inputs = dict(reference=vanilla.capture.SHA1,main=SHA256,nether=NETHER_SHA256 if nether else None,
                            panel=PANEL_SHA256 if panel else None,presses=presses,grid=grid_plan,
                            points=positions,targets=target_positions,portal_only=args.portal_only_at,
                            remote=args.remote_at,ticks=event_ticks)
        fingerprint = hashlib.sha256(json.dumps(cache_inputs,sort_keys=True).encode())
        for name in (Path(__file__).name,"check-vanilla-litematic.py","litematic_fixture.py",
                     "litematic_anvil_ticks.py","FreezeVanilla.java"):
            fingerprint.update(Path(__file__).with_name(name).read_bytes())
        cache_key = fingerprint.hexdigest()
        if args.reference_cache.exists():
            with gzip.open(args.reference_cache,"rt",encoding="utf-8") as source:
                cached = json.load(source)
            if cached["key"]!=cache_key:
                parser.error("--reference-cache was made for different inputs or reference tools")
        else:
            cached = None
    else:
        cached = None
    with tempfile.TemporaryDirectory(prefix="mk61-startup-",dir=jar.parent) as folder:
        directory = Path(folder)
        actual = native_frames(args.probe,directory/"native",snapshot,positions,hoppers,event_ticks,
                               presses,nether_snapshot,grid_plan,args.remote_at,args.portal_only_at,target_positions)
        if cached:
            reference = dict(zip(event_ticks,cached["frames"],strict=True))
            print(f"Using official 1.21.11 frames from {args.reference_cache}",flush=True)
        else:
            reference = vanilla_frames(args.java,jar,directory/"reference",merged,version,positions,
                                       hoppers,registry,item_ids,event_ticks,presses,origin,nether,panel,
                                       target_positions,grid_plan,args.remote_at,args.portal_only_at,extra_watch)
            if args.reference_cache:
                with gzip.open(args.reference_cache,"xt",encoding="utf-8") as output:
                    json.dump(dict(key=cache_key,frames=[reference[tick] for tick in event_ticks]),output)
    trace = []
    for tick,expected in sorted(reference.items()):
        observed = actual[tick]
        changes = [[pos,want,have] for pos,want,have in zip(positions,expected["states"],observed["states"])
                   if want != have]
        inventories = [[pos,want,have] for pos,want,have in zip(hoppers,expected["hoppers"],observed["hoppers"])
                       if want != have]
        wanted_tnt,actual_tnt = normalize_charges(expected["tnt"]), normalize_charges(observed["tnt"])
        wanted_vehicles = normalize_vehicles(expected["vehicles"])
        actual_vehicles = normalize_vehicles(observed["vehicles"])
        wanted_items,actual_items = normalize_items(expected["items"]),normalize_items(observed["items"])
        counts = Counter(names[want] for _,want,_ in changes)
        def describe(state):
            return [names[state],properties[state]]
        watch = [[pos,describe(expected["states"][indices[tuple(pos)]]),
                  describe(observed["states"][indices[tuple(pos)]])] for pos in args.watch]
        trace.append(dict(tick=tick,differing_blocks=changes,differing_hoppers=inventories,
                          vanilla_tnt=wanted_tnt,native_tnt=actual_tnt,
                          vanilla_vehicles=wanted_vehicles,native_vehicles=actual_vehicles,
                          vanilla_items=wanted_items,native_items=actual_items,watch=watch))
        print(f"Tick {tick}: {len(changes)} differing block states, {len(inventories)} differing hoppers, "
              f"TNT {len(wanted_tnt)}/{len(actual_tnt)}, vehicles {len(wanted_vehicles)}/{len(actual_vehicles)} "
              f"(Vanilla/native). Most common Vanilla states: {counts.most_common(4)}", flush=True)
    shot_failed = False
    if args.target:
        x,_,z = args.target
        matrix_errors = []
        for frames in (reference,actual):
            states = frames[args.submit_at]["states"]
            matrix_errors.append(sum((properties[states[indices[lamp]]].get("lit")=="true")!=bit
                                     for _,lamp,bit in grid_plan[1]))
        print(f"Settings mismatches before submission Vanilla/native: {matrix_errors}.")
        def reached(frames):
            return any(charge["dimension"]=="minecraft:overworld" and
                       math.hypot(charge["position"][0]-x,charge["position"][2]-z)<=3
                       for frame in frames.values() for charge in frame["tnt"])
        expected_damage = [pos for pos,before,after in zip(target_positions,
                           reference[0]["states"][target_begin:],reference[args.ticks]["states"][target_begin:])
                           if before!=after]
        actual_damage = [pos for pos,before,after in zip(target_positions,
                         actual[0]["states"][target_begin:],actual[args.ticks]["states"][target_begin:])
                         if before!=after]
        arrivals = reached(reference),reached(actual)
        shot_failed = not (not any(matrix_errors) and all(arrivals) and expected_damage and expected_damage==actual_damage)
        print(f"Target arrival Vanilla/native: {arrivals}. Changed target blocks Vanilla/native: "
              f"{len(expected_damage)}/{len(actual_damage)}.")
    args.output.write_text(json.dumps(trace,indent=2)+"\n",encoding="utf-8")
    matched_random_drop = matching_loader_drop(trace,loader_offset,item_ids["minecraft:oak_boat"]) if loader_offset else None
    if matched_random_drop is not None:
        print("Loader item flight through portal:", "matched" if matched_random_drop else "differs")
    return int(shot_failed or any(frame["differing_blocks"] or frame["differing_hoppers"] or
                   frame["vanilla_tnt"]!=frame["native_tnt"] or frame["vanilla_vehicles"]!=frame["native_vehicles"] or
                   (frame["vanilla_items"]!=frame["native_items"] and
                    not (matched_random_drop and 6<=frame["tick"]<=8)) for frame in trace) or matched_random_drop is False)


if __name__ == "__main__":
    raise SystemExit(main())
