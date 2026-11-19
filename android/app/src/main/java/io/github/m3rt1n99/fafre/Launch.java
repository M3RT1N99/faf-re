package io.github.m3rt1n99.fafre;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * A launcher's start from another app, the FAF client: the extras of LaunchGameActivity's contract (android/README.md), their
 * checks, and the start GameActivity gets for it. The launcher's switches pass a whitelist: what a start from outside may
 * set, each value checked, and nothing of the app's own switches, of a path or of a host off the phone. What only this side
 * may give the program, the rejoin's key file and console variables, it adds itself. No Android in here, for the tests.
 */
final class Launch {
    static final String ACTION_PLAY = "io.github.m3rt1n99.fafre.action.PLAY";
    /** The contract's extras have this prefix; a start with another of it fails, as its launcher counts on what it does. */
    static final String EXTRA_PREFIX = "io.github.m3rt1n99.fafre.extra.";
    /** A String, "faf" when none: the featured mod whose game files the game plays. */
    static final String EXTRA_FEATURED_MOD = EXTRA_PREFIX + "FEATURED_MOD";
    /** A boolean: the game offers its return to the match after a crash (RejoinOffer over GPGNet). */
    static final String EXTRA_REJOIN_OFFER = EXTRA_PREFIX + "REJOIN_OFFER";
    /** A String, "id=source,...": the RejoinOffer's players but this one; with REJOIN_SOURCE the start takes a seat back. */
    static final String EXTRA_REJOIN_PEERS = EXTRA_PREFIX + "REJOIN_PEERS";
    /** An int, 0 to 255: this player's command source, the RejoinOffer's first field. */
    static final String EXTRA_REJOIN_SOURCE = EXTRA_PREFIX + "REJOIN_SOURCE";
    /** A String: FAF's map folder name, which the game's vault gets before the program starts. */
    static final String EXTRA_MAP = EXTRA_PREFIX + "MAP";
    /** A String[] of mod uids, which the game's vault gets before the program starts. */
    static final String EXTRA_MODS = EXTRA_PREFIX + "MODS";
    /** A String[], one for each of MODS in its order: the file of that mod version in content.faforever.com/mods/. */
    static final String EXTRA_MOD_FILES = EXTRA_PREFIX + "MOD_FILES";

    static final List<String> EXTRAS = Collections.unmodifiableList(Arrays.asList(GameStart.EXTRA_GAME_VERSION,
        GameStart.EXTRA_LAUNCHER_ARGUMENTS, EXTRA_FEATURED_MOD, EXTRA_REJOIN_OFFER, EXTRA_REJOIN_PEERS, EXTRA_REJOIN_SOURCE,
        EXTRA_MAP, EXTRA_MODS, EXTRA_MOD_FILES));

    /** The featured mod whose game files the app has. */
    static final String FAF = "faf";
    /** The seat key's name in the app's no-backup folder: it proves the seat, so no backup may take it elsewhere. */
    static final String REJOIN_KEY = "faf_rejoin_seat.key";
    /** What GPGNet's rejoin needs of every player's game (docs/LAUNCHER_NATIVE_NETWORK.md, When the game crashes in a match). */
    static final String REJOIN_CONSOLE = "net_PeerFeatures=1;net_RejoinAccept=1";

    private static final Pattern REJOIN_PEERS = Pattern.compile("\\d{1,10}=\\d{1,3}(?:,\\d{1,10}=\\d{1,3}){0,15}");
    /** FAF's map folder names, but . and .., which would name the vault's folders themselves. */
    private static final Pattern MAP = Pattern.compile("(?!\\.+$)[a-z0-9_.-]{1,64}");
    /**
     * A mod's uid, as its author wrote it in mod_info.lua: a UUID mostly, but not always hex (M28AI's are
     * "fnewm028-v247-..."). It names files here, so no folder and no leading dot.
     */
    private static final Pattern MOD = Pattern.compile("(?!\\.)[0-9A-Za-z_.-]{8,64}");
    /** A file of content.faforever.com/mods/, as the last segment of its URL: no folder, and it goes into the URL encoded. */
    private static final Pattern MOD_FILE = Pattern.compile("(?!\\.)[^/\\\\\\p{Cntrl}]{1,120}\\.zip");
    /** Our own providers, whose files a launcher's replay would make the game read for it. */
    private static final String OWN_AUTHORITY = "io.github.m3rt1n99.fafre";

    /** A switch a launcher may give, and what it takes. */
    private static final class Switch {
        /** The value, whole; its first group, where it has one, is a port. Null for a flag. */
        final Pattern value;
        final String takes;

        Switch(String value, String takes) {
            this.value = value != null ? Pattern.compile(value) : null;
            this.takes = takes;
        }
    }

    private static final String LOOPBACK_REPLAY = "gpgnet://127\\.0\\.0\\.1:(\\d{1,5})/\\d{1,10}/[A-Za-z0-9_.-]{1,64}\\.SCFAreplay";
    private static final String REPLAY_TAKES = "gpgnet://127.0.0.1:<port>/<game id>/<name>.SCFAreplay";
    private static final String DECIMAL = "-?\\d{1,10}(?:\\.\\d{1,20})?";
    private static final String COUNT = "\\d{1,4}";
    private static final String COUNT_TAKES = "a whole number from 0 to 9999";

    /**
     * The whitelist: each switch a launcher may give, spelled so, once, with what it takes. Never from outside: the app's
     * own (/init, /log, /fullscreen, ...), /prefs, /con, the rejoin's (this side adds them for REJOIN_OFFER and REJOIN_PEERS),
     * any other switch, a file's path, and a host off the phone.
     */
    private static final Map<String, Switch> SWITCHES;

    static {
        Map<String, Switch> switches = new LinkedHashMap<>();
        switches.put("/gpgnet", new Switch("(?:127\\.0\\.0\\.1|localhost):(\\d{1,5})", "127.0.0.1:<port> or localhost:<port>"));
        switches.put("/savereplay", new Switch(LOOPBACK_REPLAY, REPLAY_TAKES));
        switches.put("/replay", new Switch(LOOPBACK_REPLAY, REPLAY_TAKES));
        switches.put("/nativenet", new Switch(null, null));
        switches.put("/nativenetrelay", new Switch(null, null));
        switches.put("/mean", new Switch(DECIMAL, "a decimal number"));
        switches.put("/deviation", new Switch(DECIMAL, "a decimal number"));
        switches.put("/numgames", new Switch(COUNT, COUNT_TAKES));
        switches.put("/team", new Switch(COUNT, COUNT_TAKES));
        switches.put("/players", new Switch(COUNT, COUNT_TAKES));
        switches.put("/startspot", new Switch(COUNT, COUNT_TAKES));
        switches.put("/country", new Switch("[A-Z]{2}", "two capital letters"));
        switches.put("/clan", new Switch("[A-Za-z0-9_-]{1,8}", "1 to 8 letters, digits, _ or -"));
        SWITCHES = Collections.unmodifiableMap(switches);
    }

    /** The version the start asks for, an Integer, or null for the newest (GameStart.version checks it). */
    final Integer version;
    /** The launcher's switches, then this side's. */
    final String[] arguments;
    /** FAF's map folder name, or null. */
    final String map;
    /** The mods' uids, none for none, and their files, or null when the start names none. */
    final String[] mods;
    final String[] modFiles;
    /** The start has a replay, the intent's data. */
    final boolean replay;

    private Launch(Integer version, List<String> arguments, String map, String[] mods, String[] modFiles, boolean replay) {
        this.version = version;
        this.arguments = arguments.toArray(new String[0]);
        this.map = map;
        this.mods = mods;
        this.modFiles = modFiles;
        this.replay = replay;
    }

    /** The whitelist's switches, in its order. */
    static List<String> switches() {
        return new ArrayList<>(SWITCHES.keySet());
    }

    /**
     * A start from outside: its extras as the intent has them (any type: each is checked here), whether it has data (the
     * replay) with the data's scheme and authority as Android reads them, and the file the rejoin's seat key goes to.
     * ARGUMENTS for an extra that is not what the contract says, NO_GAME_FILES for a featured mod other than faf, REPLAY for
     * data that is no replay a launcher may give.
     */
    static Launch check(Map<String, ?> extras, boolean replay, String replayScheme, String replayAuthority, File rejoinKey)
        throws GameStart.Failure {
        for (String name : extras.keySet()) {
            if (name.startsWith(EXTRA_PREFIX) && !EXTRAS.contains(name)) {
                throw GameStart.Failure.arguments("the start has the extra " + name + ", which the contract has not");
            }
        }
        Object version = extras.get(GameStart.EXTRA_GAME_VERSION);
        if (version != null && !(version instanceof Integer)) {
            throw GameStart.Failure.arguments("GAME_VERSION has the type " + version.getClass().getSimpleName() + ", not int");
        }
        Object featured = extras.get(EXTRA_FEATURED_MOD);
        if (featured != null && !(featured instanceof String)) {
            throw GameStart.Failure.arguments("FEATURED_MOD has the type " + featured.getClass().getSimpleName() + ", not String");
        }
        if (featured != null && !FAF.equals(featured)) {
            throw GameStart.Failure.noGameFiles("no game files of the featured mod " + quoted((String) featured)
                + ": the game has FAF's own (faf) only");
        }
        List<String> arguments = new ArrayList<>(switches(extras.get(GameStart.EXTRA_LAUNCHER_ARGUMENTS)));
        Object peers = extras.get(EXTRA_REJOIN_PEERS);
        arguments.addAll(rejoin(extras.get(EXTRA_REJOIN_OFFER), peers, extras.get(EXTRA_REJOIN_SOURCE), rejoinKey));
        String map = map(extras.get(EXTRA_MAP));
        String[] mods = mods(extras.get(EXTRA_MODS));
        String[] modFiles = modFiles(extras.get(EXTRA_MOD_FILES), mods);
        if (replay) {
            if (!"content".equals(replayScheme)) {
                throw GameStart.Failure.replay("a launcher gives it as a content:// URI that it grants the game to read, not "
                    + replayScheme + ":");
            }
            // Also behind a user's id ("0@..."), which Android's resolver takes off.
            if (replayAuthority == null || replayAuthority.contains(OWN_AUTHORITY)) {
                throw GameStart.Failure.replay("it must come from the launcher's own provider, not " + replayAuthority);
            }
        }
        boolean liveReplay = arguments.contains("/replay");
        if (replay && liveReplay) {
            throw GameStart.Failure.arguments("the start has a replay twice, REPLAY and /replay");
        }
        if (peers != null && (replay || liveReplay)) {
            throw GameStart.Failure.arguments("a start that takes a seat back (REJOIN_PEERS) plays no replay");
        }
        return new Launch((Integer) version, arguments, map, mods, modFiles, replay);
    }

    /**
     * The launcher's switches from outside, as the whitelist allows them: each spelled as there, once, followed by its value;
     * anything else fails the start with ARGUMENTS, also a value or a switch in another case, which the engine would find.
     * Never split: a value is one argument.
     */
    static List<String> switches(Object extra) throws GameStart.Failure {
        if (extra == null) {
            return Collections.emptyList();
        }
        if (!(extra instanceof String[])) {
            throw GameStart.Failure.arguments("LAUNCHER_ARGUMENTS has the type " + extra.getClass().getSimpleName()
                + ", not String[]");
        }
        String[] given = ((String[]) extra).clone();
        Set<String> seen = new HashSet<>();
        for (int i = 0; i < given.length; ++i) {
            String argument = given[i];
            if (argument == null) {
                throw GameStart.Failure.arguments("LAUNCHER_ARGUMENTS has null at index " + i);
            }
            Switch known = SWITCHES.get(argument);
            if (known == null) {
                throw GameStart.Failure.arguments("LAUNCHER_ARGUMENTS has " + quoted(argument) + " at index " + i
                    + ", which is no switch a launcher may give");
            }
            if (!seen.add(argument)) {
                throw GameStart.Failure.arguments("LAUNCHER_ARGUMENTS has " + argument + " twice");
            }
            if (known.value == null) {
                continue;
            }
            if (++i == given.length || given[i] == null) {
                throw GameStart.Failure.arguments("LAUNCHER_ARGUMENTS has " + argument + " without " + known.takes);
            }
            if (!valid(known.value, given[i])) {
                throw GameStart.Failure.arguments("LAUNCHER_ARGUMENTS has " + argument + " " + quoted(given[i]) + ": it takes "
                    + known.takes);
            }
        }
        return Arrays.asList(given);
    }

    private static boolean valid(Pattern pattern, String value) {
        Matcher matcher = pattern.matcher(value);
        if (!matcher.matches()) {
            return false;
        }
        if (matcher.groupCount() == 0) {
            return true;
        }
        int port = Integer.parseInt(matcher.group(1));
        return port >= 1 && port <= 65535;
    }

    /**
     * What this side adds for the rejoin: with REJOIN_OFFER the offer's switches, the seat key in {@code key}; with
     * REJOIN_PEERS and REJOIN_SOURCE, which need each other and the offer on the same start, the rejoin itself.
     */
    static List<String> rejoin(Object offer, Object peers, Object source, File key) throws GameStart.Failure {
        if (offer != null && !(offer instanceof Boolean)) {
            throw GameStart.Failure.arguments("REJOIN_OFFER has the type " + offer.getClass().getSimpleName() + ", not boolean");
        }
        List<String> arguments = new ArrayList<>();
        boolean offered = Boolean.TRUE.equals(offer);
        if (offered) {
            arguments.addAll(Arrays.asList("/rejoinoffer", "/rejoinkeyfile", key.getPath(), "/con", REJOIN_CONSOLE));
        }
        if (peers == null && source == null) {
            return arguments;
        }
        if (peers == null || source == null) {
            throw GameStart.Failure.arguments("a rejoin needs both REJOIN_PEERS and REJOIN_SOURCE");
        }
        if (!(peers instanceof String) || !REJOIN_PEERS.matcher((String) peers).matches()) {
            throw GameStart.Failure.arguments("REJOIN_PEERS is " + described(peers) + ", not the RejoinOffer's "
                + "<player id>=<source>,... (16 at most)");
        }
        if (!(source instanceof Integer) || (Integer) source < 0 || (Integer) source > 255) {
            throw GameStart.Failure.arguments("REJOIN_SOURCE is " + described(source) + ", not an int from 0 to 255");
        }
        if (!offered) {
            throw GameStart.Failure.arguments("a rejoin needs REJOIN_OFFER on the same start: the returning game offers a "
                + "return as well");
        }
        arguments.addAll(Arrays.asList("/rejoin", (String) peers, Integer.toString((Integer) source), "0"));
        return arguments;
    }

    /** MAP: FAF's map folder name, or null without one. */
    static String map(Object extra) throws GameStart.Failure {
        if (extra == null) {
            return null;
        }
        if (!(extra instanceof String) || !MAP.matcher((String) extra).matches()) {
            throw GameStart.Failure.arguments("MAP is " + described(extra) + ", not FAF's map folder name (1 to 64 of a-z, "
                + "0-9, _ . -, not . or ..)");
        }
        return (String) extra;
    }

    /** MODS: the mods' uids, each once; none without the extra. */
    static String[] mods(Object extra) throws GameStart.Failure {
        if (extra == null) {
            return new String[0];
        }
        if (!(extra instanceof String[])) {
            throw GameStart.Failure.arguments("MODS has the type " + extra.getClass().getSimpleName() + ", not String[]");
        }
        String[] mods = ((String[]) extra).clone();
        Set<String> seen = new HashSet<>();
        for (String uid : mods) {
            if (uid == null || !MOD.matcher(uid).matches()) {
                throw GameStart.Failure.arguments("MODS has " + described(uid) + ", not a mod's uid (8 to 64 of letters, digits, "
                    + "_ . -, not first a .)");
            }
            if (!seen.add(uid.toLowerCase(Locale.ROOT))) {
                throw GameStart.Failure.arguments("MODS has " + uid + " twice");
            }
        }
        return mods;
    }

    /** MOD_FILES: one file of content.faforever.com/mods/ for each of {@code mods}, in its order; null without the extra. */
    static String[] modFiles(Object extra, String[] mods) throws GameStart.Failure {
        if (extra == null) {
            return null;
        }
        if (!(extra instanceof String[])) {
            throw GameStart.Failure.arguments("MOD_FILES has the type " + extra.getClass().getSimpleName() + ", not String[]");
        }
        String[] files = ((String[]) extra).clone();
        if (files.length != mods.length) {
            throw GameStart.Failure.arguments("MOD_FILES has " + files.length + " files for " + mods.length + " MODS");
        }
        for (String file : files) {
            if (file == null || !MOD_FILE.matcher(file).matches()) {
                throw GameStart.Failure.arguments("MOD_FILES has " + described(file) + ", not a file of FAF's mods (<name>.zip, "
                    + "no folder)");
            }
        }
        return files;
    }

    /** A value for a failure's line: a string in quotes, a number or a boolean with its type, else its type. */
    private static String described(Object value) {
        if (value == null) {
            return "null";
        }
        if (value instanceof String) {
            return quoted((String) value);
        }
        if (value instanceof Number || value instanceof Boolean) {
            return value + " (" + value.getClass().getSimpleName() + ")";
        }
        return "a " + value.getClass().getSimpleName();
    }

    private static String quoted(String text) {
        return "\"" + text + "\"";
    }
}
