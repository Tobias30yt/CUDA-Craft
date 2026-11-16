"""Independent encrypted container clients: shared revisions, routing, drops and restart."""
import json, struct, tempfile, time, unittest
from pathlib import Path
import simulation_network_test as simulation
game=simulation.game;login=game.login;vi=game.vi

class Player(game.Player):
    def __init__(self,server,name='CudaTester'):
        self.play=False;self.window=0;self.revision=0;self.inventory={};self.cursor=(0,0,0);self.opened=None;self.closed=[];self.drops={};self.properties={}
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
        revision=self.revision;self.send(b'\x37'+struct.pack('>h',index)+vi(count)+vi(item)+b'\0\0');self.until(lambda:self.revision>revision)
    def click(self,index,button=0,mode=0,revision=None,window=None):
        old=self.revision
        self.send(b'\x11'+vi(self.window if window is None else window)+vi(old if revision is None else revision)+struct.pack('>hB',index,button)+vi(mode)+vi(0)+b'\0')
        self.until(lambda:self.revision>old)
    def open(self,x=2,y=64,z=2):
        self.opened=None;self.place(x,y,z,190);self.until(lambda:self.opened is not None and self.window==self.opened[0]);return self.opened
    def close_menu(self):
        self.send(b'\x12'+vi(self.window));self.until(lambda:self.window==0)

class ContainerTests(unittest.TestCase):
    def setUp(self):
        game.wire.EXE=game.FIXTURE;self.tmp=tempfile.TemporaryDirectory(prefix='container-wire-',dir=Path(game.FIXTURE).resolve().parent)
        self.server=game.wire.RunningServer('--world',self.tmp.name)
    def tearDown(self):self.server.close();self.tmp.cleanup()
    def chest(self,p,item=331,x=2,z=2):
        p.give(item,1);p.place(x,63,z,100+x);p.until(lambda:100+x in p.acks)
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
