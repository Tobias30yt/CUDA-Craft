"""Production executable: private HMAC login, account binding, parent EOF and pause."""
import hashlib, hmac, json, os, subprocess, sys, tempfile, time, unittest
EXE = sys.argv[1]
sys.argv = [sys.argv[0], EXE, EXE]
import login_network_test as login
wire = login.wire
wire.EXE = EXE
OWNER = bytes(range(16))
KEY = bytes(range(32))

class PrivateClient(login.Client):
    def challenge(self, name='CudaTester', uuid=OWNER):
        self.uuid = uuid
        self.send_raw(wire.handshake(state=2) + wire.frame(b'\x00' + login.string(name) + uuid))
        response = login.Buffer(self.packet())
        if response.varint() != 4: raise ValueError('No private challenge')
        assert response.varint() == 1
        assert response.string() == 'cudacraft:local_session'
        nonce = response.read(32); response.end()
        return nonce
    def prove(self, proof):
        self.send(b'\x02\x01\x01' + proof)
    def encrypted(self):
        request = login.Buffer(self.packet())
        assert request.varint() == 1 and request.string() == ''
        key = login.serialization.load_der_public_key(request.array())
        token = request.array(); assert request.read(1) == b'\x00'; request.end()
        secret = bytes(range(16))
        encrypted = key.encrypt(secret, login.padding.PKCS1v15())
        verify = key.encrypt(token, login.padding.PKCS1v15())
        self.send(b'\x01' + wire.varint(len(encrypted)) + encrypted + wire.varint(len(verify)) + verify)
        cipher = login.Cipher(login.algorithms.AES(secret), login.CFB8(secret))
        self.encryptor, self.decryptor = cipher.encryptor(), cipher.decryptor()
        compression = login.Buffer(self.packet()); assert compression.varint() == 3
        self.threshold = compression.varint(); compression.end()
        success = login.Buffer(self.packet()); assert success.varint() == 2
        assert success.read(16) == OWNER and success.string() == 'CudaTester'
        assert success.varint() == 0; success.end()

class PrivateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cuda-private-')
        self.env = dict(os.environ, CUDACRAFT_LOCAL_NAME='CudaTester', CUDACRAFT_LOCAL_UUID=OWNER.hex(), CUDACRAFT_LOCAL_TOKEN=KEY.hex())
        self.server = wire.RunningServer('--local-session', '--bind', '127.0.0.1', '--world', self.tmp.name, '--generator', 'flat', env=self.env)
    def tearDown(self):
        self.server.close(); self.tmp.cleanup()
    def command(self, command):
        self.server.process.stdin.write(command+'\n'); self.server.process.stdin.flush()
    def test_authenticated_encrypted_login_and_owner_op(self):
        client = PrivateClient(self.server)
        try:
            nonce = client.challenge(); client.prove(hmac.digest(KEY, nonce, 'sha256')); client.encrypted(); client.configure()
            response = login.Buffer(client.packet()); self.assertEqual(response.varint(), 0x30)
            from pathlib import Path
            data = json.loads((Path(self.tmp.name)/'operators.json').read_text())
            self.assertIn(OWNER.hex(), json.dumps(data))
        finally: client.close()
    def test_wrong_proof_and_replay_rejected(self):
        first = PrivateClient(self.server)
        nonce = first.challenge(); first.prove(bytes(32)); self.assertEqual(login.Buffer(first.packet()).varint(), 0); first.close()
        second = PrivateClient(self.server)
        try:
            self.assertNotEqual(second.challenge(), nonce)
            second.prove(hmac.digest(KEY, nonce, 'sha256')); self.assertEqual(login.Buffer(second.packet()).varint(), 0)
        finally: second.close()
    def test_other_identity_rejected(self):
        for name, uuid in [('Other', OWNER), ('CudaTester', bytes(16))]:
            client = PrivateClient(self.server)
            try: self.assertRaises(ValueError, client.challenge, name, uuid)
            finally: client.close()
    def test_parent_eof_saves_and_stops(self):
        self.command('pause on'); self.command('perf export')
        time.sleep(.2)
        from pathlib import Path
        exports = list(Path(self.tmp.name).glob('*perf*.json'))
        self.assertTrue(exports)
        report = json.loads(exports[0].read_text()); self.assertTrue(report['paused']); self.assertTrue(report['local_session'])
        self.server.process.stdin.close()
        self.assertEqual(self.server.process.wait(timeout=8), 0)
        self.assertTrue((Path(self.tmp.name)/'blocks.json').is_file())
    def test_public_bind_and_missing_secret_rejected(self):
        for args, env in [(['--bind','0.0.0.0'], self.env), ([], {k:v for k,v in os.environ.items() if not k.startswith('CUDACRAFT_LOCAL_')})]:
            result = subprocess.run([EXE,'--local-session','--world',self.tmp.name,*args],env=env,capture_output=True,timeout=5)
            self.assertNotEqual(result.returncode, 0)

if __name__ == '__main__': unittest.main(verbosity=2)
