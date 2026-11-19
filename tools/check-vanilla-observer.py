"""Capture a four-tick observer fixture on the local official 1.21.11 server."""
import argparse
import hashlib
from pathlib import Path
import queue
import subprocess
import tempfile
import threading
import time

from importlib import import_module

reference = import_module("capture-vanilla-biomes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--java", required=True)
    args = parser.parse_args()
    jar = reference.ROOT / ".tools/vanilla-1.21.11/server.jar"
    if hashlib.sha1(jar.read_bytes()).hexdigest() != reference.SHA1:
        raise ValueError("Wrong reference server JAR")
    with tempfile.TemporaryDirectory(prefix="observer-reference-", dir=jar.parent) as directory:
        folder = Path(directory)
        (folder / "eula.txt").write_text("eula=true\n")
        (folder / "server.properties").write_text(
            "server-ip=127.0.0.1\nserver-port=0\nonline-mode=false\n"
            "level-type=minecraft:flat\nview-distance=2\nsimulation-distance=2\n"
            "pause-when-empty-seconds=0\n"
        )
        with subprocess.Popen(
            [args.java, "-Xmx1G", "-jar", str(jar), "nogui"], cwd=folder,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace",
        ) as server:
            lines = queue.Queue()

            def read():
                for line in server.stdout:
                    lines.put(line)
                lines.put(None)

            def wait_for(marker, timeout=20):
                deadline = time.monotonic() + timeout
                recent = []
                while time.monotonic() < deadline:
                    try:
                        line = lines.get(timeout=max(.01, deadline - time.monotonic()))
                    except queue.Empty:
                        break
                    if line is None:
                        break
                    if marker.lower() in line.lower():
                        return
                    recent.append(line)
                raise RuntimeError(f"Missing server response {marker!r}:\n" + "".join(recent[-15:]))

            def send(command):
                server.stdin.write(command + "\n")
                server.stdin.flush()

            def command(instruction, marker):
                send(instruction)
                wait_for(marker)

            def expect(position, state, label):
                command(f"execute if block {position} {state} run say {label}", f"[Server] {label}")
                print(label, flush=True)

            worker = threading.Thread(target=read, daemon=True)
            worker.start()
            try:
                wait_for("Done (", 120)
                command("forceload add 0 0 31 15", "to be force loaded")
                command("tick freeze", "frozen")
                for position, block in [
                    ("15 64 0", "observer[facing=east,powered=false]"),
                    ("14 64 0", "redstone_lamp"),
                    ("15 64 1", "redstone_lamp"),
                    ("16 64 0", "stone"),
                ]:
                    command(f"setblock {position} {block}", "Changed the block")
                for tick, powered in enumerate(("false", "true", "true", "false"), 1):
                    command("tick step 1", "Stepping")
                    time.sleep(.25)
                    expect("15 64 0", f"observer[powered={powered}]", f"tick {tick}: powered={powered}")
                    if tick == 2:
                        expect("14 64 0", "redstone_lamp[lit=true]", "rear lamp on")
                        expect("15 64 1", "redstone_lamp[lit=false]", "side lamp off")
            finally:
                if server.poll() is None:
                    send("stop")
                    try:
                        server.wait(timeout=35)
                    except subprocess.TimeoutExpired:
                        server.kill()
                        server.wait(timeout=5)
                worker.join(timeout=5)


if __name__ == "__main__":
    main()
