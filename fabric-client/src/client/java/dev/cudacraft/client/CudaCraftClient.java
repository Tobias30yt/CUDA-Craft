package dev.cudacraft.client;

import dev.cudacraft.client.mixin.LoginConnectionAccessor;
import io.netty.buffer.Unpooled;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientLifecycleEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.networking.v1.ClientLoginNetworking;
import net.fabricmc.fabric.api.client.networking.v1.ClientPlayConnectionEvents;
import net.fabricmc.fabric.api.client.networking.v1.ClientLoginConnectionEvents;
import net.fabricmc.fabric.api.client.screen.v1.ScreenEvents;
import net.fabricmc.fabric.api.client.screen.v1.Screens;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.PauseScreen;
import net.minecraft.client.gui.screens.TitleScreen;
import net.minecraft.network.FriendlyByteBuf;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import java.nio.file.Path;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;

public final class CudaCraftClient implements ClientModInitializer {
    public static final BackendSession BACKEND = new BackendSession();
    private boolean quickPlayed;
    private long smokeStarted;
    private final ContainerSmoke containerSmoke = new ContainerSmoke();
    public static WorldCatalog catalog() { return new WorldCatalog(Minecraft.getInstance().gameDirectory.toPath()); }
    public static Path executable() {
        return Path.of(System.getProperty("cudacraft.backend", Minecraft.getInstance().gameDirectory.toPath().resolve("cudacraft/native/cuda-craft.exe").toString()));
    }
    @Override public void onInitializeClient() {
        ClientLoginNetworking.registerGlobalReceiver(Identifier.fromNamespaceAndPath("cudacraft", "local_session"), (client, handler, buffer, callbacks) -> {
            if (buffer.readableBytes() != 32) return CompletableFuture.completedFuture(null);
            byte[] nonce = new byte[32]; buffer.readBytes(nonce);
            try {
                var remote = ((LoginConnectionAccessor)handler).cudacraft$connection().getRemoteAddress();
                byte[] proof = BACKEND.proof(nonce, remote);
                return CompletableFuture.completedFuture(new FriendlyByteBuf(Unpooled.wrappedBuffer(proof)));
            } catch (Exception invalid) { return CompletableFuture.completedFuture(null); }
        });
        ScreenEvents.AFTER_INIT.register((client, screen, width, height) -> {
            if (screen instanceof TitleScreen) Screens.getButtons(screen).add(Button.builder(Component.literal("CUDA Singleplayer"), button ->
                client.setScreen(new CudaWorldScreen(screen))).bounds(width / 2 + 104, height / 4 + 48, 140, 20).build());
        });
        ClientPlayConnectionEvents.JOIN.register((handler, sender, client) -> {
            if (BACKEND.state() == BackendSession.State.READY) BACKEND.connected();
        });
        ClientPlayConnectionEvents.DISCONNECT.register((handler, client) -> { if (BACKEND.active()) BACKEND.stopAsync(); });
        ClientLoginConnectionEvents.DISCONNECT.register((handler, client) -> {
            if (BACKEND.state() == BackendSession.State.READY) BACKEND.stopAsync();
        });
        ClientLifecycleEvents.CLIENT_STOPPING.register(client -> BACKEND.stop());
        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            int smokeSeconds = Integer.getInteger("cudacraft.smokeSeconds", 0);
            if (smokeSeconds > 0 && client.level != null && client.player != null && BACKEND.state() == BackendSession.State.CONNECTED) {
                if (smokeStarted == 0) { smokeStarted = System.nanoTime(); org.slf4j.LoggerFactory.getLogger("CUDA Craft").info("CUDA Craft smoke: world received"); }
                containerSmoke.tick(client);
                if (System.nanoTime() - smokeStarted > Math.min(smokeSeconds, 120) * 1_000_000_000L) client.stop();
            }
            BACKEND.pause(client.screen != null && client.screen.isPauseScreen());
            if (client.screen instanceof TitleScreen && !quickPlayed) {
                quickPlayed = true;
                String id = System.getProperty("cudacraft.quickPlayWorld");
                if (id != null) {
                    try { client.setScreen(new CudaLaunchScreen(client.screen, catalog().load(UUID.fromString(id)))); }
                    catch (Exception e) { client.setScreen(new CudaWorldScreen(client.screen, e.getMessage())); }
                }
            }
        });
    }
}
