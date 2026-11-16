package dev.cudacraft.client;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

/** Only CUDA worlds have these UUID directories; vanilla saves are never modified. */
public final class WorldCatalog {
    public record World(UUID id, String name, long seed, String generator, boolean creative, Path directory) {}
    private final Path root;
    public WorldCatalog(Path gameDirectory) { root = gameDirectory.resolve("cuda-worlds").toAbsolutePath().normalize(); }
    public Path root() { return root; }
    public World create(String name, long seed, String generator, boolean creative) throws IOException {
        if (name == null || name.isBlank() || name.length() > 48 || name.chars().anyMatch(c -> c < 32))
            throw new IllegalArgumentException("World name must contain 1..48 printable characters");
        if (!Set.of("density", "flat").contains(generator)) throw new IllegalArgumentException("Unknown generator");
        UUID id = UUID.randomUUID(); Path directory = root.resolve(id.toString()); Files.createDirectories(directory);
        Properties meta = new Properties();
        meta.setProperty("format", "1"); meta.setProperty("name", name.strip()); meta.setProperty("seed", Long.toString(seed));
        meta.setProperty("generator", generator); meta.setProperty("creative", Boolean.toString(creative));
        try (var writer = Files.newBufferedWriter(directory.resolve("cuda-world.properties"), StandardCharsets.UTF_8, StandardOpenOption.CREATE_NEW)) {
            meta.store(writer, "CUDA Craft local world");
        }
        return new World(id, name.strip(), seed, generator, creative, directory);
    }
    public World load(UUID id) throws IOException {
        Path directory = root.resolve(id.toString()); Path file = directory.resolve("cuda-world.properties");
        if (Files.isSymbolicLink(directory) || Files.size(file) > 16384) throw new IOException("Invalid CUDA world metadata");
        Properties meta = new Properties(); try (var reader = Files.newBufferedReader(file, StandardCharsets.UTF_8)) { meta.load(reader); }
        try {
            String name = meta.getProperty("name"), generator = meta.getProperty("generator");
            if (!"1".equals(meta.getProperty("format")) || name == null || name.isBlank() || name.length() > 48
                || name.chars().anyMatch(c -> c < 32) || !Set.of("density", "flat").contains(generator)) throw new IllegalArgumentException();
            String mode = meta.getProperty("creative"); if (!Set.of("true", "false").contains(mode)) throw new IllegalArgumentException();
            return new World(id, name, Long.parseLong(meta.getProperty("seed")), generator, Boolean.parseBoolean(mode), directory);
        } catch (RuntimeException e) { throw new IOException("Invalid CUDA world metadata: " + id, e); }
    }
    public List<World> list() throws IOException {
        Files.createDirectories(root); List<World> worlds = new ArrayList<>();
        try (var paths = Files.list(root)) {
            for (Path path : paths.limit(1024).toList()) {
                if (!Files.isDirectory(path) || Files.isSymbolicLink(path)) continue;
                try { worlds.add(load(UUID.fromString(path.getFileName().toString()))); }
                catch (IllegalArgumentException | IOException ignored) { /* Unrelated or damaged directories are not launched. */ }
            }
        }
        worlds.sort(Comparator.comparing(World::name, String.CASE_INSENSITIVE_ORDER).thenComparing(w -> w.id().toString()));
        return List.copyOf(worlds);
    }
}
