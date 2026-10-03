package io.github.m3rt1n99.fafre;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * Builds the String[] "argv" extra for GameActivity: the game's command line
 * exactly as the desktop exe receives it (faf::port::CommandLine parses it the
 * way CFG_GetArgOption does), so the FAF client can later start the Android
 * game with the arguments it already builds.
 *
 * <p>The launcher's defaults come first; options the user repeats in "extra
 * arguments" replace the default instead of being shadowed by it, because the
 * game uses the first occurrence of an option.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class LaunchArgs {
    static final String EXTRA_ARGV = "argv";
    static final String RENDERER_VULKAN = "vulkan";
    static final String RENDERER_GLES = "gles";
    static final String GAME_LOG = "game.sclog";

    private LaunchArgs() {
    }

    /**
     * @param initScript absolute path of init_faf.lua
     * @param gameLog    absolute path for /log
     * @param renderer   "vulkan" or "gles"
     * @param extra      user text, split like a command line
     */
    static String[] build(String initScript, String gameLog, String renderer, boolean noMovie, boolean noSound,
            String extra) {
        List<String> extras = tokenize(extra);
        List<String> args = new ArrayList<>();
        if (!hasOption(extras, "/init")) {
            args.addAll(Arrays.asList("/init", initScript));
        }
        if (!hasOption(extras, "/nobugreport")) {
            args.add("/nobugreport");
        }
        if (!hasOption(extras, "/log")) {
            args.addAll(Arrays.asList("/log", gameLog));
        }
        if (!hasOption(extras, "/renderer")) {
            args.addAll(Arrays.asList("/renderer", RENDERER_GLES.equals(renderer) ? RENDERER_GLES : RENDERER_VULKAN));
        }
        if (noMovie && !hasOption(extras, "/nomovie")) {
            args.add("/nomovie");
        }
        if (noSound && !hasOption(extras, "/nosound")) {
            args.add("/nosound");
        }
        args.addAll(extras);
        return args.toArray(new String[0]);
    }

    /** Options compare case-insensitively, like the engine's. */
    static boolean hasOption(List<String> args, String option) {
        for (String arg : args) {
            if (arg.toLowerCase(Locale.ROOT).equals(option)) {
                return true;
            }
        }
        return false;
    }

    /**
     * Splits on whitespace; double quotes group a token that contains spaces
     * ("C:/My Maps") and are removed. No other escapes: paths on the device
     * never need them. NUL characters are dropped.
     */
    static List<String> tokenize(String text) {
        List<String> tokens = new ArrayList<>();
        if (text == null) {
            return tokens;
        }
        StringBuilder token = new StringBuilder();
        boolean inToken = false;
        boolean quoted = false;
        for (int i = 0; i < text.length(); ++i) {
            char c = text.charAt(i);
            if (c == '\0') {
                continue;
            }
            if (c == '"') {
                quoted = !quoted;
                inToken = true;
            } else if (!quoted && Character.isWhitespace(c)) {
                if (inToken) {
                    tokens.add(token.toString());
                    token.setLength(0);
                    inToken = false;
                }
            } else {
                token.append(c);
                inToken = true;
            }
        }
        if (inToken) {
            tokens.add(token.toString());
        }
        return tokens;
    }

    /** One line for logs, quoting tokens with spaces. */
    static String describe(String[] argv) {
        StringBuilder out = new StringBuilder();
        for (String arg : argv) {
            if (out.length() > 0) {
                out.append(' ');
            }
            boolean quote = arg.isEmpty() || arg.indexOf(' ') >= 0;
            out.append(quote ? "\"" + arg + "\"" : arg);
        }
        return out.toString();
    }
}
