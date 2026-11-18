# Inventory controls (0.11.3)

The supported inventory, workbench, chest/double chest, barrel and furnace menus
now handle left-drag distribution, right-drag one-item placement and double-click
collection. Number-key swaps additionally accept the offhand key; F outside a menu
swaps the held stack and offhand and cancels ongoing eating/mining.

Drag start and slot-selection packets do not move items or increment the menu
revision. Only the final packet commits the transaction. Duplicate slots are
counted once, stack capacities are respected and unused items stay on the cursor.
A different click, stale revision, close, dimension change or external inventory
update cancels the pending drag. Other viewers receive the committed container
contents with their own revisions.

Double-click collection requires an occupied cursor and an empty clicked slot.
It collects matching item/damage stacks in two passes (partial stacks, then full
stacks), respects the button's traversal direction and never collects a derived
crafting output. Furnace outputs remain takeable but cannot receive drag items;
fuel and empty-bucket restrictions apply. Armor slots reject unrelated items and
limit accepted equipment to one item. Creative middle-drag fill is rejected in
survival. Offhand corrections use the dedicated 1.21.11 player-inventory packet.

The transaction phases, collection order, creative-fill permission and packet
encoding were checked against the locally installed, mapped Minecraft 1.21.11
`AbstractContainerMenu`, `Inventory` and `ClientboundSetPlayerInventoryPacket`
classes. These files are reference inputs, not distributed project code.

## Still incomplete

Shift-crafting, complete recipe/remainder handling, arbitrary item components,
equipment component overrides, binding enchantments, all specialized menus and
furnace XP remain incomplete. This release does not certify full vanilla inventory
parity. The backlog entry B005 remains partial.

Independent encrypted clients test distribution remainders, full stacks, duplicate
drag slots, stale packets, two viewers, fuel/output restrictions, equipment slots,
offhand swaps and cancellation. The opt-in real Fabric container scenario exercises
dragging, collection, offhand round trips and right-drag crafting, then verifies the
exact final item counts in the saved world. See [VALIDATION.md](VALIDATION.md).
