package io.github.m3rt1n99.fafre;

/**
 * Progress of the running import job. The worker writes it as often as it
 * likes; the service samples it on the main thread a few times per second for
 * the notification and the launcher, so neither is flooded with updates.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class Progress {
    /** Immutable copy for the UI. Byte totals are -1 when unknown. */
    static final class Snapshot {
        final String phase;
        final String item;
        final long doneBytes;
        final long totalBytes;
        final int doneItems;
        final int totalItems;

        Snapshot(String phase, String item, long doneBytes, long totalBytes, int doneItems, int totalItems) {
            this.phase = phase;
            this.item = item;
            this.doneBytes = doneBytes;
            this.totalBytes = totalBytes;
            this.doneItems = doneItems;
            this.totalItems = totalItems;
        }

        /** 0..1000, or -1 when the total is unknown. */
        int permille() {
            if (totalBytes > 0) {
                return (int) Math.min(1000, doneBytes * 1000 / totalBytes);
            }
            if (totalItems > 0) {
                return Math.min(1000, doneItems * 1000 / totalItems);
            }
            return -1;
        }

        String describe() {
            StringBuilder out = new StringBuilder();
            if (totalItems > 0) {
                out.append(Math.min(doneItems + 1, totalItems)).append(" of ").append(totalItems);
            }
            if (totalBytes > 0) {
                if (out.length() > 0) {
                    out.append(" · ");
                }
                out.append(FileOps.formatBytes(doneBytes)).append(" of ").append(FileOps.formatBytes(totalBytes));
            }
            return out.toString();
        }
    }

    private String mPhase = "";
    private String mItem = "";
    private long mDoneBytes;
    private long mTotalBytes = -1;
    private int mDoneItems;
    private int mTotalItems;

    /** Starts a new phase ("Downloading", "Checking existing files", ...) and resets the counters. */
    synchronized void phase(String phase, int totalItems, long totalBytes) {
        mPhase = phase;
        mItem = "";
        mDoneItems = 0;
        mTotalItems = totalItems;
        mDoneBytes = 0;
        mTotalBytes = totalBytes;
    }

    synchronized void item(String item, int doneItems) {
        mItem = item;
        mDoneItems = doneItems;
    }

    synchronized void bytes(long doneBytes) {
        mDoneBytes = doneBytes;
    }

    synchronized void addBytes(long delta) {
        mDoneBytes += delta;
    }

    synchronized long doneBytes() {
        return mDoneBytes;
    }

    synchronized Snapshot snapshot() {
        return new Snapshot(mPhase, mItem, mDoneBytes, mTotalBytes, mDoneItems, mTotalItems);
    }
}
