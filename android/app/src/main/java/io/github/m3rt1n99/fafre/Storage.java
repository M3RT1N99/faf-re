package io.github.m3rt1n99.fafre;

import android.content.Context;

import java.io.File;
import java.io.IOException;

/**
 * Where the app keeps its files, all under {@code Context.getExternalFilesDir(null)}
 * (/sdcard/Android/data/io.github.m3rt1n99.fafre/files), the folder the earlier launcher
 * releases imported the game data into, so an update finds it again.
 *
 * <pre>
 *   scfa/                  the user's Supreme Commander: Forged Alliance (fa_path): gamedata/*.scd,
 *                          fonts/, sounds/, movies/, maps/ if imported; .zip-import/ while a ZIP of it
 *                          is imported (ScfaZip)
 *   parity/game/&lt;v&gt;/       FAF's files for game version v (GameFiles): gamedata/*.nx2 (whole, as FAF
 *                          publishes them), bin/init_ci.lua, fa_path.lua
 *   parity/replays/        downloaded .fafreplay files
 *   parity/maps/, m28/     vault maps and M28AI versions
 *   parity/runs/&lt;id&gt;/      one replay's run: replay.scfareplay, its vault, headless.out, engine.log
 *   parity/downloads/      partial downloads; newest-target, the version the Game card's last
 *                          download was for (NewestGameFilesJob); game-&lt;v&gt;.lock, held while
 *                          a download of version v runs (GameFiles.ensure)
 * </pre>
 */
final class Storage {
    final File root;
    final File scfa;
    final File parity;

    Storage(Context context) throws IOException {
        this(filesDir(context));
    }

    /** The app's files under {@code root}; the tests make one on a folder of their own. */
    Storage(File root) {
        this.root = root.getAbsoluteFile();
        scfa = new File(this.root, "scfa");
        parity = new File(this.root, "parity");
    }

    private static File filesDir(Context context) throws IOException {
        File dir = context.getExternalFilesDir(null);
        if (dir == null) {
            throw new IOException("shared storage is not available");
        }
        return dir;
    }

    File game(int version) {
        return new File(parity, "game/" + version);
    }

    File replays() {
        return mkdirs(new File(parity, "replays"));
    }

    File maps() {
        return mkdirs(new File(parity, "maps"));
    }

    File m28() {
        return mkdirs(new File(parity, "m28"));
    }

    File runs() {
        return mkdirs(new File(parity, "runs"));
    }

    File downloads() {
        return mkdirs(new File(parity, "downloads"));
    }

    File results() {
        return mkdirs(new File(parity, "results"));
    }

    static File mkdirs(File dir) {
        if (!dir.isDirectory()) {
            dir.mkdirs();
        }
        return dir;
    }

    /** {@code dir/name}, matching the name without regard to case as the engine does; null if absent. */
    static File findChild(File dir, String name) {
        File exact = new File(dir, name);
        if (exact.exists()) {
            return exact;
        }
        String[] names = dir.list();
        if (names != null) {
            for (String entry : names) {
                if (entry.equalsIgnoreCase(name)) {
                    return new File(dir, entry);
                }
            }
        }
        return null;
    }

    static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        file.delete();
    }
}
