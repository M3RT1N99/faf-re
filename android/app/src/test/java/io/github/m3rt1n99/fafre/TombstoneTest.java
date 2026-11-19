package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

import org.junit.Test;

/**
 * A native crash's tombstone as text for the game's logs (Tombstone): the protobuf of system/core/debuggerd/proto/tombstone.proto,
 * written here field by field as debuggerd writes it, to the summary with the signal, its cause, the abort message, the stacks
 * and the libraries' build ids, without the memory, the registers, the open files, the log and the command line.
 */
public class TombstoneTest {
    private static final String APK = "/data/app/~~Qm3xT8bCzA0eYf3lJmIuWg==/io.github.m3rt1n99.fafre-9ZtjYw7eYgUlDqVb2i5n1A==";

    /** A protobuf message's bytes, field by field. */
    private static final class Message {
        private final ByteArrayOutputStream out = new ByteArrayOutputStream();

        Message varint(int number, long value) {
            raw((long) number << 3);
            raw(value);
            return this;
        }

        Message text(int number, String value) {
            return bytes(number, value.getBytes(StandardCharsets.UTF_8));
        }

        Message message(int number, Message value) {
            return bytes(number, value.bytes());
        }

        Message fixed64(int number, long value) {
            raw((long) number << 3 | 1);
            for (int i = 0; i < 8; ++i) {
                out.write((int) (value >>> (8 * i)) & 0xFF);
            }
            return this;
        }

        Message bytes(int number, byte[] value) {
            raw((long) number << 3 | 2);
            raw(value.length);
            out.write(value, 0, value.length);
            return this;
        }

        byte[] bytes() {
            return out.toByteArray();
        }

        private void raw(long value) {
            while ((value & ~0x7FL) != 0) {
                out.write((int) (value & 0x7F) | 0x80);
                value >>>= 7;
            }
            out.write((int) value);
        }
    }

    private static Message frame(long relPc, String function, long offset, String file, String buildId) {
        Message frame = new Message().varint(1, relPc).varint(2, 0x7a0000000000L + relPc).varint(3, 0x7ffd0000L);
        if (!function.isEmpty()) {
            frame.text(4, function).varint(5, offset);
        }
        frame.text(6, file).varint(7, 0);
        return buildId.isEmpty() ? frame : frame.text(8, buildId);
    }

    /** A thread of the tombstone's map: its entry, key and thread. */
    private static Message thread(int id, String name, Message... frames) {
        Message thread = new Message().varint(1, id).text(2, name)
            // Registers and memory, which the summary leaves out.
            .message(3, new Message().text(1, "x0").varint(2, 0x7a00001234L))
            .message(5, new Message().text(1, "sp").varint(2, 0x7ffd0000L).bytes(4, "Kestrel's memory".getBytes(StandardCharsets.UTF_8)));
        for (Message frame : frames) {
            thread.message(4, frame);
        }
        return new Message().varint(1, id).message(2, thread);
    }

    /** A crash of the game's process as debuggerd records it, with what no public issue should have. */
    private static byte[] crash() {
        return new Message()
            .varint(1, 1)
            .text(2, "samsung/b0qxxx/b0q:16/BP2A.250605.031/S908BXXU:user/release-keys")
            .text(3, "")
            .text(4, "2026-11-19 09:52:14.123456789+0100")
            .varint(5, 4321)
            .varint(6, 4330)
            .varint(7, 10218)
            .text(8, "u:r:untrusted_app:s0:c218,c256,c512,c768")
            .text(9, "io.github.m3rt1n99.fafre:game")
            .message(10, new Message().varint(1, 11).text(2, "SIGSEGV").varint(3, 1).text(4, "SEGV_MAPERR").varint(8, 1).varint(9, 0x40))
            .text(14, "Assert failed: an army without a brain")
            .message(15, new Message().text(1, "null pointer dereference").varint(2, 0))
            .message(16, thread(4321, "fafre:game", frame(0x9c0, "tgkill", 8, "/apex/com.android.runtime/lib64/bionic/libc.so", "def456")))
            .message(16, thread(4330, "SimThread",
                frame(0x7ffcf9, "", 0, APK + "/lib/arm64/libfafgame.so", "abc123"),
                frame(0x7ac60, "__libc_init", 117, "/apex/com.android.runtime/lib64/bionic/libc.so", "def456"),
                frame(0x1234, "", 0, APK + "/base.apk!libdxcompiler.so", ""),
                frame(0x10, "", 0, "", "")))
            .message(17, new Message().varint(1, 0x7a0000000000L).text(7, "/storage/emulated/0/Download/Kestrel.zip"))
            .message(18, new Message().text(1, "main").message(2, new Message().text(7, "faf").text(8, "LOBBY: Kestrel joined")))
            .message(19, new Message().varint(1, 42).text(2, "/storage/emulated/0/Documents/Kestrel.txt"))
            .fixed64(1000, 0x1122334455667788L)
            .varint(22, 4096)
            .bytes();
    }

    @Test
    public void aCrashsSummaryHasItsSignalCauseAbortStacksAndBuildIdsTheCrashingThreadFirst() throws IOException {
        assertEquals("arch: arm64\n"
            + "signal: 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault address 0x40\n"
            + "cause: null pointer dereference\n"
            + "abort: Assert failed: an army without a brain\n"
            + "\n"
            + "thread 4330 'SimThread' (crashed)\n"
            + "  #00 libfafgame.so+0x7ffcf9\n"
            + "  #01 libc.so+0x7ac60  __libc_init+117\n"
            + "  #02 libdxcompiler.so+0x1234\n"
            + "  #03 ?+0x10\n"
            + "\n"
            + "thread 4321 'fafre:game'\n"
            + "  #00 libc.so+0x9c0  tgkill+8\n"
            + "\n"
            + "build ids:\n"
            + "  libfafgame.so abc123\n"
            + "  libc.so def456\n", Tombstone.summary(crash()));
    }

    @Test
    public void theSummaryHasNothingOfTheMemoryTheLogTheFilesTheDeviceOrTheCommandLine() throws IOException {
        String summary = Tombstone.summary(crash());
        for (String left : new String[] {"Kestrel", "storage", "samsung", "untrusted_app", "io.github.m3rt1n99.fafre", "10218",
            "2026-11-19", "x0"}) {
            assertFalse(left, summary.contains(left));
        }
    }

    @Test
    public void aSignalWithoutAnAddressAndANegativeCodeAsDebuggerdGivesThem() throws IOException {
        // An int32's -6 is the varint of its 64-bit two's complement, ten bytes.
        byte[] kill = new Message().varint(1, 3).varint(6, 77)
            .message(10, new Message().varint(1, 11).text(2, "SIGSEGV").varint(3, -6).text(4, "SI_TKILL").varint(5, 1).varint(7, 4321))
            .message(16, thread(77, "main", frame(0xdc647, "tgkill", 7, "/apex/com.android.runtime/lib64/bionic/libc.so", "")))
            .bytes();
        assertEquals("arch: x86_64\n"
            + "signal: 11 (SIGSEGV), code -6 (SI_TKILL)\n"
            + "\n"
            + "thread 77 'main' (crashed)\n"
            + "  #00 libc.so+0xdc647  tgkill+7\n", Tombstone.summary(kill));
    }

    @Test
    public void bytesThatAreNoTombstoneAreRefused() throws IOException {
        byte[] crash = crash();
        // Cut off inside a field: the arch's varint, the build's fingerprint, the last varint.
        for (int cut : new int[] {1, 7, crash.length - 1}) {
            byte[] cutOff = Arrays.copyOf(crash, cut);
            assertThrows(String.valueOf(cut), IOException.class, () -> Tombstone.summary(cutOff));
        }
        // A group (wire type 3), which tombstone.proto has none of, and a varint of eleven bytes.
        assertThrows(IOException.class, () -> Tombstone.summary(new byte[] {(byte) 0x0b, 0}));
        assertThrows(IOException.class, () -> Tombstone.summary(new byte[] {(byte) 0x08, (byte) 0xff, (byte) 0xff, (byte) 0xff,
            (byte) 0xff, (byte) 0xff, (byte) 0xff, (byte) 0xff, (byte) 0xff, (byte) 0xff, (byte) 0xff, 1}));
        assertEquals("", Tombstone.summary(new byte[0]));
    }

    @Test
    public void aFramesLibraryIsItsFilesNameAsToolsTombstonePyNamesIt() {
        assertEquals("libfafgame.so", Tombstone.library(APK + "/lib/arm64/libfafgame.so"));
        assertEquals("libfafgame.so", Tombstone.library(APK + "/base.apk!libfafgame.so"));
        assertEquals("libc.so", Tombstone.library("libc.so"));
        assertEquals("memfd:jit-cache (deleted)", Tombstone.library("/memfd:jit-cache (deleted)"));
    }
}
