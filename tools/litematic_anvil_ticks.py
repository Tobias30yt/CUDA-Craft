"""Restore fixture tick lists in the generated, stopped reference world."""
from collections import defaultdict
import gzip
import struct
import zlib

from litematic_fixture import FixtureNBT, add, tag, tag_list

SECTOR = 4096


def encode_tick(tick):
    kind = tick["i"].encode("ascii")
    return tag(8, "i", struct.pack(">H",len(kind)) + kind) + b"".join(
        tag(3, key, struct.pack(">i",tick[key])) for key in ("x","y","z","t","p")
    ) + b"\x00"


def chunk_tick_records(records, offset, kind):
    result = defaultdict(list)
    for position, record in sorted(records, key=lambda record:record[1].get("SubTick",0)):
        x,y,z = add(position,offset)
        ticks = result[x//16,z//16]
        ticks.append(dict(i=record[kind],x=x,y=y,z=z,t=record["Time"],p=record["Priority"]))
    return result


def key(record):
    return record["i"],record["x"],record["y"],record["z"]


def merge_chunk_ticks(data, updates):
    state = FixtureNBT(data).root()
    fields = FixtureNBT(data).root_fields()
    for name, records in updates.items():
        old = state.get(name,[])
        overwritten = {key(record) for record in records}
        remaining = [record for record in old if key(record) not in overwritten]
        fields[name] = tag_list(name,10,map(encode_tick,remaining+records))
    return tag(10,"",b"".join(fields.values()) + b"\x00")


def restore_ticks(world, block_ticks, fluid_ticks, offset):
    updates_by_file = defaultdict(dict)
    for name, kind, ticks in (("block_ticks","Block",block_ticks),("fluid_ticks","Fluid",fluid_ticks)):
        for (cx,cz), records in chunk_tick_records(ticks,offset,kind).items():
            path = world/"region"/f"r.{cx//32}.{cz//32}.mca"
            entry = (cz%32)*32+cx%32
            updates_by_file[path].setdefault(entry,{})[name] = records
    for path, updates in updates_by_file.items():
        region = bytearray(path.read_bytes())
        if len(region) < 2*SECTOR or len(region)%SECTOR:
            raise ValueError(f"Invalid Anvil region: {path.name}")
        for entry, tick_lists in updates.items():
            header = entry*4
            start = int.from_bytes(region[header:header+3],"big")*SECTOR
            sectors = region[header+3]
            if start < 2*SECTOR or not sectors or start+sectors*SECTOR > len(region):
                raise ValueError(f"Missing fixture chunk at region entry {entry}")
            length = struct.unpack_from(">I",region,start)[0]
            if not 1 < length <= sectors*SECTOR-4:
                raise ValueError("Invalid Anvil record length")
            compression = region[start+4]
            raw = bytes(region[start+5:start+4+length])
            if compression not in (1,2,3):
                raise ValueError(f"Unsupported reference chunk compression {compression}")
            raw = (gzip.decompress(raw) if compression==1 else zlib.decompress(raw) if compression==2 else raw)
            compressed = zlib.compress(merge_chunk_ticks(raw,tick_lists))
            record = struct.pack(">IB",len(compressed)+1,2)+compressed
            sectors = (len(record)+SECTOR-1)//SECTOR
            if sectors > 255:
                raise ValueError("Reference chunk exceeds inline Anvil capacity")
            region[header:header+4] = (len(region)//SECTOR).to_bytes(3,"big")+bytes([sectors])
            region.extend(record.ljust(sectors*SECTOR,b"\x00"))
        temporary = path.with_suffix(".fixture.tmp")
        with temporary.open("xb") as stream:
            stream.write(region)
        temporary.replace(path)
