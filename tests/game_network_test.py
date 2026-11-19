"""End-to-end encrypted creative-world tests, using an independent chunk decoder."""
import json
import hashlib
import math
import select
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

PRODUCTION,FIXTURE=sys.argv[1:3]
import login_network_test as login
wire=login.wire

def vi(n):return wire.varint(n)
def packed(x,y,z):return struct.pack('>Q',((x&0x3ffffff)<<38)|((z&0x3ffffff)<<12)|(y&4095))
def unpack(value):
    x=value>>38;z=(value>>12)&0x3ffffff;y=value&4095
    return (x-(0x4000000 if x&0x2000000 else 0),y-(4096 if y&2048 else 0),z-(0x4000000 if z&0x2000000 else 0))

def decode_light(r):
    masks=[[struct.unpack('>Q',r.read(8))[0] for _ in range(r.varint())] for _ in range(4)]
    layers=[]
    for active,empty in ((masks[0],masks[2]),(masks[1],masks[3])):
        sections=[64*i+bit for i,word in enumerate(active) for bit in range(64) if word & (1<<bit)]
        assert r.varint()==len(sections)
        layer={}
        for section in sections:
            payload=r.array()
            assert len(payload)==2048
            layer[section]=payload
        for i,word in enumerate(empty):
            for bit in range(64):
                if word & (1<<bit):
                    section=64*i+bit
                    assert section not in layer
                    layer[section]=bytes(2048)
        layers.append(layer)
    return layers

def decode_chunk(r,biomes_out=None,section_count=24,light_out=None):
    cx,cz=struct.unpack('>ii',r.read(8))
    heightmaps={}
    for _ in range(r.varint()):
        kind=r.varint(); words=[struct.unpack('>Q',r.read(8))[0] for _ in range(r.varint())]
        assert len(words)==37
        heightmaps[kind]=[(words[i//7]>>((i%7)*9))&511 for i in range(256)]
    body=login.Buffer(r.array())
    states=[]
    biomes=[]
    for _ in range(section_count):
        nonair=struct.unpack('>H',body.read(2))[0]
        bits=body.read(1)[0]
        if bits==0: section=[body.varint()]*4096
        else:
            assert 4<=bits<=8 or bits==15
            palette=[body.varint() for _ in range(body.varint())] if bits<=8 else None
            per_word=64//bits
            words=[struct.unpack('>Q',body.read(8))[0] for _ in range((4096+per_word-1)//per_word)]
            indices=[(words[i//per_word]>>((i%per_word)*bits))&((1<<bits)-1) for i in range(4096)]
            section=[palette[i] for i in indices] if palette is not None else indices
        assert sum(s!=0 for s in section)==nonair
        states.extend(section)
        biome_bits=body.read(1)[0]
        if biome_bits==0: biomes.extend([body.varint()]*64)
        else:
            assert 1<=biome_bits<=3 or biome_bits==7
            palette=[body.varint() for _ in range(body.varint())] if biome_bits<=3 else None
            per_word=64//biome_bits
            words=[struct.unpack('>Q',body.read(8))[0] for _ in range((64+per_word-1)//per_word)]
            indices=[(words[i//per_word]>>((i%per_word)*biome_bits))&((1<<biome_bits)-1) for i in range(64)]
            biomes.extend([palette[i] for i in indices] if palette is not None else indices)
    assert all(0<=b<65 for b in biomes)
    if biomes_out is not None:biomes_out[cx,cz]=biomes
    body.end()
    assert r.varint()==0
    _,block_light=decode_light(r)
    if light_out is not None:light_out[cx,cz]=block_light
    r.end()
    return (cx,cz),states,heightmaps

class Player(login.Client):
    def __init__(self,server,name='CudaTester'):
        super().__init__(server)
        self.chunks={};self.biomes={};self.block_light={};self.light_updates=set();self.changes={};self.acks=set();self.spawned=set();self.keepalives=0;self.teleported=False;self.messages=[];self.dimension='minecraft:overworld';self.min_y=-64
        identity=bytes(range(15))+b'\x10' if name=='SecondTester' else bytes(range(16))
        self.begin(name,uuid=identity);self.success(name);self.configure()
        self.until(lambda:self.teleported and (math.floor(self.position[0]/16),math.floor(self.position[2]/16)) in self.chunks,timeout=15)
    def step(self,timeout=1):
        if not select.select([self.sock],[],[],timeout)[0]:return
        r=login.Buffer(self.packet());pid=r.varint()
        if pid==0x30:
            self.entity=struct.unpack('>i',r.read(4))[0]
            r.read(1)
            for _ in range(r.varint()):r.string()
            for _ in range(3):r.varint()
            r.read(3);r.varint();self.dimension=r.string();self.min_y=-64 if self.dimension=='minecraft:overworld' else 0
            self.seed_hash=struct.unpack('>Q',r.read(8))[0]
        elif pid==0x50:
            r.varint();self.dimension=r.string();self.min_y=-64 if self.dimension=='minecraft:overworld' else 0
            self.chunks.clear();self.biomes.clear();self.block_light.clear();self.light_updates.clear();self.changes.clear();self.spawned.clear();self.teleported=False
        elif pid==0x46:
            teleport=r.varint();self.position=struct.unpack('>ddd',r.read(24));self.send(b'\0'+vi(teleport));self.teleported=True
        elif pid==0x2c:
            key,states,maps=decode_chunk(r,self.biomes,24 if self.min_y==-64 else 16,self.block_light);self.chunks[key]=(states,maps)
        elif pid==0x2f:
            key=tuple((value if value<2**31 else value-2**32) for value in (r.varint(),r.varint()))
            _,block_light=decode_light(r)
            self.block_light.setdefault(key,{}).update(block_light)
            self.light_updates.add(key)
            r.end()
        elif pid==0x0b:self.send(b'\x0a'+struct.pack('>f',20))
        elif pid==0x2b:self.send(b'\x1b'+r.read(8));self.keepalives+=1
        elif pid==0x08:
            position=unpack(struct.unpack('>Q',r.read(8))[0]);self.changes[position]=r.varint()
        elif pid==0x04:self.acks.add(r.varint())
        elif pid==0x01:self.spawned.add(r.varint())
        elif pid==0x20:raise RuntimeError('Unexpected play disconnect: '+repr(r.io.read()))
        elif pid==0x77:self.messages.append(r.io.read())
    def until(self,predicate,timeout=6):
        deadline=time.monotonic()+timeout
        while not predicate():
            if time.monotonic()>deadline:raise TimeoutError('Expected game state did not arrive')
            self.step(min(0.25,max(0,deadline-time.monotonic())))
    def place(self,x,y,z,seq=42):
        self.send(b'\x3f'+vi(0)+packed(x,y,z)+vi(1)+struct.pack('>fff',.5,1,.5)+b'\0\0'+vi(seq))
    def dig(self,x,y,z,seq=43):
        self.send(b'\x28'+vi(0)+packed(x,y,z)+b'\x01'+vi(seq))
    def block(self,x,y,z):
        if (x,y,z) in self.changes:return self.changes[x,y,z]
        states,_=self.chunks[x//16,z//16]
        return states[(y-self.min_y)*256+(z&15)*16+(x&15)]
    def light(self,x,y,z):
        section=(y-self.min_y)//16+1
        layer=self.block_light.get((x//16,z//16),{}).get(section)
        if layer is None:return None
        index=(y&15)*256+(z&15)*16+(x&15)
        return (layer[index//2] >> (4*(index%2))) & 15

class GameTests(unittest.TestCase):
    def setUp(self):
        wire.EXE=FIXTURE
        self.server=wire.RunningServer()
    def tearDown(self):self.server.close()
    def test_global_block_palette_over_256_states(self):
        self.server.close()
        states=list(range(1000,1300))  # Non-fluid/non-redstone 1.21.11 states; exercise the global palette.
        self.assertEqual(len(set(states)),300)
        with tempfile.TemporaryDirectory(prefix='palette-test-',dir=Path(FIXTURE).resolve().parent) as folder:
            edits=[[i%16,80+i//256,(i//16)%16,state] for i,state in enumerate(states)]
            (Path(folder)/'blocks.json').write_text(json.dumps({'format':2,'protocol':774,'generator':'cuda-craft-flat-v1','seed':0,'edits':edits}))
            self.server=wire.RunningServer('--world',folder)
            p=Player(self.server)
            try:
                p.until(lambda:(0,0) in p.chunks)
                for x,y,z,state in edits:self.assertEqual(p.block(x,y,z),state)
            finally:p.close();self.server.close()
    def test_world_chunks_and_movement(self):
        p=Player(self.server)
        try:
            self.assertEqual(p.block(0,-64,0),85)
            self.assertEqual(p.block(0,60,0),10)
            self.assertEqual(p.block(0,63,0),9)
            self.assertEqual(p.block(0,64,0),0)
            self.assertEqual(p.chunks[0,0][1][1],[128]*256)
            p.until(lambda:len(p.chunks)>=49)
            p.send(b'\x1e'+struct.pack('>dddffB',48.5,70,.5,20,0,0))
            p.until(lambda:(6,0) in p.chunks)
            p.teleported=False
            p.send(b'\x06'+login.string('spawn'))
            p.until(lambda:p.teleported)
            self.assertEqual(p.position,(.5,64,.5))
            p.send(b'\x1e'+struct.pack('>dddffB',4.5,64,.5,0,0,0))
            p.place(0,63,0,61);p.until(lambda:61 in p.acks)
            self.assertEqual(p.block(0,64,0),1)
            p.teleported=False;p.send(b'\x06'+login.string('spawn'));p.until(lambda:p.teleported)
            self.assertEqual(p.position,(.5,65,.5))
        finally:p.close()
    def test_two_players_build_break_and_inventory(self):
        first=Player(self.server);second=Player(self.server,'SecondTester')
        try:
            first.until(lambda:second.entity in first.spawned)
            self.assertIn(first.entity,second.spawned)
            first.place(1,63,1)
            first.until(lambda:42 in first.acks)
            second.until(lambda:second.changes.get((1,64,1))==1)
            self.assertEqual(first.block(1,64,1),1)
            first.dig(1,64,1)
            second.until(lambda:second.changes.get((1,64,1))==0)
            first.send(b'\x37'+struct.pack('>h',36)+vi(64)+vi(195)+vi(0)+vi(0))
            first.place(1,63,1,44)
            first.until(lambda:44 in first.acks)
            second.until(lambda:second.changes.get((1,64,1))==562)
            first.place(10,63,10,45)
            first.until(lambda:45 in first.acks)
            self.assertEqual(first.block(10,64,10),0)
        finally:first.close();second.close()
    def test_edit_survives_restart(self):
        self.server.close()
        with tempfile.TemporaryDirectory(prefix='world-test-',dir=Path(FIXTURE).resolve().parent) as folder:
            self.server=wire.RunningServer('--world',folder)
            p=Player(self.server)
            try:
                p.place(2,63,2,55);p.until(lambda:55 in p.acks)
                path=Path(folder)/'blocks.json';deadline=time.monotonic()+5
                while not path.exists() or [2,64,2,1] not in json.loads(path.read_text())['edits']:
                    if time.monotonic()>deadline:raise TimeoutError('World did not autosave')
                    p.step(.1)
                self.assertIn([2,64,2,1],json.loads(path.read_text())['edits'])
            finally:p.close()
            self.server.close()
            self.server=wire.RunningServer('--world',folder)
            p=Player(self.server)
            try:self.assertEqual(p.block(2,64,2),1)
            finally:p.close();self.server.close()
    def test_overworld_stream_save_and_seed_protection(self):
        self.check_seeded_world('overworld')
    def test_climate_world_matches_vanilla_biomes(self):
        self.check_seeded_world('climate')
    def test_density_world_stream_build_save_reload(self):
        self.check_seeded_world('density')
    def check_seeded_world(self,generator):
        self.server.close()
        with tempfile.TemporaryDirectory(prefix='overworld-test-',dir=Path(FIXTURE).resolve().parent) as folder:
            self.server=wire.RunningServer('--world',folder,'--generator',generator,'--seed','20261113')
            p=Player(self.server)
            try:
                p.until(lambda:len(p.chunks)>=49,timeout=30)
                self.assertGreater(len({b for values in p.biomes.values() for b in values}),1)
                heights=[h for _,maps in p.chunks.values() for h in maps[1]]
                self.assertGreater(max(heights)-min(heights),5)
                if generator in ('climate','density'):
                    expected_hash=int.from_bytes(hashlib.sha256(struct.pack('<q',20261113)).digest()[:8],'little')
                    self.assertEqual(p.seed_hash,expected_hash)
                    fixture=json.loads((Path(__file__).parent/'fixtures/vanilla-biomes-20261113.json').read_text())
                    checked=0
                    for chunk in fixture['chunks']:
                        key=chunk['x'],chunk['z']
                        if key not in p.biomes:continue
                        expected=[v for v,count in chunk['biomes_rle'] for _ in range(count)]
                        self.assertEqual(p.biomes[key],expected);checked+=1
                    self.assertGreater(checked,0,'Spawn view did not include any reference chunk')
                x,y,z=map(math.floor,p.position)
                # Step away, then build on the exact solid spawn floor; terrain need not be flat.
                p.send(b'\x1e'+struct.pack('>dddffB',x+4.5,y,z+.5,0,0,0))
                p.place(x,y-1,z,70);p.until(lambda:70 in p.acks)
                self.assertEqual(p.block(x,y,z),1)
                path=Path(folder)/'blocks.json';deadline=time.monotonic()+5
                while [x,y,z,1] not in json.loads(path.read_text())['edits']:
                    if time.monotonic()>deadline:raise TimeoutError('Natural-world edit did not save')
                    p.step(.1)
                saved=json.loads(path.read_text());self.assertEqual(saved['seed'],20261113)
                self.assertEqual(saved['generator'],'cuda-craft-'+generator+'-v1')
                key=math.floor(x/16),math.floor(z/16)
                original_biomes=p.biomes[key]
            finally:p.close();self.server.close()
            self.server=wire.RunningServer('--world',folder)
            p=Player(self.server)
            try:
                self.assertEqual(p.block(x,y,z),1)
                self.assertEqual(p.position,(x+4.5,y,z+.5),'Creative reconnect retains its saved position, not the changed spawn floor')
                self.assertEqual(p.biomes[key],original_biomes)
            finally:p.close();self.server.close()
            before=path.read_bytes()
            for args in [('--generator','flat'),('--seed','999')]:
                failed=subprocess.run([PRODUCTION,'--world',folder,*args,'--port','0','--run-for-ms','10'],capture_output=True,timeout=10)
                self.assertNotEqual(failed.returncode,0)
                self.assertIn(b'Choose a NEW --world',failed.stderr)
                self.assertEqual(path.read_bytes(),before)
    def test_legacy_flat_world_is_preserved(self):
        self.server.close()
        with tempfile.TemporaryDirectory(prefix='legacy-world-test-',dir=Path(FIXTURE).resolve().parent) as folder:
            path=Path(folder)/'blocks.json'
            legacy={'format':1,'protocol':774,'generator':'cuda-craft-flat-v1','edits':[[2,64,2,1]]}
            path.write_text(json.dumps(legacy))
            self.server=wire.RunningServer('--world',folder)
            p=Player(self.server)
            try:
                self.assertEqual(p.position,(.5,64,.5))
                self.assertEqual(p.block(2,64,2),1)
                self.assertEqual(set(p.biomes[0,0]),{40})
                p.place(3,63,3,71);p.until(lambda:71 in p.acks)
                deadline=time.monotonic()+5
                while json.loads(path.read_text())['format']==1:
                    if time.monotonic()>deadline:raise TimeoutError('Legacy world upgrade did not save')
                    p.step(.1)
                upgraded=json.loads(path.read_text())
                self.assertEqual(upgraded['seed'],0)
                self.assertEqual(upgraded['generator'],'cuda-craft-flat-v1')
                self.assertIn([2,64,2,1],upgraded['edits'])
                self.assertIn([3,64,3,1],upgraded['edits'])
            finally:p.close();self.server.close()
    def test_play_survives_login_deadline(self):
        p=Player(self.server)
        try:
            deadline=time.monotonic()+31
            while time.monotonic()<deadline:p.step(.5)
            self.assertGreaterEqual(p.keepalives,5)
            p.place(3,63,3,99);p.until(lambda:99 in p.acks)
            self.assertEqual(p.block(3,64,3),1)
        finally:p.close()

if __name__=='__main__':unittest.main(verbosity=2)
