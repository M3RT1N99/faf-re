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
 *                          maps/ if imported
 *   parity/game/&lt;v&gt;/       FAF's files for game version v, as tools/parity-data packs them:
 *                          gamedata/*.nx2 (stripped), bin/init_ci.lua, fa_path.lua
 *   parity/replays/        downloaded .fafreplay files
 *   parity/maps/, m28/     vault maps and M28AI versions
 *   parity/runs/&lt;id&gt;/      one replay's run: replay.scfareplay, its vault, headless.out, engine.log
 *   parity/downloads/      partial downloads
 * </pre>
 */
final class Storage {
    final File root;
    final File scfa;
    final File parity;

    Storage(Context context) throws IOException {
        File dir = context.getExternalFilesDir(null);
        if (dir == null) {
            throw new IOException("shared storage is not available");
        }
        root = dir.getAbsoluteFile();
        scfa = new File(root, "scfa");
        parity = new File(root, "parity");
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
