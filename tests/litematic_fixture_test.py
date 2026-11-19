import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from litematic_fixture import FixtureNBT, Region, inspect_regions, merge_regions, tag, tag_list, template_tiles
from litematic_anvil_ticks import chunk_tick_records, encode_tick, merge_chunk_ticks


class FixtureTests(unittest.TestCase):
    def test_signed_region_extents_and_record_origins(self):
        air, stone = {"Name": "minecraft:air"}, {"Name": "minecraft:stone"}
        region = Region("example", (8, 12, -3), (-2, 2, -2), (air, stone),
                        (0b0100000100010001,),
                        ({"x": 1, "y": 0, "z": 1},),
                        ({"Pos": [-0.5, 1, -0.25]},),
                        ({"x": 0, "y": 1, "z": 0},), ())
        self.assertEqual(region.minimum, (7, 12, -4))
        self.assertEqual([position for position, state in region.blocks() if state == stone],
                         [(7, 12, -4), (7, 12, -3), (7, 13, -4), (8, 13, -3)])
        self.assertEqual(next(region.positioned_records("block_entities"))[0], (8, 12, -3))
        self.assertEqual(next(region.positioned_records("entities"))[0], (7.5, 13, -3.25))
        self.assertEqual(next(region.positioned_records("block_ticks"))[0], (7, 13, -4))

    def test_typed_compound_payload_is_unchanged(self):
        payload = b"\x01\x00\x04Slot\x01\x03\x00\x05count" + struct.pack(">i", 1) + b"\x00"
        result = FixtureNBT(b"\x0a\x00\x00" + payload).root()
        self.assertEqual(dict(result), {"Slot": 1, "count": 1})
        self.assertEqual(bytes(result.payload), payload)

    def test_overlapping_regions_report_conflicts_without_dropping_records(self):
        def region(name, block):
            return Region(name, (0,0,0), (1,1,1), ({"Name": block},), (0,), (), (), (), ())

        report = inspect_regions([region("a", "minecraft:stone"), region("b", "minecraft:stone"),
                                  region("c", "minecraft:glass")])
        self.assertEqual(report["identical_overlaps"], 1)
        self.assertEqual(report["conflicting_overlaps"], 1)
        self.assertEqual(report["distinct_non_air_positions"], 1)

    def test_merge_uses_global_position_for_duplicate_block_entity(self):
        barrel = {"Name": "minecraft:barrel"}
        first = Region("a", (0,0,0), (2,1,1), (barrel,), (0,),
                       ({"id": "minecraft:barrel", "x": 1, "y": 0, "z": 0},), (), (), ())
        second = Region("b", (1,0,0), (1,1,1), (barrel,), (0,),
                        ({"id": "minecraft:barrel", "x": 0, "y": 0, "z": 0},), (), (), ())
        blocks, block_entities, *_ = merge_regions((first, second))
        self.assertEqual(len(blocks), 2)
        self.assertEqual(list(block_entities), [(1,0,0)])
        second.block_entities[0]["id"] = "minecraft:chest"
        with self.assertRaises(ValueError):
            merge_regions((first, second))

    def test_template_round_trips_typed_nbt_and_negative_coordinates(self):
        def string(name, value):
            value = value.encode()
            return tag(8, name, struct.pack(">H", len(value)) + value)

        state = FixtureNBT(tag(10, "", string("Name", "minecraft:barrel") + b"\x00")).root()
        data = FixtureNBT(tag(10, "", string("id", "minecraft:barrel") +
                              tag(1, "Slot", b"\x03") + b"\x00")).root()
        position = (-1, 0, -17)
        ((origin, template),) = template_tiles({position: state}, {position: (position, data)}, (), 4671)
        decoded = FixtureNBT(template).root()
        self.assertEqual(origin, (-16,0,-32))
        self.assertEqual(decoded["blocks"][0]["pos"], [15,0,15])
        self.assertEqual(bytes(decoded["blocks"][0]["nbt"].payload), bytes(data.payload))

    def test_ticks_replace_matching_records_preserving_order_and_other_nbt(self):
        original = dict(i="minecraft:observer", x=-1, y=80, z=-17, t=9, p=0)
        unrelated = dict(i="minecraft:stone", x=-4, y=81, z=-17, t=5, p=1)
        unknown = tag(4, "LastUpdate", struct.pack(">q",123456789))
        data = tag(10, "", unknown+tag_list("block_ticks",10,map(encode_tick,(original,unrelated)))+b"\x00")
        records = [
            ((-1,0,-17),dict(Block="minecraft:observer",Time=2,Priority=-1,SubTick=4)),
            ((-2,0,-17),dict(Block="minecraft:repeater",Time=2,Priority=-1,SubTick=3)),
        ]
        ticks = chunk_tick_records(records,(0,80,0),"Block")[-1,-2]
        changed = merge_chunk_ticks(data,{"block_ticks":ticks})
        self.assertEqual(FixtureNBT(changed).root_fields()["LastUpdate"],unknown)
        self.assertEqual(FixtureNBT(changed).root()["block_ticks"],[unrelated]+ticks)
        self.assertEqual([tick["i"] for tick in ticks],["minecraft:repeater","minecraft:observer"])


if __name__ == "__main__":
    unittest.main()
