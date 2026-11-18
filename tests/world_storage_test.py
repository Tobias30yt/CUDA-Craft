"""Real-process world lease, save failure and restart regressions; no GPU required."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE = str(Path(sys.argv[1]).resolve())
sys.argv = [sys.argv[0], EXE]
import network_test as wire


class WorldStorageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='CUDA Welt ü ')
        self.world = Path(self.temporary.name) / 'Welt ö mit Leerzeichen'
        self.servers = []

    def tearDown(self):
        for server in self.servers:
            server.close()
        self.temporary.cleanup()

    def start(self, world=None, generator='overworld'):
        server = wire.RunningServer('--world', str(world or self.world), '--generator', generator)
        self.servers.append(server)
        return server

    def stop(self, server, expected=0):
        server.process.stdin.write('stop\n'); server.process.stdin.flush()
        self.assertEqual(server.process.wait(timeout=10), expected)

    def refused(self, world=None):
        result = subprocess.run([EXE, '--port', '0', '--world', str(world or self.world),
                                 '--run-for-ms', '100'], capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        return result.stderr.decode('utf-8', errors='replace')

    def test_concurrent_open_and_path_alias_rejected(self):
        first = self.start()
        for path in (self.world, self.world / '..' / self.world.name):
            self.assertIn('already open', self.refused(path))
        with first.connect() as sock:
            sock.sendall(wire.handshake() + wire.frame(b'\x00'))
            self.assertEqual(wire.status_json(wire.packet(sock))['version']['protocol'], 774)
        self.stop(first)
        self.stop(self.start())

    def test_process_termination_releases_lease(self):
        first = self.start()
        first.process.kill(); first.process.wait(timeout=10)
        self.assertTrue((self.world / 'native-world.lock').is_file())
        self.stop(self.start())

    def test_failed_load_preserves_file_and_releases_lease(self):
        self.world.mkdir()
        save = self.world / 'blocks.json'; save.write_bytes(b'{broken save')
        for _ in range(2):
            self.assertNotIn('already open', self.refused())
            self.assertEqual(save.read_bytes(), b'{broken save')
        save.unlink()
        self.stop(self.start())

    def test_failed_save_returns_error_and_preserves_previous_save(self):
        server = self.start()
        save = self.world / 'blocks.json'; previous = save.read_bytes()
        (self.world / 'blocks.json.tmp').mkdir()
        self.stop(server, expected=1)
        self.assertEqual(save.read_bytes(), previous)
        (self.world / 'blocks.json.tmp').rmdir()
        self.stop(self.start())

    def test_dimension_save_is_also_locked(self):
        self.stop(self.start())
        child = self.world / 'DIM-1'; child.mkdir()
        data = json.loads((self.world / 'blocks.json').read_text())
        data['generator'] = 'cuda-craft-nether-v1'
        (child / 'blocks.json').write_text(json.dumps(data))
        root = self.start()
        self.assertIn('already open', self.refused(child))
        self.stop(root)

    def test_independent_worlds_can_run_together(self):
        first = self.start()
        second = self.start(self.world.parent / 'Andere Welt')
        self.stop(second); self.stop(first)

    def test_density_save_reopens_with_same_identity(self):
        self.stop(self.start(generator='density'))
        save = self.world / 'blocks.json'
        before = json.loads(save.read_text())
        self.stop(self.start(generator='density'))
        after = json.loads(save.read_text())
        self.assertEqual(before['generator'], 'cuda-craft-density-v1')
        self.assertEqual((before['generator'], before['seed'], before['edits']),
                         (after['generator'], after['seed'], after['edits']))


if __name__ == '__main__':
    unittest.main(verbosity=2)
