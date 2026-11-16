"""Exact 1.21.11 carvers-stage block-state comparisons; no mismatch tolerance."""
import collections
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
exe=Path(sys.argv[1]).resolve()
postprocess='--postprocess' in sys.argv[2:]
checked=0; failures=0
with tempfile.TemporaryDirectory(prefix='terrain-reference-',dir=exe.parent) as folder:
    out=Path(folder)/'chunk.u32le'
    fixtures=ROOT/'tests/fixtures'
    if postprocess:fixtures=fixtures/'postprocess'
    for path in sorted(fixtures.glob('vanilla-density-*.json')):
        fixture=json.loads(path.read_text())
        assert fixture['version']=='1.21.11'
        assert fixture['server_sha1']=='64bb6d763bed0a9f1d632ec347938594144943ed'
        if postprocess:assert fixture['status']=='minecraft:full'
        else:assert fixture['status']=='minecraft:carvers' or (fixture['status']=='minecraft:full' and fixture.get('carvers_disabled'))
        assert fixture['features_disabled'] and fixture['structures_disabled']
        assert fixture['chunks'], 'Empty reference set'
        for chunk in fixture['chunks']:
            raw=zlib.decompress((path.parent/chunk['file']).read_bytes())
            assert len(raw)==98304*4
            assert hashlib.sha256(raw).hexdigest()==chunk['sha256']
            args=[str(exe),str(fixture['seed']),str(chunk['x']),str(chunk['z']),str(out)]
            if fixture.get('carvers_disabled'):args.append('--no-carvers')
            subprocess.run(args,check=True,capture_output=True,timeout=30)
            actual=out.read_bytes()
            assert len(actual)==len(raw)
            expected=struct.unpack('<98304I',raw); actual=struct.unpack('<98304I',actual)
            mismatch=[i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b]
            checked+=len(expected)
            if mismatch:
                failures+=1
                print(f"FAIL seed={fixture['seed']} chunk=({chunk['x']},{chunk['z']}): {len(mismatch)} / 98304 states differ")
                print('actual -> expected:',collections.Counter((actual[i],expected[i]) for i in mismatch).most_common(12))
                for i in mismatch[:4]:
                    print(' ',chunk['x']*16+i%16,i//256-64,chunk['z']*16+i//16%16,actual[i],expected[i])
            else:print(f"PASS {fixture['seed']} ({chunk['x']},{chunk['z']}): 98304 blocks")
assert checked==(4718592 if postprocess else 6291456), 'Missing or unexpected reference chunk set'
print(f'{checked} reference block states checked; {failures} chunks differ')
sys.exit(1 if failures else 0)
