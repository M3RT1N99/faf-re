package io.github.m3rt1n99.fafre;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.TreeSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * FAF's newest game version, the one a PC on FAF plays: the number of GitHub's latest release of FAForever/fa, as FAF's
 * updater has its files (the init file there, where the game files come from). A release that is tagged but not
 * deployed yet gives way to the next lower tag that is. GitHub answers 60 requests an hour from one address without a
 * key; a check makes one, or two with the tags. No Android in here, for the tests.
 */
final class FafRelease {
    static final String LATEST_URL = "https://api.github.com/repos/FAForever/fa/releases/latest";
    /** The first page: GitHub lists the highest numbers there, after a few named tags. */
    static final String TAGS_URL = "https://api.github.com/repos/FAForever/fa/tags?per_page=100";
    /** Lower tags tried after the release's own: a deploy lags its tag by a version or two. */
    static final int LOWER_TRIES = 5;

    /** A game version's tag is a number and nothing else: "3840", not "v1.5.3634" or "patch_3608". */
    private static final Pattern VERSION = Pattern.compile("[0-9]{1,9}");

    /** A check that found no version; its message says why, in words for the card. */
    static final class Failed extends IOException {
        Failed(String message) {
            super(message);
        }

        Failed(String message, IOException cause) {
            super(message, cause);
        }
    }

    private FafRelease() {
    }

    /** FAF's newest game version, or a Failed that says why there is none; the user's Cancel stays a Cancelled. */
    static int newest(Http.Requests requests) throws IOException {
        List<String> tag = strings(github(requests, LATEST_URL), "tag_name");
        if (tag.isEmpty()) {
            throw new Failed("GitHub's answer names no latest release of FAForever/fa");
        }
        if (!VERSION.matcher(tag.get(0)).matches()) {
            throw new Failed("GitHub's latest release of FAForever/fa is " + tag.get(0) + ", not a game version");
        }
        int latest = Integer.parseInt(tag.get(0));
        if (deployed(requests, latest)) {
            return latest;
        }
        TreeSet<Integer> lower = new TreeSet<>(Collections.reverseOrder());
        for (String name : strings(github(requests, TAGS_URL), "name")) {
            if (VERSION.matcher(name).matches() && Integer.parseInt(name) < latest) {
                lower.add(Integer.parseInt(name));
            }
        }
        List<Integer> tried = new ArrayList<>(lower).subList(0, Math.min(LOWER_TRIES, lower.size()));
        for (int version : tried) {
            if (deployed(requests, version)) {
                return version;
            }
        }
        throw new Failed("FAF's updater has the files of neither GitHub's latest release " + latest
            + (tried.isEmpty() ? "" : " nor the tags below it down to " + tried.get(tried.size() - 1)));
    }

    /**
     * The plain string values of {@code key} in one of GitHub's answers. Only a key is followed by a colon, and a quote
     * inside a string is escaped, so the pattern finds keys (GitHub's are fixed words), never text in a value such as
     * a release's notes. A value with an escape in it is no game version, and is left out.
     */
    static List<String> strings(String json, String key) {
        Matcher value = Pattern.compile("\"" + Pattern.quote(key) + "\"\\s*:\\s*\"([^\"\\\\]*)\"").matcher(json);
        List<String> values = new ArrayList<>();
        while (value.find()) {
            values.add(value.group(1));
        }
        return values;
    }

    private static String github(Http.Requests requests, String url) throws IOException {
        try {
            return requests.text(url);
        } catch (Cancellation.Cancelled e) {
            throw e;
        } catch (Http.StatusException e) {
            if (e.status == 403 || e.status == 429) {
                throw new Failed("GitHub turned the check down (HTTP " + e.status + "): its 60 requests an hour for one "
                    + "address are likely used up, and phones on mobile data often share an address; try again later", e);
            }
            throw new Failed("GitHub answered the check with HTTP " + e.status + "; try again later", e);
        } catch (IOException e) {
            throw new Failed("GitHub is not reachable (" + reason(e) + "); is the phone online?", e);
        }
    }

    /** Whether FAF's updater has the version's files: its init file, which a download fetches last of all. */
    private static boolean deployed(Http.Requests requests, int version) throws IOException {
        String init = "init_faf_" + version + ".lua";
        try {
            requests.length(Http.GAME_URL + init);
            return true;
        } catch (Cancellation.Cancelled e) {
            throw e;
        } catch (Http.StatusException e) {
            if (e.status == 404) {
                return false;
            }
            throw new Failed("FAF's updater answered HTTP " + e.status + " for " + init + "; try again later", e);
        } catch (IOException e) {
            throw new Failed("FAF's updater is not reachable (" + reason(e) + "); is the phone online?", e);
        }
    }

    private static String reason(IOException e) {
        return e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
    }
}
