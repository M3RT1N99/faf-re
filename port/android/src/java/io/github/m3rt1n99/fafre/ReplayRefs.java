package io.github.m3rt1n99.fafre;

import android.content.Context;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;

/**
 * The reference results the APK carries ({@code assets/replay_refs.json}, from
 * port/android/assets/replay_refs.json): for replays whose file the release notes link (vault
 * downloads), the checkpoint chain the runner produced on the emulator with the same binaries and
 * no host preferences. Keyed by the sha256 of the .fafreplay as downloaded, so a phone run is only
 * compared when its input is byte-identical. Only hashes and numbers, no replay content.
 *
 * <p>Read leniently: an object keyed by sha256 (optionally under "replays"), or an array of entries
 * with a "sha256" field. Per entry: id, map, beats, game_over_beat, chain (a string, or an object or
 * array of per-ABI chains; also "chains"), measured_on, first_diverging_vs_windows and build ids
 * anywhere under keys containing "build".
 *
 * <p>Since 0.4.1 an APK carries two runner builds (-O0 and -O2), and the table one measurement per
 * build and ABI ("runs": {"arm64-v8a": {chain, build_ids, ...}, ...}). Any object in an entry that
 * has both a chain and build ids is such a measurement; a run's chain is compared with the measurements
 * made with exactly its binaries ({@link Ref#compareFor}), so an -O2 chain is never matched against
 * the -O0 one or the other way round.
 */
final class ReplayRefs {
    static final String ASSET_NAME = "replay_refs.json";
    private static final Pattern SHA256 = Pattern.compile("[0-9a-f]{64}");
    private static final Pattern CHAIN = Pattern.compile("[0-9a-f]{16}");
    private static final Pattern BUILD_ID = Pattern.compile("[0-9a-f]{40}");

    static final class Ref {
        final String sha256;
        final JSONObject json;
        final String id;
        final String map;
        final int beats;
        final int gameOverBeat;
        /** Every chain the entry accepts (per ABI or measurement); lower-case hex. */
        final Set<String> chains;
        final String measuredOn;
        final String firstDivergingVsWindows;
        final String windowsChain;
        final Set<String> buildIds;
        /** Every object with a chain and build ids: one measurement of one build. */
        final List<Measurement> measurements;

        Ref(String sha256, JSONObject json) {
            this.sha256 = sha256;
            this.json = json;
            Object idValue = json.opt("id");
            id = idValue == null || idValue == JSONObject.NULL ? "" : idValue.toString();
            map = json.optString("map", "");
            beats = json.optInt("beats", json.optInt("beats_in_replay", -1));
            gameOverBeat = json.optInt("game_over_beat", -1);
            Set<String> found = new LinkedHashSet<>();
            collectChains(json.opt("chain"), found);
            collectChains(json.opt("chains"), found);
            collectChains(json.opt("checkpoint_chain_fnv1a"), found);
            Object measured = json.opt("measured_on");
            if (measured instanceof JSONArray || measured instanceof JSONObject) {
                collectChains(measured, found);
            }
            // Per-ABI measurements ({"runs": {"x86_64": {"chain": ...}, ...}}), Windows left out.
            collectChains(json.opt("runs"), found);
            chains = Collections.unmodifiableSet(found);
            measuredOn = describeRuns(json.optJSONObject("runs"), measured);
            Object diverging = json.opt("first_diverging_vs_windows");
            firstDivergingVsWindows = diverging instanceof Number ? "beat " + diverging : describe(diverging);
            windowsChain = windowsChain(json);
            Set<String> ids = new LinkedHashSet<>();
            collectBuildIds(json, false, ids);
            buildIds = Collections.unmodifiableSet(ids);
            List<Measurement> runs = new ArrayList<>();
            collectMeasurements("", json, runs);
            measurements = Collections.unmodifiableList(runs);
        }

        /**
         * Compares a run's chain with the measurements made with all of {@code ids} (the run's engine and
         * runner build ids); the most specific ones (fewest build ids) win, so a per-build measurement beats
         * an entry that lists every build. Without such a measurement, every chain of the entry counts and
         * {@link Comparison#sameBinaries} is false.
         */
        Comparison compareFor(String chain, String... ids) {
            List<Measurement> matching = new ArrayList<>();
            int fewest = Integer.MAX_VALUE;
            for (Measurement m : measurements) {
                boolean all = ids.length > 0;
                for (String id : ids) {
                    all &= id != null && !id.isEmpty() && m.buildIds.contains(id.toLowerCase(Locale.ROOT));
                }
                if (!all) {
                    continue;
                }
                if (m.buildIds.size() < fewest) {
                    matching.clear();
                    fewest = m.buildIds.size();
                }
                if (m.buildIds.size() == fewest) {
                    matching.add(m);
                }
            }
            Comparison c = new Comparison();
            if (matching.isEmpty()) {
                c.expected.addAll(chains);
                c.measuredOn = measuredOn;
            } else {
                c.sameBinaries = true;
                StringBuilder where = new StringBuilder();
                for (Measurement m : matching) {
                    c.expected.add(m.chain);
                    String text = m.describe();
                    if (!text.isEmpty() && where.indexOf(text) < 0) {
                        where.append(where.length() > 0 ? "; " : "").append(text);
                    }
                    if (m.gameOverBeat >= 0) {
                        c.gameOverBeat = m.gameOverBeat;
                    }
                }
                c.measuredOn = where.toString();
            }
            if (c.gameOverBeat < 0) {
                c.gameOverBeat = gameOverBeat;
            }
            c.status = c.expected.isEmpty() ? "no-chain"
                    : chain != null && c.expected.contains(chain.toLowerCase(Locale.ROOT)) ? "match" : "differs";
            return c;
        }

        /** "match", "differs" or "no-chain" for a run's chain. */
        String compare(String chain) {
            if (chains.isEmpty()) {
                return "no-chain";
            }
            return chain != null && chains.contains(chain.toLowerCase(Locale.ROOT)) ? "match" : "differs";
        }

        /** Whether the reference was measured with a binary of this build id (false when it names none). */
        boolean measuredWith(String buildId) {
            return buildId != null && !buildId.isEmpty() && buildIds.contains(buildId.toLowerCase(Locale.ROOT));
        }
    }

    /** The outcome of {@link Ref#compareFor}. */
    static final class Comparison {
        /** "match", "differs" or "no-chain". */
        String status = "no-chain";
        /** Whether the chains compared with were measured with the run's own binaries. */
        boolean sameBinaries;
        final Set<String> expected = new LinkedHashSet<>();
        String measuredOn = "";
        int gameOverBeat = -1;
    }

    /** One reference run: a chain and the build ids of the binaries that produced it. */
    static final class Measurement {
        private static final Pattern LEVEL = Pattern.compile("(?i)(?:^|[^a-z0-9])(O[0-3s])(?:$|[^a-z0-9])");

        final String path;
        final String chain;
        final Set<String> buildIds;
        final String abi;
        final boolean translated;
        final String opt;
        final String device;
        final int gameOverBeat;

        Measurement(String path, JSONObject json, String chain, Set<String> buildIds) {
            this.path = path;
            this.chain = chain;
            this.buildIds = Collections.unmodifiableSet(buildIds);
            abi = json.optString("abi", "");
            translated = json.optBoolean("translated", false);
            String level = json.optString("opt", json.optString("optimization", ""));
            if (level.isEmpty()) {
                // "runs": {"arm64-v8a-O2": ...}: the level from the key.
                java.util.regex.Matcher m = LEVEL.matcher(path);
                level = m.find() ? m.group(1).toUpperCase(Locale.ROOT) : "";
            }
            opt = level;
            String text = json.optString("device", "");
            device = text.contains(" ") ? text.substring(0, text.indexOf(' ')) : text;
            gameOverBeat = json.optInt("game_over_beat", -1);
        }

        /** "emulator-5554 arm64-v8a (translated) O2". */
        String describe() {
            StringBuilder out = new StringBuilder(device);
            if (!abi.isEmpty()) {
                out.append(out.length() > 0 ? " " : "").append(abi);
            }
            if (translated) {
                out.append(" (translated)");
            }
            if (!opt.isEmpty()) {
                out.append(out.length() > 0 ? " " : "").append(opt);
            }
            return out.toString();
        }
    }

    /** Every object below {@code value} that has a chain and build ids; Windows entries left out. */
    private static void collectMeasurements(String path, Object value, List<Measurement> out) {
        if (value instanceof JSONObject) {
            JSONObject object = (JSONObject) value;
            String chain = "";
            for (String key : new String[] {"chain", "checkpoint_chain_fnv1a"}) {
                String text = object.optString(key, "").toLowerCase(Locale.ROOT).trim();
                if (chain.isEmpty() && CHAIN.matcher(text).matches()) {
                    chain = text;
                }
            }
            if (!chain.isEmpty()) {
                Set<String> ids = new LinkedHashSet<>();
                collectBuildIds(object, false, ids);
                if (!ids.isEmpty()) {
                    out.add(new Measurement(path, object, chain, ids));
                }
            }
            for (Iterator<String> keys = object.keys(); keys.hasNext();) {
                String key = keys.next();
                String lower = key.toLowerCase(Locale.ROOT);
                if (!lower.contains("windows") && !lower.contains("build")) {
                    collectMeasurements(path + "/" + key, object.opt(key), out);
                }
            }
        } else if (value instanceof JSONArray) {
            JSONArray array = (JSONArray) value;
            for (int i = 0; i < array.length(); ++i) {
                collectMeasurements(path + "[" + i + "]", array.opt(i), out);
            }
        }
    }

    /** "emulator-5554: arm64-v8a (translated), x86_64" from per-ABI runs, else the measured_on text. */
    private static String describeRuns(JSONObject runs, Object measured) {
        if (runs != null && runs.length() > 0) {
            StringBuilder out = new StringBuilder();
            String device = "";
            for (Iterator<String> keys = runs.keys(); keys.hasNext();) {
                String abi = keys.next();
                JSONObject run = runs.optJSONObject(abi);
                out.append(out.length() > 0 ? ", " : "").append(abi);
                if (run != null && run.optBoolean("translated", false)) {
                    out.append(" (translated)");
                }
                if (run != null && device.isEmpty()) {
                    String text = run.optString("device", "");
                    device = text.contains(" ") ? text.substring(0, text.indexOf(' ')) : text;
                }
            }
            return device.isEmpty() ? out.toString() : device + ": " + out;
        }
        if (measured instanceof JSONArray) {
            JSONArray array = (JSONArray) measured;
            StringBuilder out = new StringBuilder();
            for (int i = 0; i < array.length(); ++i) {
                out.append(i > 0 ? "; " : "").append(describe(array.opt(i)));
            }
            return out.toString();
        }
        return describe(measured);
    }

    private final Map<String, Ref> mRefs;
    private final String mError;

    private ReplayRefs(Map<String, Ref> refs, String error) {
        mRefs = refs;
        mError = error;
    }

    private static ReplayRefs sInstance;

    /** The table from the APK's assets; empty (with {@link #error()}) when it is missing or unreadable. */
    static synchronized ReplayRefs get(Context context) {
        if (sInstance == null) {
            String text = null;
            String error = null;
            try (InputStream in = context.getAssets().open(ASSET_NAME)) {
                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                byte[] buffer = new byte[8192];
                int n;
                while ((n = in.read(buffer)) > 0) {
                    bytes.write(buffer, 0, n);
                }
                text = new String(bytes.toByteArray(), StandardCharsets.UTF_8);
            } catch (IOException e) {
                error = "this APK has no reference table (assets/" + ASSET_NAME + ")";
            }
            sInstance = text != null ? parse(text) : new ReplayRefs(Collections.<String, Ref>emptyMap(), error);
        }
        return sInstance;
    }

    static ReplayRefs parse(String text) {
        Map<String, Ref> refs = new LinkedHashMap<>();
        try {
            Object root = new org.json.JSONTokener(text).nextValue();
            if (root instanceof JSONObject && ((JSONObject) root).opt("replays") != null) {
                root = ((JSONObject) root).opt("replays");
            }
            if (root instanceof JSONObject) {
                JSONObject object = (JSONObject) root;
                for (Iterator<String> keys = object.keys(); keys.hasNext();) {
                    String key = keys.next();
                    String sha = key.toLowerCase(Locale.ROOT);
                    JSONObject entry = object.optJSONObject(key);
                    if (SHA256.matcher(sha).matches() && entry != null) {
                        refs.put(sha, new Ref(sha, entry));
                    }
                }
            } else if (root instanceof JSONArray) {
                JSONArray array = (JSONArray) root;
                for (int i = 0; i < array.length(); ++i) {
                    JSONObject entry = array.optJSONObject(i);
                    String sha = entry != null ? entry.optString("sha256", "").toLowerCase(Locale.ROOT) : "";
                    if (SHA256.matcher(sha).matches()) {
                        refs.put(sha, new Ref(sha, entry));
                    }
                }
            } else {
                return new ReplayRefs(Collections.<String, Ref>emptyMap(), "the reference table is not a JSON object");
            }
        } catch (JSONException e) {
            return new ReplayRefs(Collections.<String, Ref>emptyMap(), "the reference table is not valid JSON: "
                    + e.getMessage());
        }
        return new ReplayRefs(Collections.unmodifiableMap(refs), null);
    }

    /** The entry for the first of {@code sha256s} the table knows, or null. */
    Ref find(String... sha256s) {
        for (String sha : sha256s) {
            if (sha != null && !sha.isEmpty()) {
                Ref ref = mRefs.get(sha.toLowerCase(Locale.ROOT));
                if (ref != null) {
                    return ref;
                }
            }
        }
        return null;
    }

    int size() {
        return mRefs.size();
    }

    /** Why the table is empty, or null. */
    String error() {
        return mError;
    }

    List<String> ids() {
        List<String> ids = new ArrayList<>();
        for (Ref ref : mRefs.values()) {
            ids.add(ref.id.isEmpty() ? ref.sha256.substring(0, 12) : ref.id);
        }
        return ids;
    }

    private static void collectChains(Object value, Set<String> out) {
        if (value == null || value == JSONObject.NULL) {
            return;
        }
        if (value instanceof String) {
            String text = ((String) value).toLowerCase(Locale.ROOT).trim();
            if (CHAIN.matcher(text).matches()) {
                out.add(text);
            }
        } else if (value instanceof JSONArray) {
            JSONArray array = (JSONArray) value;
            for (int i = 0; i < array.length(); ++i) {
                collectChains(array.opt(i), out);
            }
        } else if (value instanceof JSONObject) {
            JSONObject object = (JSONObject) value;
            for (Iterator<String> keys = object.keys(); keys.hasNext();) {
                String key = keys.next();
                String lower = key.toLowerCase(Locale.ROOT);
                // Per-ABI strings ({"x86_64": "4971..."}) or measurement objects ({"chain": ...}); never
                // build ids, and never the Windows chain, which the phone is not expected to match.
                if (!lower.contains("build") && !lower.contains("windows")) {
                    collectChains(object.opt(key), out);
                }
            }
        }
    }

    /** The first chain under a key naming Windows ("windows_chain", "windows": {...}), or "". */
    private static String windowsChain(JSONObject json) {
        for (Iterator<String> keys = json.keys(); keys.hasNext();) {
            String key = keys.next();
            String lower = key.toLowerCase(Locale.ROOT);
            if (lower.contains("windows") && !lower.contains("diverg")) {
                Set<String> found = new LinkedHashSet<>();
                Object value = json.opt(key);
                if (value instanceof JSONObject) {
                    JSONObject object = (JSONObject) value;
                    for (Iterator<String> inner = object.keys(); inner.hasNext();) {
                        String name = inner.next();
                        if (!name.toLowerCase(Locale.ROOT).contains("build")) {
                            collectChains(object.opt(name), found);
                        }
                    }
                } else {
                    collectChains(value, found);
                }
                if (!found.isEmpty()) {
                    return found.iterator().next();
                }
            }
        }
        return "";
    }

    private static void collectBuildIds(Object value, boolean underBuildKey, Set<String> out) {
        if (value instanceof String) {
            String text = ((String) value).toLowerCase(Locale.ROOT);
            if (underBuildKey && BUILD_ID.matcher(text).matches()) {
                out.add(text);
            }
        } else if (value instanceof JSONArray) {
            JSONArray array = (JSONArray) value;
            for (int i = 0; i < array.length(); ++i) {
                collectBuildIds(array.opt(i), underBuildKey, out);
            }
        } else if (value instanceof JSONObject) {
            JSONObject object = (JSONObject) value;
            for (Iterator<String> keys = object.keys(); keys.hasNext();) {
                String key = keys.next();
                collectBuildIds(object.opt(key), underBuildKey || key.toLowerCase(Locale.ROOT).contains("build"), out);
            }
        }
    }

    private static String describe(Object value) {
        if (value == null || value == JSONObject.NULL) {
            return "";
        }
        if (value instanceof JSONObject || value instanceof JSONArray) {
            String text = value.toString().replace("\\/", "/");
            return text.length() > 400 ? text.substring(0, 400) + "…" : text;
        }
        return value.toString();
    }
}
