package dev.cudacraft.client;

import java.io.*;
import java.net.SocketAddress;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.GeneralSecurityException;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.HexFormat;
import java.util.UUID;
import java.util.concurrent.*;
import java.util.regex.Pattern;

/** Owns exactly one native child, its stdin control pipe, proof key and world lock. */
public final class BackendSession {
    public enum State { STOPPED, STARTING, READY, CONNECTED, STOPPING, FAILED }
    private static final Pattern LISTENING = Pattern.compile("^LISTENING 127\\.0\\.0\\.1:(\\d+) \\|.*");
    private volatile State state = State.STOPPED;
    private volatile String status = "";
    private volatile Process process;
    private volatile int port;
    private byte[] secret;
    private BufferedWriter input;
    private FileChannel lockChannel;
    private FileLock lock;
    private CompletableFuture<Integer> ready;
    private boolean paused;
    private long generation;
    public State state() { return state; }
    public String status() { return status; }
    public boolean active() { return state == State.STARTING || state == State.READY || state == State.CONNECTED || state == State.STOPPING; }
    public synchronized CompletableFuture<Integer> start(Path executable, WorldCatalog.World world, String name, UUID uuid) {
        if (active() || (process != null && process.isAlive())) return CompletableFuture.failedFuture(new IllegalStateException("A local world is already running or saving"));
        if (!Files.isRegularFile(executable) || !executable.getFileName().toString().endsWith(".exe"))
            return CompletableFuture.failedFuture(new IOException("Native server not found: " + executable));
        if (!name.matches("[A-Za-z0-9_]{1,16}")) return CompletableFuture.failedFuture(new IOException("Invalid Minecraft profile name"));
        process = null; releaseLock();
        if (secret != null) Arrays.fill(secret, (byte)0);
        state = State.STARTING; status = "Starting local server..."; paused = false; port = 0;
        secret = new byte[32]; new SecureRandom().nextBytes(secret); ready = new CompletableFuture<>();
        final long run = ++generation; final var result = ready;
        Thread.ofPlatform().daemon().name("CUDA Craft backend").start(() -> {
            Process owned = null;
            try {
                synchronized (this) {
                    if (generation != run || state != State.STARTING) throw new IOException("Launch cancelled");
                    lockChannel = FileChannel.open(world.directory().resolve("frontend.lock"), StandardOpenOption.CREATE, StandardOpenOption.WRITE);
                    lock = lockChannel.tryLock(); if (lock == null) throw new IOException("This world is already open in another client");
                    ProcessBuilder builder = new ProcessBuilder(executable.toAbsolutePath().toString(), "--local-session", "--bind", "127.0.0.1", "--port", "0",
                        "--world", world.directory().toAbsolutePath().toString(), "--generator", world.generator(), "--seed", Long.toString(world.seed()),
                        "--gamemode", world.creative() ? "creative" : "survival");
                    builder.directory(executable.toAbsolutePath().getParent().toFile()); builder.redirectErrorStream(true);
                    builder.environment().put("CUDACRAFT_LOCAL_TOKEN", HexFormat.of().formatHex(secret));
                    builder.environment().put("CUDACRAFT_LOCAL_NAME", name);
                    builder.environment().put("CUDACRAFT_LOCAL_UUID", uuid.toString().replace("-", ""));
                    process = builder.start(); owned = process; input = new BufferedWriter(new OutputStreamWriter(process.getOutputStream(), StandardCharsets.UTF_8));
                }
                Path log = world.directory().resolve("native-server.log");
                if (Files.exists(log)) Files.move(log, world.directory().resolve("native-server.previous.log"), StandardCopyOption.REPLACE_EXISTING);
                try (var reader = new BufferedReader(new InputStreamReader(owned.getInputStream(), StandardCharsets.UTF_8));
                     var writer = Files.newBufferedWriter(log, StandardCharsets.UTF_8, StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING)) {
                    String line; long written = 0;
                    while ((line = reader.readLine()) != null) {
                        if (written < 8 * 1024 * 1024) { writer.write(line); writer.newLine(); writer.flush(); written += line.length() + 1; }
                        var match = LISTENING.matcher(line);
                        synchronized (this) {
                            if (generation == run && state == State.STARTING && match.matches()) {
                                port = Integer.parseInt(match.group(1));
                                if (port < 1 || port > 65535) throw new IOException("Invalid backend port");
                                state = State.READY; status = "Connecting..."; result.complete(port);
                            }
                        }
                    }
                }
                int code = owned.waitFor();
                synchronized (this) {
                    if (generation == run && state != State.STOPPING && state != State.STOPPED) {
                        state = State.FAILED; status = "Local server exited (" + code + "). See native-server.log.";
                        result.completeExceptionally(new IOException(status));
                    }
                }
            } catch (Exception e) {
                synchronized (this) { if (generation == run && state != State.STOPPING && state != State.STOPPED) { state = State.FAILED; status = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage(); } }
                result.completeExceptionally(e);
                synchronized (this) { if (generation == run && owned != null && owned.isAlive()) stopAsync(); }
            } finally {
                synchronized (this) { if (generation == run && (process == null || !process.isAlive())) releaseLock(); }
            }
        });
        result.orTimeout(60, TimeUnit.SECONDS).exceptionally(error -> { synchronized (this) { if (generation == run) stopAsync(); } return null; });
        return result;
    }
    public synchronized byte[] proof(byte[] nonce, SocketAddress remote) throws GeneralSecurityException {
        if (state != State.READY || secret == null) throw new GeneralSecurityException("No pending private connection");
        return LocalProof.answer(secret, nonce, remote, port);
    }
    public synchronized void connected() { if (state == State.READY) { state = State.CONNECTED; status = "Local world running"; } }
    public synchronized void pause(boolean value) {
        if (state == State.CONNECTED && paused != value) { paused = value; command("pause " + (value ? "on" : "off")); }
    }
    private synchronized void command(String text) {
        if (input == null) return;
        try { input.write(text); input.newLine(); input.flush(); } catch (IOException ignored) { /* Exit is handled by the reader. */ }
    }
    public synchronized CompletableFuture<Void> stopAsync() { final long run = generation; return CompletableFuture.runAsync(() -> stop(run)); }
    public void stop() {
        long run; synchronized (this) { run = generation; } stop(run);
    }
    private void stop(long expectedGeneration) {
        Process child; long run;
        synchronized (this) {
            if (generation != expectedGeneration || state == State.STOPPED || state == State.STOPPING) return;
            run = generation; state = State.STOPPING; status = "Saving local world..."; child = process;
            if (ready != null && !ready.isDone()) ready.completeExceptionally(new IOException("Launch cancelled"));
            command("stop");
            try { if (input != null) input.close(); } catch (IOException ignored) { }
            input = null;
        }
        if (child != null) {
            try { if (!child.waitFor(8, TimeUnit.SECONDS)) { child.destroy(); if (!child.waitFor(2, TimeUnit.SECONDS)) child.destroyForcibly().waitFor(); } }
            catch (InterruptedException e) { Thread.currentThread().interrupt(); child.destroy(); }
        }
        synchronized (this) {
            if (run == generation) {
                if (secret != null) Arrays.fill(secret, (byte)0); secret = null; process = null; port = 0;
                releaseLock(); state = State.STOPPED; status = "Local world saved";
            }
        }
    }
    private void releaseLock() {
        try { if (lock != null && lock.isValid()) lock.release(); } catch (IOException ignored) { }
        try { if (lockChannel != null) lockChannel.close(); } catch (IOException ignored) { }
        lock = null; lockChannel = null;
    }
}
