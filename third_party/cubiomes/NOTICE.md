# Cubiomes native biome dependency

Copyright (c) 2020 Cubitect. MIT license retained in LICENSE.
Contributor version correction by Zes-MinKey-Young, pull request 162:
https://github.com/Cubitect/cubiomes/pull/162

Pinned source commit: ca9fcb4e2b4f29a88b15a19fe1f00a73bf503ec2.
Per-file SHA-256 hashes are in tools/cubiomes-manifest.json.
Only the native generator/noise/biome subset and its table dependencies are used.
`biomenoise.c` has one local adapter: a UINT64_MAX cache sentinel requests a cold
lookup at the start of each chunk, rather than guessing a previous lookup below
the world. Original and patched SHA-256 hashes are retained in the manifest; the
fetch script reproduces the patch after verifying upstream bytes. This preserves
deterministic per-chunk behavior and fixes a captured first-cell biome mismatch.
Other upstream source files are unmodified. CUDA Craft supplies build integration:
MSVC C11 with /fp:precise, no FMA contraction option; GCC uses -fwrapv and
-ffp-contract=off. The library is linked statically and needs no JVM at runtime.

MC_1_21_11 selects the corrected 1.21.5+ table. The older upstream master table
does not reflect the increased Pale Garden distribution in these versions.
The integration maps historical library IDs to the 1.21.11 protocol registry,
preserves section sampling order, and sends the SHA-256-derived biome zoom seed.

Official-server biome fixtures test the integration; sample agreement does not
prove complete world-generation parity. Approximate terrain height, caves,
ores, trees and other decorations remain CUDA Craft prototype behavior.
