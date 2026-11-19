"""Decode locally supplied Litematica fixtures for an isolated compatibility run."""
from collections import Counter, defaultdict
from dataclasses import dataclass
import gzip
import hashlib
from importlib import import_module
import math
from pathlib import Path
import struct

audit = import_module("audit-litematic")


class Compound(dict):
    """Decoded fields plus the original typed NBT payload, including TAG_End."""

    def __init__(self, fields, payload):
        super().__init__(fields)
        self.payload = payload


class FixtureNBT(audit.nbt.NBT):
    def payload(self, tag, depth=0):
        start = self.f.tell()
        value = super().payload(tag, depth)
        if tag != 10:
            return value
        return Compound(value, self.f.getbuffer()[start:self.f.tell()])

    def root_fields(self):
        """Return top-level named tags as raw bytes in serialization order."""
        if self.number("B") != 10:
            raise ValueError("Expected root compound")
        self.string()
        fields = {}
        while True:
            start = self.f.tell()
            kind = self.number("B")
            if not kind:
                if self.f.read(1):
                    raise ValueError("Trailing root data")
                return fields
            name = self.string()
            self.payload(kind)
            if name in fields:
                raise ValueError(f"Duplicate root field {name}")
            fields[name] = bytes(self.f.getbuffer()[start:self.f.tell()])


def xyz(tag):
    return tuple(tag[axis] for axis in ("x", "y", "z"))


def add(left, right):
    return tuple(a + b for a, b in zip(left, right))


@dataclass(frozen=True)
class Region:
    name: str
    anchor: tuple
    size: tuple
    palette: tuple
    packed: tuple
    block_entities: tuple
    entities: tuple
    block_ticks: tuple
    fluid_ticks: tuple

    @property
    def minimum(self):
        return add(self.anchor, tuple(min(0, n + 1) for n in self.size))

    def blocks(self):
        width, height, depth = map(abs, self.size)
        for index, state in enumerate(audit.indices(self.packed, width * height * depth, len(self.palette))):
            layer, x = divmod(index, width)
            y, z = divmod(layer, depth)
            yield add(self.minimum, (x, y, z)), self.palette[state]

    def positioned_records(self, field):
        if field == "entities":
            # Entity coordinates are relative to the region anchor, including for negative sizes.
            for entity in self.entities:
                position = entity["Pos"]
                if len(position) != 3 or not all(math.isfinite(v) for v in position):
                    raise ValueError(f"Invalid entity position in {self.name}")
                yield add(self.anchor, position), entity
            return
        size = tuple(map(abs, self.size))
        for record in getattr(self, field):
            local = xyz(record)
            if not all(isinstance(pos, int) and 0 <= pos < limit for pos, limit in zip(local, size)):
                raise ValueError(f"Out-of-bounds {field} in {self.name}")
            yield add(self.minimum, local), record


def read_fixture(path: Path, expected_sha256=None):
    if path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError("Compressed fixture exceeds 16 MiB")
    if expected_sha256 and hashlib.sha256(path.read_bytes()).hexdigest() != expected_sha256:
        raise ValueError(f"Fixture differs from pinned SHA-256: {path.name}")
    with gzip.open(path, "rb") as stream:
        raw = stream.read(64 * 1024 * 1024 + 1)
    if len(raw) > 64 * 1024 * 1024:
        raise ValueError("Decompressed fixture exceeds 64 MiB")
    nbt = FixtureNBT(raw)
    document = nbt.root()
    if nbt.f.read(1) or document["Version"] not in (5, 6, 7):
        raise ValueError("Unsupported Litematica fixture")
    if document["MinecraftDataVersion"] > 4671:
        raise ValueError("Fixture is newer than Minecraft 1.21.11")
    if not document["Regions"] or len(document["Regions"]) != document["Metadata"]["RegionCount"]:
        raise ValueError("Fixture region count mismatch")
    decoded = []
    volume = 0
    for name, region in document["Regions"].items():
        size = xyz(region["Size"])
        volume += math.prod(map(abs, size))
        if 0 in size or volume > 8_000_000:
            raise ValueError("Invalid fixture volume")
        decoded.append(Region(name, xyz(region["Position"]), size, tuple(region["BlockStatePalette"]),
                              tuple(region["BlockStates"]), tuple(region.get("TileEntities", ())),
                              tuple(region.get("Entities", ())), tuple(region.get("PendingBlockTicks", ())),
                              tuple(region.get("PendingFluidTicks", ()))))
    return document["MinecraftDataVersion"], tuple(decoded)


def inspect_regions(regions):
    """Identify placements that need an explicit overlap policy before importing."""
    occupied = {}
    counts = Counter()
    for region in regions:
        for position, state in region.blocks():
            counts["block_cells"] += 1
            if state["Name"] in audit.AIR:
                continue
            counts["non_air_entries"] += 1
            if position in occupied:
                counts["identical_overlaps" if occupied[position] == state else "conflicting_overlaps"] += 1
            occupied[position] = state
        for field in ("block_entities", "entities", "block_ticks", "fluid_ticks"):
            counts[field] += sum(1 for _ in region.positioned_records(field))
    counts["distinct_non_air_positions"] = len(occupied)
    return dict(counts)


def merge_regions(regions):
    blocks, block_entities, entities, block_ticks, fluid_ticks = {}, {}, {}, {}, {}

    def same_record(left, right, field):
        if field != "entities":
            # The same cell appears at different local offsets in overlapping regions.
            left = {key: value for key, value in left.items() if key not in ("x", "y", "z")}
            right = {key: value for key, value in right.items() if key not in ("x", "y", "z")}
        return left == right

    for region in regions:
        for position, state in region.blocks():
            if state["Name"] not in audit.AIR:
                if position in blocks and blocks[position] != state:
                    raise ValueError(f"Conflicting block states at {position}")
                blocks.setdefault(position, state)
        for field, target in (("block_entities", block_entities), ("entities", entities),
                              ("block_ticks", block_ticks), ("fluid_ticks", fluid_ticks)):
            for position, record in region.positioned_records(field):
                if field == "entities":
                    key = tuple(record["UUID"])
                elif field.endswith("ticks"):
                    key = (position, record.get("Block", record.get("Fluid")))
                else:
                    key = position
                if key in target and (target[key][0] != position or not same_record(target[key][1], record, field)):
                    raise ValueError(f"Conflicting {field} at {position}")
                target.setdefault(key, (position, record))
    missing = set(block_entities) - set(blocks)
    if missing:
        raise ValueError(f"{len(missing)} block entities have no block")
    return blocks, block_entities, tuple(entities.values()), tuple(block_ticks.values()), tuple(fluid_ticks.values())


def tag(kind, name, payload):
    name = name.encode("utf-8")
    return struct.pack(">BH", kind, len(name)) + name + payload


def tag_list(name, kind, entries):
    entries = list(entries)
    return tag(9, name, struct.pack(">Bi", kind, len(entries)) + b"".join(entries))


def template_tiles(blocks, block_entities, entities, data_version, tile_size=16):
    """Yield Vanilla template tiles staged with no block updates. Tick data is separate."""
    if not 1 <= tile_size <= 48:
        raise ValueError("Vanilla template tile size must be 1..48")

    def cell(coordinates):
        return tuple(math.floor(coordinate / tile_size) * tile_size for coordinate in coordinates)

    def relative(coordinates, origin):
        return tuple(value - base for value, base in zip(coordinates, origin))

    blocks_by_tile, entities_by_tile = defaultdict(list), defaultdict(list)
    for position, state in sorted(blocks.items()):
        blocks_by_tile[cell(position)].append((position, state))
    for position, entity in entities:
        entities_by_tile[cell(position)].append((position, entity))

    for origin in sorted(blocks_by_tile.keys() | entities_by_tile.keys()):
        palette, block_records, entity_records = {}, [], []
        for position, state in blocks_by_tile[origin]:
            payload = bytes(state.payload)
            index = palette.setdefault(payload, len(palette))
            record = tag_list("pos", 3, (struct.pack(">i", v) for v in relative(position, origin)))
            record += tag(3, "state", struct.pack(">i", index))
            if position in block_entities:
                record += tag(10, "nbt", block_entities[position][1].payload)
            block_records.append(record + b"\x00")

        for position, entity in entities_by_tile[origin]:
            coordinates = relative(position, origin)
            record = tag_list("pos", 6, (struct.pack(">d", v) for v in coordinates))
            record += tag_list("blockPos", 3, (struct.pack(">i", math.floor(v)) for v in coordinates))
            record += tag(10, "nbt", entity.payload)
            entity_records.append(record + b"\x00")

        template = tag(3, "DataVersion", struct.pack(">i", data_version))
        template += tag_list("size", 3, [struct.pack(">i", tile_size)] * 3)
        template += tag_list("palette", 10, palette.keys())
        template += tag_list("blocks", 10, block_records)
        template += tag_list("entities", 10, entity_records)
        yield origin, tag(10, "", template + b"\x00")
