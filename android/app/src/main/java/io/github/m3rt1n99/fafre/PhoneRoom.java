package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.os.storage.StorageManager;

import java.io.File;
import java.io.IOException;
import java.util.UUID;

/**
 * The room on the volume of the app's files (Storage.root) as Android counts it for the app, by StorageManager (API 26):
 * getAllocatableBytes and allocateBytes, which take the volume's UUID. Its calls run on the writers' threads, as Android
 * asks of them. A call that fails is an IOException that says so, also a framework's RuntimeException, so that each writer
 * ends through its own failure before the write starts, a start among them, whose process would otherwise end.
 */
final class PhoneRoom extends Room {
    private final StorageManager mManager;
    private final File mFiles;
    /** The volume's, once Android has named it. */
    private UUID mVolume;

    PhoneRoom(Context context, Storage storage) {
        mManager = context.getSystemService(StorageManager.class);
        mFiles = storage.root;
    }

    @Override
    long allocatable() throws IOException {
        try {
            return mManager.getAllocatableBytes(volume());
        } catch (IOException | RuntimeException e) {
            throw failed("say how much the app may write to the phone's storage", e);
        }
    }

    @Override
    void allocate(long bytes) throws IOException {
        try {
            mManager.allocateBytes(volume(), bytes);
        } catch (IOException | RuntimeException e) {
            throw failed("clear " + NewestGameFiles.size(bytes) + " on the phone's storage for the write", e);
        }
    }

    private UUID volume() throws IOException {
        if (mVolume == null) {
            mVolume = mManager.getUuidForPath(mFiles);
        }
        return mVolume;
    }

    private static IOException failed(String what, Exception e) {
        String why = e.getMessage() != null ? e.getMessage() : e.toString();
        return new IOException("Android cannot " + what + ": " + why, e);
    }
}
