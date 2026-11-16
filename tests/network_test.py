"""Black-box wire tests against the real executable; no Minecraft libraries required."""
import concurrent.futures
import json
import queue
import socket
import struct
import subprocess
import sys
import threading
import time
import unittest

EXE = sys.argv[1]
del sys.argv[1:]
MOTD = 'CUDA Craft "test" \\ line\nsecond line'


def varint(value):
    value &= 0xffffffff
    result = bytearray()
    while True:
        byte = value & 127
        value >>= 7
        result.append(byte | (128 if value else 0))
        if not value:
            return bytes(result)


def frame(payload):
    return varint(len(payload)) + payload


def handshake(state=1, version=774, host=b"localhost"):
    return frame(b"\x00" + varint(version) + varint(len(host)) + host + b"\x63\xdd" + varint(state))


def exact(sock, count):
    result = b""
    while len(result) < count:
        data = sock.recv(count - len(result))
        if not data:
            raise EOFError("Truncated response")
        result += data
    return result


def read_varint(sock):
    value = 0
    for shift in range(0, 35, 7):
        byte = exact(sock, 1)[0]
        value |= (byte & 127) << shift
        if not byte & 128:
            return value
    raise ValueError("Invalid response VarInt")


def packet(sock):
    size = read_varint(sock)
    if not 0 < size <= 16384:
        raise ValueError("Invalid response frame length")
    return exact(sock, size)


def status_json(payload):
    assert payload[0] == 0
    size = 0
    for i, byte in enumerate(payload[1:6], 1):
        size |= (byte & 127) << (7 * (i - 1))
        if not byte & 128:
            assert len(payload) == i + 1 + size
            return json.loads(payload[i + 1:].decode("utf-8"))
    raise ValueError("Invalid JSON string length")


class RunningServer:
    def __init__(self, *args, env=None):
        self.process = subprocess.Popen(
            [EXE, "--port", "0", "--motd", MOTD, "--run-for-ms", "30000", *args],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            encoding="utf-8", errors="replace", env=env)
        self.lines = queue.Queue()
        def read_output():
            for line in self.process.stdout:
                self.lines.put(line)
            self.lines.put(None)
        self.reader = threading.Thread(target=read_output, daemon=True)
        self.reader.start()
        deadline = time.monotonic() + 10
        output = []
        try:
            while time.monotonic() < deadline:
                line = self.lines.get(timeout=max(0.01, deadline - time.monotonic()))
                if line is None:
                    break
                output.append(line)
                if line.startswith("LISTENING "):
                    self.port = int(line.split()[1].rsplit(":", 1)[1])
                    return
            raise RuntimeError("Server did not start: " + "".join(output))
        except BaseException:
            self.close()
            raise

    def connect(self):
        return socket.create_connection(("127.0.0.1", self.port), timeout=3)

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
        self.reader.join(timeout=1)
        self.process.stdin.close()
        self.process.stdout.close()


class NetworkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = RunningServer()

    @classmethod
    def tearDownClass(cls):
        cls.server.close()

    def assert_closed(self, sock):
        try:
            self.assertEqual(sock.recv(1), b"")
        except ConnectionResetError:
            pass

    def query(self, fragmented=False, version=774):
        with self.server.connect() as sock:
            request = handshake(version=version) + frame(b"\x00")
            if fragmented:
                for byte in request:
                    sock.sendall(bytes([byte]))
                    time.sleep(0.001)
            else:
                sock.sendall(request)
            response = status_json(packet(sock))
            self.assertEqual(response["version"], {"name": "CUDA Craft 1.21.11", "protocol": 774})
            self.assertEqual(response["description"]["text"], MOTD)
            self.assertEqual(response["players"]["online"], 0)
            self.assertEqual(response["players"]["max"], 20)
            ping = b"\x01" + struct.pack(">q", -81985529216486896)
            sock.sendall(frame(ping))
            self.assertEqual(packet(sock), ping)
            self.assert_closed(sock)

    def test_status_and_ping(self):
        self.query()

    def test_unicode_motd(self):
        motd = 'CUDA Craft: Gr\u00fc\u00dfe \u00a7a \U0001f30d'
        server = RunningServer("--motd", motd)
        try:
            with server.connect() as sock:
                sock.sendall(handshake() + frame(b"\x00"))
                self.assertEqual(status_json(packet(sock))["description"]["text"], motd)
        finally:
            server.close()

    def test_fragmented_tcp(self):
        self.query(fragmented=True)

    def test_discovery_protocol(self):
        self.query(version=-1)

    def test_parallel_clients_and_idle_peer(self):
        with self.server.connect() as idle:
            idle.sendall(b"\x80")
            with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
                list(pool.map(lambda _: self.query(), range(24)))

    def test_coalesced_handshake_status_ping(self):
        with self.server.connect() as sock:
            ping = b"\x01" + bytes.fromhex("8000000000000000")
            sock.sendall(handshake() + frame(b"\x00") + frame(ping))
            self.assertEqual(status_json(packet(sock))["version"]["protocol"], 774)
            self.assertEqual(packet(sock), ping)
            self.assert_closed(sock)

    def test_ping_without_status(self):
        with self.server.connect() as sock:
            ping = b"\x01" + bytes.fromhex("7fffffffffffffff")
            sock.sendall(handshake() + frame(ping))
            self.assertEqual(packet(sock), ping)
            self.assert_closed(sock)

    def test_wrong_login_version_refused(self):
        with self.server.connect() as sock:
            sock.sendall(handshake(state=2, version=767))
            self.assertIn("1.21.11", status_json(packet(sock))["text"])
            self.assert_closed(sock)

    def test_half_close_after_request(self):
        with self.server.connect() as sock:
            sock.sendall(handshake() + frame(b"\x00"))
            sock.shutdown(socket.SHUT_WR)
            self.assertEqual(status_json(packet(sock))["version"]["protocol"], 774)
            self.assert_closed(sock)

    def test_malformed_inputs_do_not_kill_server(self):
        cases = [
            b"\x00", b"\x80\x80\x80", varint(4097),
            frame(b"\x00\xff\xff\xff\xff\x10"),
            handshake(host=b"\xc0\xaf"), handshake(state=3),
            handshake() + frame(b"\x00\x00"),
            handshake() + frame(b"\x00") + frame(b"\x00"),
            handshake() + frame(b"\x01\x00"),
            handshake() + frame(b"\x02"),
        ]
        for data in cases:
            with self.subTest(data=data), self.server.connect() as sock:
                sock.sendall(data)
                self.assert_closed(sock)
        self.query()

    def test_connection_deadline_and_capacity(self):
        server = RunningServer("--timeout-ms", "500", "--max-connections", "1")
        try:
            with server.connect() as first:
                first.sendall(handshake() + frame(b"\x00"))
                packet(first)  # First connection is now known to occupy the single slot.
                with server.connect() as excess:
                    self.assert_closed(excess)
                self.assert_closed(first)  # Absolute deadline frees an idle slot.
            with server.connect() as recovered:
                recovered.sendall(handshake() + frame(b"\x00"))
                self.assertEqual(status_json(packet(recovered))["version"]["protocol"], 774)
        finally:
            server.close()

    def test_cli_validation_and_clean_timed_shutdown(self):
        for args in (["--port", "-1"], ["--port", "65536"], ["--port", "12x"],
                     ["--unknown", "x"], ["--timeout-ms", "0"], ["--bind", "invalid"],
                     ["--motd", "x" * 2049], ["--max-connections", "33"]):
            result = subprocess.run([EXE, *args], capture_output=True, timeout=3)
            self.assertNotEqual(result.returncode, 0, args)
        result = subprocess.run([EXE, "--port", "0", "--run-for-ms", "100"], capture_output=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"CUDA Craft stopped.", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
