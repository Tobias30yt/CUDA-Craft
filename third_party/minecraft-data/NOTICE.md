# Minecraft protocol and registry facts

The generated file `include/cudacraft/generated_game_data.hpp` derives registry
values and block/item identifiers from the independent PrismarineJS/minecraft-data
project, targeting Minecraft Java 1.21.11. Source authors: the contributors to that
project. Upstream declares MIT in its README:

https://github.com/PrismarineJS/minecraft-data#license

The exact upstream commit and input SHA-256 hashes are recorded in
`tools/game-data-manifest.json`. Regenerate using `tools/fetch-game-data.ps1` and
`tools/generate-game-data.py`. Generated NBT/packet encoding and engine logic are
original CUDA Craft code. No Mojang executable, source, texture, sound, or model is
included in this repository. Registry data does not provide simulation behavior.

The supplied registry tag snapshot is deliberately limited to referenced tags:
timeline tags contain the four relevant timeline entries; gameplay-related tags
remain empty until their mechanics are implemented. It is not a complete vanilla
tag dataset.
