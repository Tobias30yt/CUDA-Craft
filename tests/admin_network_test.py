"""Console/permission integration, measured telemetry, graceful stop and restart."""
import json
import queue
import struct
import tempfile
import time
import unittest
from pathlib import Path
import simulation_network_test as simulation

game=simulation.game
login=game.login

class Player(simulation.Player):
    def __init__(self,server):
        self.commands=set();self.hud=[];self.disconnect=None
        super().__init__(server,operator=False)
    def packet(self):
        data=super().packet()
        if not self.play:return data
        r=login.Buffer(data);pid=r.varint()
        if pid==0x10:
            commands=set();nodes=r.varint()
            for _ in range(nodes):
                flags=r.read(1)[0];children=[r.varint() for _ in range(r.varint())]
                assert all(0<=i<nodes for i in children)
                if flags&8:r.varint()
                kind=flags&3
                if kind in (1,2):name=r.string()
                if kind==1:commands.add(name)
                if kind==2:assert r.varint()==5 and r.varint()==2
                if flags&16:assert r.string()=='minecraft:ask_server'
            assert r.varint()==0;r.end();self.commands=commands
        elif pid==0x77 and data[-1]==1:self.hud.append(data)
        elif pid==0x20:self.disconnect=data
        return data
    def request(self,command,expected):
        begin=len(self.messages);self.command(command)
        self.until(lambda:any(expected in text for text in self.messages[begin:]))

class AdminNetworkTests(unittest.TestCase):
    def setUp(self):
        game.wire.EXE=game.FIXTURE
        self.folder=tempfile.TemporaryDirectory(prefix='admin-wire-',dir=Path(game.FIXTURE).resolve().parent)
        self.server=game.wire.RunningServer('--world',self.folder.name,'--gamemode','survival')
    def tearDown(self):self.server.close();self.folder.cleanup()
    def console(self,command):
        self.server.process.stdin.write(command+'\n');self.server.process.stdin.flush()
    def output(self,needle,timeout=6):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            try:line=self.server.lines.get(timeout=max(.01,deadline-time.monotonic()))
            except queue.Empty:break
            if line is None:raise AssertionError('Server exited before '+needle)
            if needle in line:return line
        raise AssertionError('Console output not found: '+needle)
    def test_permissions_command_tree_and_persistence(self):
        p=Player(self.server)
        try:
            self.assertNotIn('op',p.commands);self.assertNotIn('gamemode',p.commands);self.assertIn('perf',p.commands)
            p.request('op CudaTester',b'Permission denied')
            p.request('gamemode creative',b'Permission denied');self.assertIsNone(p.mode)
            self.console('op CudaTester');p.until(lambda:'op' in p.commands)
            p.mode_set(1);p.request('give @s diamond 3',b'Gave 3 diamond')
            p.request('tp ~2 ~ ~-2',b'Teleported');p.until(lambda:p.position==(2.5,64,-1.5))
            p.request('monitor on',b'monitor on');p.until(lambda:p.hud and b'TPS' in p.hud[-1])
            self.console('deop CudaTester');p.until(lambda:'op' not in p.commands)
            p.request('stop',b'Permission denied');self.assertIsNone(self.server.process.poll())
            p.request('perf export',b'Permission denied')
            self.console('op CudaTester');p.until(lambda:'op' in p.commands)
            p.request('save-all',b'World and players saved')
            self.console('stop');self.server.process.wait(timeout=6)
        finally:p.close()
        self.server.close()
        operators=json.loads((Path(self.folder.name)/'operators.json').read_text())
        self.assertEqual(operators['operators']['000102030405060708090a0b0c0d0e0f'],'CudaTester')
        self.server=game.wire.RunningServer('--world',self.folder.name)
        p=Player(self.server)
        try:
            self.assertIn('op',p.commands)
            p.request('deop CudaTester',b'no longer an operator');p.until(lambda:'op' not in p.commands)
            p.request('give @s diamond 3',b'Permission denied')
        finally:p.close()
    def test_console_metrics_export_and_clean_stop(self):
        self.console('monitor on 1');self.output('[MONITOR]');self.output('[MONITOR]');self.output('[MONITOR]')
        self.console('perf export');self.output('Exported')
        metrics=json.loads((Path(self.folder.name)/'performance.json').read_text())
        self.assertGreater(metrics['ticks'],20);self.assertTrue(all(15<t<=20 for t in metrics['tps']))
        self.assertGreater(metrics['simulation_ms']['samples'],20)
        self.assertGreater(metrics['working_set_bytes'],0);self.assertGreater(metrics['private_commit_bytes'],0)
        self.assertIsInstance(metrics['process_cpu_percent'],(int,float));self.assertGreaterEqual(metrics['process_cpu_percent'],0)
        self.assertEqual(metrics['simulation_backend'],'CPU')
        p=Player(self.server)
        try:
            p.request('perf',b'TPS');self.console('save-all');self.output('World and players saved')
            self.console('perf export');self.output('Exported')
            metrics=json.loads((Path(self.folder.name)/'performance.json').read_text())
            self.assertGreater(metrics['network']['sent_bytes'],0);self.assertGreater(metrics['network']['received_bytes'],0)
            self.assertGreater(metrics['chunks']['sent'],0);self.assertEqual(metrics['players'],1)
            self.console('stop');self.server.process.wait(timeout=6)
            save=json.loads((Path(self.folder.name)/'blocks.json').read_text())
            self.assertIn('000102030405060708090a0b0c0d0e0f',save['simulation']['players'])
            self.assertEqual(self.server.process.returncode,0)
        finally:p.close()
    def test_bad_console_lines_and_kick(self):
        # Oversized command is discarded as a whole, including its suffix.
        self.console('x'*2200+' stop');self.console('list');self.output('Online (0)')
        self.assertIsNone(self.server.process.poll())
        self.console('op NeverJoined');self.output('Unknown player')
        p=Player(self.server)
        try:
            p.send(b'\x2d'+game.vi(0)+b'\x01\x00')
            p.send(b'\x2d'+game.vi(0)+b'\x00\x01')
            p.send(b'\x2e'+game.vi(0))
            p.request('list',b'Online (1)')
            self.console('kick CudaTester Integration test')
            with self.assertRaisesRegex(RuntimeError,'play disconnect'):p.until(lambda:False)
            self.assertIn(b'Integration test',p.disconnect)
        finally:p.close()
        self.console('list');self.output('Online (0)')
    def test_eof_does_not_stop_server(self):
        self.server.process.stdin.close()
        p=Player(self.server)
        try:p.request('list',b'Online (1)');self.assertIsNone(self.server.process.poll())
        finally:p.close()
    def test_large_template_streams_bounded_chunk_refresh(self):
        root=Path.cwd()/'data/structures';root.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='integration_',dir=root) as directory:
            template=Path(directory)/'big.json'
            template.write_text(json.dumps({'format':1,'protocol':774,'size':[16,160,16],'palettes':[[1]],
                'blocks':[[x,y,z,0] for y in range(160) for z in range(16) for x in range(16)]}))
            p=Player(self.server)
            try:
                p.until(lambda:len(p.chunks)==49)
                del p.chunks[0,0]
                self.console('place template '+Path(directory).name+'/big 0 80 0')
                self.output('40960 blocks')
                p.until(lambda:(0,0) in p.chunks)
                self.assertEqual(p.block(0,80,0),1);self.assertEqual(p.block(15,239,15),1)
                p.request('list',b'Online (1)')
            finally:p.close()
    def test_gamemode_hotkey_and_signed_command_permissions(self):
        p=Player(self.server)
        def signed(command):
            p.send(b'\x07'+login.string(command)+struct.pack('>qq',0,0)+game.vi(0)+game.vi(0)+b'\0'*4)
        try:
            p.send(b'\x04'+game.vi(1));p.until(lambda:p.mode==0)
            self.console('op CudaTester');p.until(lambda:'op' in p.commands)
            p.send(b'\x04'+game.vi(1));p.until(lambda:p.mode==1)
            mark=len(p.messages);p.send(b'\x04'+game.vi(3))
            p.until(lambda:any(b'not available yet' in m for m in p.messages[mark:]));self.assertEqual(p.mode,1)
            signed('gamemode survival');p.until(lambda:p.mode==0)
            self.console('deop CudaTester');p.until(lambda:'op' not in p.commands)
            mark=len(p.messages);signed('gamemode creative')
            p.until(lambda:any(b'Permission denied' in m for m in p.messages[mark:]));self.assertEqual(p.mode,0)
            mark=len(p.messages);p.send(b'\x04'+game.vi(1))
            p.until(lambda:any(b'Permission denied' in m for m in p.messages[mark:]));self.assertEqual(p.mode,0)
            p.request('list',b'Online (1)')
        finally:p.close()

if __name__=='__main__':unittest.main()
