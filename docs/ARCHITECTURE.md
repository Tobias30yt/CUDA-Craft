# CUDA Craft architecture

## Implemented boundary

The executable owns its native TCP listener. The protocol library consumes complete
packet payloads and returns framed output without depending on sockets or CUDA.
The network layer owns bounded per-connection buffers and a nonblocking select loop.
CUDA is a separately linked backend with an executable diagnostic, now built and
executed on an RTX 4060. Windows login owns per-connection AES streams and compression
state; it submits identity lookups to a bounded two-worker authentication pool. The
production verifier uses CNG and WinHTTP. A separate test executable injects a fake
verifier without exposing that option through the production CLI.

Version 0.9.0 adds a Fabric client supervisor around the native server. The client
owns a loopback child process, a UUID world catalog, a world lock and a private
HMAC login proof. The normal multiplayer verifier is unchanged. The control pipe
handles pause/stop and parent EOF; details are in FABRIC_CLIENT.md.

The native engine continues through registry configuration into survival or creative.
`World` owns a shared block-edit map and active player list. Flat worlds reuse one
16 x 384 x 16 template. Experimental overworld chunks use integer noise on CUDA,
followed by CPU biome/decorations and a bounded LRU cache of 256 base chunks (about
100 MiB). Feature anchors use global coordinates and a canopy halo so request order
does not change border decorations. Integer CPU/CUDA outputs are compared in tests.
The retained `climate` generator uses a per-world native `BiomeSource` backed by a
pinned Cubiomes revision with the corrected 1.21.5+ lookup table. It samples full
3D biome volumes in section order and translates historical IDs into the 1.21.11
wire registry. The client receives the SHA-256-derived biome zoom seed. Surface
heights use Cubiomes' explicitly approximate climate-height function; surface
materials and tree species follow the sampled biome. CUDA fills blocks from the
256 CPU-sampled columns. Cave/ore/decoration algorithms remain approximations.
The new default `density` generator uses a version-adapted MIT Hyperchunk subset
and embedded official 1.21.11 JSON configuration facts. A per-world 32 MiB arena
holds the compiled density graph, seeded noises, surface rules and transient chunk
state; it rewinds to a checkpoint between chunks. Generation includes aquifers,
ore veins and cave/canyon carvers. A 64-chunk LRU retains undecorated terrain for
height/halo queries. The existing approximate vegetation pass follows actual new
surface heights; reference probes bypass vegetation to compare equal stages.
This generator is CPU-only and used on the main thread; it is not thread-safe.
`GameSession` owns movement, inventory, chunk visibility, keepalive and play packets.
Inputs modify the shared CPU world immediately and broadcast changes. Chunk palettes,
heightmaps, biome palettes and initial light are serialized on the CPU. Skylight
attenuates through leaves/water; a local flood fill spreads skylight and lava light.
Cross-chunk and dynamic light updates remain incomplete. This is a playable
gameplay baseline, not a device-resident or fully vanilla simulation.

`Simulation` executes fixed 50 ms CPU steps with a five-step catch-up bound.
Ordered water due-times, redstone device events, wire strength propagation,
simple pig/zombie/item entities and player needs share the world state. Registry
hardness, tool and stack facts are generated from the pinned 1.21.11 data.
`gameplay.cpp` implements authoritative inventory operations, a small recipe set,
damage/death/respawn and mob packet synchronization. Simulation-unit tests advance
the same scheduler directly; independent encrypted clients check observable packets.

Version 0.10 stores bounded container records in `Simulation`, keyed by block
position and persisted alongside player state. `containers.cpp` resolves canonical
double-chest layouts, validates access and routes authoritative menu transactions.
Each viewer receives its own revision; stale clicks cannot mutate inventory.
World edits preflight the item-entity capacity before replacing filled containers.
Furnaces execute in the same CPU scheduler using generated recipe/fuel facts and
persist both slots and cooking counters. They currently tick globally, without
vanilla chunk tickets, hopper transfers or XP accounting.

`Administration` is the shared console/chat command dispatcher. UUID-based OP and
known-player mappings use an atomically replaced world-local `operators.json`.
Commands are permission-checked on execution; live command trees and permission
status update when roles change. There is no automatic first-player promotion.
`ConsoleInput` polls console events or available pipe bytes on the server thread;
it has no detached blocking reader. Graceful stop drains final packets for at most
200 ms, destroys sessions (returning open crafting items), then saves the world.

`Performance` records real completed ticks, simulation durations and active server
loop durations separately from socket wait. Bounded samples provide mean/p95/max;
process CPU/memory and TCP byte counters are sampled on a roughly one-second cadence.
Chunk-generation, chunk-packet and save costs have separate counters. The select
timeout targets the next simulation tick instead of adding a fixed 50 ms delay
after every batch of work. HUD/console subscriptions and JSON snapshots share
the same measured state. These metrics do not report GPU utilization or client FPS.

Persistence writes differences from the seeded generator to versioned JSON.
A temporary file and atomic replacement protect the previous save from incomplete
writes; edits autosave once per second and on graceful shutdown. There is no journal,
Anvil support or concurrent-writer coordination. Player/inventory state, open crafting
items, tick queues and mobs are persisted in a versioned simulation object. The world
has a fixed border and a 100,000-edit cap. Registry facts are generated from pinned,
hash-checked independent data; the tag subset deliberately omits unimplemented mechanics.
Format 2 pins generator identity and signed 64-bit seed. Explicit settings that
conflict with saved metadata fail before modifying the save. Legacy format 1 stays
flat and migrates on a subsequent save. A changed algorithm needs a new generator ID.
The default density world, retained climate world, 0.4 overworld and flat starter use separate
directories and immutable generator IDs. Loading a saved generator never selects
a newer one implicitly.

`terrain_math.hpp` is an original experimental generator, not a port of the vanilla
noise router. Its GPU/CPU equivalence tests do not establish vanilla parity.
The separate density generator has strict block-level tests against 1.21.11
noise/surface/carvers captures. Full feature/structure ordering and fluid promotion
are still absent. The post-promotion reference diagnostic retains two known fluid
differences instead of masking them as terrain matches.
Separate biome tests compare captures from an unmodified official 1.21.11 server;
that JVM is an opt-in test dependency and is never invoked by the native runtime.

## Target engine design (not implemented)

| Module | Responsibility and boundary |
| --- | --- |
| Protocol and sessions | Explicit handshake/login/configuration/play states; version-pinned codecs; authenticated inputs become immutable tick events |
| Network and I/O | CPU-owned asynchronous connections, compression/encryption, immutable outbound batches; no direct world mutation |
| World storage | Chunk/section indexing, palettes and registries; structure-of-arrays block/entity data; GPU-resident active region pages |
| Persistence | CPU-side NBT/Anvil decoding, bounded background reads/writes, atomic region-file updates; committed snapshots only |
| Tick coordinator | Authoritative 20 Hz clock, ordered external input queue, vanilla tick-phase boundaries and deterministic publication |
| Event engine | Neighbor updates, scheduled block/fluid ticks, block events and piston actions; explicit causal ordering within a tick |
| GPU runtime | Persistent device arenas, stream/event ownership, bounded work queues and dirty-page extraction |
| Dependency scheduler | Read/write footprints, boundary dependencies, conservative serialization of conflicting event chains |
| Speculation | Versioned snapshots, tentative future results, dependency invalidation and authoritative commit validation |
| Gameplay | Block entities, inventories, physics, AI, fluids, light and generation, introduced behind parity tests |
| Validation | CPU reference paths, deterministic replay, vanilla differential traces and CPU/GPU equivalence tests |

### State and ordering

Allocate world state in chunk-section pages, with separate arrays for block IDs,
metadata, lighting and versions. Entity components use independent arrays and stable
IDs. Keep active pages resident on the GPU; transfer only dirty committed results and
input batches. Loading, ticking and visibility are separate flags derived from chunk
tickets and simulation distance. Unloading requires both I/O completion and absence
of references from authoritative or speculative work.

Represent each event with tick, phase, causal predecessor, source order and local
sequence. The exact comparator must be derived from 1.21.11 differential traces;
sorting coordinates alone is not a substitute for vanilla neighbor-update ordering.
Parallelize only disjoint, proven-independent footprints. Conflicting updates follow
the recorded causal order, even if that means serial execution. Never resolve game
conflicts by CUDA thread arrival order or atomic race winners.

Redstone must preserve intermediate observable transitions, neighbor notifications,
quasi-connectivity, piston block events and zero-tick behavior. Tick-end block equality
is insufficient. Use a deterministic CPU event executor first as an oracle for GPU
batches, then preserve the same trace across both paths.

### Dependency-aware GPU execution

Each task declares read versions, write regions and boundary events. Construct a DAG
over causal and memory hazards; tasks in an independent frontier may use separate CUDA
streams. CUDA events enforce cross-stream completion. A cyclic/event-recursive region
must execute through a deterministic ordered queue; it cannot be forced into a false
DAG. CUDA Graphs are an optimization for proven recurring graph shapes, after parity.
Do not move inherently serial or small workloads to a GPU solely to satisfy branding.

### Speculative ticks

An authoritative snapshot at tick T produces a versioned branch. Predict only regions
whose dependencies and inputs are known. Each result records its base versions, read
set, produced events and target tick. Speculation never sends packets, saves files or
mutates authoritative state. Commit at the corresponding authoritative tick only when
all read versions and causal dependencies still match; publish writes in canonical
event order. Invalidation propagates transitively through dependents, including
cross-region reads, chunk lifecycle changes and externally visible scheduled events.

Use copy-on-write pages with a bounded memory budget and explicit generation IDs to
avoid stale page reuse. Adaptive horizons shrink with invalidation rate, boundary
activity and memory pressure. Deterministic random-number streams belong to snapshots;
speculative retries must not consume authoritative RNG. Floating-point differences
must be accounted for before claiming physics parity. A horizon of 100 ticks is a
possible optimization target, not an unconditional correctness guarantee.

### Future differential harness

Run a legally obtained, version-pinned 1.21.11 reference server separately. Keep its
binary outside the repository and record its hash, JVM, settings, seed and inputs.
Drive both servers using the same recorded client actions and compare packet-visible
effects, persisted state and observable intra-tick test fixtures. Add fixtures for
redstone event order, zero ticks, quasi-connectivity, pistons and chunk boundaries.
Privately acquired artifacts and proprietary source must never enter this repository.
Where observability is insufficient, mark the fixture inconclusive instead of claiming
equivalence. A first harness now captures biome volumes from the unmodified 1.21.11
reference and compares them with the native output. The broader block/tick/gameplay
differential harness remains to be implemented.
