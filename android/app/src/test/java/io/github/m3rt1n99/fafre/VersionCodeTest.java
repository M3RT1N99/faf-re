package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

import org.junit.Test;

/**
 * The app's version code (app/build.gradle.kts): -PfafVersionCode when the build is given it, which the test task passes on
 * ("" for none), else git's count of the branch's commits, as before.
 */
public class VersionCodeTest {
    @Test
    public void theVersionCodeIsTheGivenOneElseGitsCommitCount() throws IOException, InterruptedException {
        String given = System.getProperty("fafVersionCode", "");
        int expected = given.isEmpty() ? gitCommitCount() : Integer.parseInt(given);
        assertEquals(expected, BuildConfig.VERSION_CODE);
    }

    private static int gitCommitCount() throws IOException, InterruptedException {
        Process git = new ProcessBuilder("git", "rev-list", "--count", "HEAD").redirectErrorStream(true).start();
        String output;
        try (InputStream in = git.getInputStream()) {
            output = new String(in.readAllBytes(), StandardCharsets.UTF_8).trim();
        }
        assertEquals(output, 0, git.waitFor());
        return Integer.parseInt(output);
    }
}
