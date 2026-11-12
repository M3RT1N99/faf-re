package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeTrue;

import java.io.File;
import java.io.IOException;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/**
 * The check against the real GitHub and FAF's updater, from a PC: four requests to GitHub's 60 an hour, so not in the
 * normal run. In android: FAFRE_ONLINE=1 ./gradlew :app:testReleaseUnitTest --tests '*FafReleaseOnlineTest' --rerun;
 * the lines it prints are in the test's XML result.
 */
public class FafReleaseOnlineTest {
    @Rule
    public final TemporaryFolder folder = new TemporaryFolder();

    @Before
    public void online() {
        assumeTrue("FAFRE_ONLINE=1 runs it", "1".equals(System.getenv("FAFRE_ONLINE")));
    }

    @Test
    public void fafsNewestVersionAndItsSize() throws IOException {
        int newest = FafRelease.newest(Http.ONE_TRY);
        long bytes = GameFiles.toDownload(new File(folder.getRoot(), String.valueOf(newest)), folder.newFolder("downloads"), newest,
            Http.ONE_TRY);
        System.out.println("FAF's newest game version: " + newest + ", " + bytes + " bytes (" + NewestGameFiles.size(bytes)
            + ") to download");
        assertTrue(bytes > 100_000_000L);
    }

    /** With the latest release taken as not deployed yet, GitHub's real tags and FAF's real updater choose. */
    @Test
    public void theNextLowerDeployedTag() throws IOException {
        int newest = FafRelease.newest(Http.ONE_TRY);
        String init = Http.GAME_URL + "init_faf_" + newest + ".lua";
        int lower = FafRelease.newest(new Http.Requests() {
            @Override
            public String text(String url) throws IOException {
                return Http.ONE_TRY.text(url);
            }

            @Override
            public long length(String url) throws IOException {
                if (url.equals(init)) {
                    throw new Http.StatusException(url, 404);
                }
                return Http.ONE_TRY.length(url);
            }
        });
        System.out.println("With " + newest + " not deployed: " + lower);
        assertTrue(lower < newest);
    }
}
