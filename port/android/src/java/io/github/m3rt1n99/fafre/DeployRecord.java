package io.github.m3rt1n99.fafre;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * {@code <root>/.deploy/deployed.json}: the files the PC deploy script or the
 * in-app importer actually placed, as {dest, size} pairs relative to the data
 * root. The status check uses it to know what a wildcard selection
 * ("fonts/*.ttf") expanded to on the user's install, which the manifest alone
 * cannot say.
 *
 * <p>Read leniently: a bare array of records or an object with a "files" (or
 * "entries") array. Written back in the shape it was read, keeping unknown
 * top-level keys, so the importer and the PC script can both update it.
 */
final class DeployRecord {
    static final class Item {
        final String dest;
        final long size;

        Item(String dest, long size) {
            this.dest = dest;
            this.size = size;
        }
    }

    private final Map<String, Item> mItems = new LinkedHashMap<>();
    private JSONObject mObject;
    private String mListKey = "files";
    private boolean mArrayShape;

    /** The record at {@code file}; empty when missing. A damaged file is reported and treated as empty. */
    static DeployRecord read(File file, LogSink log) {
        DeployRecord record = new DeployRecord();
        String text;
        try {
            text = FileOps.readText(file, 16 * 1024 * 1024);
        } catch (IOException e) {
            log.log("deployed.json unreadable: " + e.getMessage());
            return record;
        }
        if (text == null) {
            return record;
        }
        try {
            JSONArray list;
            String trimmed = text.trim();
            if (trimmed.startsWith("[")) {
                record.mArrayShape = true;
                list = new JSONArray(trimmed);
            } else {
                record.mObject = new JSONObject(trimmed);
                if (record.mObject.has("files")) {
                    list = record.mObject.getJSONArray("files");
                } else if (record.mObject.has("entries")) {
                    record.mListKey = "entries";
                    list = record.mObject.getJSONArray("entries");
                } else {
                    list = new JSONArray();
                }
            }
            for (int i = 0; i < list.length(); ++i) {
                JSONObject item = list.optJSONObject(i);
                if (item == null) {
                    continue;
                }
                String dest = item.optString("dest", "").replace('\\', '/');
                if (Names.isSafeRelativePath(dest)) {
                    record.put(dest, item.optLong("size", -1));
                }
            }
        } catch (JSONException e) {
            log.log("deployed.json is damaged, ignoring it: " + e.getMessage());
            record.mItems.clear();
            record.mObject = null;
            record.mArrayShape = false;
        }
        return record;
    }

    /** Adds or replaces the record for {@code dest} (matched case-insensitively). */
    void put(String dest, long size) {
        mItems.put(Names.toLowerAscii(dest), new Item(dest, size));
    }

    Collection<Item> items() {
        return Collections.unmodifiableCollection(mItems.values());
    }

    /** Records whose destination lies below {@code directory} (case-insensitive), e.g. "scfa/gamedata". */
    List<Item> under(String directory) {
        String prefix = Names.toLowerAscii(directory) + "/";
        List<Item> out = new ArrayList<>();
        for (Map.Entry<String, Item> entry : mItems.entrySet()) {
            if (entry.getKey().startsWith(prefix)) {
                out.add(entry.getValue());
            }
        }
        return out;
    }

    boolean isEmpty() {
        return mItems.isEmpty();
    }

    void write(File file) throws IOException {
        try {
            JSONArray list = new JSONArray();
            for (Item item : mItems.values()) {
                JSONObject json = new JSONObject();
                json.put("dest", item.dest);
                json.put("size", item.size);
                list.put(json);
            }
            String text;
            if (mArrayShape) {
                text = list.toString(1);
            } else {
                JSONObject root = mObject != null ? mObject : new JSONObject();
                if (!root.has("schema")) {
                    root.put("schema", 1);
                }
                root.put("updated", Instant.now().truncatedTo(ChronoUnit.SECONDS).toString());
                root.put(mListKey, list);
                text = root.toString(1);
            }
            FileOps.writeJson(file, text);
        } catch (JSONException e) {
            throw new IOException(e.getMessage(), e);
        }
    }
}
