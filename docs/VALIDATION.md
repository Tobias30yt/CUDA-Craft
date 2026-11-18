# Validation record

Date: 2026-11-18. CUDA Craft 0.11.5. Minecraft Java 1.21.11 / protocol 774.

## Version 0.11.5 additions

- The final statically linked native build passed **20/20 CTest suites in
  103.79 seconds**, including CUDA diagnostics on the RTX 4060. Earlier runs
  exposed test-world sharing and cleanup assumptions after introducing world
  leases; the fixtures now use isolated saves and remove the persistent lock file
  only after closing their worlds. The knockback test compares consecutive motion
  snapshots because a pig can wander away from its summon position before an attack.
- Seven real-process storage cases cover concurrent/path-alias opens, independent
  worlds, dimension saves, OS lease release after termination, invalid-save
  preservation, failed-save exit codes, and density world reopen. The last valid
  save survives an injected temporary-file write failure. This is not a proof of
  power-loss durability or atomic commits across dimensions.
- The Fabric build passed **seven JUnit cases**, including real child processes
  that exit successfully, fail while saving, hang during shutdown or fail startup.
  Concurrent stop requests share one completion; the synchronous shutdown waits
  for it. Forced/nonzero exits retain a failure status, visible in the world screen.
- A real Fabric 1.21.11 client received the native world, exercised chest/furnace
  menus, drag/collect/offhand and bulk crafting, and shut down cleanly. Saved counts
  were exactly 17 diamonds, one iron ingot and two chests, without leftover planks
  or duplicated container contents.
- The extracted `.mrpack` passed the same seven storage/status cases in a fresh
  Unicode/spaced directory with PATH restricted to Windows System32. Native and
  mod versions and package checksums match. PE imports are only `bcrypt.dll`,
  `kernel32.dll`, `winhttp.dll` and `ws2_32.dll`; VC++ and CUDA runtimes do not need
  separate runtime DLLs. This host test is not a clean second-PC test or an actual
  Modrinth application's import-dialog test. Machine-readable evidence is written
  next to the pack as `.verification.json`.
- Seven original Cubicmetre schematics were inventoried with hashes and checked
  against the official 1.21.11 block-state report. Three decoder tests cover signed
  packed words, cross-word indices, malformed arrays and unknown states. The strict
  Mk.6.1/OSC-6-panel/Nether compatibility gate passes; the newer MS panel is correctly
  rejected by its data version despite recognized block states. See
  [OSC_VANILLA_TEST.md](OSC_VANILLA_TEST.md) and [osc-fixture-audit.json](osc-fixture-audit.json).
  No complete cannon integration run has passed or been attempted on the native server.
- The updated workbook contains 59 tasks, 1,166 block rows and 89 mob rows.
  Summary reconciliation, status-change recalculation, formula-error search and
  visual checks of all three sheets passed.

The release remains an experimental build. Full Vanilla mechanics, the >90%
verified-block target and the Orbital Strike Cannon acceptance test remain open.

## Version 0.11.4 additions

- All **18 CTest suites have passing results** for the release: the full run passed
  17 suites; its new restart case incorrectly force-terminated the fixture before
  saving. After correcting that test to send `stop` and await exit, the complete
  container-network suite (13 scenarios) passed again in 16.26 seconds. No native
  code changed after the full run.
- **2,030 crafting checks** cover both normal and mirrored/permuted matching for
  all 1,010 imported recipes, plus tag alternatives, shifted shapes, invalid extra
  ingredients, excluded component recipes and crafting remainders.
- **818 simulation checks** include atomic refusal when the item-entity capacity
  cannot hold a crafting remainder or partial output. Removing the capacity limit
  allows the same action without duplicating or losing ingredients/results.
- Independent encrypted clients verify 20 logs yielding 80 planks, stale output
  clicks, full inventories, partial-output drops, cake returning three buckets,
  honey returning eight bottles and exact counts after a saved server restart.
  Ping barriers distinguish transaction replies from already queued peer updates.
- Fabric build and all three JUnit tests passed. The real Fabric client drag-filled
  a workbench with 16 planks, shift-crafted two chests and shut down cleanly. The
  server save contained exactly two chests, 17 transferred diamonds and one iron
  ingot, with no leftover planks or duplicate container contents.
- Client 0.11.4 is installed; its native EXE is compared by SHA-256 with the tested
  build. The updated Modrinth package checks every payload against its release file.
  The workbook's formulas/input recalculation and all three rendered sheets passed.

The 60 excluded crafting entries are enumerated in crafting-data-manifest.json.
This release does not establish complete crafting-component support, crash-safe
storage, or the >90% vanilla-block target. See [CRAFTING.md](CRAFTING.md).

## Version 0.11.3 additions

- Full native release gate: **17/17 CTest suites passed in 100.15 seconds**.
  The 811 simulation checks and 704 container lifecycle checks remain passing.
- Five additional encrypted-client inventory scenarios cover left/right dragging,
  remainders, capacity limits, duplicate selections, double-click collection order,
  two-viewer invalidation, stale messages, armor/furnace restrictions, offhand swaps
  inside menus and via the normal F-key packet, incomplete-drag cancellation and
  creative fill authorization. All ten container-network scenarios passed.
- Fabric build and all three JUnit tests passed. The real Fabric container scenario
  split 17 diamonds across three chest slots, collected them again and swapped them
  through the offhand. It then crafted a chest using right-drag ingredient placement.
  The save retained exactly 17 diamonds, one smelted iron ingot and one crafted chest;
  the client and isolated backend shut down cleanly.
- Offhand synchronization uses the version-pinned player-inventory packet, verified
  from the installed 1.21.11 mapped classes and the generated protocol report.
- The local client and Modrinth package are updated to 0.11.3. Installed native files
  are compared to the tested build by SHA-256; all pack members are checked against
  their source files. The updated workbook passed formula, input-change and visual checks.

Inventory parity is still partial: shift-crafting, full components, recipes and
specialized menus remain open. See [INVENTORY.md](INVENTORY.md).

## Version 0.11.2 additions

- Full native release gate: **17/17 CTest suites passed in 95.14 seconds**.
- **811 simulation checks**, including 42 additional portal checks. They cover
  automatic 8:1 travel and return, inventory/position persistence, arrival cooldown,
  axis rotation, floor conversion at negative coordinates, different search radii,
  distance ordering, broken frames, dry fallback platforms, refusal to overwrite a
  chest, world-boundary clamping and source validation before travel.
- Fabric build and all three JUnit tests passed. The real-client dimension scenario
  also entered the saved Overworld portal at (800.5, 64, 800.5), received the linked
  Nether portal at (100.5, 70, 100.5), waited through cooldown, returned automatically
  and saved the exact horizontal return position. The same run received both extra
  dimensions and all 89 mob types and shut down cleanly.
- `--smoke --smoke-dimensions` now seeds isolated portal fixtures and requires both
  linked-travel log markers plus the saved return position. Normal worlds are untouched.
- The updated workbook has 55 tasks: 23 open, 31 partial, one completed. Portal
  linking/creation remain partial pending exact vanilla placement/collision comparisons.
  Formula checks, status-input recalculation and all three sheet previews passed.
- The regular Fabric instance is updated to 0.11.2. Its EXE is compared by SHA-256
  with the tested binary; the Modrinth package checks every archived release file.

This establishes the tested portal behavior, not full vanilla portal-generation
parity or a new block-completion percentage. See [PORTALS.md](PORTALS.md).

## Version 0.11.1 additions

- Full native release gate: **17/17 CTest suites passed in 97.29 seconds**.
- **769 simulation checks**, including 81 additional checks for portal validity:
  both axes, removal of required floor/roof/side blocks, interior holes, optional
  corners and a player standing in a damaged portal before the next tick.
- Fabric build and all three JUnit tests passed. The real Fabric client received
  Nether, End and all 89 mob types with 0.11.1; return travel, dimension saves and
  clean shutdown were verified by `--smoke --smoke-dimensions`.
- The regular client instance was updated to 0.11.1. Its native executable has
  the same SHA-256 as the tested build. The 0.11.1 Modrinth pack was created and
  every packaged file was compared with its source release file.
- The implementation backlog contains 55 tasks with priorities, dependencies and
  acceptance criteria. The accompanying workbook contains 1,166 block audit rows
  and 89 mob audit rows; no full vanilla behavior certification is inferred from
  registration or spawning. Formula counts and a changed status input were checked,
  the formula-error scan was empty, and previews of all three sheets were reviewed.

## Version 0.11 additions

- Full native release gate: **17/17 CTest suites passed in 98.89 seconds**.
  The final dimension-clock guard also has a targeted regression: loading a child
  dimension cannot rewind its clock behind the birth times of saved entities.
  The three affected suites passed again in 6.96 seconds after that guard.
- 688 simulation checks cover the previous gameplay plus every mob profile's
  creation/persistence, dimension isolation, inventory on round trips, independent
  dimension reloads, entity-ID reservation and high-health mobs. Frame tests reject
  incomplete Nether portals, fill complete ones and exercise actual portal travel.
- New block checks cover falling sand, concrete hardening, hydrated farmland,
  bone-meal growth and button release timing.
- Independent encrypted clients decode Nether/End 16-section chunks and their
  biome IDs, verify all three respawn transitions and unchanged item quantities.
- A real Fabric 1.21.11 client entered Nether and End, received **all 89 mob
  types**, returned to the Overworld and exited cleanly. All three dimension save
  files and the persisted return dimension were checked. The scenario is available
  through `--smoke --smoke-dimensions`; source fixture names are generated from the
  pinned entity registry. All three Fabric JUnit tests pass.
- The real-client chest/furnace/workbench scenario also passed against 0.11,
  with exact final item quantities checked in the server save.
- The regular `client-instance` now contains the 0.11 mod and native backend;
  the installed EXE matches the tested build by SHA-256. The 0.11 Windows `.mrpack`
  was rebuilt and its ZIP contents compared against the release files.
- Creative positions now persist on movement and reconnect, including when the
  original spawn floor was modified. The wire regression checks the last saved
  position instead of forcing creative players back to spawn.

These checks do not establish the requested >90% complete-block-behavior target,
vanilla Nether/End generation or complete species-specific AI/game mechanics.
See EXPANSION_0_11.md, MOBS.md and block-coverage.csv.

## Prior version 0.10 additions (2026-11-16)

- Final native Release gate after the knockback fix: **17/17 CTest suites passed
  in 96.66 seconds**. The regular `client-instance` is updated to 0.10.0; its
  native executable matches the tested build by SHA-256 and only one CUDA Craft
  mod is installed. Launch with `Start CUDA Client.cmd`.
- 704 container checks cover persistence, break drops, entity-capacity preflight,
  invalid saves and all 227 furnace input/type mappings, including exact cooking
  completion ticks and resuming a partially cooked item after loading.
- Five encrypted container wire tests cover simultaneous viewers, stale clicks,
  double-chest layout, reopening, range checks and furnace output protection.
- 483 simulation checks include six storage/furnace crafting recipes through
  real menu packets, consuming each ingredient exactly once. Empty recipe cells
  now correctly match instead of being interpreted as the registry's air item.
- Nonlethal melee hits now apply mob knockback. Regression cases cover direction,
  cooldown, AI ticks, drag, floor/wall/ceiling collision, overlapping positions and
  saved momentum. An independent encrypted client observes nonzero motion and
  increasing position after a survival punch.
- Fabric build and all three JUnit tests pass. A real Fabric 1.21.11 survival
  client opened a chest and furnace, transferred 17 diamonds and one freshly
  smelted iron ingot, then crafted a chest from eight planks at a workbench.
  The client and backend exited cleanly; the final save contains those exact
  inventory quantities, no remaining planks and no duplicated container contents.
  Logs/world pointer: `.tools/fabric-smoke`.
- Generated data records 107 original 1.21.11 smelting recipes, 227 expanded
  input/type mappings and 280 fuel items with pinned provenance hashes.

Full vanilla parity is still open: furnace XP, hoppers, additional inventories,
loot/NBT, automatic structures and complete mob/redstone behavior are not covered.

## Prior 0.9 additions (2026-11-15)

- Final native Release gate: **15/15 CTest suites passed in 82.92 seconds**.
- Fabric Loader 0.20.1, API 0.141.6+1.21.11, Loom 1.14.10, Gradle 9.2.1.
  Java 21 target compiled with JDK 23. The mod builds and all three JUnit tests pass.
- Real Fabric client sessions entered both flat and density native worlds. The
  final density smoke test ran for 20 seconds after joining, exited with code 0,
  checked native shutdown and found the local UUID in the persisted player state.
  Logs and the world pointer are in `.tools/fabric-smoke`.
- Five independent private-session wire tests cover encrypted login/configuration,
  owner binding, HMAC failure/replay, rejection of public bind/missing secrets,
  pause and saving/exit on parent EOF. Normal online-login regression tests remain.
- 395,969 registry/structure checks cover registered item placement, property
  round trips, orientation, multipart placement/removal, neighbor connections,
  waterlogged flow, atomic template bounds checks and path rejection.
- Gameplay wire tests independently decode a section with 300 different states,
  exercising the 15-bit global block palette. The admin wire test places 40,960
  blocks and receives the refreshed chunk without exhausting the output queue.
- All 1,202 locally imported original templates were independently checked:
  19,502 palette entries exactly match the official Mojang block report.
- The simulation suite now contains 406 checks, including protocol-driven slab
  merging, door creation/removal and survival bucket fill/extraction.
- A Windows graceful-close issue found during testing was fixed by draining peer
  input and half-closing output before bounded cleanup, preserving kick reasons.

Full vanilla structure assembly, automatic placement, block-entity payloads,
loot and all block/item behaviors are not covered by these passing checks.
At that milestone the regular `client-instance` contained the 0.9 mod, Fabric API,
native executable and locally imported templates.

## Environment

Windows x64, MSVC 19.42.34436 / VS 2022 Build Tools, SDK 10.0.22621.0,
CMake 4.2.3 / Ninja, Python 3.10.11 with cryptography. RTX 4060, driver 595.95,
local CUDA 12.8.1, architecture 89, static runtime. zlib 1.4, nlohmann/json 3.12.0,
CNG/WinHTTP. Cubiomes and adapted Hyperchunk are static C11 dependencies with
original and patched source hashes.

## Prior 0.8 automated checks

Release build: **13/13 CTest suites passed in 75.68 seconds**, without compiler
warnings. This includes the new console, permissions and telemetry suites.
In the prior 0.6 validation all four starters bound temporary loopback ports
and exited cleanly; SHA256 checks confirmed existing flat/0.4/0.5 saves unchanged.
The adapted dependency was reproduced and every pinned hash verified in that run.
The starter reports version 0.8.0. The production executable was also exercised
through an actual interactive Windows console: `monitor on 1`, `perf`, `help tp`,
`monitor off`, `perf export`, `save-all`, `stop`. It exited with code 0. Its empty
flat test world measured approximately 20 TPS, a 119 MiB working set and zero
dropped tick slots; these are an idle smoke sample, not a load benchmark.
The exported snapshot is `.tools/console-smoke-0.8/performance.json`.
After a real-client test exposed two missing packet paths, recipe-book settings
and the 1.21.11 gamemode-hotkey packet were added. Hotkey and signed-command
requests use the same session-UUID OP checks. Unsupported adventure/spectator
requests resynchronize the current mode without disconnecting the client.
The five affected suites passed again in 9.48 seconds after the final correction.

| Check | Scope |
| --- | --- |
| Protocol | 83 checks |
| Network | 12 integration tests |
| Crypto/auth/compression | 58 checks |
| Login wire | 7 integration tests |
| Gameplay wire | 8 integration tests: all four generators, multiplayer, 49-chunk streaming, building, save/reload, seed protection, legacy migration and keepalive |
| Simulation unit | 400 checks: water range/recession/falling/chunk boundary/source regeneration/cooling, redstone decay/off/repeater delay/lock/torch inversion, finite inventory, mining timing, drops, crafting, mob combat, eating, zombie attacks, drowning, buckets, falls, death/respawn, pending ticks and player/mob/crafting persistence |
| Simulation wire | 3 independent encrypted tests: survival injection rejection/crafting/mob drop metadata/death/respawn; water bucket flow/recession; lever/wire/lamp on/off |
| Admin/performance unit | 31 checks: wall-time TPS, timing quantiles/expiry, OP denial/grant/deop, UUID identity/rename/restart, tp bounds/NaN/relative coordinates, give limits, command effects and JSON export |
| Admin wire/console | 5 independent encrypted/pipe tests: command-tree parsing/filtering, live role changes, HUD, OP restart persistence, process/network values, oversized input/EOF, recipe-book notifications, kick/stop, and hotkey/signed-command permission enforcement |
| Terrain unit | 393,216 legacy CPU/CUDA comparisons; signed seeds/coordinates; new density decoration order, spawn and edits |
| Vanilla biome reference | 73,728 cells in 48 unmodified official chunks, 3 seeds, exact equality and zoom hash |
| Vanilla terrain reference | 6,291,456 states in 64 controlled stage-matched chunks, zero tolerance |
| CUDA smoke | Actual RTX 4060 kernel, streams and memory copies |

Gameplay tests independently decode encrypted/compressed packets, check received
biomes and seed hashes, build on the actual spawn floor, observe autosave, restart,
and verify the edit and adjusted spawn. Conflicting explicit seed/generator options
leave save bytes unchanged. Old worlds never adopt a different generator implicitly.

## What the terrain comparison establishes

Official server bundle SHA-1: `64bb6d763bed0a9f1d632ec347938594144943ed`.
The binary is unchanged. A test datapack empties biome feature lists and
`generate-structures=false` isolates the implemented stages. Noise, surface, biome,
aquifer, ore-vein and carver configurations remain unchanged.

- 48 chunks saved at CARVERS, before promotion/fluid postprocessing: 16 each for
  seeds 20261113, -1 and 1234567890. The last set is near chunk (768,-512).
- 16 additional noise/surface-only chunks, seed 20261113, with carvers disabled.
- All 64 match every block: **6,291,456 / 6,291,456**.

The adapter adds the 1.21.11 `weird_scaled_sampler` to the upstream 26.2 graph.
An input-dependency bug discovered during integration was fixed while preserving
expected output. These are actual state comparisons, not heightmap/occupancy checks.
They demonstrate sampled stage parity, not every seed or complete world generation.

## Retained failing later-stage diagnostic

The original 48 FULL-stage captures remain under `tests/fixtures/postprocess`.
`--postprocess` still correctly returns failure on two cells: vanilla promotion
placed flowing water (state 87), while the native stage result is air. Seed
1234567890: (12286,22,-8196), (12285,22,-8195). The other 4,718,590 states match.
This gap is not masked with a tolerance. Full fluid behavior must close this gate.

Normal CTest compares equal stages. CARVERS chunks from ordinary decorated worlds
were unsuitable as stage goldens because neighboring features had already written
ore and vegetation into them; controlled worlds prevent that contamination.

## Real-client smoke

On 2026-11-15 the final 0.8 build was tested with the unchanged installed 1.21.11
client and an isolated synthetic GuiTester session. Console OP/gamemode/give/tp/heal
worked. After restarting the fixture, the saved operator joined in the saved
creative mode; live deop/op refreshed the client and console gamemode/say messages
appeared in the client log. The player remained connected until `save-all`/`stop`.
The fixture exited with code 0 and the client logged the explicit reason
`Server stopping`. The final save retained GuiTester's OP entry, survival mode
and tick 4142. The verified isolated client was closed afterward.

The initial attempts exposed unsupported recipe-book packet 0x2d and gamemode
hotkey packet 0x04. Both are fixed and covered by independent wire regressions,
including permission denial after deop and unsupported spectator-mode recovery.
Current client log: `.tools/gui-client/console.log`; earlier failure logs are
`console-0.8-before-recipe-fix.log` and `console-0.8-before-hotkey-fix.log`.
The native console transcript is in the tool session; the tested world path is
recorded in `.tools/simulation-client-world.path` with its performance JSON export.
The normal starter was also checked with `--version` and exits successfully.

Historical 0.7 smoke:

The installed unchanged 1.21.11 client joined an isolated 0.7 survival fixture
using Java 21 and synthetic GuiTester identity. The prepared flat world contained
a pig, a sheltered zombie, item drops, a damaged pickaxe, food, a water source
and a lever/wire/lamp circuit. Logs show survival entry and chunk rendering;
no registry/chunk/entity/inventory decode errors were observed. At tick 1144,
the autosave contained 6 entities, 112 water edits and one lit lamp, with player
mode 0. This is a log/protocol smoke test, not manual UI gameplay validation.
The prior 0.6 client smoke also entered the density world successfully.
Account/Realms HTTP 401 warnings are expected for the synthetic identity.
Production still requires official online authentication.

Historical logs: `.tools/simulation-client-server.log`, `.tools/simulation-client-server-error.log`,
`.tools/gui-client/console-0.7.log`. Previous density log: `console-0.6.log`.
Only the verified test client and fixture processes were stopped afterward.
No screenshot or mouse/keyboard automation was performed. Earlier denied Java
window access was not retried or bypassed. The user confirmed the prior playable
build; independent positive real-account authentication evidence remains absent.
The official authentication rejection path was previously validated.

## Reproduce

```powershell
& '.\tools\build-cuda.cmd'
py -3 tests/terrain_reference_test.py build/cuda/terrain-probe.exe
py -3 tests/biome_reference_test.py build/cuda/biome-probe.exe
& '.\tools\fetch-hyperchunk.ps1' -DestinationDirectory '.tools/hyperchunk-repro'
# Deliberately failing until fluid promotion is implemented:
py -3 tests/terrain_reference_test.py build/cuda/terrain-probe.exe --postprocess
```

Optional capture with the separately verified official JAR and Java 21:

```powershell
py -3 tools/capture-vanilla-biomes.py --java 'PATH-TO-JAVA.exe' --terrain-only --seed 20261113
py -3 tools/capture-vanilla-terrain.py --controlled --seed 20261113 --limit 16
py -3 tools/capture-vanilla-biomes.py --java 'PATH-TO-JAVA.exe' --terrain-only --no-carvers --seed 20261113
```

Fixtures store 98,304 uint32 little-endian states per chunk (x, z, y), zlib-compressed,
with SHA256 over the raw bytes. Configuration/registry provenance is recorded in
`tools/density-data-manifest.json`. JARs, full worlds and reports stay ignored.
CTest and the native runtime never start Java or require the reference server.

## Limits

Precise generation currently uses the CPU. Legacy CUDA terrain remains available.
The playable density world applies approximate vegetation afterward. Placed ores,
other features, automatic structures, exact fluid promotion, dynamic light, complete survival
progression/recipes/loot, further block entities, full AI, pistons/advanced redstone, Anvil
and GPU gameplay remain absent. The new simulation is an initial CPU implementation;
its tests do not compare traces against vanilla. See GAMEPLAY.md for the supported
subset. Passing checks and the version bump do not claim complete vanilla compatibility.
