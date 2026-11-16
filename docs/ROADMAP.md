# Incremental implementation plan

Each milestone must produce runnable code and automated checks. Version 0.10.0 adds
persistent chests, double chests, barrels, furnaces, blast furnaces and smokers,
with original 1.21.11 smelting/fuel data and authoritative vanilla menus. Shared
viewers, stale clicks, output-slot protection, break drops and restart persistence
are tested. Workbench recipes now correctly match empty cells. Hopper ordering,
furnace XP and other block entities remain open. Version 0.9.0 adds
a real Fabric 1.21.11 frontend with a supervised private native backend, full block
registry/property lookup, global chunk palettes and manual placement of 1,202
imported vanilla templates. Basic multipart placement, door interaction, slab
merging, fence/pane connections and waterlogging extend the building model.
Automatic seed-correct structures, jigsaw assembly, block entities and complete
vanilla gameplay remain unfinished. The Fabric frontend has been tested with real
flat and density world sessions. Version 0.8.0 added
measured performance telemetry, a polled native/pipe console, UUID-based persistent
operators and shared permission-checked commands.
Version 0.7 added
a 20 Hz CPU scheduler, water flow, survival/inventory/crafting basics, pigs/zombies
and a limited redstone circuit model. These have deterministic and encrypted wire
tests, not a claim of full vanilla parity. Version 0.6 added native 1.21.11 terrain with
6,291,456 exact stage-matched block comparisons. Decorations remain approximate;
placed features, structures and fluid promotion are outstanding. A real 1.21.11
client entered both test worlds. The user confirmed the prior build worked.
The original complete-engine requirements remain the target, not a completed claim.

| Milestone | Deliverable and acceptance gate | Status |
| --- | --- | --- |
| M1 Foundation/status | C++20 executable, CMake/CUDA integration, real TCP status/ping, parser and blackbox tests | Verified, including RTX 4060 kernel execution |
| M2 Login | 1.21.11 Login Start, online authentication, encryption, compression, failure handling; protocol and crypto tests | Implemented on Windows; real-account positive validation pending |
| M3 Configuration/world sync | Registries, tags, configuration, minimal spawn/chunks, keepalive/movement; unmodified 1.21.11 client joins | Creative baseline implemented and tested; limited tags, multiplayer building and basic chat/commands |
| M4 Storage/chunks | Versioned section storage, NBT/Anvil, loading/unloading/saving; round-trip and corruption tests | Chunk streaming and custom JSON edit saves implemented; Anvil and full storage model still planned |
| M5 Authoritative scheduler | Tick phases, tickets, simulation distance, deterministic event replay | Fixed 20 Hz CPU steps and bounded water/device queues implemented; tickets and full replay remain open |
| M6 GPU world processing | Device-resident pages, dirty transfers, real block-state workload, CPU/GPU parity | CUDA terrain generation with CPU/GPU parity checks; live GPU simulation still planned |
| M7 Redstone event baseline | Ordered neighbor/scheduled events, observable traces against vanilla | Lever/dust/torch/repeater/lamp baseline tested; differential traces still open |
| M8 GPU dependency scheduler | Read/write hazards, streams and deterministic merges; randomized interleaving tests | Planned |
| M9 Speculation | Snapshots, invalidation, adaptive horizon, commit barriers; replay matches non-speculative execution | Planned |
| M10 Redstone/pistons | Full 1.21.11 behavior including zero ticks and quasi-connectivity; differential fixtures | Planned |
| M11 Block entities/inventories | Transactions, persistence, menus, hopper/container ordering | Authoritative inventory, limited crafting, persistent chests/barrels and three furnace types; hoppers, XP and other block entities remain open |
| M12 Physics/entities | Collision, movement, spawning and deterministic entity lifecycle | Pig/zombie/item lifecycle, simple motion, combat and survival needs; full physics open |
| M13 AI | Navigation, targeting, goals and version-specific mob behavior | Simple pig wandering and zombie pursuit/attack; pathfinding and other mobs open |
| M14 Fluids/light | Boundary-safe propagation with reference-world comparison | Water flow/recession/buckets/source regeneration and waterlogged sources; lava flow, dynamic light and reference parity open |
| M15 Generation | GPU generation with exact seed/RNG/structure parity | Native climate and CPU density/surface/aquifer/carver stages have exact sampled reference matches. Legacy CUDA terrain retained. Feature/structure placement, fluid promotion and GPU port of precise generation remain outstanding |
| M16 Full gameplay/performance | Broad vanilla regression suite, long-running saves, profiling and optimization | Planned |

No completion date or full-vanilla claim is implied by the architecture. Keep the
protocol support matrix current as individual packets and systems become functional.
