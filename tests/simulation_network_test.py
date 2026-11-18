"""Encrypted 1.21.11 wire tests for survival, entities, water and redstone."""
import struct
import unittest
import time
import game_network_test as game

login=game.login
vi=game.vi

def slot(r):
    count=r.varint()
    if not count:return (0,0,0)
    item=r.varint();added=r.varint();removed=r.varint();damage=0
    for _ in range(added):
        assert r.varint()==3,'Unexpected component'
        damage=r.varint()
    for _ in range(removed):r.varint()
    return item,count,damage

class Player(game.Player):
    def __init__(self,server,operator=True):
        self.play=False;self.inventory={};self.revision=0;self.mode=None;self.health=None;self.food=None;self.mob_types={};self.motion={};self.metadata={};self.dead=False;self.respawns=0
        super().__init__(server)
        if operator:
            server.process.stdin.write('op CudaTester\n');server.process.stdin.flush()
            self.until(lambda:any(b'You are now an operator.' in message for message in self.messages))
    def configure(self):
        super().configure();self.play=True
    def packet(self):
        data=super().packet()
        if not self.play:return data
        r=login.Buffer(data);pid=r.varint()
        if pid==0x12:
            self.window=r.varint();self.revision=r.varint();self.inventory={i:slot(r) for i in range(r.varint())};self.cursor=slot(r);r.end()
        elif pid==0x14:
            r.varint();self.revision=r.varint();index=struct.unpack('>h',r.read(2))[0];self.inventory[index]=slot(r);r.end()
        elif pid==0x66:
            self.health=struct.unpack('>f',r.read(4))[0];self.food=r.varint();self.saturation=struct.unpack('>f',r.read(4))[0];r.end()
        elif pid==0x26:
            reason=r.read(1)[0];value=struct.unpack('>f',r.read(4))[0]
            if reason==3:self.mode=int(value)
            r.end()
        elif pid==0x23:
            entity=r.varint();position=struct.unpack('>ddd',r.read(24));velocity=struct.unpack('>ddd',r.read(24))
            r.read(9);r.end();self.motion[entity]=(position,velocity)
        elif pid==0x01:
            entity=r.varint();r.read(16);kind=r.varint();xyz=struct.unpack('>ddd',r.read(24))
            assert r.read(1)==b'\0','Expected stationary spawn lpVec3'
            r.read(3);r.varint();r.end();self.mob_types[entity]=(kind,xyz)
        elif pid==0x61:
            entity=r.varint();values={}
            while True:
                index=r.read(1)[0]
                if index==255:break
                kind=r.varint()
                if kind in (0,8):value=r.read(1)[0]
                elif kind==1:value=r.varint()
                elif kind==3:value=struct.unpack('>f',r.read(4))[0]
                elif kind==7:value=slot(r)
                else:raise AssertionError('Unknown metadata type '+str(kind))
                values[index]=value
            self.metadata[entity]=values;r.end()
        elif pid==0x50:
            dimension_type=r.varint();dimension=r.string()
            assert dimension_type=={'minecraft:overworld':0,'minecraft:the_nether':3,'minecraft:the_end':2}[dimension]
            r.read(8);r.read(4);assert r.read(1)==b'\0';r.varint();r.varint();r.read(1);r.end();self.respawns+=1
        elif pid==0x42:self.dead=True
        return data
    def command(self,text):self.send(b'\x06'+login.string(text))
    def mode_set(self,value):
        self.command('gamemode '+('survival' if value==0 else 'creative'));self.until(lambda:self.mode==value)
    def give(self,item,count=64,index=36):
        old=self.revision
        self.send(b'\x37'+struct.pack('>h',index)+vi(count)+vi(item)+vi(0)+vi(0))
        self.until(lambda:self.revision>old)
    def click(self,index,button=0):
        old=self.revision
        self.send(b'\x11'+vi(0)+vi(old)+struct.pack('>hB',index,button)+vi(0)+vi(0)+b'\0')
        self.until(lambda:self.revision>old)

class SimulationNetworkTests(unittest.TestCase):
    def setUp(self):
        game.wire.EXE=game.FIXTURE;self.server=game.wire.RunningServer('--gamemode','survival')
    def tearDown(self):self.server.close()
    def test_survival_inventory_craft_mob_death_respawn(self):
        p=Player(self.server)
        try:
            self.assertEqual(p.health,20);self.assertTrue(all(not s[0] for s in p.inventory.values()))
            p.give(1);self.assertEqual(p.inventory[36][0],0)
            p.mode_set(1)
            ids={'oak_log':134,'oak_planks':36,'porkchop':983} # Pinned 1.21.11 registry.
            p.give(ids['oak_log'],2);p.mode_set(0)
            p.click(36);p.click(1,1);p.click(36);p.click(0);p.click(37)
            self.assertEqual(p.inventory[37],(ids['oak_planks'],4,0));self.assertEqual(p.inventory[36][1],1)
            p.mode_set(1);p.command('summon pig')
            p.until(lambda:any(kind==100 for kind,_ in p.mob_types.values()))
            pig=next(e for e,(kind,_) in p.mob_types.items() if kind==100)
            p.send(b'\x19'+vi(pig)+vi(1)+b'\0')
            p.until(lambda:any(v.get(8,(0,))[0]==ids['porkchop'] for v in p.metadata.values() if isinstance(v.get(8),tuple)))
            p.mode_set(0)
            p.send(b'\x1d'+struct.pack('>dddB',.5,92,.5,0));p.send(b'\x1d'+struct.pack('>dddB',.5,64,.5,1))
            p.until(lambda:p.dead);self.assertEqual(p.health,0)
            p.teleported=False;p.send(b'\x0b'+vi(0));p.until(lambda:p.respawns==1 and p.teleported and p.health==20)
        finally:p.close()
    def test_survival_mob_knockback(self):
        p=Player(self.server)
        try:
            p.command('summon pig 1.5 64 0.5')
            p.until(lambda:any(kind==100 for kind,_ in p.mob_types.values()))
            pig=next(e for e,(kind,_) in p.mob_types.items() if kind==100)
            p.send(b'\x19'+vi(pig)+vi(1)+b'\0')
            p.until(lambda:pig in p.motion and p.motion[pig][1][0]>.2 and p.motion[pig][1][1]>0)
            position,velocity=p.motion[pig]
            self.assertGreater(position[1],64)
            self.assertGreater(velocity[0],0)
            # The pig may walk before the attack arrives. Compare successive
            # knockback snapshots, not its original summon coordinate.
            p.until(lambda:p.motion[pig][0][0]>position[0] and p.motion[pig][0][1]>position[1])
        finally:p.close()
    def test_dimensions_inventory_and_chunk_layout(self):
        p=Player(self.server)
        try:
            p.mode_set(1);p.give(1,9);p.mode_set(0)
            for name,chunk in [('nether',(0,0)),('end',(6,0)),('overworld',(0,0))]:
                expected='minecraft:'+('the_'+name if name!='overworld' else name)
                p.command('dimension '+name)
                p.until(lambda:p.dimension==expected and p.teleported and chunk in p.chunks,timeout=15)
                self.assertEqual(len(p.chunks[chunk][0]),16*16*(384 if name=='overworld' else 256))
                self.assertEqual(p.inventory[36],(1,9,0))
                if name=='nether':
                    self.assertNotEqual(p.block(0,63,0),0)
                    self.assertEqual(set(p.biomes[chunk]),{34})
                if name=='end':
                    self.assertNotEqual(p.block(100,48,0),0)
                    self.assertEqual(p.block(100,49,0),0)
                    self.assertEqual(set(p.biomes[chunk]),{56})
            self.assertEqual(p.respawns,3)
        finally:p.close()
    def test_water_bucket_flow_and_recession(self):
        p=Player(self.server)
        try:
            p.mode_set(1);p.give(1013,1);p.mode_set(0)
            p.place(2,63,1,80);p.until(lambda:80 in p.acks)
            p.until(lambda:p.changes.get((3,64,1))==87)
            self.assertEqual(p.inventory[36],(1012,1,0))
            p.place(2,64,1,81);p.until(lambda:81 in p.acks)
            p.until(lambda:p.changes.get((3,64,1))==0,timeout=6)
            self.assertEqual(p.inventory[36],(1013,1,0))
        finally:p.close()
    def test_redstone_lever_wire_lamp(self):
        p=Player(self.server)
        try:
            p.mode_set(1);p.give(732);p.place(0,63,2,90);p.until(lambda:90 in p.acks)
            p.give(717)
            for x in range(1,4):p.place(x,63,2,90+x);p.until(lambda:90+x in p.acks)
            p.give(747);p.place(4,63,2,94);p.until(lambda:94 in p.acks)
            p.place(0,64,2,95);p.until(lambda:p.changes.get((4,64,2))==9278)
            self.assertEqual(((p.block(3,64,2)-3810)//9)%16,13)
            p.place(0,64,2,96);p.until(lambda:p.changes.get((4,64,2))==9279)
            self.assertEqual(((p.block(3,64,2)-3810)//9)%16,0)
        finally:p.close()

if __name__=='__main__':unittest.main()
