"""Opt-in real HTTPS failure-path check. No account credentials, no successful join."""
import json
import queue
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
executable = sys.argv[1]
sys.argv = [sys.argv[0], executable, executable]
import login_network_test as login

login.wire.EXE = executable
server = login.wire.RunningServer()
client = login.Client(server)
try:
    client.sock.settimeout(15)
    client.begin("CudaCraftProbe")
    response = login.Buffer(client.packet())
    if response.varint() != 0:
        raise RuntimeError("An unjoined probe unexpectedly authenticated")
    message = json.loads(response.string())["text"]
    if "Could not verify" not in message:
        raise RuntimeError("Unexpected authentication failure response")
    response.end()
    observed_rejection = False
    while True:
        try:
            line = server.lines.get(timeout=1)
        except queue.Empty:
            break
        if line and "Minecraft session was not joined (HTTP 204)" in line:
            observed_rejection = True
            break
    if not observed_rejection:
        raise RuntimeError("Probe was refused, but no HTTP 204 from the official service was confirmed")
    print("Production RSA/AES/HTTPS rejection path passed; official session service returned HTTP 204.")
    print("A successful Microsoft/Minecraft account login is not tested by this probe.")
finally:
    client.close()
    server.close()
