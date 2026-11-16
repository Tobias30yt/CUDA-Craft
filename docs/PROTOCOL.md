# Protocol support: Minecraft Java 1.21.11

Target protocol: **774**, pinned to the independent
[PrismarineJS schema](https://github.com/PrismarineJS/minecraft-data/blob/9b995542ab4a17837dee785439954e43d031b33e/data/pc/1.21.11/protocol.json).

| Feature | Implementation |
| --- | --- |
| Transport/status | IPv4 nonblocking TCP, bounded buffers, UTF-8 MOTD, live player count, exact ping echo |
| Login | Version gate 774, name/UUID, RSA-1024 SPKI challenge, PKCS#1 v1.5, continuous AES-128-CFB8 |
| Online authentication | Signed SHA-1 session hash, official HTTPS hasJoined, verified UUID/name/properties |
| Compression | zlib threshold 256; exact lengths, trailing-data and size checks |
| Configuration | Brand, vanilla feature flag, empty known-packs offer, 23 registries with NBT data, limited tags, finish/ack |
| World entry | Survival or creative overworld, player info, abilities, health, daytime, border, spawn, inventory, command tree, teleport/ack |
| Chunks | 24 sections, y=-64..319; 1.21.11 block and biome palettes without array-length prefixes; heightmaps and initial sky/block light arrays |
| Streaming | Radius 3, 49 chunks; 2 chunks per 50 ms batch, unloads and view-center changes |
| Movement | Position/rotation validation, teleport recovery, peer position and head-rotation updates |
| Building | All 1,166 registered block types / 29,671 states; default block-item placement and common orientation properties, indirect/global palettes, reach/bounds checks, sequence ack, peer updates; incomplete special behavior |
| Inventory | Authoritative counts, damage component, left/right/number/shift/drop clicks, 2x2/3x3 crafting subset; unsupported click modes resynchronized |
| Survival/entities | Health/food/air, eating, damage, death/respawn, spawn and metadata, position, item collection and removal |
| Chat/commands | Shared console/chat dispatcher; permissions-filtered Brigadier tree, greedy string arguments, server suggestions, UUID OP/deop; command reference in ADMIN.md |
| Monitoring/admin | System-chat actionbar live monitor, console/perf JSON, permission status updates, targeted teleport/give/heal/kick, graceful save/stop |
| Containers | Vanilla chest/barrel/furnace menus, shared inventories, per-viewer revisions, stale-window rejection, double chests, persisted contents/timers, furnace property packets and protected result slots |
| Client UI packets | Recipe-book settings/display notifications accepted; gamemode hotkey requests enforce OP and support survival/creative only; signed-command envelopes decoded with the same session authorization |
| Session lifetime | Play keepalive every 5 seconds; 20-second response timeout; no login deadline after world entry |
| Storage | Custom JSON edit map plus generator identity and seed, atomic replacement, periodic save/reload, legacy-flat support; not Anvil |
| Terrain | Native 1.21.11 climate, density router, aquifers, ore veins, surface rules and carvers; CPU execution with approximate vegetation. Previous generator IDs and CUDA paths retained |

The normal network server requests online authentication. Fabric private sessions
use the loopback-only, owner-bound HMAC login described in FABRIC_CLIENT.md; the
explicit mode requires an ephemeral port, a world path and a random secret.
There is no public offline mode or custom authentication host. TLS certificate/hostname checks remain enabled,
redirects are disabled, and the client handles its own session-service join.
The server never asks for account passwords or access tokens.

Two background workers handle identity verification with a queue of at most 32 jobs.
HTTP timeouts, a 64 KiB response cap and bounded JSON depth/properties apply.
Status defaults to a 5-second absolute deadline, 16 KiB buffers and 4096-byte frames.
Login/configuration uses a 30-second absolute deadline. Decoded inbound packets are
capped at 64 KiB, with bounded framing buffers. Outbound chunk packets may be larger.
The default connection cap is 32; advertised player capacity is not an enforced
player-slot limit. Encryption/compression transitions occur at packet boundaries.

## Known limitations

Tags are a limited snapshot, not the full vanilla tag set. Initial skylight and lava
light propagate within individual chunks with simple opacity rules. Cross-chunk and
dynamic lighting updates are not implemented. Water flows; lava only supports
contact cooling. Movement relies on the client for normal physics and collision;
this is not authoritative vanilla physics or anti-cheat. Only a subset of items,
recipes and inventory operations has useful behavior; see GAMEPLAY.md.

Signed player chat is not implemented; received text is relayed as system messages.
Command-signature envelopes are parsed, but signatures are not verified or forwarded;
authorization uses the authenticated session UUID and server-side OP state.
Two roles (player/operator) are implemented. There are no fine-grained OP levels, bans, proxy forwarding,
general login plugins, transfer/cookie flows, public offline mode, IPv6, Query or RCON.
Pistons, full AI/loot/progression, generic block-entity NBT import, automatic structure generation and exact feature placement are absent.
Native chest/barrel/furnace storage is implemented; hopper transfer, comparator signals and XP are not.
Native manual template placement supports imported vanilla block palettes, with bounded chunk refresh.

## Verification boundary

Independent Python clients implement their own RSA/AES, zlib and chunk-palette decoder.
Tests include encrypted login/configuration, malformed inputs, two-player building,
movement streaming, save/reload, keepalive and returning to a rebuilt spawn point.
Positive identity verification is injected only into a separate loopback-only test
executable. Production rejection was checked against the official service (HTTP 204).

An unmodified 1.21.11 client entered both flat and natural test fixture worlds and
processed chunks, confirmed through client/server logs. The user reported that the
previous playable build worked. Direct visual automation remains unavailable.
Biome-only differential comparison now covers 48 chunks / 73,728 cells from three
unmodified official 1.21.11 worlds, including Pale Garden and negative seeds.
All captured biome cells match; this is not a claim of complete world-generation
parity. Separate strict tests now cover 6,291,456 terrain states at matching
noise/surface/carvers stages. Full feature placement and fluid promotion remain
outside that passing gate; the later-stage diagnostic retains two water differences.
