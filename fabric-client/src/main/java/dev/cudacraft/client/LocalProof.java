package dev.cudacraft.client;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import java.net.InetSocketAddress;
import java.net.SocketAddress;
import java.security.GeneralSecurityException;

public final class LocalProof {
    private LocalProof() {}
    public static byte[] answer(byte[] secret, byte[] nonce, SocketAddress remote, int ownedPort) throws GeneralSecurityException {
        if (secret.length != 32 || nonce.length != 32 || ownedPort < 1 || !(remote instanceof InetSocketAddress peer)
            || peer.getAddress() == null || !peer.getAddress().getHostAddress().equals("127.0.0.1") || peer.getPort() != ownedPort)
            throw new GeneralSecurityException("Query did not come from the owned local backend");
        Mac mac = Mac.getInstance("HmacSHA256"); mac.init(new SecretKeySpec(secret, "HmacSHA256")); return mac.doFinal(nonce);
    }
}
