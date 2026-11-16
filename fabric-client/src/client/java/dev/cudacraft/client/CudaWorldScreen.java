package dev.cudacraft.client;

import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.EditBox;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;
import java.util.List;
import java.util.concurrent.ThreadLocalRandom;

final class CudaWorldScreen extends Screen {
    private final Screen parent;
    private String error;
    private int page;
    private boolean creative, flat;
    private EditBox name, seed;
    CudaWorldScreen(Screen parent) { this(parent, null); }
    CudaWorldScreen(Screen parent, String error) { super(Component.literal("CUDA Craft Worlds")); this.parent = parent; this.error = error; }
    @Override protected void init() {
        String oldName = name == null ? "New CUDA World" : name.getValue();
        String oldSeed = seed == null ? "" : seed.getValue();
        int left = width / 2 - 155;
        List<WorldCatalog.World> worlds;
        try { worlds = CudaCraftClient.catalog().list(); } catch (Exception e) { worlds = List.of(); error = e.getMessage(); }
        int rows = Math.max(1, Math.min(5, (height - 210) / 24));
        int maxPage = Math.max(0, (worlds.size() - 1) / rows); page = Math.min(page, maxPage);
        for (int i = page * rows; i < Math.min(worlds.size(), (page + 1) * rows); i++) {
            var world = worlds.get(i);
            addRenderableWidget(Button.builder(Component.literal(world.name() + "  [" + (world.creative() ? "Creative" : "Survival") + "]"), b -> minecraft.setScreen(new CudaLaunchScreen(this, world)))
                .bounds(left, 42 + (i - page * rows) * 24, 310, 20).build());
        }
        int form = Math.max(80, height - 150);
        var previous = addRenderableWidget(Button.builder(Component.literal("<"), b -> { page--; rebuildWidgets(); }).bounds(left, form - 26, 40, 20).build()); previous.active = page > 0;
        var next = addRenderableWidget(Button.builder(Component.literal(">"), b -> { page++; rebuildWidgets(); }).bounds(left + 270, form - 26, 40, 20).build()); next.active = page < maxPage;
        name = addRenderableWidget(new EditBox(font, left, form, 190, 20, Component.literal("World name"))); name.setMaxLength(48); name.setValue(oldName);
        seed = addRenderableWidget(new EditBox(font, left + 200, form, 110, 20, Component.literal("Seed (optional)"))); seed.setMaxLength(40); seed.setHint(Component.literal("Random seed")); seed.setValue(oldSeed);
        addRenderableWidget(Button.builder(Component.literal(creative ? "Creative" : "Survival"), b -> { creative = !creative; b.setMessage(Component.literal(creative ? "Creative" : "Survival")); }).bounds(left, form + 26, 150, 20).build());
        addRenderableWidget(Button.builder(Component.literal(flat ? "Flat" : "Overworld"), b -> { flat = !flat; b.setMessage(Component.literal(flat ? "Flat" : "Overworld")); }).bounds(left + 160, form + 26, 150, 20).build());
        addRenderableWidget(Button.builder(Component.literal("Create and play"), b -> {
            try {
                long value; String text = seed.getValue().strip();
                if (text.isEmpty()) value = ThreadLocalRandom.current().nextLong();
                else { try { value = Long.parseLong(text); } catch (NumberFormatException ignored) { value = text.hashCode(); } }
                var world = CudaCraftClient.catalog().create(name.getValue(), value, flat ? "flat" : "density", creative);
                minecraft.setScreen(new CudaLaunchScreen(this, world));
            } catch (Exception e) { error = e.getMessage(); }
        }).bounds(left, form + 52, 310, 20).build());
        addRenderableWidget(Button.builder(Component.literal("Back"), b -> onClose()).bounds(left, height - 28, 310, 20).build());
    }
    @Override public void onClose() { minecraft.setScreen(parent); }
    @Override public void render(GuiGraphics graphics, int mouseX, int mouseY, float partialTick) {
        super.render(graphics, mouseX, mouseY, partialTick);
        graphics.drawCenteredString(font, title, width / 2, 12, 0xffffffff);
        graphics.drawCenteredString(font, "Native backend - experimental gameplay", width / 2, 26, 0xffaaaaaa);
        if (error != null) graphics.drawCenteredString(font, font.plainSubstrByWidth(error, width - 20), width / 2, height - 47, 0xffff7777);
    }
}
