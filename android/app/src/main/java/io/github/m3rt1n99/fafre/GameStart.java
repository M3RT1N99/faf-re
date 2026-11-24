package io.github.m3rt1n99.fafre;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

/**
 * A start of the game (GameActivity) as its starter asks for it: the Game card's, a test's am start, or a launcher's, the
 * FAF client, another app, which starts LaunchGameActivity for a result (Launch: its extras and their checks), which
 * forwards the start here with the extras below; the client hosts GPGNet itself (upstream's
 * docs/LAUNCHER_NATIVE_NETWORK.md). Here are the game version it plays, the program's command line, and why a start runs
 * no program of its own: a code and a line in English, which the activity's result gives the starter (RESULT_FIRST_USER),
 * and the test app's wording of them. The program's end in front, which ends the process, leaves the starter
 * RESULT_CANCELED; in the back see refusal. No Android in here, for the tests.
 */
final class GameStart {
    /** The lobby's FAF game version, an int. */
    static final String EXTRA_GAME_VERSION = "io.github.m3rt1n99.fafre.extra.GAME_VERSION";
    /** The launcher's arguments, a String[]: never split, as a value may hold spaces. */
    static final String EXTRA_LAUNCHER_ARGUMENTS = "io.github.m3rt1n99.fafre.extra.LAUNCHER_ARGUMENTS";
    /** A failed start's result: the code, a {@link Code}'s name, and the failure's line. */
    static final String RESULT_FAILURE = "failure";
    static final String RESULT_MESSAGE = "message";
    /** NO_ROOM's too, a long: how much more the player must free on the phone, in bytes. */
    static final String RESULT_BYTES_TO_FREE = "bytes_to_free";

    /** The program's path in its install, as FAF's client starts it: the engine takes its folder from it. */
    static final String PROGRAM = "bin/ForgedAlliance.exe";
    /**
     * The switches the activity gives the program itself, which none of the launcher's arguments may be: the engine finds a
     * switch in any case, and takes its first.
     */
    static final List<String> OWN_SWITCHES = Collections.unmodifiableList(Arrays.asList(
        "/init", "/log", "/notelemetry", HudScale.SWITCH, HudScale.MOST_SWITCH, "/nomovie", "/fullscreen"));

    private static final Pattern SPACE = Pattern.compile("\\s");

    /** Why a start runs no program of its own: the result's "failure" is the name. */
    enum Code {
        /** No Supreme Commander: Forged Alliance imported. */
        NO_SCFA,
        /** The import has no fonts. */
        NO_FONTS,
        /**
         * No ready FAF game files for a start that asks for no version, a version FAF's updater has no files of, or another
         * featured mod.
         */
        NO_GAME_FILES,
        /** The game's init file could not be written. */
        INIT_FILE,
        /** The program's environment (HOME, TMPDIR, the report's path) could not be set. */
        ENVIRONMENT,
        /** The app's shared storage is not there. */
        STORAGE,
        /** An extra is not what a start takes: one of the activity's switches among the launcher's arguments, or another type. */
        ARGUMENTS,
        /** A launcher's start while the game runs, whose version and arguments would not reach it (refusal). */
        RUNNING,
        /** Android brought back the activity of a launcher's game whose process had ended in the back (refusal). */
        PROCESS_ENDED,
        /**
         * The app that started LaunchGameActivity is not one the game allows (Callers), did not start it for a result, or gave no
         * token of the game's (PlayTokens).
         */
        CALLER,
        /** FAF's map vault has no map of the start's MAP, as for a generated map, which no request asks it for (Vault). */
        NO_MAP,
        /** A mod of the start's MODS is not on the phone, and FAF has no file of it that MOD_FILES names. */
        NO_MOD,
        /** A download of the start's game files, map or mods failed: the network, or the phone's storage. */
        DOWNLOAD,
        /**
         * The game files of the start's version do not fit in the phone's room, Android's reserve and the spare kept (Room):
         * apart from DOWNLOAD, as the player's remedy is to free RESULT_BYTES_TO_FREE more.
         */
        NO_ROOM,
        /** The start's replay (the intent's data) could not be read or copied. */
        REPLAY,
        /**
         * Back ended the start while it fetched what the game needs (its version's game files, MAP, MODS, its replay), or the
         * player said no to the game files' download on a metered network.
         */
        CANCELLED
    }

    /** A start that runs no program of its own: its code, and its line as the message, which names none of the test app's cards. */
    static final class Failure extends Exception {
        final Code code;
        /** NO_ROOM's RESULT_BYTES_TO_FREE; 0 for the other codes. */
        final long bytesToFree;
        /** The phone has no FAF game files at all, which the test app's cards get: FAF's newest, not a version FAF has not. */
        private final boolean mCardsGetThem;

        private Failure(Code code, String message) {
            this(code, message, false, 0);
        }

        private Failure(Code code, String message, boolean cardsGetThem, long bytesToFree) {
            super(message);
            this.code = code;
            this.bytesToFree = bytesToFree;
            mCardsGetThem = cardsGetThem;
        }

        static Failure noScfa() {
            return new Failure(Code.NO_SCFA, "no Supreme Commander: Forged Alliance imported yet");
        }

        static Failure noFonts() {
            return new Failure(Code.NO_FONTS,
                "no fonts of Supreme Commander: Forged Alliance yet: import its folder or a ZIP of it again, which takes fonts/ too");
        }

        static Failure initFile(String why) {
            return new Failure(Code.INIT_FILE, "the init file: " + why);
        }

        static Failure environment(String why) {
            return new Failure(Code.ENVIRONMENT, "the program's environment: " + why);
        }

        static Failure storage(String why) {
            return new Failure(Code.STORAGE, why);
        }

        static Failure running() {
            return new Failure(Code.RUNNING,
                "a game runs already, which this start's version and arguments cannot reach: end it first");
        }

        static Failure processEnded() {
            return new Failure(Code.PROCESS_ENDED, "the game's process ended while the game was in the back (Android ended it, it "
                + "crashed, or the program ended): its game is gone, and the activity Android brought back starts none with the "
                + "launcher's arguments");
        }

        static Failure caller() {
            return new Failure(Code.CALLER, "the app that started the game is not one it allows: start it for a result, from an "
                + "app signed with a key on the game's allowlist");
        }

        static Failure token() {
            return new Failure(Code.CALLER, "the start has no token the game gave out, or one spent or older than "
                + PlayTokens.LIFETIME_MS / 1000 + " s: ask its status provider for one (play_token) right before each start");
        }

        static Failure arguments(String why) {
            return new Failure(Code.ARGUMENTS, why);
        }

        static Failure noGameFiles(String why) {
            return new Failure(Code.NO_GAME_FILES, why);
        }

        static Failure download(String why) {
            return new Failure(Code.DOWNLOAD, why);
        }

        /** The start's game files that do not fit (GameFilesFetch): Room's line, and the bytes the player must free. */
        static Failure noRoom(Room.NoRoom e) {
            return new Failure(Code.NO_ROOM, e.getMessage(), false, e.toFree);
        }

        static Failure replay(String why) {
            return new Failure(Code.REPLAY, "the replay: " + why);
        }

        static Failure cancelled() {
            return new Failure(Code.CANCELLED, "Back ended the start while it fetched what the game needs");
        }

        /** The player's no to {@code bytes} of {@code version} on a metered network (GameFilesFetch); -1: the updater did not say. */
        static Failure notDownloaded(int version, long bytes) {
            return new Failure(Code.CANCELLED, "the player did not download FAF game version " + version + " ("
                + NewestGameFiles.size(bytes) + ") on a metered network");
        }

        /** The test app's line, which its Game card (the report's end) and its toast show: with the cards that help, as before. */
        String cardLine() {
            switch (code) {
                case NO_SCFA:
                    return "FAIL: " + getMessage() + " (card 2)";
                case NO_FONTS:
                    return "FAIL: no fonts of Supreme Commander: Forged Alliance yet: import its folder or a ZIP of it again (card 2), "
                        + "which takes fonts/ too";
                case NO_GAME_FILES:
                    return mCardsGetThem
                        ? "FAIL: " + getMessage() + "; \"Get FAF's newest version\" on card 5 downloads them, as does the replay test (card 3)"
                        : "FAIL: " + getMessage();
                default:
                    return "FAIL: " + getMessage();
            }
        }

        /** The game's report on it: what the result says, and the end, which the test app's Game card shows. */
        List<String> reportLines() {
            return Arrays.asList(reportLine(), GameReport.END + cardLine());
        }

        /** What the result says, the line for the running program's report, which the refusal does not end (RUNNING). */
        String reportLine() {
            return "game: the start failed, " + code + ": " + getMessage();
        }
    }

    private GameStart() {
    }

    /**
     * A failed fetch of what a launcher's start needs, {@code what} ("the map x"), as the start's code: Back (CANCELLED); FAF
     * has no such file, or its file holds none of the name or uid asked for ({@code missing}: NO_GAME_FILES, NO_MAP,
     * NO_MOD); else DOWNLOAD, the network or the phone's storage.
     */
    static Failure fetchFailure(IOException e, Code missing, String what) {
        if (e instanceof Cancellation.Cancelled) {
            return Failure.cancelled();
        }
        boolean notThere = e instanceof Http.StatusException
            && (((Http.StatusException) e).status == 404 || ((Http.StatusException) e).status == 403);
        if (notThere || e instanceof Vault.Missing) {
            return new Failure(missing, what + ": " + e.getMessage());
        }
        return Failure.download(what + ": " + e.getMessage());
    }

    /**
     * Why a new activity of a launcher's start, one with either extra (null for none), runs no program of its own and ends
     * at once; null when it starts the program or shows the running one. {@code programRuns}: the process runs the program;
     * {@code restored}: Android made the activity anew from its saved state. A launcher's start beside the running program
     * would only show it (RUNNING); a launcher's game whose process ended in the back, which Android brings back, would start
     * anew with the launcher's arguments, its GPGNet session among them, which the launcher saw end (PROCESS_ENDED). Android's
     * re-creation of an activity around the running program shows it, the same game going on, and a start without the extras
     * goes as before.
     */
    static Failure refusal(boolean programRuns, boolean restored, Object gameVersion, Object launcherArguments) {
        if (gameVersion == null && launcherArguments == null) {
            return null;
        }
        if (programRuns && !restored) {
            return Failure.running();
        }
        if (!programRuns && restored) {
            return Failure.processEnded();
        }
        return null;
    }

    /**
     * The version the start plays: the one it asks for, also when its files are not on the phone, which the start's
     * preparation then fetches (GameFilesFetch), and no other, as a lobby would refuse it or desync with it; without one the
     * newest ready version. {@code asked} is the extra as the start gives it, null for none.
     */
    static int version(Storage storage, Object asked) throws Failure {
        if (asked == null) {
            List<Integer> ready = GameFiles.readyVersions(storage);
            if (ready.isEmpty()) {
                throw new Failure(Code.NO_GAME_FILES, "no whole FAF game files yet", true, 0);
            }
            return ready.get(ready.size() - 1);
        }
        if (!(asked instanceof Integer)) {
            throw new Failure(Code.ARGUMENTS, "GAME_VERSION has the type " + asked.getClass().getSimpleName() + ", not int");
        }
        int version = (Integer) asked;
        if (version < 1) {
            // Not worth a request to FAF's updater.
            throw Failure.noGameFiles("FAF has no game version " + version);
        }
        return version;
    }

    /**
     * The program's command line without its display mode: the program's path in {@code game}, the version's folder, then
     * the activity's own arguments, a test's (the extra "arguments", one string split at spaces), and the launcher's as
     * they are, the extra as the start gives it, null for none. {@code noMovie}: an import by an earlier release has no
     * movies, and the splash screen would wait for a tap for each.
     */
    static List<String> arguments(File game, File init, File log, String hudScale, String hudMost, boolean noMovie,
            String testArguments, Object launcherArguments) throws Failure {
        List<String> arguments = new ArrayList<>(Arrays.asList(new File(game, PROGRAM).getPath(), "/init", init.getPath(),
            "/log", log.getPath(),
            // The engine would send its records of desyncs and of a game's end to its author's server, with the players'
            // names, and nobody here has agreed to that.
            "/notelemetry",
            // The HUD's factor, which the UI package's Lua reads (fafre/scale.lua), and the most the user's UI scale can make of it.
            HudScale.SWITCH, hudScale, HudScale.MOST_SWITCH, hudMost));
        if (noMovie) {
            arguments.add("/nomovie");
        }
        if (testArguments != null && !testArguments.trim().isEmpty()) {
            arguments.addAll(Arrays.asList(testArguments.trim().split("\\s+")));
        }
        arguments.addAll(launcherArguments(launcherArguments));
        return arguments;
    }

    private static List<String> launcherArguments(Object extra) throws Failure {
        if (extra == null) {
            return Collections.emptyList();
        }
        if (!(extra instanceof String[])) {
            throw new Failure(Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has the type " + extra.getClass().getSimpleName()
                + ", not String[]");
        }
        List<String> arguments = Arrays.asList((String[]) extra);
        for (int i = 0; i < arguments.size(); ++i) {
            String argument = arguments.get(i);
            if (argument == null) {
                throw new Failure(Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has null at index " + i);
            }
            if (OWN_SWITCHES.contains(argument.toLowerCase(Locale.ROOT))) {
                throw new Failure(Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has " + argument + ", which the activity gives the program itself");
            }
        }
        return arguments;
    }

    /** The command line the program starts with: the display mode, the picture's size, last. */
    static String[] commandLine(List<String> arguments, int width, int height) {
        List<String> line = new ArrayList<>(arguments);
        line.addAll(Arrays.asList("/fullscreen", Integer.toString(width), Integer.toString(height)));
        return line.toArray(new String[0]);
    }

    /** A command line for the report: an argument with a space in it, or an empty one, in quotes, as it is one argument. */
    static String inWords(String[] commandLine) {
        StringBuilder words = new StringBuilder();
        for (String argument : commandLine) {
            if (words.length() > 0) {
                words.append(' ');
            }
            words.append(argument.isEmpty() || SPACE.matcher(argument).find() ? "\"" + argument + "\"" : argument);
        }
        return words.toString();
    }
}
