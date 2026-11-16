# References and provenance

Target selected by the project owner: Minecraft Java **1.21.11**.

Consulted 2026-11-13:

- [PrismarineJS minecraft-data: 1.21.11 version record](https://github.com/PrismarineJS/minecraft-data/blob/master/data/pc/1.21.11/version.json): protocol 774.
- [PrismarineJS minecraft-data: 1.21.11 protocol schema](https://github.com/PrismarineJS/minecraft-data/blob/master/data/pc/1.21.11/protocol.json): handshake, status and login disconnect field definitions.
- [PrismarineJS minecraft-data license declaration](https://github.com/PrismarineJS/minecraft-data#license): upstream README declares MIT; attribution is retained in `third_party/minecraft-data/NOTICE.md`.

Additional references for 0.2.0:

- [NVIDIA CUDA 12.8.1 manifest](https://developer.download.nvidia.com/compute/cuda/redist/redistrib_12.8.1.json): official Windows downloads and SHA-256 checksums.
- [NVIDIA Windows installation guide](https://docs.nvidia.com/cuda/archive/12.8.1/cuda-installation-guide-microsoft-windows/index.html): toolkit/toolchain requirements.
- [Microsoft CNG properties](https://learn.microsoft.com/en-us/windows/win32/seccng/cng-property-identifiers): one-byte AES CFB message block length.
- [PrismarineJS node-yggdrasil](https://github.com/PrismarineJS/node-yggdrasil/blob/master/src/Server.js): independent hasJoined/session hash reference.
- [PrismarineJS node-minecraft-protocol](https://github.com/PrismarineJS/node-minecraft-protocol/blob/master/src/server/login.js): independent login sequence reference.

Dependencies:

| Dependency | Location / license | Integrity |
| --- | --- | --- |
| CUDA 12.8.1 packages | `.tools/cuda`, NVIDIA license retained per component | Checked against NVIDIA manifest; nvcc 12.8.93, runtime/CCCL 12.8.90 |
| zlib 1.4 | `.tools/dependencies/zlib-1.4` or FetchContent; zlib license in archive | Official `https://zlib.net/zlib14.zip`; SHA-256 `38bc7ed1c8ac3c870a159623deacf74a46c393ae1ad23f1dca1ed8e272deb058` |
| nlohmann/json 3.12.0 | `third_party/nlohmann/json.hpp`; adjacent MIT license | Official tagged GitHub header; SHA-256 `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63` |
| Python cryptography | Tests only; installed package license | Independent client crypto |
| Windows CNG/WinHTTP | Operating system APIs | No bundled implementation |
| PrismarineJS minecraft-data | Generated registry/block/item facts; upstream MIT declaration and local notice | Commit `9b995542ab4a17837dee785439954e43d031b33e`; per-file SHA-256 in `tools/game-data-manifest.json` |
| Cubiomes native subset | `third_party/cubiomes`; MIT license and local notice | PR 162 revision `ca9fcb4e2b4f29a88b15a19fe1f00a73bf503ec2`; original/patched SHA-256 in `tools/cubiomes-manifest.json` |
| Hyperchunk native subset | `third_party/hyperchunk`; MIT license and local notice | Revision `387c9ad30ac1b4eddbcaad31a02e8864d5b394c1`; original/patched SHA-256 in `tools/hyperchunk-manifest.json` |

For 0.3/0.4, `tools/fetch-game-data.ps1` downloads the pinned 1.21.11 `loginPacket.json`,
`blocks.json` and `items.json`. `tools/generate-game-data.py` emits the registry NBT
packets, terrain block/biome identifiers and the initial building-block list. Version
0.9 additionally generates all 1,166 block records and their property tables.
Referenced gameplay tags are empty;
timeline tags contain their referenced entries. This is not a full vanilla tag dump.
The independent [prismarine-chunk implementation](https://github.com/PrismarineJS/prismarine-chunk)
was also consulted for version-specific palette packing.

World-generation feasibility references (0.4.0):

- [hyperchunk](https://github.com/Soju06/hyperchunk), inspected commit
  `387c9ad30ac1b4eddbcaad31a02e8864d5b394c1`: targets 26.2, not 1.21.11;
  initially investigated only; a subset is now linked in 0.6 with 1.21.11 data and
  four adapted files. No upstream parity claim transfers to CUDA Craft; its own
  differential tests establish only the explicitly sampled stage output.
- [cubiomes](https://github.com/Cubitect/cubiomes): independent biome-generation
  library; investigated in 0.4 and linked statically in 0.5, with the
  [1.21.5–1.21.11 correction](https://github.com/Cubitect/cubiomes/pull/162).
- [Pumpkin world architecture](https://docs.pumpkinmc.org/developer/world):
  independent engine architecture reference; no implementation copied.

The experimental CUDA Craft terrain algorithm is original integer value-noise code.
Only its block/biome identifiers come from the pinned minecraft-data inputs.
Version 0.5 adds Cubiomes climate sampling and approximate surface height through
an explicitly separate generator identity. A documented local cache-initialization
adapter fixes a captured first-cell boundary mismatch. No proprietary source is used.

The official 1.21.11 reference was downloaded from the server URL in the installed
Mojang version manifest, with SHA-1 `64bb6d763bed0a9f1d632ec347938594144943ed` verified.
The `net.minecraft.data.Main --reports` tool produced local reference data without
decompilation. Separate short-lived vanilla runs produced the biome-only fixtures.
The official JAR, libraries, reports and complete worlds remain in ignored `.tools`;
none is a native-runtime dependency or included in the distributable executable.

The engine integration is C++ with the attributed native dependencies above. No
Minecraft server source, game assets or server JARs are copied or bundled. Registry
facts derived from the independent source above are included in the generated header.
Third-party libraries retain their own notices. Future dependencies must record
license and provenance here.
Protocol documentation is evidence about the wire format, not evidence of gameplay
parity. The biome and density/surface/carver subsystems now have reference-server
comparisons. Feature placement, structures, fluid promotion and full gameplay still
need implementation and a broader differential harness.

`tools/generate-density-data.py` verifies the official bundle hash, extracts only
worldgen JSON (not class code), and combines it with the official block-state report
to embed configuration facts. The runtime has no JAR/JVM dependency. Test datapacks
empty biome feature lists and disable structures so stage comparisons are meaningful.
The two later fluid differences are retained under `tests/fixtures/postprocess`.

`tools/generate-simulation-data.py` derives hardness, default states, simple drop
facts, stack limits and tool durability from the same hash-pinned independent
1.21.11 block/item files. It generates `simulation_data.hpp`; neither these data
nor protocol tests establish vanilla simulation parity.

`tools/generate-smelting-data.py` verifies the official 1.21.11 server bundle
and its embedded server hash, expands original recipe ingredient tags and derives
numeric fuel durations from the local mapped `FuelValues` bytecode via `javap`.
`tools/smelting-data-manifest.json` records the source and generated hashes:
107 recipes, 227 input/type mappings and 280 fuel items. The native header contains
these numerical facts; the official JAR and bytecode reference stay in ignored
local tooling. This does not establish full furnace parity (XP and chunk tickets
are not implemented).

The performance monitor uses Windows process counters, distinguishing working set
from private committed memory as documented in
[PROCESS_MEMORY_COUNTERS_EX](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex)
and [GetProcessMemoryInfo](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo).
The console polls the documented
[console input event API](https://learn.microsoft.com/en-us/windows/console/low-level-console-input-functions)
and [PeekNamedPipe](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-peeknamedpipe)
before reading. No external monitoring process or Java runtime is used by these features.
