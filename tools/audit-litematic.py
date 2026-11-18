"""Read-only Litematica fixture inventory. Registry recognition is NOT gameplay parity.

No schematics, positions, inventory contents or entity UUIDs are published by this
report. Original fixtures remain local; hashes pin the exact files being audited.
"""
import argparse
from collections import Counter
import gzip
import hashlib
import importlib.util
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('reference_nbt', ROOT / 'tools/capture-vanilla-biomes.py')
nbt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nbt)
AIR = {'minecraft:air', 'minecraft:cave_air', 'minecraft:void_air'}


def indices(words, count, palette_size):
    if not 1 <= palette_size <= 65536 or not 0 <= count <= 8_000_000:
        raise ValueError('Invalid palette or region volume')
    bits = max(2, (palette_size - 1).bit_length())
    if len(words) != (count * bits + 63) // 64:
        raise ValueError('Packed block array has the wrong length')
    mask = (1 << bits) - 1
    for i in range(count):
        word, shift = divmod(i * bits, 64)
        value = (words[word] & 0xffffffffffffffff) >> shift
        if shift + bits > 64:
            value |= (words[word + 1] & 0xffffffffffffffff) << (64 - shift)
        value &= mask
        if value >= palette_size:
            raise ValueError('Packed block index exceeds palette')
        yield value


def state_error(entry, registry):
    name = entry['Name']
    if name not in registry:
        return 'unknown block: ' + name
    properties = registry[name].get('properties', {})
    for key, value in entry.get('Properties', {}).items():
        if key not in properties or value not in properties[key]:
            return f'unknown property: {name}[{key}={value}]'
    return None


def audit(path, registry, target_data_version):
    if path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError('Compressed fixture exceeds 16 MiB')
    with gzip.open(path, 'rb') as stream:
        raw = stream.read(64 * 1024 * 1024 + 1)
    if len(raw) > 64 * 1024 * 1024:
        raise ValueError('Decompressed fixture exceeds 64 MiB')
    reader = nbt.NBT(raw)
    data = reader.root()
    if reader.f.read(1):
        raise ValueError('Trailing NBT data')
    if data['Version'] not in (5, 6, 7):
        raise ValueError('Unsupported Litematica format')
    blocks, block_entities, entities = Counter(), Counter(), Counter()
    inventory_entities, item_stacks, pending_block_ticks, pending_fluid_ticks = 0, 0, 0, 0
    errors, regions = set(), []

    def entity_counts(entity):
        entities[entity.get('id', '<missing id>')] += 1
        for passenger in entity.get('Passengers', []):
            entity_counts(passenger)

    for name, region in sorted(data['Regions'].items()):
        size = [region['Size'][axis] for axis in ('x', 'y', 'z')]
        if any(v == 0 for v in size):
            raise ValueError('Empty region')
        volume = math.prod(abs(v) for v in size)
        palette = region['BlockStatePalette']
        counts = Counter(indices(region['BlockStates'], volume, len(palette)))
        non_air = 0
        for index, count in counts.items():
            entry = palette[index]; block = entry['Name']; blocks[block] += count
            if block not in AIR: non_air += count
            error = state_error(entry, registry)
            if error: errors.add(error)
        for entity in region.get('TileEntities', []):
            block_entities[entity.get('id', '<missing id>')] += 1
            items = entity.get('Items', entity.get('items', []))
            inventory_entities += bool(items); item_stacks += len(items)
        for entity in region.get('Entities', []): entity_counts(entity)
        pending_block_ticks += len(region.get('PendingBlockTicks', []))
        pending_fluid_ticks += len(region.get('PendingFluidTicks', []))
        regions.append({'name': name, 'size': size, 'volume': volume, 'non_air_blocks': non_air,
                        'palette_states': len(palette), 'used_states': len(counts)})
    total = sum(count for name, count in blocks.items() if name not in AIR)
    declared = data['Metadata']['TotalBlocks']
    if total != declared:
        raise ValueError(f'Block count mismatch: decoded {total}, metadata {declared}')
    return {
        'file': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'litematica_version': data['Version'], 'data_version': data['MinecraftDataVersion'],
        'newer_than_target': data['MinecraftDataVersion'] > target_data_version,
        'registry_state_errors': sorted(errors), 'registry_states_recognized': not errors,
        'gameplay_verified': False, 'non_air_blocks': total, 'regions': regions,
        'blocks': dict(sorted(blocks.items())), 'block_entities': dict(sorted(block_entities.items())),
        'entities_including_passengers': dict(sorted(entities.items())),
        'block_entities_with_items': inventory_entities, 'stored_item_stacks': item_stacks,
        'pending_block_ticks': pending_block_ticks, 'pending_fluid_ticks': pending_fluid_ticks,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixtures', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--blocks', type=Path, default=ROOT / '.tools/vanilla-1.21.11/reports-1.21.11/reports/blocks.json')
    parser.add_argument('--require-compatible', action='store_true', help='Reject newer data versions or unknown states; does not assert gameplay parity')
    args = parser.parse_args()
    raw = args.blocks.read_bytes(); registry = json.loads(raw)
    results = [audit(path, registry, 4671) for path in sorted(args.fixtures)]
    report = {'target_minecraft': '1.21.11', 'target_data_version': 4671,
              'registry_sha256': hashlib.sha256(raw).hexdigest(), 'fixtures': results}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf-8', newline='\n')
    for result in results:
        print(f"{result['file']}: {result['non_air_blocks']} blocks; newer={result['newer_than_target']}; state errors={len(result['registry_state_errors'])}; gameplay unverified")
    if args.require_compatible and any(r['newer_than_target'] or r['registry_state_errors'] for r in results):
        raise SystemExit('Fixture compatibility gate FAILED; see audit JSON')


if __name__ == '__main__': main()
