"""Independent encrypted container clients: shared revisions, routing, drops and restart."""
import json, struct, tempfile, time, unittest
from pathlib import Path
import simulation_network_test as simulation
game=simulation.game;login=game.login;vi=game.vi

class Player(game.Player):
    def __init__(self,server,name='CudaTester'):
        self.play=False;self.window=0;self.revision=0;self.inventory={};self.cursor=(0,0,0);self.opened=None;self.closed=[];self.drops={};self.properties={};self.offhand=(0,0,0);self.pings=0;self.pong=0
        super().__init__(server,name)
    def configure(self):super().configure();self.play=True
    def packet(self):
        data=super().packet()
        if not self.play:return data
        r=login.Buffer(data);pid=r.varint()
        if pid==0x12:
            self.window=r.varint();self.revision=r.varint();self.inventory={i:simulation.slot(r) for i in range(r.varint())};self.cursor=simulation.slot(r);r.end()
        elif pid==0x14:
            window=r.varint();self.revision=r.varint();index=struct.unpack('>h',r.read(2))[0];stack=simulation.slot(r)
            if window==self.window:self.inventory[index]=stack
            r.end()
        elif pid==0x39:self.opened=(r.varint(),r.varint())
        elif pid==0x3c:self.pong=struct.unpack('>Q',r.read(8))[0];r.end()
        elif pid==0x6a:
            index=r.varint();stack=simulation.slot(r);r.end()
            if index==40:self.offhand=stack
        elif pid==0x11:self.closed.append(r.varint())
        elif pid==0x13:
            window=r.varint();prop,value=struct.unpack('>hh',r.read(4));r.end()
            if window==self.window:self.properties[prop]=value
        elif pid==0x61:
            entity=r.varint()
            while True:
                index=r.read(1)[0]
                if index==255:break
                kind=r.varint()
                if kind in (0,8):r.read(1)
                elif kind==1:r.varint()
                elif kind==3:r.read(4)
                elif kind==7:self.drops[entity]=simulation.slot(r)
                else:raise AssertionError('Unexpected metadata type '+str(kind))
        return data
    def give(self,item,count=64,index=36):
        self.barrier();revision=self.revision;self.send(b'\x37'+struct.pack('>h',index)+vi(count)+(vi(item)+b'\0\0' if count else b''));self.until(lambda:self.revision>revision);self.barrier()
    def click(self,index,button=0,mode=0,revision=None,window=None):
        self.barrier();old=self.revision
        self.send(b'\x11'+vi(self.window if window is None else window)+vi(old if revision is None else revision)+struct.pack('>hB',index,button)+vi(mode)+vi(0)+b'\0')
        self.until(lambda:self.revision>old);self.barrier()
    def barrier(self):
        self.pings+=1;self.send(b'\x25'+struct.pack('>Q',self.pings));self.until(lambda:self.pong==self.pings)
    def open(self,x=2,y=64,z=2):
        self.opened=None;self.place(x,y,z,190);self.until(lambda:self.opened is not None and self.window==self.opened[0]);return self.opened
    def close_menu(self):
        old=self.revision;self.send(b'\x12'+vi(self.window));self.until(lambda:self.window==0 and self.revision>old);self.barrier()
    def raw_click(self,index,button,mode=5,revision=None):
        self.send(b'\x11'+vi(self.window)+vi(self.revision if revision is None else revision)+struct.pack('>hB',index,button)+vi(mode)+vi(0)+b'\0')
    def drag(self,slots,kind=0):
        self.barrier();before=self.revision;self.raw_click(-999,kind*4)
        for slot in slots:self.raw_click(slot,kind*4+1)
        self.raw_click(-999,kind*4+2);self.until(lambda:self.revision>before);self.barrier()

class ContainerTests(unittest.TestCase):
    def setUp(self):
        game.wire.EXE=game.FIXTURE;self.tmp=tempfile.TemporaryDirectory(prefix='container-wire-',dir=Path(game.FIXTURE).resolve().parent)
        self.server=game.wire.RunningServer('--world',self.tmp.name)
    def tearDown(self):self.server.close();self.tmp.cleanup()
    def chest(self,p,item=331,x=2,z=2):
        p.give(item,1);p.place(x,63,z,100+x);p.until(lambda:100+x in p.acks)
    def test_player_drag_collect_and_offhand(self):
        p=Player(self.server)
        try:
            p.give(898,17,index=37);p.click(37);p.drag([9,10,11])
            self.assertEqual([p.inventory[i][1] for i in [9,10,11]],[5,5,5]);self.assertEqual(p.cursor,(898,2,0))
            p.drag([12,13],kind=1);self.assertEqual(p.cursor[0],0)
            self.assertEqual([p.inventory[i][1] for i in [12,13]],[1,1])
            p.click(9);p.click(9,mode=6);self.assertEqual(p.cursor,(898,17,0))
            self.assertTrue(all(p.inventory[i][0]==0 for i in range(9,14)))
            p.click(37);p.click(37,button=40,mode=2)
            self.assertEqual(p.inventory[45],(898,17,0));self.assertEqual(p.offhand,(898,17,0));self.assertEqual(p.inventory[37][0],0)
            p.click(37,button=40,mode=2);self.assertEqual(p.offhand[0],0);self.assertEqual(p.inventory[37],(898,17,0))
            p.send(b'\x34'+struct.pack('>h',1)) # Select inventory slot 37.
            p.send(b'\x28'+vi(6)+struct.pack('>qB',0,0)+vi(951));p.until(lambda:951 in p.acks)
            self.assertEqual(p.offhand,(898,17,0));self.assertEqual(p.inventory[37][0],0)
            p.send(b'\x28'+vi(6)+struct.pack('>qB',0,0)+vi(952));p.until(lambda:952 in p.acks)
            self.assertEqual(p.offhand[0],0);self.assertEqual(p.inventory[37],(898,17,0))
        finally:p.close()
    def test_bulk_crafting_logs_and_stale_result_click(self):
        p=Player(self.server)
        try:
            for slot in range(36,45):p.give(0,0,index=slot)
            p.give(134,20,index=37);p.click(37);p.click(1)
            self.assertEqual(p.inventory[0],(36,4,0));old=p.revision;p.click(0,mode=1)
            self.assertEqual(sum(s[1] for i,s in p.inventory.items() if i>=9 and s[0]==36),80)
            self.assertEqual(p.cursor[0],0);self.assertEqual(p.inventory[1][0],0)
            p.give(134,2,index=37);p.click(37);p.click(1);p.click(0,mode=1,revision=old)
            self.assertEqual(p.inventory[1],(134,2,0));self.assertEqual(sum(s[1] for i,s in p.inventory.items() if i>=9 and s[0]==36),80)
        finally:p.close()
    def test_bulk_crafting_full_inventory_and_partial_output_drop(self):
        p=Player(self.server)
        try:
            p.give(134,1,index=37);p.click(37);p.click(1)
            for slot in range(9,45):p.give(898,64,index=slot)
            p.click(0,mode=1);self.assertEqual(p.inventory[1],(134,1,0));self.assertEqual(p.cursor[0],0)
            p.give(36,63,index=9);p.click(0,mode=1)
            self.assertEqual(p.inventory[9],(36,64,0));self.assertEqual(p.inventory[1][0],0)
            p.until(lambda:any(s==(36,3,0) for s in p.drops.values()))
            self.assertEqual(sum(s[1] for s in p.drops.values() if s[0]==36),3)
        finally:p.close()
    def test_cake_and_honey_remainders_survive_restart(self):
        p=Player(self.server)
        try:
            self.chest(p,item=332);self.assertEqual(p.open()[1],12)
            for slot,item in [(1,1018),(2,1018),(3,1018),(4,1084),(5,1031),(6,1084),(7,952),(8,952),(9,952)]:
                p.give(item,1,index=36);p.click(37);p.click(slot)
            self.assertEqual(p.inventory[0],(1085,1,0));p.click(0,mode=1)
            self.assertEqual([p.inventory[i] for i in (1,2,3)],[(1012,1,0)]*3)
            self.assertEqual(sum(s[1] for i,s in p.inventory.items() if i>=10 and s[0]==1085),1)
            p.close_menu();p.give(1381,8,index=37);p.click(37);p.click(1);p.click(0,mode=1)
            self.assertEqual(p.inventory[1],(1120,1,0))
            self.assertEqual(sum(s[1] for i,s in p.inventory.items() if i>=9 and s[0]==1084),24)
            self.assertEqual(sum(s[1] for i,s in p.inventory.items() if i>=1 and s[0]==1120),8)
            p.close_menu()
        finally:p.close()
        # RunningServer.close force-terminates fixtures; exercise a real saved shutdown here.
        self.server.process.stdin.write('stop\n');self.server.process.stdin.flush()
        self.assertEqual(self.server.process.wait(timeout=10),0)
        self.server.close();self.server=game.wire.RunningServer('--world',self.tmp.name);p=Player(self.server);p.barrier()
        try:
            for item,count in [(1012,3),(1085,1),(1084,24),(1120,8)]:
                self.assertEqual(sum(s[1] for i,s in p.inventory.items() if i>=9 and s[0]==item),count)
        finally:p.close()
    def test_chest_drag_caps_duplicates_and_peer_cancellation(self):
        p=Player(self.server);peer=Player(self.server,'SecondTester')
        try:
            self.chest(p);p.give(898,17,index=37);p.give(898,60,index=38);p.open();p.click(56);p.click(0);p.click(55);peer.open()
            old=p.revision;p.raw_click(-999,0);p.raw_click(1,1)
            peer.click(2);p.until(lambda:p.revision>old)
            p.click(-999,button=2,mode=5,revision=old)
            self.assertEqual(p.cursor,(898,17,0));self.assertEqual(p.inventory[1][0],0)
            p.drag([0,0,1]);self.assertEqual(p.inventory[0],(898,64,0));self.assertEqual(p.inventory[1],(898,8,0));self.assertEqual(p.cursor,(898,5,0))
            peer.until(lambda:peer.inventory[0]==(898,64,0) and peer.inventory[1]==(898,8,0))
            p.click(2);p.click(2);p.click(2,mode=6)
            self.assertEqual(p.cursor,(898,64,0));self.assertEqual(p.inventory[0],(898,13,0));self.assertEqual(p.inventory[1][0],0)
            p.click(3);p.click(3,button=40,mode=2);self.assertEqual(p.offhand,(898,64,0));self.assertEqual(p.inventory[3][0],0)
            p.close_menu();self.assertEqual(p.inventory[45],(898,64,0))
        finally:p.close();peer.close()
    def test_furnace_drag_and_offhand_slot_restrictions(self):
        p=Player(self.server)
        try:
            self.chest(p,item=334);p.give(898,17,index=37);p.open();p.click(31);p.drag([0,1,2])
            self.assertEqual(p.inventory[0],(898,17,0));self.assertEqual(p.inventory[1][0],0);self.assertEqual(p.inventory[2][0],0);self.assertEqual(p.cursor[0],0)
            p.click(0,button=40,mode=2);self.assertEqual(p.offhand,(898,17,0))
            p.click(1,button=40,mode=2);p.click(2,button=40,mode=2)
            self.assertEqual(p.offhand,(898,17,0));self.assertEqual(p.inventory[1][0],0);self.assertEqual(p.inventory[2][0],0)
        finally:p.close()
    def test_drag_interrupt_and_armor_restrictions(self):
        p=Player(self.server)
        try:
            p.give(898,7,index=37);p.click(37)
            old=p.revision;p.raw_click(-999,0);p.raw_click(9,1);p.raw_click(10,0,mode=0)
            p.until(lambda:p.revision>old);self.assertEqual(p.cursor,(898,7,0));self.assertEqual(p.inventory[9][0],0);self.assertEqual(p.inventory[10][0],0)
            p.drag([0,5,6,7,8]);self.assertEqual(p.cursor,(898,7,0));self.assertTrue(all(p.inventory[i][0]==0 for i in range(5,9)))
            p.click(5);self.assertEqual(p.cursor,(898,7,0));self.assertEqual(p.inventory[5][0],0)
            p.click(37);p.click(5,button=1,mode=2);self.assertEqual(p.inventory[37],(898,7,0));self.assertEqual(p.inventory[5][0],0)
        finally:p.close()
    def test_creative_fill_is_rejected_in_survival_and_close_cancels_drag(self):
        p=Player(self.server)
        try:
            p.give(898,7,index=37);p.click(37)
            self.server.process.stdin.write('gamemode survival CudaTester\n');self.server.process.stdin.flush()
            p.until(lambda:any(b'Survival enabled.' in message for message in p.messages))
            p.click(-999,button=8,mode=5);self.assertEqual(p.cursor,(898,7,0))
            p.click(9,button=9,mode=5);self.assertEqual(p.inventory[9][0],0)
            p.click(-999,button=10,mode=5);self.assertEqual(p.cursor,(898,7,0))
            p.raw_click(-999,0);p.raw_click(9,1);p.close_menu()
            self.assertEqual(p.cursor[0],0);self.assertEqual(sum(s[1] for s in p.inventory.values() if s[0]==898),7)
            p.click(-999,button=2,mode=5);self.assertEqual(sum(s[1] for s in p.inventory.values() if s[0]==898),7)
            self.server.process.stdin.write('gamemode creative CudaTester\n');self.server.process.stdin.flush()
            p.until(lambda:any(b'Creative enabled.' in message for message in p.messages))
            slot=next(i for i,s in p.inventory.items() if s[0]==898);p.click(slot);p.drag([9,10],kind=2)
            self.assertEqual(p.inventory[9],(898,64,0));self.assertEqual(p.inventory[10],(898,64,0));self.assertEqual(p.cursor[0],0)
        finally:p.close()
    def test_shift_hotbar_cursor_and_stale_messages(self):
        p=Player(self.server)
        try:
            self.chest(p);p.give(898,17,index=37)
            self.assertEqual(p.open()[1],2);self.assertEqual(len(p.inventory),63)
            p.click(55,mode=1);self.assertEqual(p.inventory[0],(898,17,0));self.assertEqual(p.inventory[55][0],0)
            stale=p.revision;p.click(0,button=1);self.assertEqual(p.cursor,(898,9,0));self.assertEqual(p.inventory[0],(898,8,0))
            p.click(1,revision=stale);self.assertEqual(p.cursor,(898,9,0));self.assertEqual(p.inventory[1][0],0)
            p.click(1);p.click(1,button=2,mode=2);self.assertEqual(p.inventory[56],(898,9,0))
            old=p.revision;p.send(b'\x12\x00');p.until(lambda:p.revision>old);self.assertNotEqual(p.window,0)
            old_window=p.window;p.close_menu();p.open();self.assertNotEqual(p.window,old_window)
            p.click(0,window=old_window);self.assertEqual(p.inventory[0],(898,8,0));self.assertEqual(p.cursor[0],0)
            p.close_menu()
        finally:p.close()
    def test_two_viewers_receive_updates_and_reject_stale_click(self):
        first=Player(self.server);second=Player(self.server,'SecondTester')
        try:
            self.chest(first);first.give(898,12,index=37);first.open();second.open()
            stale=second.revision;first.click(55,mode=1)
            second.until(lambda:second.inventory.get(0)==(898,12,0))
            second.click(0,revision=stale);self.assertEqual(second.cursor[0],0)
            second.click(0);first.until(lambda:first.inventory[0][0]==0);self.assertEqual(second.cursor,(898,12,0))
            first.click(0);self.assertEqual(first.cursor[0],0)
        finally:first.close();second.close()
    def test_double_chest_and_break_contents(self):
        p=Player(self.server)
        try:
            self.chest(p);self.chest(p,x=3);p.give(898,11,index=37)
            self.assertEqual(p.open()[1],5);self.assertEqual(len(p.inventory),90)
            p.click(82);p.click(30);p.close_menu()
            p.open(x=3);self.assertEqual(p.inventory[30],(898,11,0))
            old_window=p.window;p.dig(3,64,2,195);p.until(lambda:195 in p.acks and old_window in p.closed and p.window==0)
            # With north-facing chests, x=3 is the right half (slots 27..53).
            p.until(lambda:any(item==(898,11,0) for item in p.drops.values()))
            self.assertEqual(p.open()[1],2);self.assertTrue(all(p.inventory[i][0]==0 for i in range(27)))
        finally:p.close()
    def test_barrel_save_restart_and_range_close(self):
        p=Player(self.server)
        try:
            self.chest(p,item=1354);p.give(898,23,index=37);p.open();p.click(55,mode=1);p.close_menu()
            self.server.process.stdin.write('save-all\n');self.server.process.stdin.flush()
            deadline=time.monotonic()+5
            while True:
                saved=json.loads((Path(self.tmp.name)/'blocks.json').read_text())
                if saved['simulation'].get('containers') and saved['simulation']['containers'][0]['items'][0]==[898,23,0]:break
                if time.monotonic()>deadline:raise TimeoutError('Container was not saved')
                p.step(.1)
        finally:p.close()
        self.server.close();self.server=game.wire.RunningServer('--world',self.tmp.name)
        p=Player(self.server)
        try:
            p.open();self.assertEqual(p.inventory[0],(898,23,0));window=p.window
            p.send(b'\x1d'+struct.pack('>dddB',20.5,64,.5,1));p.until(lambda:p.window==0 and window in p.closed)
            p.click(0,window=window);self.assertEqual(p.cursor[0],0)
        finally:p.close()

    def test_furnace_progress_output_protection_and_shift_routing(self):
        p=Player(self.server)
        try:
            self.chest(p,item=334);p.give(903,1,index=37);p.give(896,1,index=38);p.give(898,7,index=39)
            self.assertEqual(p.open()[1],14);self.assertEqual(len(p.inventory),39)
            p.click(31,mode=1);self.assertEqual(p.inventory[0],(903,1,0))
            p.click(32,mode=1);p.until(lambda:p.properties.get(0,0)>0 and p.properties.get(2,0)>0)
            self.assertEqual(p.properties[3],200)
            p.click(33);p.click(2);self.assertEqual(p.inventory[2][0],0);self.assertEqual(p.cursor,(898,7,0));p.click(33)
            p.click(2,button=3,mode=2);self.assertEqual(p.inventory[2][0],0);self.assertEqual(p.inventory[33],(898,7,0))
            p.until(lambda:p.inventory[2]==(904,1,0),timeout=15)
            p.click(2,mode=1);self.assertEqual(p.inventory[2][0],0);self.assertTrue(any(stack==(904,1,0) for i,stack in p.inventory.items() if i>=3))
        finally:p.close()

if __name__=='__main__':unittest.main(verbosity=2)
