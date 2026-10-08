package io.github.m3rt1n99.fafre;

import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The header of a galtrace file (port/graphics/trace/format: {@code "GALTRACE"}, u32 version, u32 n,
 * n x (str key, str value), each str a u32 length and UTF-8 bytes, little-endian), and what the menu
 * replay reads from its metadata: the frames the trace reads back, the PC's reference hashes of those
 * frames on Diligent-Vulkan, the game archives whose files the trace references instead of carrying
 * them, and whether its game-file payloads are references at all (format version 2).
 *
 * <p>The key names are galtrace's (port/graphics/trace/format/GalTraceFormat.h, {@code kMeta*}):
 * {@code harness_frames}, {@code frame_rate}, {@code presents}, {@code payload_refs}, {@code ref_archives}
 * and {@code reference_frames.diligent:vk}. A missing key degrades to "no reference" or the defaults
 * instead of failing.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class GalTrace {
    static final byte[] MAGIC = {'G', 'A', 'L', 'T', 'R', 'A', 'C', 'E'};
    /** Sanity limits for a header read from an untrusted stream. */
    private static final int MAX_ENTRIES = 4096;
    private static final int MAX_STRING = 16 * 1024 * 1024;
    private static final Pattern HASH = Pattern.compile("[0-9a-fA-F]{16}");
    /** "10:d92c3a2233b80c55" or "10=..."; the number stands alone (not the "64" of "rgb_fnv1a64": ...). */
    private static final Pattern PAIR =
            Pattern.compile("(?<![\\w.])(\\d+)\\s*[:=]\\s*([0-9a-fA-F]{16})(?![0-9a-fA-F])");
    private static final int[] DEFAULT_FRAMES = {10, 30, 60, 300, 900};
    /** GalTraceFormat.h kMetaReferenceFramesPrefix: "reference_frames.<backend>" = "10:d92c...,15:...". */
    static final String REFERENCE_FRAMES_PREFIX = "reference_frames.";

    final int version;
    /** Header metadata in file order. */
    final Map<String, String> metadata;
    /** Bytes the header takes (magic to the last metadata value). */
    final long headerBytes;

    private GalTrace(int version, Map<String, String> metadata, long headerBytes) {
        this.version = version;
        this.metadata = Collections.unmodifiableMap(metadata);
        this.headerBytes = headerBytes;
    }

    /** Reads the header from the start of a trace; the stream is left after the last metadata value. */
    static GalTrace readHeader(InputStream in) throws IOException {
        byte[] magic = readExactly(in, MAGIC.length);
        for (int i = 0; i < MAGIC.length; ++i) {
            if (magic[i] != MAGIC[i]) {
                throw new IOException("not a galtrace file (no GALTRACE header)");
            }
        }
        long version = readU32(in);
        long count = readU32(in);
        if (version < 1 || version > 1000) {
            throw new IOException("unsupported galtrace version " + version);
        }
        if (count > MAX_ENTRIES) {
            throw new IOException("galtrace header claims " + count + " metadata entries");
        }
        long bytes = 16;
        Map<String, String> metadata = new LinkedHashMap<>();
        for (long i = 0; i < count; ++i) {
            String key = readString(in);
            String value = readString(in);
            bytes += 8 + utf8Length(key) + utf8Length(value);
            metadata.put(key, value);
        }
        return new GalTrace((int) version, metadata, bytes);
    }

    String meta(String key) {
        String value = metadata.get(key);
        return value != null ? value : "";
    }

    /**
     * Whether the trace declares that payloads read from game files are references (VFS path and content
     * hash) and not the files' bytes: format version 2 with PayloadRef records ({@code payload_refs} > 0).
     * This is what the header says; that no embedded payload is a copy of game content is checked on the
     * PC (galtrace-refs and the release review), not here.
     */
    boolean declaresReferences() {
        String mode = firstValue("payloads", "payload_mode", "game_payloads");
        if (!mode.isEmpty()) {
            return version >= 2 && mode.toLowerCase(Locale.ROOT).contains("reference");
        }
        return version >= 2 && payloadRefs() > 0;
    }

    /** GalTraceFormat.h kMetaPayloadRefs: the number of PayloadRef records (version 2), or -1. */
    long payloadRefs() {
        Matcher digits = Pattern.compile("\\d+").matcher(firstValue("payload_refs"));
        return digits.find() ? Long.parseLong(digits.group()) : -1;
    }

    /** The frames the trace reads back ({@code harness_frames}), else the M6 gate frames. */
    int[] readbackFrames() {
        List<Integer> frames = parseFrameList(firstValue("harness_frames", "readback_frames", "frames"));
        if (frames.isEmpty()) {
            return DEFAULT_FRAMES.clone();
        }
        int[] out = new int[frames.size()];
        for (int i = 0; i < out.length; ++i) {
            out[i] = frames.get(i);
        }
        return out;
    }

    /**
     * The PC's reference hashes for Diligent-Vulkan, frame -> FNV-1a 64 over R,G,B (gfx_capture.py's
     * {@code rgb_fnv1a64}), lower-case hex. Empty when the trace has none.
     */
    Map<Integer, String> vulkanReferences() {
        // Exact names first, then any key that names a reference and Vulkan.
        String value = firstValue(REFERENCE_FRAMES_PREFIX + "diligent:vk", REFERENCE_FRAMES_PREFIX + "diligent:vulkan",
                "reference.diligent:vk", "reference_vk");
        if (value.isEmpty()) {
            for (Map.Entry<String, String> entry : metadata.entrySet()) {
                String key = entry.getKey().toLowerCase(Locale.ROOT);
                if (key.contains("ref") && (key.contains("vk") || key.contains("vulkan"))) {
                    value = entry.getValue();
                    break;
                }
            }
        }
        return parseHashes(value);
    }

    /** The game archives the trace's references were read from on the PC, as lower-case file names. */
    List<String> referencedArchives() {
        String value = firstValue("ref_archives", "reference_archives", "archives");
        List<String> out = new ArrayList<>();
        for (String item : splitList(value)) {
            String name = item.replace('\\', '/');
            int slash = name.lastIndexOf('/');
            name = (slash >= 0 ? name.substring(slash + 1) : name).trim().toLowerCase(Locale.ROOT);
            if (!name.isEmpty() && !out.contains(name)) {
                out.add(name);
            }
        }
        return out;
    }

    /** The FAF game version the trace was recorded with, or -1. */
    int fafVersion() {
        String value = firstValue("faf_version", "game_version", "fa_version");
        Matcher digits = Pattern.compile("\\d+").matcher(value);
        return digits.find() ? Integer.parseInt(digits.group()) : -1;
    }

    /** The recorded pace in frames per second (30 for the harness's /framerate 30). */
    int framesPerSecond() {
        String value = firstValue("frame_rate", "framerate", "fps");
        Matcher digits = Pattern.compile("\\d+").matcher(value);
        int fps = digits.find() ? Integer.parseInt(digits.group()) : 30;
        return fps > 0 && fps <= 240 ? fps : 30;
    }

    /** The number of frames (presents) the trace holds, or -1 if the header does not say. */
    int presents() {
        String value = firstValue("presents");
        Matcher digits = Pattern.compile("\\d+").matcher(value);
        return digits.find() ? Integer.parseInt(digits.group()) : -1;
    }

    /** The first non-empty value of these keys (case-insensitive), or "". */
    String firstValue(String... keys) {
        for (String wanted : keys) {
            for (Map.Entry<String, String> entry : metadata.entrySet()) {
                if (entry.getKey().equalsIgnoreCase(wanted) && !entry.getValue().trim().isEmpty()) {
                    return entry.getValue().trim();
                }
            }
        }
        return "";
    }

    /**
     * Frame -> hash from "10:abcd...,30:...", "10=abcd...", a JSON object {"10":"abcd..."} or a JSON array of
     * objects with a frame number and a 16-digit hash: every (number, 16 hex digits) pair in order.
     */
    static Map<Integer, String> parseHashes(String value) {
        Map<Integer, String> out = new TreeMap<>();
        if (value == null || value.isEmpty()) {
            return out;
        }
        Matcher pairs = PAIR.matcher(value.replace("\"", ""));
        while (pairs.find()) {
            out.put(Integer.parseInt(pairs.group(1)), pairs.group(2).toLowerCase(Locale.ROOT));
        }
        if (out.isEmpty()) {
            // [{"frame":10,"rgb_fnv1a64":"..."}, ...]: a frame number, then its hash.
            Matcher frame = Pattern.compile("\"?frame\"?\\s*[:=]\\s*(\\d+)").matcher(value);
            while (frame.find()) {
                Matcher hash = HASH.matcher(value);
                if (hash.find(frame.end())) {
                    out.put(Integer.parseInt(frame.group(1)), hash.group().toLowerCase(Locale.ROOT));
                }
            }
        }
        return out;
    }

    static List<Integer> parseFrameList(String value) {
        List<Integer> out = new ArrayList<>();
        if (value == null) {
            return out;
        }
        Matcher digits = Pattern.compile("\\d+").matcher(value);
        while (digits.find()) {
            try {
                int frame = Integer.parseInt(digits.group());
                if (frame > 0 && !out.contains(frame)) {
                    out.add(frame);
                }
            } catch (NumberFormatException ignored) {
                // a number too large to be a frame
            }
        }
        return out;
    }

    /** Items of a comma, semicolon or newline separated list, or of a JSON array of strings. */
    static List<String> splitList(String value) {
        List<String> out = new ArrayList<>();
        if (value == null) {
            return out;
        }
        String text = value.trim();
        if (text.startsWith("[") && text.endsWith("]")) {
            text = text.substring(1, text.length() - 1);
        }
        for (String part : text.split("[,;\\n]")) {
            String item = part.trim();
            if (item.startsWith("\"") && item.endsWith("\"") && item.length() >= 2) {
                item = item.substring(1, item.length() - 1);
            }
            if (!item.isEmpty()) {
                out.add(item);
            }
        }
        return out;
    }

    private static String readString(InputStream in) throws IOException {
        long length = readU32(in);
        if (length > MAX_STRING) {
            throw new IOException("galtrace header string of " + length + " bytes");
        }
        return new String(readExactly(in, (int) length), StandardCharsets.UTF_8);
    }

    private static long readU32(InputStream in) throws IOException {
        byte[] b = readExactly(in, 4);
        return (b[0] & 0xffL) | (b[1] & 0xffL) << 8 | (b[2] & 0xffL) << 16 | (b[3] & 0xffL) << 24;
    }

    private static byte[] readExactly(InputStream in, int length) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream(Math.min(length, 1 << 16));
        byte[] buffer = new byte[Math.min(Math.max(length, 1), 1 << 16)];
        int left = length;
        while (left > 0) {
            int n = in.read(buffer, 0, Math.min(buffer.length, left));
            if (n < 0) {
                throw new EOFException("the galtrace header ends early");
            }
            out.write(buffer, 0, n);
            left -= n;
        }
        return out.toByteArray();
    }

    private static long utf8Length(String value) {
        return value.getBytes(StandardCharsets.UTF_8).length;
    }
}
