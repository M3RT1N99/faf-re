package io.github.m3rt1n99.fafre;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;

/**
 * A native crash's tombstone, which ProcessExit saves (Android's protobuf, system/core/debuggerd/proto/tombstone.proto), as a
 * summary in text for the game's logs (GameLogs): the signal, its cause and the abort message, each thread's stack, the
 * crashing thread's first, and the libraries' build ids, by which tools/tombstone.py's way with a build's libraries
 * symbolizes the frames. Without what else it holds, which no public issue should: the memory around the registers and the
 * fault, the registers, the open files, the process's log and its command line. No Android in here, for the tests.
 */
final class Tombstone {
    // Field numbers of tombstone.proto.
    private static final int ARCH = 1;
    private static final int TID = 6;
    private static final int SIGNAL = 10;
    private static final int ABORT = 14;
    private static final int CAUSES = 15;
    private static final int THREADS = 16;
    private static final int SIGNAL_NUMBER = 1;
    private static final int SIGNAL_NAME = 2;
    private static final int SIGNAL_CODE = 3;
    private static final int SIGNAL_CODE_NAME = 4;
    private static final int SIGNAL_HAS_ADDRESS = 8;
    private static final int SIGNAL_ADDRESS = 9;
    private static final int CAUSE_TEXT = 1;
    /** A map's entry: its key, the thread's id, and its value, the thread. */
    private static final int ENTRY_VALUE = 2;
    private static final int THREAD_ID = 1;
    private static final int THREAD_NAME = 2;
    private static final int THREAD_BACKTRACE = 4;
    private static final int FRAME_REL_PC = 1;
    private static final int FRAME_FUNCTION = 4;
    private static final int FRAME_FUNCTION_OFFSET = 5;
    private static final int FRAME_FILE = 6;
    private static final int FRAME_BUILD_ID = 8;
    /** tombstone.proto's Architecture, by its number. */
    private static final String[] ARCHES = {"arm32", "arm64", "x86", "x86_64", "riscv64"};

    /** One field of a message: a varint's or a fixed field's value, or where a length-delimited one's bytes are. */
    private static final class Field {
        final int number;
        final long value;
        final int from;
        final int to;

        Field(int number, long value, int from, int to) {
            this.number = number;
            this.value = value;
            this.from = from;
            this.to = to;
        }
    }

    private Tombstone() {
    }

    /** The summary of {@code tombstone}'s bytes; IOException for bytes that are no protobuf message. */
    static String summary(byte[] tombstone) throws IOException {
        long crashing = -1;
        String arch = null;
        Field signal = null;
        List<String> causes = new ArrayList<>();
        List<String> aborts = new ArrayList<>();
        List<Field> threads = new ArrayList<>();
        for (Field field : fields(tombstone, 0, tombstone.length)) {
            switch (field.number) {
                case ARCH:
                    arch = field.value >= 0 && field.value < ARCHES.length ? ARCHES[(int) field.value] : "arch " + field.value;
                    break;
                case TID:
                    crashing = field.value;
                    break;
                case SIGNAL:
                    signal = field;
                    break;
                case ABORT:
                    aborts.add(text(tombstone, field));
                    break;
                case CAUSES:
                    for (Field cause : fields(tombstone, field.from, field.to)) {
                        if (cause.number == CAUSE_TEXT) {
                            causes.add(text(tombstone, cause));
                        }
                    }
                    break;
                case THREADS:
                    for (Field entry : fields(tombstone, field.from, field.to)) {
                        if (entry.number == ENTRY_VALUE) {
                            threads.add(entry);
                        }
                    }
                    break;
                default:
                    break;
            }
        }
        StringBuilder out = new StringBuilder();
        if (arch != null) {
            out.append("arch: ").append(arch).append('\n');
        }
        if (signal != null) {
            out.append("signal: ").append(signal(tombstone, signal)).append('\n');
        }
        for (String cause : causes) {
            out.append("cause: ").append(cause).append('\n');
        }
        for (String abort : aborts) {
            out.append("abort: ").append(abort).append('\n');
        }
        // The crashing thread first, then the others by their ids.
        Map<Long, Field> byId = new TreeMap<>();
        for (Field thread : threads) {
            byId.put(threadId(tombstone, thread), thread);
        }
        List<Long> order = new ArrayList<>(byId.keySet());
        if (order.remove((Long) crashing)) {
            order.add(0, crashing);
        }
        Map<String, String> buildIds = new LinkedHashMap<>();
        for (long id : order) {
            out.append('\n').append(thread(tombstone, byId.get(id), id == crashing, buildIds));
        }
        if (!buildIds.isEmpty()) {
            out.append("\nbuild ids:\n");
            for (Map.Entry<String, String> library : buildIds.entrySet()) {
                out.append("  ").append(library.getKey()).append(' ').append(library.getValue()).append('\n');
            }
        }
        return out.toString();
    }

    /** "11 (SIGSEGV), code 1 (SEGV_MAPERR), fault address 0x40" */
    private static String signal(byte[] tombstone, Field signal) throws IOException {
        long number = 0;
        long code = 0;
        String name = "";
        String codeName = "";
        boolean hasAddress = false;
        long address = 0;
        for (Field field : fields(tombstone, signal.from, signal.to)) {
            switch (field.number) {
                case SIGNAL_NUMBER:
                    number = field.value;
                    break;
                case SIGNAL_NAME:
                    name = text(tombstone, field);
                    break;
                case SIGNAL_CODE:
                    code = field.value;
                    break;
                case SIGNAL_CODE_NAME:
                    codeName = text(tombstone, field);
                    break;
                case SIGNAL_HAS_ADDRESS:
                    hasAddress = field.value != 0;
                    break;
                case SIGNAL_ADDRESS:
                    address = field.value;
                    break;
                default:
                    break;
            }
        }
        // int32 fields: a negative one comes as its 64-bit two's complement.
        return (int) number + " (" + name + "), code " + (int) code + " (" + codeName + ")"
            + (hasAddress ? ", fault address 0x" + Long.toHexString(address) : "");
    }

    private static long threadId(byte[] tombstone, Field thread) throws IOException {
        for (Field field : fields(tombstone, thread.from, thread.to)) {
            if (field.number == THREAD_ID) {
                return field.value;
            }
        }
        return 0;
    }

    /** "thread <id> '<name>'", " (crashed)" for the crashing one, and a line for each frame; the frames' build ids go to {@code buildIds}. */
    private static String thread(byte[] tombstone, Field thread, boolean crashed, Map<String, String> buildIds) throws IOException {
        long id = 0;
        String name = "";
        List<Field> frames = new ArrayList<>();
        for (Field field : fields(tombstone, thread.from, thread.to)) {
            if (field.number == THREAD_ID) {
                id = field.value;
            } else if (field.number == THREAD_NAME) {
                name = text(tombstone, field);
            } else if (field.number == THREAD_BACKTRACE) {
                frames.add(field);
            }
        }
        StringBuilder out = new StringBuilder("thread ").append(id).append(" '").append(name).append('\'')
            .append(crashed ? " (crashed)" : "").append('\n');
        for (int i = 0; i < frames.size(); ++i) {
            long relPc = 0;
            String function = "";
            long offset = 0;
            String file = "";
            String buildId = "";
            for (Field field : fields(tombstone, frames.get(i).from, frames.get(i).to)) {
                switch (field.number) {
                    case FRAME_REL_PC:
                        relPc = field.value;
                        break;
                    case FRAME_FUNCTION:
                        function = text(tombstone, field);
                        break;
                    case FRAME_FUNCTION_OFFSET:
                        offset = field.value;
                        break;
                    case FRAME_FILE:
                        file = text(tombstone, field);
                        break;
                    case FRAME_BUILD_ID:
                        buildId = text(tombstone, field);
                        break;
                    default:
                        break;
                }
            }
            String library = file.isEmpty() ? "?" : library(file);
            if (!buildId.isEmpty()) {
                buildIds.putIfAbsent(library, buildId);
            }
            out.append(String.format(Locale.ROOT, "  #%02d %s+0x%x", i, library, relPc))
                .append(function.isEmpty() ? "" : "  " + function + "+" + offset).append('\n');
        }
        return out.toString();
    }

    /** A frame's library by its name alone, as tools/tombstone.py names it: of /data/app/.../libfafgame.so and .../base.apk!libfafgame.so alike. */
    static String library(String file) {
        return file.substring(Math.max(file.lastIndexOf('/'), file.lastIndexOf('!')) + 1);
    }

    private static String text(byte[] tombstone, Field field) {
        return new String(tombstone, field.from, field.to - field.from, StandardCharsets.UTF_8);
    }

    /** The fields of the message in {@code data} from {@code from} to {@code to}, in their order. */
    private static List<Field> fields(byte[] data, int from, int to) throws IOException {
        List<Field> fields = new ArrayList<>();
        int[] at = {from};
        while (at[0] < to) {
            long key = varint(data, at, to);
            int number = (int) (key >>> 3);
            int wire = (int) (key & 7);
            switch (wire) {
                case 0:
                    fields.add(new Field(number, varint(data, at, to), 0, 0));
                    break;
                case 1:
                case 5: {
                    int size = wire == 1 ? 8 : 4;
                    if (to - at[0] < size) {
                        throw new IOException("a fixed field runs past its message at " + at[0]);
                    }
                    long value = 0;
                    for (int i = size - 1; i >= 0; --i) {
                        value = value << 8 | (data[at[0] + i] & 0xFF);
                    }
                    at[0] += size;
                    fields.add(new Field(number, value, 0, 0));
                    break;
                }
                case 2: {
                    long length = varint(data, at, to);
                    if (length < 0 || length > to - at[0]) {
                        throw new IOException("a field of " + length + " bytes runs past its message at " + at[0]);
                    }
                    fields.add(new Field(number, 0, at[0], at[0] + (int) length));
                    at[0] += (int) length;
                    break;
                }
                default:
                    throw new IOException("wire type " + wire + " at " + at[0]);
            }
        }
        return fields;
    }

    private static long varint(byte[] data, int[] at, int to) throws IOException {
        long value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (at[0] >= to) {
                throw new IOException("a varint runs past its message at " + at[0]);
            }
            int b = data[at[0]++] & 0xFF;
            value |= (long) (b & 0x7F) << shift;
            if (b < 0x80) {
                return value;
            }
        }
        throw new IOException("a varint of more than 10 bytes at " + at[0]);
    }
}
