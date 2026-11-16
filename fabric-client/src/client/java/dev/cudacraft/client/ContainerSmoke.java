package dev.cudacraft.client;

import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.inventory.ChestMenu;
import net.minecraft.world.inventory.CraftingMenu;
import net.minecraft.world.inventory.FurnaceMenu;
import net.minecraft.world.inventory.ClickType;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.Vec3;

/** Explicit --smoke-containers integration scenario; never active in normal play. */
final class ContainerSmoke {
    private int stage;
    private long next;
    private int ingredient;
    private static final int[] CHEST_GRID = {1,2,3,4,6,7,8,9};
    void tick(Minecraft client) {
        if (!Boolean.getBoolean("cudacraft.smokeContainers") || client.player == null || client.level == null || client.gameMode == null) return;
        long now = System.nanoTime(); if (now < next) return;
        var menu = client.player.containerMenu;
        if (stage == 0 && client.level.getBlockState(new BlockPos(2,64,2)).is(Blocks.CHEST)) {
            use(client, new BlockPos(2,64,2)); stage=1;
        } else if (stage == 1 && menu instanceof ChestMenu chest && chest.getRowCount() == 3 && menu.getSlot(0).getItem().is(Items.DIAMOND) && menu.getSlot(0).getItem().getCount() == 17) {
            client.gameMode.handleInventoryMouseClick(menu.containerId,0,0,ClickType.QUICK_MOVE,client.player);stage=2;
        } else if (stage == 2 && client.player.getInventory().countItem(Items.DIAMOND) == 17) {
            client.gameMode.handleInventoryMouseClick(menu.containerId,1,0,ClickType.QUICK_MOVE,client.player);stage=20;
        } else if (stage == 20 && client.player.getInventory().countItem(Items.OAK_PLANKS) == 8) {
            client.player.closeContainer();stage=3;
        } else if (stage == 3) {
            use(client,new BlockPos(4,64,2));stage=4;
        } else if (stage == 4 && menu instanceof FurnaceMenu && menu.getSlot(2).getItem().is(Items.IRON_INGOT)) {
            client.gameMode.handleInventoryMouseClick(menu.containerId,2,0,ClickType.QUICK_MOVE,client.player);stage=5;
        } else if (stage == 5 && client.player.getInventory().countItem(Items.IRON_INGOT) == 1) {
            client.player.closeContainer();stage=6;
            org.slf4j.LoggerFactory.getLogger("CUDA Craft").info("CUDA Craft smoke: chest and furnace menus verified");
        } else if (stage == 6) {
            use(client,new BlockPos(3,64,2));stage=7;
        } else if (stage == 7 && menu instanceof CraftingMenu) {
            for (int slot=10;slot<menu.slots.size();slot++) {
                if (menu.getSlot(slot).getItem().is(Items.OAK_PLANKS)) {
                    client.gameMode.handleInventoryMouseClick(menu.containerId,slot,0,ClickType.PICKUP,client.player);stage=8;break;
                }
            }
        } else if (stage == 8 && menu instanceof CraftingMenu && menu.getCarried().is(Items.OAK_PLANKS)) {
            client.gameMode.handleInventoryMouseClick(menu.containerId,CHEST_GRID[ingredient++],1,ClickType.PICKUP,client.player);
            if (ingredient == CHEST_GRID.length) stage=9;
        } else if (stage == 9 && menu instanceof CraftingMenu && menu.getSlot(0).getItem().is(Items.CHEST) && menu.getCarried().isEmpty()) {
            client.gameMode.handleInventoryMouseClick(menu.containerId,0,0,ClickType.PICKUP,client.player);stage=10;
        } else if (stage == 10 && menu instanceof CraftingMenu && menu.getCarried().is(Items.CHEST)) {
            for (int slot=10;slot<menu.slots.size();slot++) {
                if (menu.getSlot(slot).getItem().isEmpty()) {
                    client.gameMode.handleInventoryMouseClick(menu.containerId,slot,0,ClickType.PICKUP,client.player);stage=11;break;
                }
            }
        } else if (stage == 11 && client.player.getInventory().countItem(Items.CHEST) == 1 && menu.getCarried().isEmpty()) {
            client.player.closeContainer();stage=12;
            org.slf4j.LoggerFactory.getLogger("CUDA Craft").info("CUDA Craft smoke: crafting menu verified");
        } else if (stage == 12 && client.player.containerMenu == client.player.inventoryMenu && client.player.getInventory().countItem(Items.CHEST) == 1) {
            stage=13;client.stop();
        }
        next=now+300_000_000L;
    }
    private void use(Minecraft client,BlockPos position) {
        client.gameMode.useItemOn(client.player,InteractionHand.MAIN_HAND,new BlockHitResult(new Vec3(position.getX()+.5,position.getY()+1,position.getZ()+.5),Direction.UP,position,false));
    }
}
