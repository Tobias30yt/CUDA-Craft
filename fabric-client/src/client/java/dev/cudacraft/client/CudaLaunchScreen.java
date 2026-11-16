package dev.cudacraft.client;

import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.ConnectScreen;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.multiplayer.ServerData;
import net.minecraft.client.multiplayer.resolver.ServerAddress;
import net.minecraft.network.chat.Component;

final class CudaLaunchScreen extends Screen {
    private final Screen parent;
    private final WorldCatalog.World world;
    private boolean started, cancelled;
    private String error;
    CudaLaunchScreen(Screen parent, WorldCatalog.World world) { super(Component.literal("CUDA Craft: " + world.name())); this.parent = parent; this.world = world; }
    @Override protected void init() {
        addRenderableWidget(Button.builder(Component.literal("Back / Cancel"), button -> onClose()).bounds(width / 2 - 100, height / 2 + 45, 200, 20).build());
        if (!started) {
            started = true;
            var client = minecraft;
            CudaCraftClient.BACKEND.start(CudaCraftClient.executable(), world, client.getUser().getName(), client.getUser().getProfileId()).whenComplete((port, failure) -> client.execute(() -> {
                if (cancelled) return;
                if (failure != null) { error = failure.getMessage() == null ? "Could not start the native server" : failure.getMessage(); return; }
                String address = "127.0.0.1:" + port;
                ConnectScreen.startConnecting(parent, client, ServerAddress.parseString(address), new ServerData(world.name(), address, ServerData.Type.OTHER), false, null);
            }));
        }
    }
    @Override public void onClose() { cancelled = true; CudaCraftClient.BACKEND.stopAsync(); minecraft.setScreen(parent); }
    @Override public void render(GuiGraphics graphics, int mouseX, int mouseY, float partialTick) {
        super.render(graphics, mouseX, mouseY, partialTick);
        graphics.drawCenteredString(font, title, width / 2, height / 2 - 35, 0xffffffff);
        graphics.drawCenteredString(font, error == null ? CudaCraftClient.BACKEND.status() : font.plainSubstrByWidth(error, width - 30), width / 2, height / 2, error == null ? 0xffdddddd : 0xffff7777);
    }
}
