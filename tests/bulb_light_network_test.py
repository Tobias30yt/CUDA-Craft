"""Check initial and live copper bulb block lighting on the real play connection."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

LIVE = "--live" in sys.argv
if LIVE:
    sys.argv.remove("--live")

import game_network_test as game


class BulbLighting(unittest.TestCase):
    def open_world(self, edits):
        temporary = tempfile.TemporaryDirectory(prefix="cuda-bulb-light-")
        self.addCleanup(temporary.cleanup)
        folder = Path(temporary.name)
        (folder/"blocks.json").write_text(json.dumps(dict(
            format=2, protocol=774, generator="cuda-craft-flat-v1", seed=0, edits=edits,
        )), encoding="utf-8")
        game.wire.EXE = game.FIXTURE
        server = game.wire.RunningServer("--world", str(folder))
        self.addCleanup(server.close)
        player = game.Player(server)
        self.addCleanup(player.close)
        return player

    def test_initial_block_light(self):
        cases = [(2,2,26878,15),(18,2,26882,12),(2,18,26886,8),(18,18,26890,4)]
        player = self.open_world([[x,64,z,state] for x,z,state,_ in cases])
        for x,z,state,level in cases:
            with self.subTest(state=state):
                player.until(lambda:(x//16,z//16) in player.chunks)
                self.assertEqual(player.block(x,64,z),state)
                self.assertEqual(player.light(x,64,z),level)
                self.assertEqual(player.light(x,65,z),level-1)

    def assert_live_light(self, old_state, new_state, old_light, new_light):
        player = self.open_world([[2,64,2,old_state],[3,64,2,6571]])
        self.assertEqual(player.light(2,65,2),old_light)
        player.place(3,64,2,41)
        player.until(lambda:41 in player.acks and player.block(2,64,2)==new_state)
        try:
            player.until(lambda:player.light(2,65,2)==new_light,timeout=2)
        except TimeoutError:
            observed = player.light(2,65,2)
            updated = (0,0) in player.light_updates
            self.fail(f"Bulb state changed. Expected adjacent light {new_light}, got {observed}. "
                      f"Received standalone light packet: {updated}.")

    @unittest.skipUnless(LIVE, "Use --live to require lighting updates in an already streamed chunk")
    def test_live_block_light_on(self):
        self.assert_live_light(26880,26877,0,14)

    @unittest.skipUnless(LIVE, "Use --live to require lighting updates in an already streamed chunk")
    def test_live_block_light_off(self):
        self.assert_live_light(26878,26879,14,0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
