# Crafting (0.11.4)

The native server now matches **1,010 ordinary crafting recipes** from the pinned
official Minecraft Java 1.21.11 archive: 706 shaped and 304 shapeless recipes.
This replaces the small handwritten pattern list. Matching supports the 2x2 player
grid and 3x3 workbench, shifted/mirrored shapes, empty pattern cells and recursively
resolved item tags. Shapeless ingredients use bipartite matching so overlapping
ingredient choices cannot cause a false rejection through greedy assignment.

Clicking the output takes one recipe. Shift-click repeats while the output item
remains the same, ingredients remain and some output fits (at most 64 iterations,
the maximum ordinary ingredient stack). Output merges into the player inventory
from the last hotbar slot backwards. When nothing fits, no ingredients are consumed.
If only part of a recipe's output fits, the remaining output is dropped, matching
the installed vanilla workbench's behavior.

Crafting remainder facts come from the version-pinned item definitions: water,
lava and milk buckets return buckets; honey bottles and dragon breath return glass
bottles when used in a supported crafting recipe. Remainders first use their emptied
ingredient slot, otherwise inventory space, then a drop. Using an item and crafting
with an item have distinct remainder rules; powder-snow buckets are not treated as
crafting bucket returns.

Each recipe is staged before committing inventory/grid/cursor changes. Drop-capacity
failure prevents that recipe from consuming ingredients. Completed earlier recipes
in the same shift-click remain completed. The existing stale-window/revision checks
apply. Closing a crafting menu returns ingredients as one inventory update rather
than sending intermediate snapshots for each item.

## Evidence and scope

`tools/generate-crafting-data.py` verifies the official wrapper SHA-1, inner archive
SHA-256 and item-registry hash. It generates numerical recipe facts and
`tools/crafting-data-manifest.json`, including the exact excluded recipe list.
Recipe matching is tested in both normal and mirrored/permuted forms for all 1,010
entries, plus targeted tag, pattern and remainder cases. Integration tests cover
bulk output, full/partially full inventories, stale clicks, cake buckets, honey
bottles, save/reload and exhausted drop capacity.

**60 crafting entries remain excluded:** 16 effect-bearing suspicious-stew recipes,
32 transmutation recipes and 12 special serializers (such as dyeing armor, fireworks,
repair, map/book copying and decorated pots). Arbitrary item components, recipe-book
autofill, shift-moving ingredients into the crafting grid and every output-slot
click variant are not complete. This recipe count does not measure full block
behavior or establish the project's >90% vanilla-block target.
