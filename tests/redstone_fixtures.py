"""Run the same tick-by-tick block fixtures on the native and official servers."""
import argparse
import json
from pathlib import Path
import struct
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


def comparator_piston_wakeup_actions():
    piston, conductor, comparator, source = (16,80,0), (16,81,0), (17,81,0), (18,81,0)
    return [
        *floor(comparator),place(piston,"piston",facing="down"),place(conductor,"stone"),
        place(comparator,"comparator",facing="east"),place(source,"barrel"),step(4),
        check("the empty inventory leaves the piston retracted",piston,"piston",extended="false"),
        inventory_edit(source,0,"stone",1),step(2),
        check("the comparator notifies the piston beneath its output block",piston,"piston",extended="true"),
        inventory_edit(source,0,"air",0),step(2),
        check("losing comparator power begins retraction",piston,"moving_piston",facing="down"),
        step(2),check("piston finishes retracting",piston,"piston",extended="false"),
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


def hopper_lock_actions():
    hopper, signal = (16,80,0), (15,80,0)
    return [
        place(hopper,"hopper"),place(signal,"redstone_block"),
        check("neighbor locks hopper immediately",hopper,"hopper",enabled="false"),
        place(signal,"air"),check("removing neighbor unlocks hopper immediately",hopper,"hopper",enabled="true"),
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


def item_stagger_actions():
    supports = [(x,79,0) for x in range(14,18)]
    actions = [place(pos,"stone") for pos in supports]
    actions.extend(dict(pos=(x+.5,80,.5),spawn_resting_item=True) for x,_,_ in supports)
    actions.extend(place(pos,"air") for pos in supports)
    for tick in range(1,5):
        actions.extend([step(),dict(pos=(16,80,0),falling_item_count=tick,
                                    check=f"{tick} of four items falling after tick {tick}")])
    return actions


def dropper_neighbor_order_actions():
    power, east, north = (16,80,4), (17,80,4), (16,80,3)
    return [
        place(east,"dropper",facing="up"),place(north,"dropper",facing="up"),
        inventory_edit(east,0,"stone",1),inventory_edit(north,0,"stone",1),
        place(power,"redstone_block"),step(4),
        dict(inventory_edit(east,0,"air",0),check="the east dropper used its item"),
        dict(inventory_edit(north,0,"air",0),check="the north dropper used its item"),
        dict(pos=(16.5,81.25,4.5),first_item_from=(1,0,0),check="east dropper ejects before north dropper"),
    ]


def piston_dropper_update_actions():
    upper, lower, piston, moved = (16,81,0), (16,80,0), (17,81,0), (17,82,0)
    repeater, source = (18,82,0), (19,82,0)
    return [
        *floor(repeater),place(piston,"sticky_piston",facing="up"),place(moved,"white_concrete"),
        place(upper,"dropper",facing="down"),place(lower,"dropper",facing="up"),
        place(repeater,"repeater",facing="east",delay="2"),inventory_edit(upper,0,"redstone",1),step(6),
        place(source,"redstone_block"),step(4),
        check("the powered block starts moving",piston,"sticky_piston",extended="true"),
        step(4),
        dict(inventory_edit(lower,0,"air",0),check="the piston does not trigger the diagonal dropper"),
        dict(inventory_edit(upper,0,"redstone",1),check="the upper dropper retains its item"),
    ]


def repeater_output_order_actions():
    signal, source, conductor = (14,80,4), (15,80,4), (16,80,4)
    east, north = (17,80,4), (16,80,3)
    east_dropper, north_dropper = (18,80,4), (16,80,2)
    return [
        *floor(source,east,north),place(conductor,"stone"),
        place(source,"repeater",facing="west"),place(east,"repeater",facing="west"),
        place(north,"repeater",facing="south"),place(east_dropper,"dropper",facing="up"),
        place(north_dropper,"dropper",facing="up"),inventory_edit(east_dropper,0,"stone",1),
        inventory_edit(north_dropper,0,"stone",1),step(4),
        place(signal,"redstone_block"),step(8),
        dict(inventory_edit(east_dropper,0,"air",0),check="the east branch fired"),
        dict(inventory_edit(north_dropper,0,"air",0),check="the north branch fired"),
        dict(pos=(16.5,81.25,4.5),first_item_from=(2,0,0),check="the east diode gets the first notification"),
    ]


def rail_pulse_order_actions():
    rails = [(x,80,4) for x in range(16,19)]
    near, far = (16,80,5), (18,80,5)
    near_dropper, far_dropper, signal = (16,80,6), (18,80,6), (15,80,4)
    return [
        *floor(*rails),*[place(rail,"activator_rail",shape="east_west") for rail in rails],
        place(far,"observer",facing="north"),place(near,"observer",facing="north"),
        place(far_dropper,"dropper",facing="up"),place(near_dropper,"dropper",facing="up"),
        inventory_edit(far_dropper,0,"stone",1),inventory_edit(near_dropper,0,"stone",1),step(4),
        place(signal,"redstone_block"),step(6),
        dict(inventory_edit(far_dropper,0,"air",0),check="the far rail observer activated its dropper"),
        dict(inventory_edit(near_dropper,0,"air",0),check="the near rail observer activated its dropper"),
        dict(pos=(16.5,81.25,6.5),first_item_from=(2,0,0),check="the far observer schedules first during rail propagation"),
    ]


def jukebox_sorter_actions():
    source, jukebox, below = (15,80,0),(16,80,0),(16,79,0)
    power, lamp, comparator, wire = (15,79,0),(16,81,0),(17,80,0),(18,80,0)
    return [
        *floor(comparator,wire),place(power,"redstone_block"),place(below,"hopper"),
        place(source,"hopper",facing="east"),place(jukebox,"jukebox"),place(lamp,"redstone_lamp"),
        place(comparator,"comparator",facing="west"),place(wire,"redstone_wire"),
        inventory_edit(source,0,"stone",1),inventory_edit(source,1,"music_disc_stal",1),
        step(2),check("locked sender holds the record",jukebox,"jukebox",has_record="false"),
        place(power,"air"),place((16,78,0),"redstone_block"),step(4),
        check("record inserted by hopper",jukebox,"jukebox",has_record="true"),
        check("playing record powers adjacent lamp",lamp,"redstone_lamp",lit="true"),
        check("selector reads Stal as eight",wire,"redstone_wire",power="8"),
        dict(inventory_edit(source,0,"stone",1),check="incompatible item stays in the sender"),
        place((16,78,0),"stone"),step(2),
        check("playing power locks a block hopper below",below,"hopper",enabled="false"),
        place(below,"rail",shape="east_west"),
        dict(vehicle="sorter",kind="hopper_minecart",pos=(16.5,79.0625,.5),velocity=(0,0,0)),step(6),
        dict(vehicle="sorter",pos=(16.5,79.0625,.5),vehicle_item="music_disc_stal",
             check="hopper minecart extracts a playing record"),
        check("jukebox clears when minecart extracts",jukebox,"jukebox",has_record="false"),
        check("weak output stops",lamp,"redstone_lamp",lit="false"),
        check("analog output returns to zero",wire,"redstone_wire",power="0"),
    ]


def hopper_trapdoor_actions():
    hopper, cover, drop = (16,80,0),(16,81,0),(16.5,81.1875,.5)
    return [
        place(hopper,"hopper"), place(cover,"iron_trapdoor",half="bottom",open="false"),
        dict(pos=drop,spawn_resting_item=True), step(),
        dict(inventory_edit(hopper,0,"stone",1),check="hopper pulls through the closed trapdoor's partial shape"),
    ]


def item_landing_actions():
    return [
        place((16,81,0),"iron_trapdoor",half="bottom",open="false"),
        dict(pos=(16.5,81.22,.5),item_probe="falling",velocity=(0,0,0)),step(),
        dict(pos=(16.5,81.1875,.5),item_probe="falling",velocity=(0,0,0),
             check="item stops at the trapdoor surface without bouncing"),
    ]


def powder_snow_fall_actions():
    item_x,tnt_x = 16.5,19.5
    y = 81.9
    actions = [
        place((16,81,0),"powder_snow"),place((19,81,0),"powder_snow"),
        dict(pos=(item_x,y,.5),item_probe="snow_item",velocity=(0,0,0)),
        dict(pos=(tnt_x,y,.5),tnt="snow_tnt",velocity=(0,0,0),fuse=20),
    ]
    vy=0
    for tick in range(1,4):
        vy-=.04
        y+=vy*(1 if tick==1 else 1.5)
        vy=vy*.98 if tick==1 else 0
        actions.extend([step(),
                        dict(pos=(item_x,y,.5),item_probe="snow_item",velocity=(0,vy,0),
                             check=f"item moves through powder snow on tick {tick}"),
                        dict(pos=(tnt_x,y,.5),tnt="snow_tnt",velocity=(0,vy,0),fuse=20-tick,
                             check=f"TNT moves through powder snow on tick {tick}")])
    return actions


def piston_wire_wakeup_actions():
    piston, support, dust, power = (16,80,0),(16,81,0),(16,82,0),(16,82,1)
    return [
        place(piston,"sticky_piston",facing="east"),place(support,"stone"),place(dust,"redstone_wire"),step(4),
        check("piston starts retracted",piston,"sticky_piston",extended="false"),
        place(power,"redstone_block"),step(4),
        check("dust notifies the piston two blocks below",piston,"sticky_piston",extended="true"),
        place(power,"air"),step(4),
        check("dust removal of power wakes the piston again",piston,"sticky_piston",extended="false"),
    ]


def piston_wire_timing_actions():
    piston, moved, source = (16,80,0), (17,80,0), (15,80,0)
    near_dust, far_dust = (17,80,1), (18,80,1)
    return [
        *floor(near_dust,far_dust),place(piston,"piston",facing="east"),
        place(near_dust,"redstone_wire"),place(far_dust,"redstone_wire"),
        place(moved,"redstone_block"),step(4),
        check("dust reads the resting redstone block",near_dust,"redstone_wire",power="15"),
        place(source,"redstone_block"),step(),
        check("dust turns off in the tick the block starts moving",near_dust,"redstone_wire",power="0"),
        check("the moving redstone block emits no signal",far_dust,"redstone_wire",power="0"),
        step(2),check("dust turns on when the moved block settles",far_dust,"redstone_wire",power="15"),
    ]


def piston_moving_base_actions():
    mover,near,far,source = (16,80,0),(17,80,0),(18,80,0),(15,80,0)
    return [
        place(mover,"sticky_piston",facing="east"),place(near,"sticky_piston",facing="up"),step(2),
        place(source,"redstone_block"),step(3),
        check("sticky piston extends with a retracted piston in front",far,"sticky_piston",facing="up",extended="false"),
        place(source,"air"),step(3),
        check("sticky piston pulls the moved piston base back",near,"sticky_piston",facing="up",extended="false"),
        check("former position clears",far,"air"),
    ]


def piston_drop_pulse_actions():
    piston,block,far,source = (16,80,0),(17,80,0),(18,80,0),(15,80,0)
    return [
        place(piston,"sticky_piston",facing="east"),place(block,"stone"),step(2),
        place(source,"redstone_block"),step(),
        check("load is still expanding",far,"moving_piston",facing="east"),
        place(source,"air"),step(),
        check("short pulse finishes the pushed block at once",far,"stone"),
        check("head clears without pulling the load",block,"air"),
        step(2),check("piston remains retracted",piston,"sticky_piston",extended="false"),
    ]


def repeater_lock_priority_actions():
    target, lock = (16,80,0),(16,80,1)
    return [
        *floor(target,lock),place(target,"repeater",facing="west"),place(lock,"repeater",facing="south"),step(4),
        place((15,80,0),"redstone_block"),place((16,80,2),"redstone_block"),step(2),
        check("side repeater wins the same-tick race",lock,"repeater",powered="true"),
        check("lock prevents the pending rise",target,"repeater",locked="true",powered="false"),
    ]


def comparator_same_tick_dust_actions():
    repeater,side,rear,comparator,out = (17,80,-1),(18,80,-1),(17,80,0),(18,80,0),(19,80,0)
    return [
        *floor(repeater,side,comparator,out),place(repeater,"repeater",facing="west"),
        place(side,"redstone_wire"),place(rear,"composter",level="4"),
        place(comparator,"comparator",facing="west",mode="subtract"),place(out,"redstone_wire"),step(6),
        check("comparator begins with analog input four",out,"redstone_wire",power="4"),
        place(rear,"composter",level="7"),place((16,80,-1),"redstone_block"),step(2),
        check("earlier repeater tick settles the side dust",side,"redstone_wire",power="15"),
        check("comparator reads the settled dust in the same tick",out,"redstone_wire",power="0"),
    ]


def daylight_same_tick_dust_actions():
    sensor, wire = (16,80,0),(17,80,0)
    return [
        *floor(sensor,wire),dict(time=6000),place(sensor,"daylight_detector",inverted="true"),
        place(wire,"redstone_wire"),dict(next_tick_multiple=20),
        check("inverted detector is off in daylight",wire,"redstone_wire",power="0"),
        dict(time=18000),dict(next_tick_multiple=20),
        check("detector reads night at the scheduled tick",sensor,"daylight_detector",power="11"),
        check("dust receives the night signal in the same tick",wire,"redstone_wire",power="11"),
    ]


def torch_burnout_actions():
    torch,support,power = (16,80,0),(16,79,0),(15,79,0)
    actions=[place(support,"stone"),place(torch,"redstone_torch"),step(2)]
    actions.extend([place(power,"redstone_block"),step(3),
                    check("weak power does not pass through the support",torch,"redstone_torch",lit="true"),
                    place(power,"air")])
    for _ in range(7):
        actions.extend([place(support,"redstone_block"),step(2),place(support,"stone"),step(2)])
    return [
        *actions,check("seventh off transition can recover",torch,"redstone_torch",lit="true"),
        place(support,"redstone_block"),step(2),
        check("directly powered support turns the torch off",torch,"redstone_torch",lit="false"),
        place(support,"stone"),step(4),
        check("eighth off transition prevents immediate recovery",torch,"redstone_torch",lit="false"),
        step(155),check("torch is still out at tick 159",torch,"redstone_torch",lit="false"),
        step(),check("torch relights at tick 160",torch,"redstone_torch",lit="true"),
    ]


def powered_rail_actions(kind):
    line=[(x,80,0) for x in range(14,24)]
    other=(24,80,0)
    signal=(13,80,0)
    other_kind="activator_rail" if kind=="powered_rail" else "powered_rail"
    return [
        *floor(*line,other),
        *[place(p,kind,shape="east_west") for p in line],
        place(other,other_kind,shape="east_west"),place(signal,"redstone_block"),step(4),
        check("rail adjacent to source powers",line[0],kind,powered="true"),
        check("power reaches the eighth chained rail",line[8],kind,powered="true"),
        check("ninth chained rail remains off",line[9],kind,powered="false"),
        place(signal,"air"),place((23,80,1),"redstone_block"),step(4),
        check("direct power reaches the last rail",line[9],kind,powered="true"),
        check("power does not cross rail types",other,other_kind,powered="false"),
        place((23,80,1),"air"),step(4),
        check("rail power clears",line[8],kind,powered="false"),
    ]


def rail_slope_observer_actions():
    low,slope,high,power,sensor = (15,80,0),(16,80,0),(17,81,0),(17,81,1),(15,81,0)
    rail="activator_rail"
    return [
        *floor(low,slope,high),place(low,rail,shape="east_west"),place(high,rail,shape="east_west"),
        place(slope,rail,shape="ascending_east"),place(sensor,"observer",facing="down"),step(6),
        check("track keeps its slope",slope,rail,shape="ascending_east"),
        place(power,"redstone_block"),
        check("power crosses the slope immediately",low,rail,powered="true"),
        step(),check("observer waits for its scheduled tick",sensor,"observer",powered="false"),
        step(),check("lower rail wakes the observer on tick two",sensor,"observer",powered="true"),
    ]


def cart_straight_actions():
    track = [(x,80,0) for x in range(13,21)]
    def f32(value):
        return struct.unpack("<f",struct.pack("<f",value))[0]
    speed=.1
    position=15.75
    friction=f32(f32(.98)+f32(15*f32(.001)))
    actions=[
        *floor(*track),*[place(p,"rail",shape="east_west") for p in track],
        dict(vehicle="cart",kind="chest_minecart",pos=(position,80.0625,.5),velocity=(speed,0,0)),
    ]
    for tick in range(1,7):
        position+=speed
        speed*=friction
        actions += [step(),dict(vehicle="cart",check=f"cart tick {tick}",pos=(position,80.0625,.5),velocity=(speed,0,0))]
    return actions


def dispenser_vehicle_actions(kind):
    signal, dispenser, front = (15,80,0),(16,80,0),(17,80,0)
    if kind=="oak_boat":
        track = place(front,"water")
    else:
        track = place(front,"rail",shape="east_west")
    marker=(17.625,80.2,.5) if kind=="chest_minecart" else (17.75,81.5,.5)
    return [
        *floor(front),track,place(dispenser,"dispenser",facing="east"),
        inventory_edit(dispenser,0,kind,1),place(signal,"redstone_block"),step(3),
        dict(pos=marker,vehicle_present=kind,expected=False,check="vehicle is absent after three ticks"),
        step(),dict(pos=marker,vehicle_present=kind,expected=True,check="vehicle is present after four ticks"),
        dict(inventory_edit(dispenser,0,kind,0),check="dispensed vehicle consumed exactly one item"),
    ]


def detector_rail_actions():
    position, wire = (16,80,0),(16,80,1)
    return [
        *floor(position,wire),place(position,"detector_rail",shape="east_west"),place(wire,"redstone_wire"),
        dict(vehicle="detected",kind="chest_minecart",pos=(16.5,80.0625,.5),velocity=(0,0,0)),
        step(),check("cart powers detector",position,"detector_rail",powered="true"),
        step(),check("occupied detector emits fifteen",wire,"redstone_wire",power="15"),
        dict(remove_vehicle="detected"),step(18),
        check("detector holds power until its scheduled check",position,"detector_rail",powered="true"),
        step(),check("empty detector clears after twenty ticks",position,"detector_rail",powered="false"),
        check("detector output clears",wire,"redstone_wire",power="0"),
    ]


def tnt_diode_power_actions():
    source, repeater, conductor, charge = [(x,80,0) for x in range(14,18)]
    return [
        *floor(repeater),place(conductor,"stone"),place(charge,"tnt"),
        place(repeater,"repeater",facing="west"),step(2),
        place(source,"redstone_block"),step(),
        check("TNT stays intact while the diode is waiting",charge,"tnt"),
        step(),check("diode ignites TNT through its output block",charge,"air"),
    ]


def piston_tnt_duplication_actions():
    sticky, source = (16,80,-3), (16,80,-4)
    buffer, initial, shifted = (16,80,-2), (16,80,-1), (16,80,0)
    note, watcher, piston = (16,81,-1), (16,81,0), (16,80,1)
    relay, booster, relay_source = (17,80,-2), (17,80,-3), (17,80,-4)
    dust, side_note = (17,80,-1), (17,80,0)
    charge = (16.5,80,.5)
    return [
        *floor(relay,dust,booster),
        place(sticky,"sticky_piston",facing="south"),place(buffer,"observer",facing="west"),
        place(initial,"tnt"),place(note,"note_block"),place(watcher,"observer",facing="north"),
        place(piston,"piston",facing="north"),place(relay,"repeater",facing="north"),
        place(booster,"repeater",facing="north"),place(dust,"redstone_wire"),
        place(side_note,"note_block"),step(6),
        place(source,"redstone_block"),step(),place(relay_source,"redstone_block"),step(),
        place(source,"air"),step(),
        check("the first piston displaces the TNT block",shifted,"tnt"),
        check("the moved observer changes the instrument",note,"note_block",instrument="basedrum"),
        dict(pos=charge,tnt_present=False,check="the first stroke does not prime TNT"),
        step(2),check("the return stroke coincides with the dust pulse",piston,"piston",extended="true"),
        check("the dust routes fifteen power during the return stroke",dust,"redstone_wire",power="15"),
        dict(pos=charge,tnt_present=True,check="the return stroke primes an entity"),
        step(2),check("the TNT block returns to its original position",initial,"tnt"),
    ]


def tnt_fuse_actions():
    position=(16.5,85,.5)
    stone, obsidian=(17,84,0),(15,84,0)
    actions=[
        place(stone,"stone"),place(obsidian,"obsidian"),
        dict(tnt="charge",pos=position,velocity=(0,0,0),fuse=4),
    ]
    y=position[1]
    vy=0
    for tick in range(1,4):
        vy-=.04
        y+=vy
        vy*=.98
        actions += [step(),dict(tnt="charge",check=f"primed TNT tick {tick}",pos=(16.5,y,.5),
                               velocity=(0,vy,0),fuse=4-tick)]
    actions += [
        step(),dict(tnt="charge",check="entity disappears when fuse expires",pos=position),
        check("blast removes neighboring stone",stone,"air"),
        check("blast leaves obsidian",obsidian,"obsidian"),
    ]
    return actions


def tnt_impulse_actions():
    source = (16.5,90,.5)
    target = (18.5,90,.5)
    height = struct.unpack("<f",struct.pack("<f",.98))[0]
    dy = .04-height*.0625
    distance = (4+dy*dy)**.5
    scale = (1-distance/8)/distance
    vx,vy = 2*scale,dy*scale-.04
    return [
        dict(tnt="source",pos=source,velocity=(0,0,0),fuse=1),
        dict(tnt="target",pos=target,velocity=(0,0,0),fuse=12),
        step(),dict(tnt="target",pos=(target[0]+vx,target[1]+vy,target[2]),
                   velocity=(vx*.98,vy*.98,0),fuse=11,
                   check="blast accelerates TNT along the vector to its base"),
    ]


def tnt_slab_actions():
    position=(16.5,80.6,.5)
    return [
        place((16,80,0),"stone_slab",type="bottom"),step(3),
        dict(tnt="charge",pos=position,velocity=(0,0,0),fuse=80),
        step(),dict(tnt="charge",check="TNT can enter the upper half of a slab cell",pos=(16.5,80.56,.5),
                    velocity=(0,-.0392,0),fuse=79),
        step(),dict(tnt="charge",check="TNT lands at the slab surface and clears vertical motion",pos=(16.5,80.5,.5),
                    velocity=(0,0,0),fuse=78),
        dict(tnt="charge",remove=True,pos=position),
    ]


def moving_slime_actions():
    signal,piston,slime=(14,80,0),(15,80,0),(16,80,0)
    position=(17.5,80,.5)
    pushed_x=18.000000009536743
    return [
        *floor((17,80,0),(18,80,0),(19,80,0)),
        place(piston,"piston",facing="east"),place(slime,"slime_block"),step(3),
        dict(tnt="charge",pos=position,velocity=(0,0,0),fuse=80),
        place(signal,"redstone_block"),step(),
        dict(tnt="charge",check="moving slime pushes TNT and gives it velocity",pos=(pushed_x,80,.5),
             velocity=(1,0,0),fuse=79),
        step(),dict(tnt="charge",check="TNT carries the slime impulse into the next tick",
                    pos=(pushed_x+1,80,.5),velocity=(.686,0,0),fuse=78),
        dict(tnt="charge",remove=True,pos=position),
    ]


def daylight_actions():
    normal,inverted=(16,80,0),(18,80,0)
    kind="daylight_detector"
    return [
        dict(time=6000),place(normal,kind),place(inverted,kind,inverted="true"),step(20),
        check("open daylight reaches full power",normal,kind,power="15"),
        check("open inverted detector is off at noon",inverted,kind,power="0"),
        dict(time=18000),step(20),
        check("normal detector is off at midnight",normal,kind,power="0"),
        check("inverted detector reads sky darkening",inverted,kind,power="11"),
        dict(time=6000),place((18,81,0),"stone"),step(20),
        check("inverted detector reads light entering from the side",inverted,kind,power="1"),
    ]


def powered_receiver_actions():
    note,door=(16,80,0),(16,80,4)
    note_signal,door_signal=(15,80,0),(15,80,4)
    gate,gate_signal=(16,80,8),(15,80,8)
    return [
        *floor(note),place(note,"note_block",instrument="basedrum"),
        place(door,"iron_trapdoor",half="top"),place(note_signal,"redstone_block"),
        place(door_signal,"redstone_block"),step(),
        check("note block records a rising input",note,"note_block",powered="true"),
        check("powered iron trapdoor opens",door,"iron_trapdoor",powered="true",open="true"),
        place(note_signal,"air"),place(door_signal,"air"),step(),
        check("note block clears on falling input",note,"note_block",powered="false"),
        check("iron trapdoor closes",door,"iron_trapdoor",powered="false",open="false"),
        place(gate,"oak_fence_gate",open="true"),place(gate_signal,"stone"),
        check("an open unpowered gate stays open on a neighbor update",gate,"oak_fence_gate",powered="false",open="true"),
        place(gate_signal,"redstone_block"),
        check("fence gate recognizes power immediately",gate,"oak_fence_gate",powered="true",open="true"),
        place(gate_signal,"air"),
        check("gate closes immediately on falling input",gate,"oak_fence_gate",powered="false",open="false"),
    ]


def powered_door_actions():
    lower, upper, signal = (16,80,0), (16,81,0), (17,81,0)
    return [
        *floor(lower),place(lower,"iron_door",half="lower"),place(upper,"iron_door",half="upper"),
        place(signal,"redstone_block"),
        check("the upper half opens immediately when powered",upper,"iron_door",half="upper",powered="true",open="true"),
        check("power at the upper half also opens the lower half",lower,"iron_door",half="lower",powered="true",open="true"),
        place(signal,"air"),
        check("the upper half closes on signal loss",upper,"iron_door",half="upper",powered="false",open="false"),
        check("the lower half closes with it",lower,"iron_door",half="lower",powered="false",open="false"),
    ]


def note_instrument_actions():
    note, below, above, watcher = (16,81,0), (16,80,0), (16,82,0), (17,81,0)
    return [
        place(below,"tnt"),place(note,"note_block"),place(watcher,"observer",facing="west"),step(4),
        check("TNT selects harp",note,"note_block",instrument="harp",powered="false"),
        place(below,"observer",facing="north"),
        check("an observer below immediately selects basedrum",note,"note_block",instrument="basedrum",powered="false"),
        step(2),check("the instrument change triggers the watcher",watcher,"observer",powered="true"),
        step(2),place(above,"skeleton_skull"),
        check("a skull above overrides the block below",note,"note_block",instrument="skeleton",powered="false"),
        place(above,"air"),
        check("removing the head restores basedrum",note,"note_block",instrument="basedrum",powered="false"),
    ]


def piston_note_instrument_actions():
    piston, moving, source = (16,80,0), (17,80,0), (15,80,0)
    note, watcher = (18,81,0), (18,81,1)
    return [
        place(piston,"piston",facing="east"),place(moving,"observer",facing="south"),
        place(note,"note_block"),place(watcher,"observer",facing="north"),step(4),
        place(source,"redstone_block"),step(),
        check("moving observer has not selected basedrum yet",note,"note_block",instrument="harp"),
        step(2),check("piston completes the instrument change",note,"note_block",instrument="basedrum"),
        step(),check("observer waits another tick",watcher,"observer",powered="false"),
        step(),check("instrument change sends a two-tick delayed pulse",watcher,"observer",powered="true"),
    ]


def piston_powered_observer_actions():
    piston, original, moved = (16,80,0), (16,81,0), (16,82,0)
    observed, signal = (17,81,0), (16,79,0)
    return [
        place(piston,"piston",facing="up"),place(original,"observer",facing="east"),step(4),
        place(observed,"stone"),step(2),
        check("observer is active before moving",original,"observer",powered="true"),
        place(signal,"redstone_block"),step(3),
        check("relocated observer clears power without a scheduled tick at the destination",moved,
              "observer",facing="east",powered="false"),
        step(2),check("a move begun while powered does not replay the old pulse",moved,"observer",powered="false"),
    ]


def piston_observer_pulse_actions():
    piston, original, moved = (16,80,0), (16,81,0), (16,82,0)
    signal = (16,79,0)
    return [
        place(piston,"piston",facing="up"),place(original,"observer",facing="east"),step(4),
        place(signal,"redstone_block"),step(3),
        check("the resting observer has not pulsed yet",moved,"observer",powered="false"),
        step(),check("the moved observer waits a second tick",moved,"observer",powered="false"),
        step(),check("moving an idle observer starts a pulse",moved,"observer",powered="true"),
        step(2),check("its pulse clears after two ticks",moved,"observer",powered="false"),
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


def piston_animation_actions():
    signal,piston,head,moved = [(x,80,0) for x in range(14,18)]
    return [
        place(piston,"sticky_piston",facing="east"),place(head,"stone"),
        place(signal,"redstone_block"),step(),
        check("piston extends the base",piston,"sticky_piston",extended="true"),
        check("sticky head is in motion",head,"moving_piston",facing="east",type="sticky"),
        check("pushed block is in motion",moved,"moving_piston",facing="east",type="normal"),
        step(),check("motion lasts through a second tick",head,"moving_piston"),
        step(),check("head finishes extension",head,"piston_head",facing="east",type="sticky"),
        check("stone settles at destination",moved,"stone"),place(signal,"air"),step(),
        check("sticky base is retracting",piston,"moving_piston",facing="east",type="sticky"),
        check("pulled block is in motion",head,"moving_piston",facing="east",type="normal"),
        step(2),check("base settles retracted",piston,"sticky_piston",extended="false"),
        check("stone settles back at head",head,"stone"),
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
    "comparator_piston_wakeup": comparator_piston_wakeup_actions(),
    "hopper_transfer": hopper_transfer_actions(),
    "hopper_lock": hopper_lock_actions(),
    "dropper_transfer": dropper_transfer_actions(),
    "item_stagger": item_stagger_actions(),
    "dropper_neighbor_order": dropper_neighbor_order_actions(),
    "piston_dropper_update": piston_dropper_update_actions(),
    "repeater_output_order": repeater_output_order_actions(),
    "rail_pulse_order": rail_pulse_order_actions(),
    "jukebox_sorter": jukebox_sorter_actions(),
    "hopper_trapdoor": hopper_trapdoor_actions(),
    "item_landing": item_landing_actions(),
    "powder_snow_fall": powder_snow_fall_actions(),
    "piston_wire_wakeup": piston_wire_wakeup_actions(),
    "piston_wire_timing": piston_wire_timing_actions(),
    "piston_moving_base": piston_moving_base_actions(),
    "piston_drop_pulse": piston_drop_pulse_actions(),
    "repeater_lock_priority": repeater_lock_priority_actions(),
    "comparator_same_tick_dust": comparator_same_tick_dust_actions(),
    "daylight_same_tick_dust": daylight_same_tick_dust_actions(),
    "torch_burnout": torch_burnout_actions(),
    "powered_rail": powered_rail_actions("powered_rail"),
    "activator_rail": powered_rail_actions("activator_rail"),
    "rail_slope_observer": rail_slope_observer_actions(),
    "cart_straight": cart_straight_actions(),
    "dispenser_chest_minecart": dispenser_vehicle_actions("chest_minecart"),
    "dispenser_oak_boat": dispenser_vehicle_actions("oak_boat"),
    "detector_rail": detector_rail_actions(),
    "tnt_fuse": tnt_fuse_actions(),
    "tnt_diode_power": tnt_diode_power_actions(),
    "piston_tnt_duplication": piston_tnt_duplication_actions(),
    "tnt_impulse": tnt_impulse_actions(),
    "tnt_slab": tnt_slab_actions(),
    "moving_slime": moving_slime_actions(),
    "daylight_detector": daylight_actions(),
    "powered_receivers": powered_receiver_actions(),
    "powered_door": powered_door_actions(),
    "note_instrument": note_instrument_actions(),
    "piston_note_instrument": piston_note_instrument_actions(),
    "piston_powered_observer": piston_powered_observer_actions(),
    "piston_observer_pulse": piston_observer_pulse_actions(),
    "piston_push": piston_actions(False),
    "piston_animation": piston_animation_actions(),
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
            if "remove_vehicle" in action:
                yield dict(remove_vehicle=f"fixture_{i}_{action['remove_vehicle']}")
                continue
            if "pos" not in action:
                yield action
                continue
            x, y, z = action["pos"]
            moved = dict(action, pos=(x + 32 * (i % 4), y, z + 32 * (i // 4)))
            if "vehicle" in moved:
                moved["vehicle"] = f"fixture_{i}_{moved['vehicle']}"
            if "tnt" in moved:
                moved["tnt"] = f"fixture_{i}_{moved['tnt']}"
            if "item_probe" in moved:
                moved["item_probe"] = f"fixture_{i}_{moved['item_probe']}"
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
