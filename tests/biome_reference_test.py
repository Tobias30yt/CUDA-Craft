"""Compare native biome output with facts captured from unmodified Vanilla 1.21.11.
No JVM, download or official server is required to run this regression test.
"""
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

probe=Path(sys.argv[1]);fixtures=Path(__file__).parent/'fixtures'
total=0
for path in sorted(fixtures.glob('vanilla-biomes-*.json')):
    reference=json.loads(path.read_text());seed=reference['seed']
    assert reference['version']=='1.21.11'
    assert reference['server_sha1']=='64bb6d763bed0a9f1d632ec347938594144943ed'
    coordinates=[str(v) for chunk in reference['chunks'] for v in (chunk['x'],chunk['z'])]
    native=json.loads(subprocess.check_output([str(probe),str(seed),*coordinates],text=True,timeout=30))
    expected_hash=int.from_bytes(hashlib.sha256(struct.pack('<q',seed)).digest()[:8],'little')
    assert native['seed_hash']==expected_hash,('Seed hash mismatch',seed)
    for expected,actual in zip(reference['chunks'],native['chunks'],strict=True):
        values=[v for v,count in expected['biomes_rle'] for _ in range(count)]
        assert all(0<=v<65 and count>0 for v,count in expected['biomes_rle'])
        assert len(values)==1536 and len(actual['biomes'])==1536
        mismatches=[(i,a,b) for i,(a,b) in enumerate(zip(values,actual['biomes'])) if a!=b]
        if mismatches:
            raise AssertionError(f'{path.name} chunk ({expected["x"]},{expected["z"]}): {len(mismatches)} mismatches, first {mismatches[:8]}')
        total+=len(values)
    print(f'Vanilla biome parity passed: seed {seed}, {len(reference["chunks"])} chunks')
assert total>0,'No official-reference fixtures found'
print(f'{total} biome cells match official 1.21.11 captures; SHA-256 seed hashes verified')
