package io.github.m3rt1n99.fafre;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

/**
 * A start of the game (GameActivity) as its starter asks for it: the Game card's, a test's am start, or a launcher's, the
 * FAF client's activity in the app's main process, which starts it for a result with the extras below and hosts GPGNet
 * itself (upstream's docs/LAUNCHER_NATIVE_NETWORK.md). Here are the game version it plays, the program's command line, and
 * why a start fails before the program runs: a code and a line in English, which the activity's result gives the starter
 * (RESULT_FIRST_USER), and the test app's wording of them. The program's end, which ends the process, leaves the starter
 * RESULT_CANCELED. No Android in here, for the tests.
 */
final class GameStart {
    /** The lobby's FAF game version, an int. */
    static final String EXTRA_GAME_VERSION = "io.github.m3rt1n99.fafre.extra.GAME_VERSION";
    /** The launcher's arguments, a String[]: never split, as a value may hold spaces. */
    static final String EXTRA_LAUNCHER_ARGUMENTS = "io.github.m3rt1n99.fafre.extra.LAUNCHER_ARGUMENTS";
    /** A failed start's result: the code, a {@link Code}'s name, and the failure's line. */
    static final String RESULT_FAILURE = "failure";
    static final String RESULT_MESSAGE = "message";

    /** The program's path in its install, as FAF's client starts it: the engine takes its folder from it. */
    static final String PROGRAM = "bin/ForgedAlliance.exe";
    /**
     * The switches the activity gives the program itself, which none of the launcher's arguments may be: the engine finds a
     * switch in any case, and takes its first.
     */
    static final List<String> OWN_SWITCHES = Collections.unmodifiableList(Arrays.asList(
        "/init", "/log", "/notelemetry", HudScale.SWITCH, HudScale.MOST_SWITCH, "/nomovie", "/fullscreen"));

    private static final Pattern SPACE = Pattern.compile("\\s");

    /** Why a start failed before the program ran: the result's "failure" is the name. */
    enum Code {
        /** No Supreme Commander: Forged Alliance imported. */
        NO_SCFA,
        /** The import has no fonts. */
        NO_FONTS,
        /** No ready FAF game files, or none of the version the start asks for. */
        NO_GAME_FILES,
        /** The game's init file could not be written. */
        INIT_FILE,
        /** The program's environment (HOME, TMPDIR, the report's path) could not be set. */
        ENVIRONMENT,
        /** The app's shared storage is not there. */
        STORAGE,
        /** An extra is not what a start takes: one of the activity's switches among the launcher's arguments, or another type. */
        ARGUMENTS
    }

    /** A start that failed before the program ran: its code, and its line as the message, which names none of the test app's cards. */
    static final class Failure extends Exception {
        final Code code;

        private Failure(Code code, String message) {
            super(message);
            this.code = code;
        }

        static Failure noScfa() {
            return new Failure(Code.NO_SCFA, "no Supreme Commander: Forged Alliance imported yet");
        }

        static Failure noFonts() {
            return new Failure(Code.NO_FONTS,
                "no fonts of Supreme Commander: Forged Alliance yet: import its folder again, which takes fonts/ too");
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

        /** The test app's line, which its Game card (the report's end) and its toast show: with the cards that help, as before. */
        String cardLine() {
            switch (code) {
                case NO_SCFA:
                    return "FAIL: " + getMessage() + " (card 2)";
                case NO_FONTS:
                    return "FAIL: no fonts of Supreme Commander: Forged Alliance yet: import its folder again (card 2), which takes fonts/ too";
                case NO_GAME_FILES:
                    return "FAIL: " + getMessage() + "; \"Get FAF's newest version\" on card 5 downloads them, as does the replay test (card 3)";
                default:
                    return "FAIL: " + getMessage();
            }
        }

        /** The game's report on it: what the result says, and the end, which the test app's Game card shows. */
        List<String> reportLines() {
            return Arrays.asList("game: the start failed, " + code + ": " + getMessage(), GameReport.END + cardLine());
        }
    }

    private GameStart() {
    }

    /**
     * The version the start plays: the one it asks for, whose files must be ready (no other: a lobby would refuse it, or
     * desync with it); without one the newest ready version. {@code asked} is the extra as the start gives it, null for none.
     */
    static int version(Storage storage, Object asked) throws Failure {
        if (asked == null) {
            List<Integer> ready = GameFiles.readyVersions(storage);
            if (ready.isEmpty()) {
                throw new Failure(Code.NO_GAME_FILES, "no whole FAF game files yet");
            }
            return ready.get(ready.size() - 1);
        }
        if (!(asked instanceof Integer)) {
            throw new Failure(Code.ARGUMENTS, "GAME_VERSION has the type " + asked.getClass().getSimpleName() + ", not int");
        }
        int version = (Integer) asked;
        if (!GameFiles.ready(storage, version)) {
            throw new Failure(Code.NO_GAME_FILES, "no whole FAF game files of version " + version + " yet");
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
