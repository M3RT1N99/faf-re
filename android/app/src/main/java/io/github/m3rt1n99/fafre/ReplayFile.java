package io.github.m3rt1n99.fafre;

import com.github.luben.zstd.ZstdInputStream;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Base64;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.InflaterInputStream;

/**
 * A FAF vault replay, read as tools/replay-parity/replayfile.py and tools/parity-data/common.py
 * read it: the .fafreplay JSON header line, the .scfareplay body (zstd frame, or base64 of a
 * big-endian length and zlib), the body's engine version, map and sim mods, its length in
 * beats and every checksum the live clients recorded.
 */
final class ReplayFile {
    final JSONObject header;
    final byte[] body;

    /** What the parity runner checks before it plays a replay (common.py check_replay). */
    static final class Info {
        String engineVersion;
        String scmap;
        String map;
        Integer m28;
        int players;
        int gameVersion;
        String featuredMod;
        Integer beats;
        /** Why the replay cannot be played here, or null. */
        String unsupported;
    }

    private ReplayFile(JSONObject header, byte[] body) {
        this.header = header;
        this.body = body;
    }

    static ReplayFile decode(byte[] raw) throws IOException {
        int newline = -1;
        for (int i = 0; i < raw.length; ++i) {
            if (raw[i] == '\n') {
                newline = i;
                break;
            }
        }
        if (newline < 0) {
            throw new IOException("no header line");
        }
        JSONObject header;
        try {
            header = new JSONObject(new String(raw, 0, newline, StandardCharsets.UTF_8));
        } catch (JSONException e) {
            throw new IOException("header is not JSON: " + e.getMessage());
        }
        int start = newline + 1;
        boolean zstd = "zstd".equals(header.optString("compression"))
            || (raw.length >= start + 4 && (raw[start] & 0xFF) == 0x28 && (raw[start + 1] & 0xFF) == 0xB5
                && (raw[start + 2] & 0xFF) == 0x2F && (raw[start + 3] & 0xFF) == 0xFD);
        byte[] body;
        if (zstd) {
            body = readAll(new ZstdInputStream(new ByteArrayInputStream(raw, start, raw.length - start)));
        } else {
            String text = new String(raw, start, raw.length - start, StandardCharsets.US_ASCII).trim();
            byte[] blob = Base64.getMimeDecoder().decode(text);
            int length = ByteBuffer.wrap(blob, 0, 4).order(ByteOrder.BIG_ENDIAN).getInt();
            body = readAll(new InflaterInputStream(new ByteArrayInputStream(blob, 4, blob.length - 4)));
            if (body.length != length) {
                throw new IOException("zlib body length mismatch");
            }
        }
        return new ReplayFile(header, body);
    }

    private static byte[] readAll(InputStream in) throws IOException {
        try (InputStream stream = in) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[1 << 16];
            int read;
            while ((read = stream.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
            return out.toByteArray();
        }
    }

    /** The replay's game version: the largest featured-mod version in the header. */
    int gameVersion() {
        JSONObject versions = header.optJSONObject("featured_mod_versions");
        int best = 0;
        if (versions != null) {
            for (Iterator<String> keys = versions.keys(); keys.hasNext(); ) {
                Object value = versions.opt(keys.next());
                if (value instanceof Integer || value instanceof Long) {
                    best = Math.max(best, ((Number) value).intValue());
                }
            }
        }
        return best;
    }

    Info info() {
        Info info = new Info();
        info.featuredMod = header.optString("featured_mod", null);
        info.players = header.optInt("num_players", 0);
        info.gameVersion = gameVersion();
        try {
            readBodyInfo(info);
        } catch (RuntimeException | IOException e) {
            info.unsupported = "unreadable: " + e.getMessage();
            return info;
        }
        if (!"faf".equals(info.featuredMod)) {
            info.unsupported = "featured mod " + info.featuredMod;
        } else if (!("Supreme Commander v1.50." + info.gameVersion).equals(info.engineVersion)) {
            info.unsupported = "engine version " + info.engineVersion;
        } else if (info.map == null) {
            info.unsupported = "map path " + info.scmap;
        }
        if (info.unsupported == null) {
            try {
                info.beats = beats();
            } catch (RuntimeException e) {
                info.beats = null; // the engine judges a damaged stream; only the length is unknown
            }
        }
        return info;
    }

    // --- Body header ---------------------------------------------------------------------------

    private static final Pattern MAP_LINE = Pattern.compile("Replay v[\\d.]+\r\n([^\u0000]*)\u0000");
    private static final Pattern MAP_FOLDER = Pattern.compile("/maps/([^/]+)/", Pattern.CASE_INSENSITIVE);

    private void readBodyInfo(Info info) throws IOException {
        info.engineVersion = new String(body, 0, indexOf(body, (byte) 0, 0), StandardCharsets.ISO_8859_1);
        String head = new String(body, 0, Math.min(body.length, 8192), StandardCharsets.ISO_8859_1);
        Matcher line = MAP_LINE.matcher(head);
        if (!line.find()) {
            throw new IOException("no replay map line");
        }
        info.scmap = line.group(1);
        Matcher folder = MAP_FOLDER.matcher(info.scmap);
        if (folder.find() && folder.start() == 0) {
            info.map = folder.group(1).toLowerCase();
        }
        int pos = line.end();
        if (body[pos] != '\r' || body[pos + 1] != '\n' || body[pos + 2] != 0x1A || body[pos + 3] != 0) {
            throw new IOException("unexpected bytes after the map line");
        }
        int[] cursor = {pos + 8};
        Object mods = luaValue(body, cursor);
        if (!(mods instanceof Map)) {
            throw new IOException("sim mod table missing");
        }
        for (Object value : ((Map<?, ?>) mods).values()) {
            if (!(value instanceof Map)) {
                continue;
            }
            Map<?, ?> mod = (Map<?, ?>) value;
            String name = String.valueOf(mod.get("name"));
            Object version = mod.get("version");
            if (name.equalsIgnoreCase("m28ai") && version instanceof Float) {
                info.m28 = (int) (float) (Float) version;
            } else if (info.unsupported == null) {
                info.unsupported = "sim mod " + name;
            }
        }
    }

    /** One value of the engine's Lua serialization: tags 0 float, 1 string, 2 nil, 3 bool, 4 table. */
    private static Object luaValue(byte[] buf, int[] cursor) throws IOException {
        int kind = buf[cursor[0]++] & 0xFF;
        switch (kind) {
        case 0: {
            float value = ByteBuffer.wrap(buf, cursor[0], 4).order(ByteOrder.LITTLE_ENDIAN).getFloat();
            cursor[0] += 4;
            return value;
        }
        case 1: {
            int end = indexOf(buf, (byte) 0, cursor[0]);
            String value = new String(buf, cursor[0], end - cursor[0], StandardCharsets.ISO_8859_1);
            cursor[0] = end + 1;
            return value;
        }
        case 2:
            return null;
        case 3:
            return buf[cursor[0]++] != 0;
        case 4: {
            Map<Object, Object> table = new LinkedHashMap<>();
            while ((buf[cursor[0]] & 0xFF) != 5) {
                Object key = luaValue(buf, cursor);
                Object value = luaValue(buf, cursor);
                table.put(key, value);
            }
            cursor[0] += 1;
            return table;
        }
        default:
            throw new IOException("unknown Lua value tag " + kind + " at " + (cursor[0] - 1));
        }
    }

    private static int indexOf(byte[] buf, byte value, int from) throws IOException {
        for (int i = from; i < buf.length; ++i) {
            if (buf[i] == value) {
                return i;
            }
        }
        throw new IOException("unterminated string at " + from);
    }

    // --- Command stream ----------------------------------------------------------------------

    private static int int32(byte[] buf, int pos) {
        return ByteBuffer.wrap(buf, pos, 4).order(ByteOrder.LITTLE_ENDIAN).getInt();
    }

    /** The command sources' names and the offset of the command stream (VCR_SetupReplaySession). */
    private int streamStart(List<String> names) {
        int pos = cstringEnd(cstringEnd(0)) + 13; // engine version, "\r\n", "Replay v1.9\r\n"
        pos = cstringEnd(cstringEnd(pos));        // map path, "\r\n\x1a"
        for (int i = 0; i < 2; ++i) {             // sim mods, scenario info
            pos += 4 + int32(body, pos);
        }
        int sources = body[pos] & 0xFF;
        pos += 1;
        for (int i = 0; i < sources; ++i) {
            int end = cstringEnd(pos);
            names.add(new String(body, pos, end - 1 - pos, StandardCharsets.ISO_8859_1));
            pos = end + 4;                        // name, timeouts
        }
        int armies = body[pos + 1] & 0xFF;        // after the cheats flag
        pos += 2;
        for (int i = 0; i < armies; ++i) {
            pos += 4 + int32(body, pos);
            while ((body[pos] & 0xFF) != 0xFF) {  // unit sources, 0xFF-terminated
                ++pos;
            }
            ++pos;
        }
        return pos + 4;                           // random seed
    }

    private int cstringEnd(int pos) {
        while (body[pos] != 0) {
            ++pos;
        }
        return pos + 1;
    }

    /** The game's length in beats: the sum of the CMDST_Advance counts. */
    int beats() {
        int pos = streamStart(new ArrayList<>());
        int beats = 0;
        while (pos + 3 <= body.length) {
            int size = (body[pos + 1] & 0xFF) | (body[pos + 2] & 0xFF) << 8;
            if (size < 3) {
                throw new IllegalStateException("bad message size at " + pos);
            }
            if (body[pos] == 0) {
                beats += int32(body, pos + 3);
            }
            pos += size;
        }
        return beats;
    }

    /** Every checksum the live clients recorded: beat -> md5 hex -> client names. */
    Map<Integer, Map<String, List<String>>> checksums() {
        List<String> names = new ArrayList<>();
        int pos = streamStart(names);
        Map<Integer, Map<String, List<String>>> beats = new HashMap<>();
        int source = -1;
        while (pos + 3 <= body.length) {
            int size = (body[pos + 1] & 0xFF) | (body[pos + 2] & 0xFF) << 8;
            if (size < 3) {
                break;
            }
            int kind = body[pos] & 0xFF;
            if (kind == 1) {
                source = body[pos + 3] & 0xFF;
            } else if (kind == 3 && size >= 3 + 20) {
                StringBuilder md5 = new StringBuilder();
                for (int i = 0; i < 16; ++i) {
                    md5.append(String.format("%02x", body[pos + 3 + i] & 0xFF));
                }
                int beat = int32(body, pos + 3 + 16);
                String name = source >= 0 && source < names.size() ? names.get(source) : String.valueOf(source);
                beats.computeIfAbsent(beat, k -> new HashMap<>()).computeIfAbsent(md5.toString(), k -> new ArrayList<>()).add(name);
            }
            pos += size;
        }
        return beats;
    }

    /**
     * True when a mismatch is the live game's, not the engine's: at that beat more recorded
     * clients agree with the simulation than with the one that differs (replayfile.py
     * live_client_desync).
     */
    static boolean liveClientDesync(Map<Integer, Map<String, List<String>>> recorded, int beat, String sim, String theirs) {
        Map<String, List<String>> votes = recorded.get(beat);
        if (votes == null) {
            return false;
        }
        List<String> forSim = votes.get(sim);
        List<String> forTheirs = votes.get(theirs);
        return (forSim == null ? 0 : forSim.size()) > (forTheirs == null ? 0 : forTheirs.size());
    }
}
