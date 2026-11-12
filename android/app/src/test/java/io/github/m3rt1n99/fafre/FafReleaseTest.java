package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.IOException;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import org.junit.Test;

/** FAF's newest game version, from GitHub's answers and FAF's updater as each test sets them up. */
public class FafReleaseTest {
    private final Network mNetwork = new Network();

    /** GitHub and FAF's updater: the answers, and what was asked of them. */
    private static final class Network implements Http.Requests {
        String release;
        String tags;
        IOException gitHubFails;
        IOException updaterFails;
        final Set<Integer> deployed = new HashSet<>();
        final List<String> asked = new ArrayList<>();

        @Override
        public String text(String url) throws IOException {
            asked.add(url);
            if (gitHubFails != null) {
                throw gitHubFails;
            }
            if (url.equals(FafRelease.LATEST_URL)) {
                return release;
            }
            if (url.equals(FafRelease.TAGS_URL)) {
                return tags;
            }
            throw new Http.StatusException(url, 404);
        }

        @Override
        public long length(String url) throws IOException {
            asked.add(url);
            if (updaterFails != null) {
                throw updaterFails;
            }
            for (int version : deployed) {
                if (url.equals(init(version))) {
                    return 26887;
                }
            }
            throw new Http.StatusException(url, 404);
        }
    }

    private static String init(int version) {
        return Http.GAME_URL + "init_faf_" + version + ".lua";
    }

    /** GitHub's latest release, with notes ahead of its tag that quote another release's answer. */
    private static String release(String tag) {
        return "{\"url\":\"https://api.github.com/repos/FAForever/fa/releases/1\","
            + "\"body\":\"Unlike {\\\"tag_name\\\": \\\"9999\\\"} in 9999's notes\","
            + "\"tag_name\":\"" + tag + "\",\"name\":\"Game version " + tag + "\",\"author\":{\"login\":\"someone\"},"
            + "\"assets\":[{\"name\":\"notes.zip\"}]}";
    }

    /** GitHub's tags as its first page has them. */
    private static String tags(String... names) {
        StringBuilder out = new StringBuilder("[");
        for (String name : names) {
            out.append(out.length() > 1 ? "," : "").append("{\"name\":\"").append(name)
                .append("\",\"zipball_url\":\"https://api.github.com/repos/FAForever/fa/zipball/refs/tags/").append(name)
                .append("\",\"commit\":{\"sha\":\"ed05\",\"url\":\"https://api.github.com/repos/FAForever/fa/commits/ed05\"},")
                .append("\"node_id\":\"MDM6UmVm\"}");
        }
        return out.append(']').toString();
    }

    @Test
    public void theLatestReleaseWhenFafsUpdaterHasIt() throws IOException {
        mNetwork.release = release("3840");
        mNetwork.deployed.addAll(Arrays.asList(3840, 3839, 9999));

        assertEquals(3840, FafRelease.newest(mNetwork));
        assertEquals("one request to GitHub, one to FAF", Arrays.asList(FafRelease.LATEST_URL, init(3840)), mNetwork.asked);
    }

    @Test
    public void aReleaseNotDeployedYetGivesWayToTheNextLowerTagThatIs() throws IOException {
        mNetwork.release = release("3842");
        mNetwork.tags = tags("3843", "3842", "3841", "3840", "3839");
        mNetwork.deployed.addAll(Arrays.asList(3843, 3840, 3839));

        assertEquals(3840, FafRelease.newest(mNetwork));
        assertEquals(Arrays.asList(FafRelease.LATEST_URL, init(3842), FafRelease.TAGS_URL, init(3841), init(3840)), mNetwork.asked);
    }

    @Test
    public void theLowerTagsGoByNumberNotByGitHubsOrder() throws IOException {
        mNetwork.release = release("3842");
        mNetwork.tags = tags("3838", "3842", "3840", "3839", "3841");
        mNetwork.deployed.addAll(Arrays.asList(3840, 3839, 3838));

        assertEquals(3840, FafRelease.newest(mNetwork));
    }

    /** Named tags come first on GitHub's page; one that holds a number is no version either. */
    @Test
    public void namedTagsAreNoVersions() throws IOException {
        mNetwork.release = release("3841");
        mNetwork.tags = tags("v1.5.3634", "support", "patch_3608", "Patch_3626", "+", "3840-beta", "3841", "3839");
        mNetwork.deployed.addAll(Arrays.asList(3840, 3839, 153634, 3608));

        assertEquals(3839, FafRelease.newest(mNetwork));
    }

    @Test
    public void aLatestReleaseWithANamedTagIsAClearError() {
        mNetwork.release = release("deploy/fafbeta");

        FafRelease.Failed failed = assertThrows(FafRelease.Failed.class, () -> FafRelease.newest(mNetwork));
        assertEquals("GitHub's latest release of FAForever/fa is deploy/fafbeta, not a game version", failed.getMessage());
        assertEquals("nothing asked of FAF", Collections.singletonList(FafRelease.LATEST_URL), mNetwork.asked);
    }

    @Test
    public void releaseNotesCannotPassForTheTag() {
        assertEquals(Collections.singletonList("3840"), FafRelease.strings(release("3840"), "tag_name"));
        assertEquals(Arrays.asList("3841", "v1.5.3634"), FafRelease.strings(tags("3841", "v1.5.3634"), "name"));
    }

    @Test
    public void gitHubsRateLimitIsAClearErrorAndNothingIsDownloaded() {
        for (int status : new int[] {403, 429}) {
            mNetwork.asked.clear();
            mNetwork.gitHubFails = new Http.StatusException(FafRelease.LATEST_URL, status);

            FafRelease.Failed failed = assertThrows(FafRelease.Failed.class, () -> FafRelease.newest(mNetwork));
            assertEquals("GitHub turned the check down (HTTP " + status + "): its 60 requests an hour for one address are "
                + "likely used up, and phones on mobile data often share an address; try again later", failed.getMessage());
            assertEquals("nothing asked of FAF", Collections.singletonList(FafRelease.LATEST_URL), mNetwork.asked);
        }
    }

    @Test
    public void gitHubUnreachableIsAClearErrorAndNothingIsDownloaded() {
        mNetwork.gitHubFails = new UnknownHostException("Unable to resolve host \"api.github.com\"");

        FafRelease.Failed failed = assertThrows(FafRelease.Failed.class, () -> FafRelease.newest(mNetwork));
        assertEquals("GitHub is not reachable (Unable to resolve host \"api.github.com\"); is the phone online?", failed.getMessage());
        assertEquals(Collections.singletonList(FafRelease.LATEST_URL), mNetwork.asked);
    }

    @Test
    public void gitHubsOtherAnswersAreClearErrors() {
        mNetwork.gitHubFails = new Http.StatusException(FafRelease.LATEST_URL, 502);

        FafRelease.Failed failed = assertThrows(FafRelease.Failed.class, () -> FafRelease.newest(mNetwork));
        assertEquals("GitHub answered the check with HTTP 502; try again later", failed.getMessage());
    }

    @Test
    public void fafsUpdaterUnreachableIsAClearError() {
        mNetwork.release = release("3840");
        mNetwork.updaterFails = new UnknownHostException("content.faforever.com");

        FafRelease.Failed failed = assertThrows(FafRelease.Failed.class, () -> FafRelease.newest(mNetwork));
        assertEquals("FAF's updater is not reachable (content.faforever.com); is the phone online?", failed.getMessage());
    }

    @Test
    public void noDeployedVersionAmongTheNewestTagsIsAClearError() {
        mNetwork.release = release("3846");
        mNetwork.tags = tags("3846", "3845", "3844", "3843", "3842", "3841", "3840");
        mNetwork.deployed.add(3840);

        FafRelease.Failed failed = assertThrows(FafRelease.Failed.class, () -> FafRelease.newest(mNetwork));
        assertEquals("FAF's updater has the files of neither GitHub's latest release 3846 nor the tags below it down to 3841",
            failed.getMessage());
        assertTrue("not further than the lower tries", !mNetwork.asked.contains(init(3840)));
    }

    @Test
    public void aCancelStaysACancel() {
        mNetwork.gitHubFails = new Cancellation.Cancelled();

        assertThrows(Cancellation.Cancelled.class, () -> FafRelease.newest(mNetwork));
    }
}
