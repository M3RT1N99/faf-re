package io.github.m3rt1n99.fafre;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;

/**
 * {@code <root>/faf/version.json}: which FAF game version the files under
 * {@code <root>/faf} belong to and how they got there. Written only after a
 * complete, checksum-verified download or import (and by the PC deploy
 * script), so its presence means "this set is consistent".
 */
final class FafVersion {
    static final String SOURCE_DOWNLOAD = "download";
    static final String SOURCE_IMPORT = "import";
    static final String SOURCE_PC_DEPLOY = "pc-deploy";

    final String featuredMod;
    final int version;
    final String source;

    private FafVersion(String featuredMod, int version, String source) {
        this.featuredMod = featuredMod;
        this.version = version;
        this.source = source;
    }

    /** The recorded version, or null if the file is missing or unreadable. */
    static FafVersion read(File file) {
        try {
            String text = FileOps.readText(file, 64 * 1024);
            if (text == null) {
                return null;
            }
            JSONObject json = new JSONObject(text);
            int version = json.getInt("version");
            return new FafVersion(json.optString("featuredMod", "faf"), version, json.optString("source", ""));
        } catch (IOException | JSONException e) {
            return null;
        }
    }

    static void write(File file, String featuredMod, int version, String source) throws IOException {
        try {
            JSONObject json = new JSONObject();
            json.put("featuredMod", featuredMod);
            json.put("version", version);
            json.put("source", source);
            FileOps.writeJson(file, json.toString());
        } catch (JSONException e) {
            throw new IOException(e.getMessage(), e);
        }
    }
}
