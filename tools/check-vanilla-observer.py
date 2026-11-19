"""Check observer or copper bulb fixtures on the local official 1.21.11 server."""
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
    parser.add_argument("--copper-bulb", action="store_true")
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

            def place(position, block):
                command(f"setblock {position} {block}", "Changed the block")

            def step(ticks):
                command(f"tick step {ticks}", "Stepping")
                time.sleep(max(.25, ticks * .075))

            def bulb_fixture():
                place("16 64 0", "waxed_copper_bulb")
                place("17 63 0", "stone")
                place("18 63 0", "stone")
                place("17 64 0", "comparator[facing=west]")
                place("18 64 0", "redstone_wire")
                place("15 64 0", "redstone_block")
                expect("16 64 0", "waxed_copper_bulb[lit=true,powered=true]", "rise lights bulb")
                step(8)
                expect("16 64 0", "waxed_copper_bulb[lit=true,powered=true]", "held signal preserves light")
                expect("18 64 0", "redstone_wire[power=15]", "comparator reads 15 from lit bulb")
                place("15 64 0", "air")
                expect("16 64 0", "waxed_copper_bulb[lit=true,powered=false]", "fall preserves light")
                step(4)
                expect("18 64 0", "redstone_wire[power=15]", "comparator still reads 15 after power loss")
                place("15 64 0", "redstone_block")
                expect("16 64 0", "waxed_copper_bulb[lit=false,powered=true]", "second rise darkens bulb")
                step(4)
                expect("18 64 0", "redstone_wire[power=0]", "comparator reads 0 from dark bulb")

            worker = threading.Thread(target=read, daemon=True)
            worker.start()
            try:
                wait_for("Done (", 120)
                command("forceload add 0 0 31 15", "to be force loaded")
                command("tick freeze", "frozen")
                if args.copper_bulb:
                    bulb_fixture()
                    return
                for position, block in [
                    ("15 64 0", "observer[facing=east,powered=false]"),
                    ("14 64 0", "redstone_lamp"),
                    ("15 64 1", "redstone_lamp"),
                    ("16 64 0", "stone"),
                ]:
                    place(position, block)
                for tick, powered in enumerate(("false", "true", "true", "false"), 1):
                    step(1)
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
