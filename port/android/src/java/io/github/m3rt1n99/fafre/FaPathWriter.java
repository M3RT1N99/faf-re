package io.github.m3rt1n99.fafre;

import java.io.File;
import java.io.IOException;

/**
 * Writes {@code <root>/faf/fa_path.lua}, which init_faf.lua reads with
 * {@code dofile(InitFileDir .. '/../fa_path.lua')} to learn where the SCFA
 * files and the vault are. On Windows the FAF client writes it; here the
 * launcher rewrites it before every start because the data root's absolute
 * path is only known on the device (and changes with the user profile).
 *
 * <p>The text comes from the manifest's faPathLua.template; every substituted
 * value lands inside a Lua double-quoted string, so it is escaped for one.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class FaPathWriter {
    private FaPathWriter() {
    }

    static String clientVersion(String versionName) {
        return "faf-re-android " + versionName;
    }

    /**
     * Fills {root}, {version} and {client} in one pass, so a value that itself
     * contains "{...}" is never substituted again.
     */
    static String render(String template, String rootPath, String version, String client) {
        StringBuilder out = new StringBuilder(template.length() + rootPath.length() * 2);
        int i = 0;
        while (i < template.length()) {
            char c = template.charAt(i);
            if (c == '{') {
                int close = template.indexOf('}', i);
                if (close > i) {
                    String key = template.substring(i + 1, close);
                    String value = null;
                    if (key.equals("root")) {
                        value = rootPath.replace('\\', '/');
                    } else if (key.equals("version")) {
                        value = version;
                    } else if (key.equals("client")) {
                        value = client;
                    }
                    if (value != null) {
                        out.append(luaEscape(value));
                        i = close + 1;
                        continue;
                    }
                }
            }
            out.append(c);
            ++i;
        }
        return out.toString();
    }

    /** Escapes text for the inside of a Lua "..." string literal. */
    static String luaEscape(String value) {
        StringBuilder out = new StringBuilder(value.length() + 8);
        for (int i = 0; i < value.length(); ++i) {
            char c = value.charAt(i);
            switch (c) {
                case '\\':
                    out.append("\\\\");
                    break;
                case '"':
                    out.append("\\\"");
                    break;
                case '\n':
                    out.append("\\n");
                    break;
                case '\r':
                    out.append("\\r");
                    break;
                case '\0':
                    out.append("\\0");
                    break;
                default:
                    out.append(c);
                    break;
            }
        }
        return out.toString();
    }

    /** Renders the template for this data root and replaces fa_path.lua atomically. Returns the file. */
    static File write(DataManifest manifest, DataRoot root, int fafVersion, String versionName) throws IOException {
        return write(manifest, root, root.path(), fafVersion, versionName);
    }

    /**
     * The same with another spelling of the root's path in the file: the replay test writes the
     * lowercase alias the runner sees (Runner.ensureAlias), GUI Start the real root.
     */
    static File write(DataManifest manifest, DataRoot root, String rootPath, int fafVersion, String versionName)
            throws IOException {
        String text = render(manifest.faPathTemplate, rootPath, Integer.toString(fafVersion),
                clientVersion(versionName));
        File target = root.prepare(manifest.layout.faPathLua);
        FileOps.writeAtomic(target, text);
        return target;
    }
}
