package io.github.m3rt1n99.fafre;

import java.util.ArrayList;
import java.util.List;

/**
 * Name rules shared by every piece of code that turns a manifest entry or a
 * SAF listing into a path under the data root.
 *
 * <p>The wildcard matcher is the one port/data/gamedata.json documents and the
 * native runtime (faf::port::fs::WildcardMatch) and the deploy script use:
 * '*' matches any run of characters (also none), '?' exactly one, and letters
 * compare case-insensitively. Case folding is ASCII only, like the C++ side's
 * std::tolower in the "C" locale, so all three implementations select the same
 * files; the manifest's patterns are all ASCII.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class Names {
    private Names() {
    }

    /**
     * One path component that may be created under the data root. Rejects what
     * could escape a directory or confuse the native side: '/', '\\', NUL, "..",
     * and the degenerate "" and ".".
     */
    static boolean isSafeName(String name) {
        if (name == null || name.isEmpty() || name.equals(".") || name.contains("..")) {
            return false;
        }
        for (int i = 0; i < name.length(); ++i) {
            char c = name.charAt(i);
            if (c == '/' || c == '\\' || c == '\0') {
                return false;
            }
        }
        return true;
    }

    /**
     * Splits a relative '/'-separated path into its components, or returns null
     * when the path is absolute, empty, or has an unsafe component. Manifest
     * paths ("faf/gamedata/lua.nx2", "sounds/Voice/US") go through here before
     * they touch the file system.
     */
    static String[] splitRelative(String path) {
        if (path == null || path.isEmpty() || path.startsWith("/") || path.endsWith("/")) {
            return null;
        }
        List<String> parts = new ArrayList<>();
        int start = 0;
        while (start <= path.length()) {
            int slash = path.indexOf('/', start);
            int end = slash < 0 ? path.length() : slash;
            String part = path.substring(start, end);
            if (!isSafeName(part)) {
                return null;
            }
            parts.add(part);
            if (slash < 0) {
                break;
            }
            start = slash + 1;
        }
        return parts.toArray(new String[0]);
    }

    static boolean isSafeRelativePath(String path) {
        return splitRelative(path) != null;
    }

    static boolean hasWildcard(String pattern) {
        return pattern.indexOf('*') >= 0 || pattern.indexOf('?') >= 0;
    }

    /** Case-insensitive (ASCII) match of a single name against a '*'/'?' pattern. */
    static boolean wildcardMatch(String pattern, String name) {
        int p = 0;
        int s = 0;
        int starPattern = -1; // pattern index just after the last '*'
        int starName = 0;     // name index that '*' currently stops at
        while (s < name.length()) {
            if (p < pattern.length() && pattern.charAt(p) == '*') {
                starPattern = ++p;
                starName = s;
            } else if (p < pattern.length()
                    && (pattern.charAt(p) == '?' || lowerAscii(pattern.charAt(p)) == lowerAscii(name.charAt(s)))) {
                ++p;
                ++s;
            } else if (starPattern >= 0) {
                // Let the last '*' swallow one more character and retry from there.
                p = starPattern;
                s = ++starName;
            } else {
                return false;
            }
        }
        while (p < pattern.length() && pattern.charAt(p) == '*') {
            ++p;
        }
        return p == pattern.length();
    }

    static boolean matchesAny(String[] patterns, String name) {
        for (String pattern : patterns) {
            if (wildcardMatch(pattern, name)) {
                return true;
            }
        }
        return false;
    }

    static boolean equalsIgnoreCaseAscii(String a, String b) {
        if (a.length() != b.length()) {
            return false;
        }
        for (int i = 0; i < a.length(); ++i) {
            if (lowerAscii(a.charAt(i)) != lowerAscii(b.charAt(i))) {
                return false;
            }
        }
        return true;
    }

    static String toLowerAscii(String value) {
        StringBuilder out = new StringBuilder(value.length());
        for (int i = 0; i < value.length(); ++i) {
            out.append(lowerAscii(value.charAt(i)));
        }
        return out.toString();
    }

    static char lowerAscii(char c) {
        return c >= 'A' && c <= 'Z' ? (char) (c + ('a' - 'A')) : c;
    }
}
