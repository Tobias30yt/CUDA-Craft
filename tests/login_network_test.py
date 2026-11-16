"""Independent RSA/AES/zlib client. Positive auth uses a separate test-only executable."""
import io
import json
import struct
import sys
import time
import unittest
import zlib

PRODUCTION, FIXTURE = sys.argv[1:3]
sys.argv = [sys.argv[0], PRODUCTION]
import network_test as wire
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms
from cryptography.hazmat.decrepit.ciphers.modes import CFB8


class Buffer:
    def __init__(self, data):
        self.io = io.BytesIO(data)
    def read(self, n):
        value = self.io.read(n)
        if len(value) != n:
            raise EOFError("Truncated packet")
        return value
    def varint(self):
        result = 0
        for i in range(5):
            byte = self.read(1)[0]
            result |= (byte & 127) << (7 * i)
            if not byte & 128:
                return result
        raise ValueError("Oversized VarInt")
    def array(self):
        return self.read(self.varint())
    def string(self):
        return self.array().decode("utf-8")
    def end(self):
        assert self.io.read() == b""


def string(text):
    data = text.encode("utf-8")
    return wire.varint(len(data)) + data


class Client:
    def __init__(self, server):
        self.sock = server.connect()
        self.encryptor = self.decryptor = None
        self.threshold = None
    def close(self):
        self.sock.close()
    def recv(self, n):
        data = self.sock.recv(n)
        return self.decryptor.update(data) if self.decryptor else data
    def send_raw(self, data, fragmented=False):
        if self.encryptor:
            data = self.encryptor.update(data)
        if fragmented:
            for byte in data:
                self.sock.sendall(bytes([byte]))
        else:
            self.sock.sendall(data)
    def send(self, payload, fragmented=False):
        if self.threshold is not None:
            payload = ((wire.varint(len(payload)) + zlib.compress(payload))
                       if len(payload) >= self.threshold else b"\x00" + payload)
        self.send_raw(wire.frame(payload), fragmented)
    def packet(self):
        payload = wire.packet(self)
        if self.threshold is not None:
            body = Buffer(payload)
            size = body.varint()
            rest = body.io.read()
            if size:
                payload = zlib.decompress(rest)
                assert len(payload) == size
            else:
                payload = rest
        return payload
    def begin(self, name="CudaTester", bad_token=False, fragmented=False, secret_size=16, uuid=None):
        self.uuid=uuid or bytes(range(16))
        start = b"\x00" + string(name) + self.uuid
        self.send_raw(wire.handshake(state=2) + wire.frame(start), fragmented)
        request = Buffer(self.packet())
        assert request.varint() == 1
        assert request.string() == ""
        key = serialization.load_der_public_key(request.array())
        assert key.key_size == 1024
        token = request.array()
        assert len(token) == 16
        assert request.read(1) == b"\x01"  # Account verification is mandatory.
        request.end()
        if bad_token:
            token = bytes([token[0] ^ 1]) + token[1:]
        secret = bytes(range(secret_size))
        encrypted_secret = key.encrypt(secret, padding.PKCS1v15())
        encrypted_token = key.encrypt(token, padding.PKCS1v15())
        self.send(b"\x01" + wire.varint(len(encrypted_secret)) + encrypted_secret
                  + wire.varint(len(encrypted_token)) + encrypted_token, fragmented)
        if secret_size == 16:
            cipher = Cipher(algorithms.AES(secret), CFB8(secret))
            self.encryptor, self.decryptor = cipher.encryptor(), cipher.decryptor()
    def success(self, name="CudaTester"):
        compression = Buffer(self.packet())
        assert compression.varint() == 3
        self.threshold = compression.varint()
        assert self.threshold == 256
        compression.end()
        success = Buffer(self.packet())
        assert success.varint() == 2
        assert success.read(16) == self.uuid
        assert success.string() == name
        assert success.varint() == 1
        assert success.string() == "textures"
        assert success.string() == "x" * 600
        assert success.read(1) == b"\x01"
        assert success.string() == "test-signature"
        success.end()

    def configure(self):
        self.send(b"\x03")
        registries = 0
        while True:
            response = Buffer(self.packet())
            packet_id = response.varint()
            if packet_id == 0x0e:
                assert response.varint() == 0
                self.send(b"\x07\x00")
            elif packet_id == 7:
                registries += 1
            elif packet_id == 3:
                assert registries == 23
                self.send(b"\x03")
                return


class LoginNetworkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        wire.EXE = PRODUCTION
        cls.production = wire.RunningServer()
        wire.EXE = FIXTURE
        cls.fixture = wire.RunningServer()
    @classmethod
    def tearDownClass(cls):
        cls.production.close()
        cls.fixture.close()
    def closed(self, client):
        try:
            self.assertEqual(client.sock.recv(1), b"")
        except ConnectionResetError:
            pass
    def test_production_encryption_request_and_bad_token(self):
        client = Client(self.production)
        try:
            client.begin(bad_token=True)
            self.closed(client)
        finally:
            client.close()
    def test_invalid_shared_secret(self):
        client = Client(self.production)
        try:
            client.begin(secret_size=15)
            self.closed(client)
        finally:
            client.close()
    def test_full_encrypted_compressed_login(self):
        client = Client(self.fixture)
        try:
            client.begin(fragmented=True)
            client.success()
            client.configure()
            result = Buffer(client.packet())
            self.assertEqual(result.varint(), 0x30)
            self.assertGreater(struct.unpack('>i',result.read(4))[0],0)
        finally:
            client.close()
    def test_auth_failure_and_identity_mismatch(self):
        for name in ("RejectMe", "Mismatch"):
            with self.subTest(name=name):
                client = Client(self.fixture)
                try:
                    client.begin(name)
                    reply = Buffer(client.packet())
                    self.assertEqual(reply.varint(), 0)
                    self.assertIn("Could not verify", json.loads(reply.string())["text"])
                    reply.end()
                    self.closed(client)
                finally:
                    client.close()
    def test_slow_auth_does_not_block_status(self):
        client = Client(self.fixture)
        try:
            client.begin("SlowAuth")
            started = time.monotonic()
            with self.fixture.connect() as status:
                status.sendall(wire.handshake() + wire.frame(b"\x00"))
                self.assertEqual(wire.status_json(wire.packet(status))["version"]["protocol"], 774)
            self.assertLess(time.monotonic() - started, 0.8)
            client.success("SlowAuth")
        finally:
            client.close()
    def test_compression_limits_and_bad_state(self):
        cases = [b"\x00\x03\x00",  # trailing fields in Login Acknowledged
                 wire.varint(1000000) + zlib.compress(b"\x03"),
                 wire.varint(1) + zlib.compress(b"\x03"),  # below negotiated threshold
                 b"\x00" + b"x" * 256,
                 wire.varint(256) + b"not-zlib"]
        for payload in cases:
            client = Client(self.fixture)
            try:
                client.begin()
                client.success()
                client.send_raw(wire.frame(payload))
                self.closed(client)
            finally:
                client.close()
    def test_invalid_username_and_truncated_login(self):
        for payload in (b"\x00" + string("bad/name") + bytes(16),
                        b"\x00" + string("CudaTester") + bytes(15)):
            client = Client(self.production)
            try:
                client.send_raw(wire.handshake(state=2) + wire.frame(payload))
                if b"bad/name" in payload:
                    response = Buffer(client.packet())
                    self.assertEqual(response.varint(), 0)
                    self.assertIn("Invalid player name", json.loads(response.string())["text"])
                self.closed(client)
            finally:
                client.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
