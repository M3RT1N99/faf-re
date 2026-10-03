package io.github.m3rt1n99.fafre;

import android.content.res.AssetManager;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/**
 * port/data/gamedata.json, packed by the build as assets/gamedata.json: which
 * game data the engine needs, where each piece comes from and how much of it is
 * needed for which tier. The launcher, the importer, the PC deploy script and
 * the docs all derive from this one file, so nothing here hard-codes a file
 * list.
 *
 * <p>Everything that later becomes a path is validated while parsing (no
 * absolute paths, no "..", no separators inside names), so a damaged or
 * tampered manifest fails loudly instead of steering writes outside the data
 * root.
 */
final class DataManifest {
    static final String ASSET_NAME = "gamedata.json";
    static final int SCHEMA = 1;

    enum Tier {
        REQUIRED("required"),
        RECOMMENDED("recommended"),
        OPTIONAL("optional");

        final String id;

        Tier(String id) {
            this.id = id;
        }

        static Tier parse(String value) throws IOException {
            for (Tier tier : values()) {
                if (tier.id.equals(value)) {
                    return tier;
                }
            }
            throw new IOException("unknown tier '" + value + "'");
        }

        String label() {
            return id.substring(0, 1).toUpperCase(Locale.ROOT) + id.substring(1);
        }
    }

    enum Kind {
        FILE,
        DIR
    }

    /** Directories relative to the data root. Fixed by contract; read from the manifest for one source of truth. */
    static final class Layout {
        final String scfa;
        final String fafBin;
        final String fafGamedata;
        final String faPathLua;
        final String fafVersion;
        final String vault;
        final String localAppData;
        final String documents;
        final String logs;
        final String launch;
        final String deployRecord;

        Layout(JSONObject json) throws IOException {
            scfa = relative(json, "scfa", "scfa");
            fafBin = relative(json, "fafBin", "faf/bin");
            fafGamedata = relative(json, "fafGamedata", "faf/gamedata");
            faPathLua = relative(json, "faPathLua", "faf/fa_path.lua");
            fafVersion = relative(json, "fafVersion", "faf/version.json");
            vault = relative(json, "vault", "vault");
            localAppData = relative(json, "localAppData", "localappdata");
            documents = relative(json, "documents", "documents");
            logs = relative(json, "logs", "logs");
            launch = relative(json, "launch", "launch");
            deployRecord = relative(json, "deployRecord", ".deploy/deployed.json");
        }

        private static String relative(JSONObject json, String key, String fallback) throws IOException {
            String value = json == null ? fallback : json.optString(key, fallback);
            if (!Names.isSafeRelativePath(value)) {
                throw new IOException("layout." + key + " is not a safe relative path: " + value);
            }
            return value;
        }
    }

    /** One FAF file: downloaded from {@code baseUrl/remote} or copied from a FAF install's {@code local}. */
    static final class FafFile {
        final String name;
        final String dest;
        final String local;
        final String remote;
        final Tier tier;
        final long size;
        final String sha256;
        final String purpose;

        FafFile(JSONObject json) throws IOException, JSONException {
            name = json.getString("name");
            dest = json.getString("dest");
            local = json.getString("local");
            remote = json.getString("remote");
            tier = Tier.parse(json.getString("tier"));
            size = json.getLong("size");
            sha256 = Names.toLowerAscii(json.getString("sha256"));
            purpose = json.optString("purpose", "");
            if (!Names.isSafeName(name)) {
                throw new IOException("faf file name is unsafe: " + name);
            }
            if (!Names.isSafeRelativePath(dest) || !Names.isSafeRelativePath(local)) {
                throw new IOException(name + ": dest/local must be safe relative paths");
            }
            if (size < 0) {
                throw new IOException(name + ": negative size");
            }
            if (!sha256.matches("[0-9a-f]{64}")) {
                throw new IOException(name + ": sha256 must be 64 hex digits");
            }
            if (!remote.replace("{v}", "0").matches("[A-Za-z0-9._-]+")) {
                throw new IOException(name + ": remote name has unexpected characters: " + remote);
            }
        }

        /** File name on the download server for FAF game version {@code version}. */
        String remoteName(int version) {
            return remote.replace("{v}", Integer.toString(version));
        }
    }

    /** One selection from the user's SCFA install or FAF vault. */
    static final class Entry {
        final String id;
        final String label;
        final String src;
        final String[] include;
        final String[] exclude;
        final Kind kind;
        final Tier tier;
        final String mount;
        final long approxBytes;
        final String purpose;

        Entry(JSONObject json) throws IOException, JSONException {
            id = json.getString("id");
            label = json.optString("label", id);
            src = json.getString("src");
            include = strings(json.getJSONArray("include"));
            exclude = json.has("exclude") ? strings(json.getJSONArray("exclude")) : new String[0];
            String kindName = json.getString("kind");
            if (kindName.equals("file")) {
                kind = Kind.FILE;
            } else if (kindName.equals("dir")) {
                kind = Kind.DIR;
            } else {
                throw new IOException(id + ": unknown kind '" + kindName + "'");
            }
            tier = Tier.parse(json.getString("tier"));
            mount = json.optString("mount", "");
            approxBytes = json.optLong("approxBytes", -1);
            purpose = json.optString("purpose", "");
            if (!Names.isSafeRelativePath(src)) {
                throw new IOException(id + ": src is not a safe relative path: " + src);
            }
            if (include.length == 0) {
                throw new IOException(id + ": empty include list");
            }
        }

        /**
         * The selection rule: the kind must fit, the name must match an include
         * pattern and must not match an exclude pattern.
         */
        boolean matches(String name, boolean isDirectory) {
            if (isDirectory != (kind == Kind.DIR)) {
                return false;
            }
            return Names.matchesAny(include, name) && !Names.matchesAny(exclude, name);
        }

        /** Include patterns without wildcards that are not excluded: names we know must exist. */
        List<String> literalNames() {
            List<String> names = new ArrayList<>();
            for (String pattern : include) {
                if (!Names.hasWildcard(pattern) && !Names.matchesAny(exclude, pattern)) {
                    names.add(pattern);
                }
            }
            return names;
        }

        private static String[] strings(JSONArray array) throws IOException, JSONException {
            String[] out = new String[array.length()];
            for (int i = 0; i < out.length; ++i) {
                out[i] = array.getString(i);
                if (out[i].isEmpty() || out[i].indexOf('/') >= 0 || out[i].indexOf('\\') >= 0) {
                    throw new IOException("pattern must be a single name: '" + out[i] + "'");
                }
            }
            return out;
        }
    }

    final Layout layout;
    final String featuredMod;
    final int fafVersion;
    final String baseUrl;
    final List<FafFile> fafFiles;
    final String scfaMarker;
    final List<Entry> scfaEntries;
    final List<Entry> userEntries;
    final String faPathTemplate;

    private DataManifest(JSONObject root) throws IOException, JSONException {
        int schema = root.getInt("schema");
        if (schema != SCHEMA) {
            throw new IOException("unsupported schema " + schema + " (expected " + SCHEMA + ")");
        }
        layout = new Layout(root.optJSONObject("layout"));

        JSONObject faf = root.getJSONObject("faf");
        featuredMod = faf.getString("featuredMod");
        fafVersion = faf.getInt("version");
        String url = faf.getString("baseUrl");
        if (!url.startsWith("https://")) {
            throw new IOException("faf.baseUrl must be https: " + url);
        }
        baseUrl = url.endsWith("/") ? url.substring(0, url.length() - 1) : url;
        List<FafFile> files = new ArrayList<>();
        JSONArray fileArray = faf.getJSONArray("files");
        for (int i = 0; i < fileArray.length(); ++i) {
            files.add(new FafFile(fileArray.getJSONObject(i)));
        }
        fafFiles = Collections.unmodifiableList(files);

        JSONObject scfa = root.getJSONObject("scfa");
        JSONObject detect = scfa.optJSONObject("detect");
        scfaMarker = detect == null ? "gamedata/textures.scd" : detect.optString("marker", "gamedata/textures.scd");
        if (!Names.isSafeRelativePath(scfaMarker)) {
            throw new IOException("scfa.detect.marker is not a safe relative path");
        }
        scfaEntries = entries(scfa.getJSONArray("entries"));
        JSONObject user = root.optJSONObject("user");
        userEntries = user == null ? Collections.<Entry>emptyList() : entries(user.getJSONArray("entries"));

        faPathTemplate = root.getJSONObject("faPathLua").getString("template");
        if (!faPathTemplate.contains("{root}")) {
            throw new IOException("faPathLua.template does not reference {root}");
        }
    }

    private static List<Entry> entries(JSONArray array) throws IOException, JSONException {
        List<Entry> out = new ArrayList<>();
        for (int i = 0; i < array.length(); ++i) {
            out.add(new Entry(array.getJSONObject(i)));
        }
        return Collections.unmodifiableList(out);
    }

    static DataManifest parse(String json) throws IOException {
        try {
            return new DataManifest(new JSONObject(json));
        } catch (JSONException e) {
            throw new IOException("gamedata.json: " + e.getMessage(), e);
        } catch (IOException e) {
            throw new IOException("gamedata.json: " + e.getMessage(), e);
        }
    }

    static DataManifest fromAssets(AssetManager assets) throws IOException {
        InputStream in;
        try {
            in = assets.open(ASSET_NAME);
        } catch (IOException e) {
            throw new IOException("assets/" + ASSET_NAME + " is missing from the APK; rebuild with "
                    + "scripts/port/build_android.ps1", e);
        }
        try (InputStream stream = in) {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buffer = new byte[16 * 1024];
            int n;
            while ((n = stream.read(buffer)) > 0) {
                bytes.write(buffer, 0, n);
            }
            return parse(new String(bytes.toByteArray(), StandardCharsets.UTF_8));
        }
    }

    FafFile fafFile(String name) {
        for (FafFile file : fafFiles) {
            if (file.name.equals(name)) {
                return file;
            }
        }
        return null;
    }

    /** Relative path of the data-path script inside the data root (the /init argument). */
    String initScript() {
        FafFile init = fafFile("init_faf.lua");
        return init != null ? init.dest : layout.fafBin + "/init_faf.lua";
    }

    /** Path inside a FAF install that proves the user picked the right folder. */
    String fafMarker() {
        FafFile init = fafFile("init_faf.lua");
        return init != null ? init.local : "bin/init_faf.lua";
    }

    long fafBytes(boolean includeRecommended) {
        long total = 0;
        for (FafFile file : fafFiles) {
            if (file.tier != Tier.RECOMMENDED || includeRecommended) {
                total += file.size;
            }
        }
        return total;
    }
}
