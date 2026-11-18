package dev.cudacraft.client;

import java.io.BufferedReader;
import java.io.InputStreamReader;

/** Real child process exercising EOF, exit codes and forced shutdown without Minecraft. */
public final class BackendTestChild {
    public static void main(String[] args) throws Exception {
        if (args[0].equals("startup-failure")) { System.out.println("Fixture startup failed"); System.exit(7); }
        System.out.println("LISTENING 127.0.0.1:25566 | fixture"); System.out.flush();
        try (var input = new BufferedReader(new InputStreamReader(System.in))) {
            input.readLine();
        }
        if (args[0].equals("hang")) Thread.sleep(60000);
        if (args[0].equals("save-failure")) System.exit(9);
    }
}
