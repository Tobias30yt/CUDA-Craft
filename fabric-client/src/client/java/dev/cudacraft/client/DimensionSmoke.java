package dev.cudacraft.client;

import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.Blocks;
import java.util.HashSet;

/** Opt-in real client integration check, inactive during ordinary play. */
final class DimensionSmoke {
    private int stage, mob;
    private long next;
    private final HashSet<String> received = new HashSet<>();
    void tick(Minecraft client) {
        if (!Boolean.getBoolean("cudacraft.smokeDimensions") || client.level == null || client.player == null || client.getConnection() == null) return;
        long now = System.nanoTime(); if (now < next) return; next=now+100_000_000L;
        for (var entity : client.level.entitiesForRendering()) received.add(BuiltInRegistries.ENTITY_TYPE.getKey(entity.getType()).getPath());
        if (stage==0) {client.getConnection().sendCommand("dimension nether");stage=1;}
        else if (stage==1 && client.level.dimension()==Level.NETHER && client.level.getBlockState(new BlockPos(0,63,0)).is(Blocks.NETHERRACK)) {
            org.slf4j.LoggerFactory.getLogger("CUDA Craft").info("CUDA Craft smoke: Nether received");
            client.getConnection().sendCommand("dimension end");stage=2;
        } else if (stage==2 && client.level.dimension()==Level.END && client.level.getBlockState(new BlockPos(100,48,0)).is(Blocks.OBSIDIAN)) {
            org.slf4j.LoggerFactory.getLogger("CUDA Craft").info("CUDA Craft smoke: End received");stage=3;
        } else if (stage==3) {
            client.getConnection().sendCommand("summon "+MobSmokeData.NAMES[mob++]+" 100.5 60 0.5");
            if (mob==MobSmokeData.NAMES.length) stage=4;
        } else if (stage==4 && received.containsAll(java.util.Arrays.asList(MobSmokeData.NAMES))) {
            org.slf4j.LoggerFactory.getLogger("CUDA Craft").info("CUDA Craft smoke: all {} mob types received",MobSmokeData.NAMES.length);
            client.getConnection().sendCommand("dimension overworld");stage=5;
        } else if (stage==5 && client.level.dimension()==Level.OVERWORLD && client.level.getBlockState(new BlockPos(0,63,0)).is(Blocks.GRASS_BLOCK)) {
            org.slf4j.LoggerFactory.getLogger("CUDA Craft").info("CUDA Craft smoke: dimension round trip verified");stage=6;
        } else if (stage==6) {stage=7;client.stop();}
    }
}
