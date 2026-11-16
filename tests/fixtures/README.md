# Official 1.21.11 reference facts

## Terrain stages (0.6)

`vanilla-density-carvers-*.json` and their compressed little-endian uint32 arrays
contain 48 chunks at the saved CARVERS stage, 16 each for seeds 20261113, -1 and
1234567890. These come from separate controlled reference worlds: the official
binary is unchanged, structures are disabled, and biome feature lists are empty.
No noise, surface, climate, aquifer, ore-vein or carver configuration is changed.
These chunks were never promoted to FULL, so later fluid postprocessing is absent.

`vanilla-density-surface-20261113-0_0.json` adds 16 chunks with carvers also disabled,
to isolate density/surface behavior. The strict test checks every uint32 against
the version-pinned official registry and verifies fixture SHA256 hashes. Total:
**64 chunks / 6,291,456 block states, zero tolerance**.

The later FULL-stage captures remain in `postprocess/`, with a separate diagnostic
that currently fails on two flowing-water cells. See that folder's README.
This separation is a stage boundary, not a tolerance or an edit to expected output.

Capture controlled worlds using `tools/capture-vanilla-biomes.py --terrain-only`,
then export pre-promotion chunks with `tools/capture-vanilla-terrain.py --controlled
--seed N --limit 16 [--cx X --cz Z]`. The surface-only capture additionally uses
`--no-carvers`. Full vanilla feature/structure placement is not covered by this gate.

## Biome volumes (0.5)

Captured on 2026-11-14 using the unmodified Minecraft Java 1.21.11 server.
Official server SHA-1: `64bb6d763bed0a9f1d632ec347938594144943ed`.
Only biome IDs at quart resolution are retained, run-length encoded in x-fastest,
then z, then y order, covering y=-64 through 319. IDs are the version-pinned wire
registry order from minecraft-data. These are observed output facts, not game code.

- Seed 20261113: 16 chunks near the origin, positive and negative coordinates.
- Seed -1: 16 chunks near the origin, testing signed-seed handling.
- Seed 1234567890: 16 chunks around (768,-512), explicitly covering Pale Garden.

Total: 48 chunks / 73,728 biome cells. The Pale Garden capture exposed a one-cell
initial-cache discrepancy at chunk (769,-514), first quart cell. A deterministic
cold start fixed it; the fixture was not edited to fit the implementation.

Recreate with `tools/capture-vanilla-biomes.py --java PATH [--seed N --cx X --cz Z]`.
It requires the separately obtained official JAR at `.tools/vanilla-1.21.11/server.jar`
and verifies its SHA-1 before running. The reference stays on loopback, uses no
accounts, and is stopped after the capture. Its binaries, full worlds and reports
remain ignored under `.tools`; the native server never invokes them.

Exact agreement with these samples is evidence for these biomes and coordinates,
not proof of all-seed parity. Vanilla's cache-dependent equal-distance choices and
generation scheduling can create other boundary cases that require further checks.
These biome fixtures do not themselves validate terrain blocks or decorations.
