package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

import org.junit.Test;
import org.junit.function.ThrowingRunnable;

/**
 * A launcher's start from another app (LaunchGameActivity): the whitelist of its switches, case by case, the extras and their
 * types, what this side adds for the rejoin, and the patterns of MAP and MODS. Each failure's code and line is what the
 * start's result gives the client.
 */
public class LaunchTest {
    private static final File KEY = new File("/data/user/0/io.github.m3rt1n99.fafre/no_backup/faf_rejoin_seat.key");

    /** Every switch of the whitelist once, with a value as the FAF client gives it. */
    private static final String[] EVERY_SWITCH = {"/gpgnet", "127.0.0.1:7237", "/savereplay",
        "gpgnet://127.0.0.1:41011/26930624/player.SCFAreplay", "/replay", "gpgnet://127.0.0.1:15000/26930624/live.SCFAreplay",
        "/nativenet", "/nativenetrelay", "/mean", "1523.75", "/deviation", "87.5", "/numgames", "3", "/team", "2", "/players", "4",
        "/startspot", "1", "/country", "DE", "/clan", "AB-1_"};

    private static void assertFailure(GameStart.Code code, String message, ThrowingRunnable start) {
        GameStart.Failure failure = assertThrows(GameStart.Failure.class, start);
        assertEquals(message, code, failure.code);
        assertEquals(message, failure.getMessage());
    }

    /** The whitelist refuses {@code arguments}, for the reason {@code why}. */
    private static void assertRefused(String why, String... arguments) {
        GameStart.Failure failure = assertThrows(why, GameStart.Failure.class, () -> Launch.switches(arguments));
        assertEquals(why, GameStart.Code.ARGUMENTS, failure.code);
    }

    private static Map<String, Object> extras(Object... nameValue) {
        Map<String, Object> extras = new HashMap<>();
        for (int i = 0; i < nameValue.length; i += 2) {
            extras.put((String) nameValue[i], nameValue[i + 1]);
        }
        return extras;
    }

    private static Launch check(Map<String, Object> extras) throws GameStart.Failure {
        return Launch.check(extras, false, null, null, KEY);
    }

    // --- The whitelist -----------------------------------------------------------------------------

    @Test
    public void theWhitelistHasTheContractsSwitches() {
        assertEquals(Arrays.asList("/gpgnet", "/savereplay", "/replay", "/nativenet", "/nativenetrelay", "/mean", "/deviation",
            "/numgames", "/team", "/players", "/startspot", "/country", "/clan"), Launch.switches());
    }

    @Test
    public void everySwitchOfTheWhitelistWithItsValuePassesAsItIs() throws GameStart.Failure {
        assertEquals(Arrays.asList(EVERY_SWITCH), Launch.switches(EVERY_SWITCH.clone()));
        assertEquals(Arrays.asList("/replay", "gpgnet://127.0.0.1:15000/26930624/a.b-c_D.SCFAreplay", "/gpgnet", "localhost:65535"),
            Launch.switches(new String[] {"/replay", "gpgnet://127.0.0.1:15000/26930624/a.b-c_D.SCFAreplay", "/gpgnet",
                "localhost:65535"}));
        assertEquals(Collections.emptyList(), Launch.switches(null));
        assertEquals(Collections.emptyList(), Launch.switches(new String[0]));
    }

    @Test
    public void theAppsOwnSwitchesAndEveryOtherSwitchNeverComeFromOutside() {
        List<String> never = new ArrayList<>(GameStart.OWN_SWITCHES);
        never.addAll(Arrays.asList("/prefs", "/nobugreport", "/con", "/rejoinoffer", "/rejoinkeyfile", "/rejoin", "/hostgame",
            "/joingame", "/connectserver", "/map", "/load", "/windowed", "/debug", "/division", "/fafre_hud_scale"));
        for (String forbidden : never) {
            assertRefused(forbidden + " after allowed ones", "/gpgnet", "127.0.0.1:7237", forbidden, "x");
            assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has \"" + forbidden + "\" at index 0, which is no switch "
                + "a launcher may give", () -> Launch.switches(new String[] {forbidden}));
        }
    }

    @Test
    public void aSwitchInAnotherCaseIsNoneOfTheWhitelistsThoughTheEngineWouldFindIt() {
        for (String allowed : Launch.switches()) {
            String upper = allowed.toUpperCase(Locale.ROOT);
            assertRefused(upper, upper, "1");
        }
        assertRefused("/Init", "/Init", "x");
    }

    @Test
    public void aPathOrAWordWhereASwitchGoesFailsTheStart() {
        assertRefused("a path", "/sdcard/Download/evil.lua");
        assertRefused("a value without its switch", "3");
        assertRefused("a value after a flag", "/nativenet", "on");
        assertRefused("an empty argument", "");
        assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has \"/data/local/tmp/x\" at index 2, which is no switch a "
            + "launcher may give", () -> Launch.switches(new String[] {"/numgames", "3", "/data/local/tmp/x"}));
    }

    @Test
    public void eachSwitchGivenTwiceFailsTheStart() {
        for (int i = 0; i < EVERY_SWITCH.length; ++i) {
            String name = EVERY_SWITCH[i];
            if (!name.startsWith("/")) {
                continue;
            }
            boolean flag = i + 1 == EVERY_SWITCH.length || EVERY_SWITCH[i + 1].startsWith("/");
            List<String> twice = new ArrayList<>(Arrays.asList(EVERY_SWITCH));
            twice.add(name);
            if (!flag) {
                twice.add(EVERY_SWITCH[i + 1]);
            }
            assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has " + name + " twice",
                () -> Launch.switches(twice.toArray(new String[0])));
        }
    }

    @Test
    public void aSwitchWithoutItsValueFailsTheStart() {
        assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has /gpgnet without 127.0.0.1:<port> or localhost:<port>",
            () -> Launch.switches(new String[] {"/nativenet", "/gpgnet"}));
        assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has /numgames without a whole number from 0 to 9999",
            () -> Launch.switches(new String[] {"/numgames", null}));
        // The next switch is no value of this one.
        assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has /clan \"/nativenet\": it takes 1 to 8 letters, digits, _ or -",
            () -> Launch.switches(new String[] {"/clan", "/nativenet"}));
    }

    @Test
    public void gpgnetTakesALoopbackPortOnly() {
        for (String host : new String[] {"192.0.2.10:7237", "example.org:7237", "127.0.0.2:7237", "[::1]:7237", "0.0.0.0:7237",
            "127.0.0.1", "127.0.0.1:", "127.0.0.1:0", "127.0.0.1:65536", "127.0.0.1:99999", "127.0.0.1:123456",
            "127.0.0.1:7237 ", "LOCALHOST:7237", "127.0.0.1:-1", "127.0.0.1:+7237"}) {
            assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has /gpgnet \"" + host + "\": it takes 127.0.0.1:<port> or "
                + "localhost:<port>", () -> Launch.switches(new String[] {"/gpgnet", host}));
        }
    }

    @Test
    public void theReplaySwitchesTakeTheLoopbacksGpgnetReplaysOnlyWithAPlainName() {
        for (String uri : new String[] {"gpgnet://192.0.2.10:15000/26930624/a.SCFAreplay", "gpgnet://localhost:15000/1/a.SCFAreplay",
            "gpgnet://127.0.0.1:15000/x/a.SCFAreplay", "gpgnet://127.0.0.1:15000/1/a b.SCFAreplay",
            "gpgnet://127.0.0.1:15000/1/../a.SCFAreplay", "gpgnet://127.0.0.1:15000/1/a.scfareplay",
            "gpgnet://127.0.0.1:15000/1/.SCFAreplay", "gpgnet://127.0.0.1:0/1/a.SCFAreplay", "gpgnet://127.0.0.1:15000/1/"
                + "a234567890123456789012345678901234567890123456789012345678901234x.SCFAreplay",
            "gpgnet://127.0.0.1:15000/12345678901/a.SCFAreplay", "/sdcard/replay.SCFAreplay", "file:///sdcard/a.SCFAreplay",
            "http://127.0.0.1:15000/1/a.SCFAreplay"}) {
            for (String name : new String[] {"/savereplay", "/replay"}) {
                assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has " + name + " \"" + uri + "\": it takes "
                    + "gpgnet://127.0.0.1:<port>/<game id>/<name>.SCFAreplay", () -> Launch.switches(new String[] {name, uri}));
            }
        }
    }

    @Test
    public void theNumbersTakeWhatTheirSwitchesSay() throws GameStart.Failure {
        for (String decimal : new String[] {"1500", "1500.0", "-12.5", "0", "1523.4567890123456"}) {
            assertEquals(decimal, Launch.switches(new String[] {"/mean", decimal, "/deviation", decimal}).get(1));
        }
        for (String bad : new String[] {"1e5", "1,5", "abc", "", ".5", "5.", "+1", "NaN", "Infinity", "1 500"}) {
            assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has /mean \"" + bad + "\": it takes a decimal number",
                () -> Launch.switches(new String[] {"/mean", bad}));
        }
        for (String name : new String[] {"/numgames", "/team", "/players", "/startspot"}) {
            for (String count : new String[] {"0", "1", "9999"}) {
                assertEquals(count, Launch.switches(new String[] {name, count}).get(1));
            }
            for (String bad : new String[] {"10000", "-1", "1.0", "x", ""}) {
                assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has " + name + " \"" + bad + "\": it takes a whole "
                    + "number from 0 to 9999", () -> Launch.switches(new String[] {name, bad}));
            }
        }
    }

    @Test
    public void theCountryIsTwoCapitalsAndTheClanATagWithoutSpaces() throws GameStart.Failure {
        assertEquals("US", Launch.switches(new String[] {"/country", "US"}).get(1));
        for (String bad : new String[] {"de", "DEU", "D", "", "D1"}) {
            assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has /country \"" + bad + "\": it takes two capital letters",
                () -> Launch.switches(new String[] {"/country", bad}));
        }
        for (String clan : new String[] {"a", "ABCDEFGH", "a_b-9"}) {
            assertEquals(clan, Launch.switches(new String[] {"/clan", clan}).get(1));
        }
        for (String bad : new String[] {"ABCDEFGHI", "a b", "", "a/b", "ä"}) {
            assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has /clan \"" + bad + "\": it takes 1 to 8 letters, digits, "
                + "_ or -", () -> Launch.switches(new String[] {"/clan", bad}));
        }
    }

    @Test
    public void launcherArgumentsOfAnotherTypeOrWithANullFailTheStart() {
        assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has the type String, not String[]",
            () -> Launch.switches("/gpgnet 127.0.0.1:7237"));
        assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has the type ArrayList, not String[]",
            () -> Launch.switches(new ArrayList<>(Collections.singletonList("/nativenet"))));
        assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has null at index 1",
            () -> Launch.switches(new String[] {"/nativenet", null}));
    }

    @Test
    public void theCheckKeepsItsOwnCopyOfTheArguments() throws GameStart.Failure {
        String[] given = {"/numgames", "3"};
        List<String> checked = Launch.switches(given);
        given[1] = "/init";
        assertEquals(Arrays.asList("/numgames", "3"), checked);
    }

    // --- The extras --------------------------------------------------------------------------------

    @Test
    public void theExtrasAndTheActionHaveTheContractsNames() {
        assertEquals("io.github.m3rt1n99.fafre.action.PLAY", Launch.ACTION_PLAY);
        assertEquals(Arrays.asList("io.github.m3rt1n99.fafre.extra.GAME_VERSION", "io.github.m3rt1n99.fafre.extra.LAUNCHER_ARGUMENTS",
            "io.github.m3rt1n99.fafre.extra.FEATURED_MOD", "io.github.m3rt1n99.fafre.extra.REJOIN_OFFER",
            "io.github.m3rt1n99.fafre.extra.REJOIN_PEERS", "io.github.m3rt1n99.fafre.extra.REJOIN_SOURCE",
            "io.github.m3rt1n99.fafre.extra.MAP", "io.github.m3rt1n99.fafre.extra.MODS", "io.github.m3rt1n99.fafre.extra.MOD_FILES"),
            Launch.EXTRAS);
    }

    @Test
    public void aStartWithoutExtrasPlaysTheNewestVersionWithNothingAdded() throws GameStart.Failure {
        Launch launch = check(extras());
        assertNull(launch.version);
        assertArrayEquals(new String[0], launch.arguments);
        assertNull(launch.map);
        assertArrayEquals(new String[0], launch.mods);
        assertNull(launch.modFiles);
        assertFalse(launch.replay);
    }

    @Test
    public void aWholeStartGivesTheGameItsCheckedExtras() throws GameStart.Failure {
        Launch launch = check(extras(GameStart.EXTRA_GAME_VERSION, 3845, GameStart.EXTRA_LAUNCHER_ARGUMENTS, EVERY_SWITCH.clone(),
            Launch.EXTRA_FEATURED_MOD, "faf", Launch.EXTRA_MAP, "scmp_007", Launch.EXTRA_MODS,
            new String[] {"e7846e9b-23a4-4b95-ae3a-fb69b289a585"}, Launch.EXTRA_MOD_FILES, new String[] {"m28ai.v0247.zip"},
            // Another app's extras, which are not the contract's, go nowhere.
            "com.faforever.rustclient.TRACE", 7L));
        assertEquals(Integer.valueOf(3845), launch.version);
        assertArrayEquals(EVERY_SWITCH, launch.arguments);
        assertEquals("scmp_007", launch.map);
        assertArrayEquals(new String[] {"e7846e9b-23a4-4b95-ae3a-fb69b289a585"}, launch.mods);
        assertArrayEquals(new String[] {"m28ai.v0247.zip"}, launch.modFiles);
    }

    @Test
    public void anExtraOfTheContractsNameThatItHasNotFailsTheStart() {
        for (String name : new String[] {"io.github.m3rt1n99.fafre.extra.REJOIN_PEER", "io.github.m3rt1n99.fafre.extra.SETUP_GAME_DATA",
            "io.github.m3rt1n99.fafre.extra.game_version"}) {
            assertFailure(GameStart.Code.ARGUMENTS, "the start has the extra " + name + ", which the contract has not",
                () -> check(extras(name, true)));
        }
    }

    @Test
    public void extrasOfAnotherTypeFailTheStartWithArguments() {
        assertFailure(GameStart.Code.ARGUMENTS, "GAME_VERSION has the type String, not int",
            () -> check(extras(GameStart.EXTRA_GAME_VERSION, "3845")));
        assertFailure(GameStart.Code.ARGUMENTS, "GAME_VERSION has the type Long, not int",
            () -> check(extras(GameStart.EXTRA_GAME_VERSION, 3845L)));
        assertFailure(GameStart.Code.ARGUMENTS, "FEATURED_MOD has the type String[], not String",
            () -> check(extras(Launch.EXTRA_FEATURED_MOD, new String[] {"faf"})));
        assertFailure(GameStart.Code.ARGUMENTS, "REJOIN_OFFER has the type String, not boolean",
            () -> check(extras(Launch.EXTRA_REJOIN_OFFER, "true")));
        assertFailure(GameStart.Code.ARGUMENTS, "MODS has the type String, not String[]",
            () -> check(extras(Launch.EXTRA_MODS, "e7846e9b-23a4-4b95-ae3a-fb69b289a585")));
        assertFailure(GameStart.Code.ARGUMENTS, "MOD_FILES has the type String, not String[]",
            () -> check(extras(Launch.EXTRA_MODS, new String[] {"e7846e9b"}, Launch.EXTRA_MOD_FILES, "m28ai.v0247.zip")));
        assertFailure(GameStart.Code.ARGUMENTS, "MAP is 7 (Integer), not FAF's map folder name (1 to 64 of a-z, 0-9, _ . -, not . or ..)",
            () -> check(extras(Launch.EXTRA_MAP, 7)));
    }

    @Test
    public void aFeaturedModOtherThanFafHasNoGameFiles() throws GameStart.Failure {
        assertNull(check(extras(Launch.EXTRA_FEATURED_MOD, "faf")).version);
        for (String featured : new String[] {"fafbeta", "fafdevelop", "coop", "nomads", "FAF", ""}) {
            assertFailure(GameStart.Code.NO_GAME_FILES, "no game files of the featured mod \"" + featured + "\": the game has FAF's "
                + "own (faf) only", () -> check(extras(Launch.EXTRA_FEATURED_MOD, featured)));
        }
    }

    @Test
    public void aReplayIsAContentUriOfAnotherAppsProvider() throws GameStart.Failure {
        assertTrue(Launch.check(extras(), true, "content", "com.faforever.rustclient.replays", KEY).replay);
        assertFailure(GameStart.Code.REPLAY, "the replay: a launcher gives it as a content:// URI that it grants the game to read, "
            + "not file:", () -> Launch.check(extras(), true, "file", null, KEY));
        assertFailure(GameStart.Code.REPLAY, "the replay: a launcher gives it as a content:// URI that it grants the game to read, "
            + "not null:", () -> Launch.check(extras(), true, null, null, KEY));
        for (String own : new String[] {"io.github.m3rt1n99.fafre.results", "io.github.m3rt1n99.fafre.status",
            "0@io.github.m3rt1n99.fafre.results"}) {
            assertFailure(GameStart.Code.REPLAY, "the replay: it must come from the launcher's own provider, not " + own,
                () -> Launch.check(extras(), true, "content", own, KEY));
        }
    }

    @Test
    public void aStartPlaysOneReplayAndARejoinNone() {
        assertFailure(GameStart.Code.ARGUMENTS, "the start has a replay twice, REPLAY and /replay",
            () -> Launch.check(extras(GameStart.EXTRA_LAUNCHER_ARGUMENTS,
                new String[] {"/replay", "gpgnet://127.0.0.1:15000/1/a.SCFAreplay"}), true, "content", "com.example.replays", KEY));
        Object[] rejoin = {Launch.EXTRA_REJOIN_OFFER, true, Launch.EXTRA_REJOIN_PEERS, "1=0", Launch.EXTRA_REJOIN_SOURCE, 1};
        assertFailure(GameStart.Code.ARGUMENTS, "a start that takes a seat back (REJOIN_PEERS) plays no replay",
            () -> Launch.check(extras(rejoin), true, "content", "com.example.replays", KEY));
        Map<String, Object> live = extras(rejoin);
        live.put(GameStart.EXTRA_LAUNCHER_ARGUMENTS, new String[] {"/replay", "gpgnet://127.0.0.1:15000/1/a.SCFAreplay"});
        assertFailure(GameStart.Code.ARGUMENTS, "a start that takes a seat back (REJOIN_PEERS) plays no replay", () -> check(live));
    }

    // --- The rejoin --------------------------------------------------------------------------------

    @Test
    public void theOfferAddsItsSwitchesWithTheSeatKeyInTheAppsNoBackupFolder() throws GameStart.Failure {
        assertEquals(Arrays.asList("/rejoinoffer", "/rejoinkeyfile", KEY.getPath(), "/con", "net_PeerFeatures=1;net_RejoinAccept=1"),
            Launch.rejoin(true, null, null, KEY));
        assertEquals(Collections.emptyList(), Launch.rejoin(false, null, null, KEY));
        assertEquals(Collections.emptyList(), Launch.rejoin(null, null, null, KEY));
        assertEquals("faf_rejoin_seat.key", Launch.REJOIN_KEY);
        // After the launcher's switches.
        List<String> arguments = Arrays.asList(Launch.check(extras(GameStart.EXTRA_LAUNCHER_ARGUMENTS,
            new String[] {"/gpgnet", "127.0.0.1:7237"}, Launch.EXTRA_REJOIN_OFFER, true), false, null, null, KEY).arguments);
        assertEquals(Arrays.asList("/gpgnet", "127.0.0.1:7237", "/rejoinoffer", "/rejoinkeyfile", KEY.getPath(), "/con",
            "net_PeerFeatures=1;net_RejoinAccept=1"), arguments);
    }

    @Test
    public void aRejoinAddsItsPeersAndSourceAfterTheOffer() throws GameStart.Failure {
        assertEquals(Arrays.asList("/rejoinoffer", "/rejoinkeyfile", KEY.getPath(), "/con", "net_PeerFeatures=1;net_RejoinAccept=1",
            "/rejoin", "1234=0,5678=2", "1", "0"), Launch.rejoin(true, "1234=0,5678=2", 1, KEY));
        assertEquals("0", Launch.rejoin(true, "1=0", 0, KEY).get(7));
        assertEquals("255", Launch.rejoin(true, "1=0", 255, KEY).get(7));
        StringBuilder sixteen = new StringBuilder("1=1");
        for (int i = 2; i <= 16; ++i) {
            sixteen.append(',').append(1234567890).append('=').append(i);
        }
        assertEquals(sixteen.toString(), Launch.rejoin(true, sixteen.toString(), 3, KEY).get(6));
    }

    @Test
    public void aRejoinNeedsTheOfferOnTheSameStartAndBothOfItsExtras() {
        assertFailure(GameStart.Code.ARGUMENTS, "a rejoin needs REJOIN_OFFER on the same start: the returning game offers a return "
            + "as well", () -> Launch.rejoin(null, "1=0", 1, KEY));
        assertFailure(GameStart.Code.ARGUMENTS, "a rejoin needs REJOIN_OFFER on the same start: the returning game offers a return "
            + "as well", () -> Launch.rejoin(false, "1=0", 1, KEY));
        assertFailure(GameStart.Code.ARGUMENTS, "a rejoin needs both REJOIN_PEERS and REJOIN_SOURCE",
            () -> Launch.rejoin(true, "1=0", null, KEY));
        assertFailure(GameStart.Code.ARGUMENTS, "a rejoin needs both REJOIN_PEERS and REJOIN_SOURCE",
            () -> Launch.rejoin(true, null, 1, KEY));
    }

    @Test
    public void theRejoinsPeersAreTheOffersPairsAndItsSourceAnIntUpTo255() {
        StringBuilder seventeen = new StringBuilder("1=1");
        for (int i = 2; i <= 17; ++i) {
            seventeen.append(',').append(i).append('=').append(i);
        }
        for (String bad : new String[] {"", "1", "1=", "=1", "1=0,", "1=0;2=1", "1 = 0", "a=0", "1=1000", "12345678901=0",
            "1=0,2=1 ", seventeen.toString(), "1=0\n/init x"}) {
            assertFailure(GameStart.Code.ARGUMENTS, "REJOIN_PEERS is \"" + bad + "\", not the RejoinOffer's <player id>=<source>,... "
                + "(16 at most)", () -> Launch.rejoin(true, bad, 1, KEY));
        }
        assertFailure(GameStart.Code.ARGUMENTS, "REJOIN_PEERS is a String[], not the RejoinOffer's <player id>=<source>,... (16 at "
            + "most)", () -> Launch.rejoin(true, new String[] {"1=0"}, 1, KEY));
        Object[][] sources = {{256, "256 (Integer)"}, {-1, "-1 (Integer)"}, {1L, "1 (Long)"}, {"1", "\"1\""}, {true, "true (Boolean)"}};
        for (Object[] bad : sources) {
            assertFailure(GameStart.Code.ARGUMENTS, "REJOIN_SOURCE is " + bad[1] + ", not an int from 0 to 255",
                () -> Launch.rejoin(true, "1=0", bad[0], KEY));
        }
    }

    // --- MAP and MODS ------------------------------------------------------------------------------

    @Test
    public void aMapIsFafsFolderNameButNoneOfTheVaultsOwn() throws GameStart.Failure {
        for (String map : new String[] {"scmp_007", "desert_arena_-_faf_version.v0002", "a", "0", "a.b", "...a",
            "abcdefghijklmnopqrstuvwxyz0123456789_.-abcdefghijklmnopqrstuvwxy"}) {
            assertEquals(map, Launch.map(map));
        }
        assertNull(Launch.map(null));
        for (String bad : new String[] {".", "..", "...", "", "SCMP_007", "a/b", "../maps", "a b", "a\\b", "ä",
            "abcdefghijklmnopqrstuvwxyz0123456789_.-abcdefghijklmnopqrstuvwxyz"}) {
            assertFailure(GameStart.Code.ARGUMENTS, "MAP is \"" + bad + "\", not FAF's map folder name (1 to 64 of a-z, 0-9, _ . -, "
                + "not . or ..)", () -> Launch.map(bad));
        }
    }

    @Test
    public void modsAreUidsEachOnce() throws GameStart.Failure {
        // M28AI's uid, as FAF's content server has it in m28ai.v0247.zip, is no hex.
        String[] mods = {"e7846e9b-23a4-4b95-ae3a-fb69b289a585", "fnewm028-v247-55b4-92b6-64398e7ge43f",
            "FFFFFFFF-0000-1111-2222-333344445555", "12345678", "a_b.c-d.e",
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};
        assertArrayEquals(mods, Launch.mods(mods.clone()));
        assertArrayEquals(new String[0], Launch.mods(null));
        for (String bad : new String[] {"1234567", "../../e7846e9b", "e7846e9b/23a4", "e7846e9b\\23a4", "e7846e9b 23a4",
            ".e7846e9b", "........", "", "e7846e9b-23a4-ä", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0"}) {
            assertFailure(GameStart.Code.ARGUMENTS, "MODS has \"" + bad + "\", not a mod's uid (8 to 64 of letters, digits, _ . -, "
                + "not first a .)", () -> Launch.mods(new String[] {bad}));
        }
        assertFailure(GameStart.Code.ARGUMENTS, "MODS has null, not a mod's uid (8 to 64 of letters, digits, _ . -, not first a .)",
            () -> Launch.mods(new String[] {null}));
        assertFailure(GameStart.Code.ARGUMENTS, "MODS has E7846E9B-23A4 twice",
            () -> Launch.mods(new String[] {"e7846e9b-23a4", "E7846E9B-23A4"}));
    }

    @Test
    public void modFilesNameOneFileOfFafsModsForEachMod() throws GameStart.Failure {
        String[] mods = {"e7846e9b-23a4-4b95-ae3a-fb69b289a585", "12345678"};
        String[] files = {"m28ai.v0247.zip", "BlackOps (Unleashed) & more!.v0012.zip"};
        assertArrayEquals(files, Launch.modFiles(files.clone(), mods));
        assertNull(Launch.modFiles(null, mods));
        assertFailure(GameStart.Code.ARGUMENTS, "MOD_FILES has 1 files for 2 MODS",
            () -> Launch.modFiles(new String[] {"m28ai.v0247.zip"}, mods));
        for (String bad : new String[] {"m28ai.v0247", "m28ai.v0247.ZIP", ".zip", "../m28ai.zip", "mods/m28ai.zip", "a\\b.zip",
            ".hidden.zip", "a\nb.zip", ""}) {
            assertFailure(GameStart.Code.ARGUMENTS, "MOD_FILES has \"" + bad + "\", not a file of FAF's mods (<name>.zip, no folder)",
                () -> Launch.modFiles(new String[] {bad}, new String[] {"12345678"}));
        }
    }
}
