package dev.cudacraft.client;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import java.net.InetSocketAddress;
import java.nio.file.Path;
import java.nio.file.Files;
import java.nio.channels.FileChannel;
import java.nio.file.StandardOpenOption;
import java.util.concurrent.TimeUnit;
import java.security.GeneralSecurityException;
import java.util.HexFormat;
import static org.junit.jupiter.api.Assertions.*;

class LocalSessionTest {
    @TempDir Path directory;
    @Test void catalogsRoundTripAndRejectTraversalNames() throws Exception {
        var catalog = new WorldCatalog(directory);
        var world = catalog.create("Meine Welt ö", Long.MIN_VALUE, "density", true);
        assertEquals(world, catalog.load(world.id())); assertEquals(1, catalog.list().size());
        var named = catalog.create("../../outside", 1, "flat", false);
        assertEquals(catalog.root(), named.directory().getParent());
        assertThrows(IllegalArgumentException.class, () -> catalog.create("", 1, "flat", false));
        assertThrows(IllegalArgumentException.class, () -> catalog.create("x", 1, "unknown", false));
    }
    @Test void proofMatchesIndependentVectorAndIsBoundToOwnedEndpoint() throws Exception {
        byte[] key = new byte[32], nonce = new byte[32];
        for (int i=0;i<32;i++) { key[i]=(byte)i; nonce[i]=(byte)(31-i); }
        var proof = LocalProof.answer(key, nonce, new InetSocketAddress("127.0.0.1", 25566), 25566);
        var mac = javax.crypto.Mac.getInstance("HmacSHA256"); mac.init(new javax.crypto.spec.SecretKeySpec(key,"HmacSHA256"));
        assertArrayEquals(mac.doFinal(nonce), proof);
        assertThrows(GeneralSecurityException.class, () -> LocalProof.answer(key,nonce,new InetSocketAddress("127.0.0.1",25565),25566));
        assertThrows(GeneralSecurityException.class, () -> LocalProof.answer(key,nonce,new InetSocketAddress("192.0.2.1",25566),25566));
        assertThrows(GeneralSecurityException.class, () -> LocalProof.answer(key,new byte[31],new InetSocketAddress("127.0.0.1",25566),25566));
    }
    @Test void absentBackendFailsWithoutStartingAnything() throws Exception {
        var session = new BackendSession(); var world = new WorldCatalog(directory).create("Test",0,"flat",true);
        assertTrue(session.start(directory.resolve("missing.exe"),world,"Tester",java.util.UUID.randomUUID()).isCompletedExceptionally());
        assertFalse(session.active()); session.stop(); assertEquals(BackendSession.State.STOPPED,session.state());
    }
    private BackendSession fixture(String mode) throws Exception {
        String java = Path.of(System.getProperty("java.home"), "bin", "java.exe").toString();
        String classes = Path.of(BackendTestChild.class.getProtectionDomain().getCodeSource().getLocation().toURI()).toString();
        return new BackendSession(builder -> new ProcessBuilder(java, "-cp", classes, BackendTestChild.class.getName(), mode)
            .redirectErrorStream(true).start(), 500, 1000);
    }
    private void shutdownScenario(String mode, BackendSession.State expected, String message) throws Exception {
        var world = new WorldCatalog(directory).create("Save test", 0, "flat", true);
        var executable = Files.createFile(directory.resolve("fixture.exe"));
        var session = fixture(mode);
        try {
            session.start(executable, world, "Tester", java.util.UUID.randomUUID()).get(10, TimeUnit.SECONDS);
            var stop = session.stopAsync(); assertSame(stop, session.stopAsync());
            session.stop(); // Synchronous application shutdown must also wait for an in-flight stop.
            assertTrue(stop.isDone()); assertEquals(expected, session.state());
            assertTrue(session.status().contains(message), session.status());
            try (var channel = FileChannel.open(world.directory().resolve("frontend.lock"), StandardOpenOption.WRITE);
                 var lock = channel.tryLock()) { assertNotNull(lock); }
        } finally { session.stop(); }
    }
    @Test void successfulShutdownConfirmsSave() throws Exception { shutdownScenario("normal", BackendSession.State.STOPPED, "saved"); }
    @Test void failedSaveCannotBeReportedAsSaved() throws Exception { shutdownScenario("save-failure", BackendSession.State.FAILED, "saving may have failed"); }
    @Test void forcedShutdownCannotBeReportedAsSaved() throws Exception { shutdownScenario("hang", BackendSession.State.FAILED, "forcibly stopped"); }
    @Test void startupErrorSurvivesCleanup() throws Exception {
        var world = new WorldCatalog(directory).create("Fail test", 0, "flat", true);
        var session = fixture("startup-failure");
        var executable = Files.createFile(directory.resolve("fixture.exe"));
        assertThrows(java.util.concurrent.ExecutionException.class,
            () -> session.start(executable, world, "Tester", java.util.UUID.randomUUID()).get(10, TimeUnit.SECONDS));
        session.stop(); assertEquals(BackendSession.State.FAILED, session.state());
        assertTrue(session.status().contains("exited (7)"), session.status());
    }
}
