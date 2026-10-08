package io.github.m3rt1n99.fafre;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;
import java.util.zip.CRC32;
import java.util.zip.Deflater;
import java.util.zip.DeflaterOutputStream;

/**
 * The menu replay's read-back frames as files and hashes, without Android's Bitmap (whose premultiplied
 * alpha would change the colour of pixels whose alpha is below 255):
 * <ul>
 * <li>{@link #rgbFnv1a64}: FNV-1a 64 over R, G, B of every pixel, top row first, exactly as
 * scripts/port/gfx_capture.py hashes frames ({@code fnv1a64(rgb_of(bgra))}), so a phone frame and a PC
 * frame compare by their hash;</li>
 * <li>{@link #writePng}: an 8-bit RGB PNG for looking at the frame (alpha dropped);</li>
 * <li>{@link #writeBmp} / {@link #readBmp}: the frame harness's BMP (32-bit BI_RGB, bottom-up, B G R A,
 * port/graphics/capture/GalCapture.cpp WriteBmp), so {@code gfx_capture.py parity <PC run> <phone run>}
 * reads the phone's frames as they are, alpha included.</li>
 * </ul>
 * Pixels are always passed top row first, 4 bytes per pixel, in {@link #BGRA} (D3D9's A8R8G8B8 in memory:
 * what galplay's readbacks hold) or {@link #RGBA} order.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class FrameImages {
    static final String BGRA = "BGRA8";
    static final String RGBA = "RGBA8";
    private static final byte[] PNG_SIGNATURE = {(byte) 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    private static final long FNV_OFFSET = 0xcbf29ce484222325L;
    private static final long FNV_PRIME = 0x100000001b3L;

    /** A decoded harness BMP: top-down BGRA. */
    static final class Image {
        final int width;
        final int height;
        final byte[] bgra;

        Image(int width, int height, byte[] bgra) {
            this.width = width;
            this.height = height;
            this.bgra = bgra;
        }
    }

    private FrameImages() {
    }

    static boolean isBgra(String format) {
        return format == null || format.isEmpty() || !RGBA.equalsIgnoreCase(format);
    }

    static void checkSize(int width, int height, byte[] pixels) throws IOException {
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
            throw new IOException("bad frame size " + width + "x" + height);
        }
        if (pixels.length < (long) width * height * 4) {
            throw new IOException("frame " + width + "x" + height + " needs " + ((long) width * height * 4)
                    + " bytes, got " + pixels.length);
        }
    }

    /** gfx_capture.py's frame hash: FNV-1a 64 over R, G, B of every pixel, as 16 lower-case hex digits. */
    static String rgbFnv1a64(int width, int height, byte[] pixels, String format) throws IOException {
        checkSize(width, height, pixels);
        boolean bgra = isBgra(format);
        int red = bgra ? 2 : 0;
        int blue = bgra ? 0 : 2;
        long hash = FNV_OFFSET;
        int end = width * height * 4;
        for (int i = 0; i < end; i += 4) {
            hash = (hash ^ (pixels[i + red] & 0xff)) * FNV_PRIME;
            hash = (hash ^ (pixels[i + 1] & 0xff)) * FNV_PRIME;
            hash = (hash ^ (pixels[i + blue] & 0xff)) * FNV_PRIME;
        }
        return String.format(Locale.ROOT, "%016x", hash);
    }

    /** Whether every pixel's alpha is 255. */
    static boolean opaque(int width, int height, byte[] pixels) {
        int end = width * height * 4;
        for (int i = 3; i < end; i += 4) {
            if ((pixels[i] & 0xff) != 0xff) {
                return false;
            }
        }
        return true;
    }

    /** An 8-bit RGB PNG (colour type 2), rows filtered with "Sub", written to a .tmp file and renamed. */
    static void writePng(File target, int width, int height, byte[] pixels, String format) throws IOException {
        checkSize(width, height, pixels);
        boolean bgra = isBgra(format);
        int red = bgra ? 2 : 0;
        int blue = bgra ? 0 : 2;
        ByteArrayOutputStream compressed = new ByteArrayOutputStream(width * height);
        Deflater deflater = new Deflater(6);
        try (DeflaterOutputStream z = new DeflaterOutputStream(compressed, deflater, 1 << 16)) {
            byte[] row = new byte[1 + width * 3];
            row[0] = 1; // filter: Sub
            for (int y = 0; y < height; ++y) {
                int in = y * width * 4;
                int prevR = 0;
                int prevG = 0;
                int prevB = 0;
                for (int x = 0, out = 1; x < width; ++x, in += 4, out += 3) {
                    int r = pixels[in + red] & 0xff;
                    int g = pixels[in + 1] & 0xff;
                    int b = pixels[in + blue] & 0xff;
                    row[out] = (byte) (r - prevR);
                    row[out + 1] = (byte) (g - prevG);
                    row[out + 2] = (byte) (b - prevB);
                    prevR = r;
                    prevG = g;
                    prevB = b;
                }
                z.write(row);
            }
        } finally {
            deflater.end();
        }
        byte[] ihdr = new byte[13];
        putBe32(ihdr, 0, width);
        putBe32(ihdr, 4, height);
        ihdr[8] = 8;  // bit depth
        ihdr[9] = 2;  // colour type: RGB
        ihdr[10] = 0; // deflate
        ihdr[11] = 0; // adaptive filtering
        ihdr[12] = 0; // no interlace
        File temp = new File(target.getParentFile(), target.getName() + ".tmp");
        try (OutputStream out = new FileOutputStream(temp)) {
            out.write(PNG_SIGNATURE);
            writeChunk(out, "IHDR", ihdr, 0, ihdr.length);
            byte[] data = compressed.toByteArray();
            // IDAT chunks of at most 1 MB (a zlib stream is never empty, so there is at least one).
            for (int at = 0; at < data.length; at += 1 << 20) {
                writeChunk(out, "IDAT", data, at, Math.min(1 << 20, data.length - at));
            }
            writeChunk(out, "IEND", new byte[0], 0, 0);
        }
        FileOps.moveReplacing(temp, target);
    }

    /** The harness's BMP: 54-byte header, 32-bit BI_RGB, rows bottom-up, B G R A per pixel. */
    static void writeBmp(File target, int width, int height, byte[] pixels, String format) throws IOException {
        checkSize(width, height, pixels);
        boolean bgra = isBgra(format);
        int pixelBytes = width * height * 4;
        byte[] header = new byte[54];
        header[0] = 'B';
        header[1] = 'M';
        putLe32(header, 2, 54 + pixelBytes);
        putLe32(header, 10, 54);
        putLe32(header, 14, 40);
        putLe32(header, 18, width);
        putLe32(header, 22, height);
        header[26] = 1;  // planes
        header[28] = 32; // bits per pixel
        putLe32(header, 30, 0); // BI_RGB
        putLe32(header, 34, pixelBytes);
        File temp = new File(target.getParentFile(), target.getName() + ".tmp");
        try (OutputStream out = new FileOutputStream(temp)) {
            out.write(header);
            byte[] row = new byte[width * 4];
            for (int y = height - 1; y >= 0; --y) {
                System.arraycopy(pixels, y * width * 4, row, 0, row.length);
                if (!bgra) {
                    for (int i = 0; i < row.length; i += 4) {
                        byte r = row[i];
                        row[i] = row[i + 2];
                        row[i + 2] = r;
                    }
                }
                out.write(row);
            }
        }
        FileOps.moveReplacing(temp, target);
    }

    /** Reads a 32-bit BI_RGB BMP (either row order) as top-down BGRA. */
    static Image readBmp(File file) throws IOException {
        long length = file.length();
        if (length < 54 || length > 300L * 1024 * 1024) {
            throw new IOException(file.getName() + ": not a frame BMP (" + length + " bytes)");
        }
        byte[] data = new byte[(int) length];
        try (InputStream in = new FileInputStream(file)) {
            int at = 0;
            while (at < data.length) {
                int n = in.read(data, at, data.length - at);
                if (n < 0) {
                    throw new IOException(file.getName() + ": truncated");
                }
                at += n;
            }
        }
        if (data[0] != 'B' || data[1] != 'M') {
            throw new IOException(file.getName() + ": not a BMP");
        }
        int offset = le32(data, 10);
        int width = le32(data, 18);
        int height = le32(data, 22);
        int bpp = (data[28] & 0xff) | (data[29] & 0xff) << 8;
        int compression = le32(data, 30);
        if (bpp != 32 || compression != 0 || width <= 0 || height == 0) {
            throw new IOException(file.getName() + ": expected a 32-bit BI_RGB BMP");
        }
        int rows = Math.abs(height);
        long need = (long) offset + (long) width * rows * 4;
        if (offset < 54 || need > data.length) {
            throw new IOException(file.getName() + ": pixel data out of range");
        }
        byte[] pixels = new byte[width * rows * 4];
        int stride = width * 4;
        for (int r = 0; r < rows; ++r) {
            int source = offset + (height > 0 ? rows - 1 - r : r) * stride;
            System.arraycopy(data, source, pixels, r * stride, stride);
        }
        return new Image(width, rows, pixels);
    }

    /** {@code frame_000010.bmp}: the harness's and galplay's file name for a frame. */
    static String frameName(int frame, String extension) {
        return String.format(Locale.ROOT, "frame_%06d.%s", frame, extension);
    }

    private static void writeChunk(OutputStream out, String type, byte[] data, int offset, int length)
            throws IOException {
        byte[] head = new byte[8];
        putBe32(head, 0, length);
        byte[] typeBytes = type.getBytes("US-ASCII");
        System.arraycopy(typeBytes, 0, head, 4, 4);
        out.write(head);
        out.write(data, offset, length);
        CRC32 crc = new CRC32();
        crc.update(typeBytes);
        crc.update(data, offset, length);
        byte[] tail = new byte[4];
        putBe32(tail, 0, (int) crc.getValue());
        out.write(tail);
    }

    private static void putBe32(byte[] b, int at, int value) {
        b[at] = (byte) (value >>> 24);
        b[at + 1] = (byte) (value >>> 16);
        b[at + 2] = (byte) (value >>> 8);
        b[at + 3] = (byte) value;
    }

    private static void putLe32(byte[] b, int at, int value) {
        b[at] = (byte) value;
        b[at + 1] = (byte) (value >>> 8);
        b[at + 2] = (byte) (value >>> 16);
        b[at + 3] = (byte) (value >>> 24);
    }

    private static int le32(byte[] b, int at) {
        return (b[at] & 0xff) | (b[at + 1] & 0xff) << 8 | (b[at + 2] & 0xff) << 16 | (b[at + 3] & 0xff) << 24;
    }
}
