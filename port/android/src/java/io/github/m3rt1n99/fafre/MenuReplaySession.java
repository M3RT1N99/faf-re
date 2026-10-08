package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.content.Intent;
import android.os.Process;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;

/**
 * One menu replay inside GameActivity (the :game process): the Java end of the native side's callbacks
 * (port/android/src/GalPlay.h, "Java contract"), the model the overlay shows, and the run's files.
 *
 * <p>The native replay thread calls {@link #onProgress}, {@link #onReadback} and {@link #onFinished}; each
 * copies what it needs and returns at once. Files are written on one worker thread, in call order:
 * <ul>
 * <li>each read-back frame as {@code frame_<N>.png} (RGB, for looking at it) and, unless the native side wrote
 * it already, {@code frame_<N>.bmp} (the PC frame harness's format, alpha included, so
 * {@code gfx_capture.py parity <PC Vulkan run> <this run>} compares the phone's frames directly);</li>
 * <li>the run's result.json, summary.txt and meta.json at the end, from the per-frame hashes (computed here
 * from the pixels: FNV-1a 64 over R,G,B, as the PC hashes frames, against the PC Diligent-Vulkan hashes in the
 * trace's metadata), the frame times the progress reports carried, and galplay.json's timings and device
 * facts ({@link MenuReplayVerdict}).</li>
 * </ul>
 */
final class MenuReplaySession {
    private static final String TAG = "fafre-menureplay";
    /** What GameActivity going away before the native side's finish is called in the headline. */
    private static final String CLOSED = "closed";

    /** What the overlay shows; a copy, owned by the caller. */
    static final class Snapshot {
        String stage = "";
        String message = "";
        int frame;
        int frames;
        double frameMs;
        double avgFrameMs;
        double gpuFrameMs;
        boolean fast;
        /** Frame -> MenuReplayVerdict.FRAME_* or "waiting". */
        Map<Integer, String> readbacks = new TreeMap<>();
        boolean finished;
        String verdict = "";
        String headline = "";
        List<String[]> lines = new ArrayList<>();
    }

    /** Called (on any thread) when the snapshot changed. */
    interface Listener {
        void onChanged();
    }

    private final Context mContext;
    private final File mRunDir;
    private final String mRun;
    private final File mTrace;
    private final MenuReplay.Options mOptions;
    private final ExecutorService mWorker = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "fafre-menureplay");
        thread.setDaemon(true);
        return thread;
    });
    private final Object mLock = new Object();
    private final String mStarted = Instant.now().toString();

    // Guarded by mLock.
    private Snapshot mSnapshot = new Snapshot();
    private final Map<Integer, MenuReplayVerdict.Frame> mFrames = new TreeMap<>();
    private final List<Double> mFrameMs = new ArrayList<>();
    private final List<Double> mGpuFrameMs = new ArrayList<>();
    private int mLastSampledFrame;
    private JSONObject mGalplay;
    private boolean mFinishSeen;
    private boolean mLogged;
    private GalTrace mHeader;
    private String mHeaderError = "";
    private Listener mListener;

    MenuReplaySession(Context context, Intent intent) {
        mContext = context.getApplicationContext();
        String runDir = intent.getStringExtra(MenuReplay.EXTRA_RUN_DIR);
        mRunDir = runDir != null ? new File(runDir) : null;
        mRun = mRunDir != null ? mRunDir.getName() : "";
        String trace = intent.getStringExtra(MenuReplay.EXTRA_TRACE);
        mTrace = trace != null ? new File(trace) : null;
        mOptions = new MenuReplay.Options();
        mOptions.fast = intent.getBooleanExtra(MenuReplay.EXTRA_FAST, false);
        mOptions.forceCpuDecode = intent.getBooleanExtra(MenuReplay.EXTRA_FORCE_CPU_DECODE, false);
        mOptions.noShaderCache = intent.getBooleanExtra(MenuReplay.EXTRA_NO_SHADER_CACHE, false);
        synchronized (mLock) {
            mSnapshot.fast = mOptions.fast;
            mSnapshot.stage = "starting";
            mSnapshot.message = "Starting the native replay…";
        }
        submit(this::begin);
    }

    void setListener(Listener listener) {
        synchronized (mLock) {
            mListener = listener;
        }
    }

    Snapshot snapshot() {
        synchronized (mLock) {
            return copy(mSnapshot);
        }
    }

    String run() {
        return mRun;
    }

    // ------------------------------------------------------------------ native callbacks

    /** GalPlay.h onMenuReplayProgress: a stage change, or one replayed frame. */
    void onProgress(String json) {
        JSONObject progress;
        try {
            progress = new JSONObject(json);
        } catch (JSONException | NullPointerException e) {
            Log.w(TAG, "progress: not JSON: " + json);
            return;
        }
        synchronized (mLock) {
            if (mSnapshot.finished) {
                return;
            }
            Snapshot next = mSnapshot;
            next.stage = progress.optString("stage", next.stage);
            next.message = progress.optString("message", next.message);
            next.frame = progress.optInt("frame", next.frame);
            next.frames = progress.optInt("frames", next.frames);
            next.frameMs = progress.optDouble("frameMs", next.frameMs);
            next.avgFrameMs = progress.optDouble("avgFrameMs", next.avgFrameMs);
            next.gpuFrameMs = progress.optDouble("gpuFrameMs", next.gpuFrameMs);
            next.fast = progress.optBoolean("fast", next.fast);
            if ("replay".equals(next.stage) && next.frame > mLastSampledFrame && progress.has("frameMs")) {
                // At the recorded pace every frame is reported; as fast as possible at most 30 per second.
                mLastSampledFrame = next.frame;
                mFrameMs.add(next.frameMs);
                if (progress.has("gpuFrameMs")) {
                    mGpuFrameMs.add(next.gpuFrameMs);
                }
            }
        }
        changed();
    }

    /** GalPlay.h onMenuReplayReadback: R G B A, top row first, valid only during the call. */
    void onReadback(int frame, int width, int height, ByteBuffer rgba, String json) {
        byte[] pixels = null;
        String error = "";
        try {
            long bytes = (long) width * height * 4;
            if (rgba == null || width <= 0 || height <= 0 || bytes > Integer.MAX_VALUE || rgba.remaining() < bytes) {
                error = "the readback has no usable pixels (" + width + "x" + height + ", "
                        + (rgba == null ? "no buffer" : rgba.remaining() + " bytes") + ")";
            } else {
                pixels = new byte[(int) bytes];
                rgba.duplicate().get(pixels);
            }
        } catch (RuntimeException e) {
            pixels = null;
            error = "the readback could not be copied: " + e;
        }
        onReadbackPixels(frame, width, height, pixels, FrameImages.RGBA, json, error);
    }

    /** The same with a Java array, in {@code format} ({@link FrameImages#RGBA} or {@link FrameImages#BGRA}). */
    void onReadbackPixels(final int frame, final int width, final int height, final byte[] pixels, final String format,
            final String json, final String copyError) {
        JSONObject details = new JSONObject();
        try {
            if (json != null && !json.isEmpty()) {
                details = new JSONObject(json);
            }
        } catch (JSONException e) {
            Log.w(TAG, "readback: not JSON: " + json);
        }
        synchronized (mLock) {
            MenuReplayVerdict.Frame entry = frameLocked(frame);
            entry.nativeHash = details.optString("hash", "");
            entry.nativeReference = details.optString("reference", "");
            entry.nativeVerdict = details.optString("verdict", "");
            if (details.has("identicalToRecording") && !details.isNull("identicalToRecording")) {
                Object identical = details.opt("identicalToRecording");
                entry.identicalToRecording = identical instanceof Boolean ? ((Boolean) identical ? 1 : 0)
                        : details.optInt("identicalToRecording", -1);
            }
            entry.width = width;
            entry.height = height;
            entry.verdict = MenuReplayVerdict.FRAME_PENDING;
            mSnapshot.readbacks.put(frame, entry.verdict);
        }
        changed();
        submit(() -> processReadback(frame, width, height, pixels, format, copyError));
    }

    /** GalPlay.h onMenuReplayFinished: galplay.json's content; also after an error or a stop. */
    void onFinished(String json) {
        JSONObject galplay;
        try {
            galplay = new JSONObject(json);
        } catch (JSONException | NullPointerException e) {
            galplay = errorResult("the native side's result is not JSON: " + json);
        }
        synchronized (mLock) {
            mFinishSeen = true;
            mGalplay = galplay;
        }
        submit(() -> finish(null));
    }

    /**
     * GameActivity is going away. When the native side has not finished, the run is recorded as closed here; a
     * finish that still arrives replaces it. Waits briefly so the frames read back so far are on disk.
     */
    void onActivityDestroyed() {
        boolean finishSeen;
        synchronized (mLock) {
            finishSeen = mFinishSeen;
        }
        if (!finishSeen) {
            submit(() -> finish(CLOSED));
        }
        drain();
    }

    /** The native runtime could not be loaded: that is the run's result. */
    void failWithoutRuntime(String problem) {
        synchronized (mLock) {
            mFinishSeen = true;
            mGalplay = errorResult(problem);
        }
        submit(() -> finish(null));
        drain();
    }

    // ------------------------------------------------------------------ worker

    /**
     * Runs {@code task} on the worker, or right here once the worker is shut down (a native callback that
     * arrives after GameActivity went away).
     */
    private void submit(Runnable task) {
        try {
            mWorker.execute(task);
        } catch (RejectedExecutionException e) {
            try {
                task.run();
            } catch (RuntimeException failure) {
                Log.e(TAG, "menu replay task failed", failure);
            }
        }
    }

    private void drain() {
        mWorker.shutdown();
        try {
            if (!mWorker.awaitTermination(2000, TimeUnit.MILLISECONDS)) {
                Log.w(TAG, "the run's files are still being written");
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private void begin() {
        GalTrace header = null;
        String error = "";
        if (mTrace == null) {
            error = "no trace path in the Intent";
        } else {
            try (InputStream in = new BufferedInputStream(new FileInputStream(mTrace), 64 * 1024)) {
                header = GalTrace.readHeader(in);
            } catch (IOException e) {
                error = MenuReplay.describe(e);
            }
        }
        synchronized (mLock) {
            mHeader = header;
            mHeaderError = error;
            if (header != null) {
                Map<Integer, String> references = header.vulkanReferences();
                for (int frame : header.readbackFrames()) {
                    frameLocked(frame);
                }
                for (MenuReplayVerdict.Frame entry : mFrames.values()) {
                    String reference = references.get(entry.frame);
                    entry.reference = reference != null ? reference : "";
                    if (!mSnapshot.readbacks.containsKey(entry.frame)) {
                        mSnapshot.readbacks.put(entry.frame, "waiting");
                    }
                }
            }
        }
        changed();
        // The provisional result names this process, so the launcher can tell a running replay from a lost one.
        if (mRunDir != null) {
            try {
                File file = new File(mRunDir, ReplayTest.RESULT_JSON);
                String text = FileOps.readText(file, 4 * 1024 * 1024);
                if (text != null) {
                    JSONObject result = new JSONObject(text);
                    if (result.optBoolean(ReplayTest.IN_PROGRESS)) {
                        result.put(MenuReplay.GAME_PID, Process.myPid()).put("game_started", mStarted);
                        FileOps.writeJson(file, result.toString(1));
                    }
                }
            } catch (IOException | JSONException e) {
                Log.w(TAG, "cannot record the process in result.json: " + e.getMessage());
            }
        }
    }

    private void processReadback(int frame, int width, int height, byte[] pixels, String format, String copyError) {
        String hash = "";
        String error = copyError == null ? "" : copyError;
        String png = "";
        String bmp = "";
        String bmpBy = "";
        boolean opaque = true;
        if (pixels != null) {
            try {
                hash = FrameImages.rgbFnv1a64(width, height, pixels, format);
                opaque = FrameImages.opaque(width, height, pixels);
                if (mRunDir != null) {
                    File pngFile = new File(mRunDir, FrameImages.frameName(frame, "png"));
                    FrameImages.writePng(pngFile, width, height, pixels, format);
                    png = pngFile.getName();
                    File bmpFile = new File(mRunDir, FrameImages.frameName(frame, "bmp"));
                    if (bmpFile.isFile()) {
                        bmpBy = "native";
                    } else {
                        FrameImages.writeBmp(bmpFile, width, height, pixels, format);
                        bmpBy = "app";
                    }
                    bmp = bmpFile.getName();
                }
            } catch (IOException e) {
                error = MenuReplay.describe(e);
            }
        }
        synchronized (mLock) {
            MenuReplayVerdict.Frame entry = frameLocked(frame);
            entry.hash = hash;
            entry.opaque = opaque;
            entry.png = png;
            entry.bmp = bmp;
            entry.bmpBy = bmpBy;
            entry.error = error;
            mSnapshot.readbacks.put(frame, entry.judge());
        }
        changed();
    }

    /** Frames the native side wrote as BMP without a readback callback: hash them and add the PNG. */
    private void adoptNativeBmps() {
        File[] files = mRunDir != null ? mRunDir.listFiles() : null;
        for (File file : files != null ? files : new File[0]) {
            String name = file.getName();
            if (!name.matches("frame_[0-9]{1,9}\\.bmp")) {
                continue;
            }
            int frame = Integer.parseInt(name.substring(6, name.length() - 4));
            synchronized (mLock) {
                MenuReplayVerdict.Frame entry = mFrames.get(frame);
                if (entry != null && !entry.hash.isEmpty()) {
                    continue;
                }
            }
            String hash = "";
            String error = "";
            String png = "";
            int width = 0;
            int height = 0;
            boolean opaque = true;
            try {
                FrameImages.Image image = FrameImages.readBmp(file);
                width = image.width;
                height = image.height;
                hash = FrameImages.rgbFnv1a64(width, height, image.bgra, FrameImages.BGRA);
                opaque = FrameImages.opaque(width, height, image.bgra);
                File pngFile = new File(mRunDir, FrameImages.frameName(frame, "png"));
                FrameImages.writePng(pngFile, width, height, image.bgra, FrameImages.BGRA);
                png = pngFile.getName();
            } catch (IOException e) {
                error = name + ": " + MenuReplay.describe(e);
            }
            synchronized (mLock) {
                MenuReplayVerdict.Frame entry = frameLocked(frame);
                entry.hash = hash;
                entry.width = width;
                entry.height = height;
                entry.opaque = opaque;
                entry.png = png;
                entry.bmp = name;
                entry.bmpBy = "native";
                entry.error = error;
                mSnapshot.readbacks.put(frame, entry.judge());
            }
        }
    }

    /**
     * galplay.json's per-frame list: "frameHashes" (what GalPlay.cpp writes; "frames" is the number of presents
     * there), else "frames" when it is a list (GalPlay.h's description).
     */
    static JSONArray nativeFrameList(JSONObject galplay) {
        JSONArray frames = galplay.optJSONArray("frameHashes");
        return frames != null ? frames : galplay.optJSONArray("frames");
    }

    /** Frames galplay.json lists: what the native side said, and frames this side never saw read back. */
    private void mergeNativeFrames(JSONObject galplay) {
        JSONArray frames = nativeFrameList(galplay);
        synchronized (mLock) {
            for (int i = 0; frames != null && i < frames.length(); ++i) {
                JSONObject frame = frames.optJSONObject(i);
                if (frame == null || frame.optInt("frame", -1) <= 0) {
                    continue;
                }
                MenuReplayVerdict.Frame entry = frameLocked(frame.optInt("frame"));
                if (entry.nativeHash.isEmpty()) {
                    entry.nativeHash = frame.optString("hash", "");
                }
                if (entry.nativeReference.isEmpty()) {
                    entry.nativeReference = frame.optString("reference", "");
                }
                if (entry.nativeVerdict.isEmpty()) {
                    entry.nativeVerdict = frame.optString("verdict", "");
                }
                if (entry.identicalToRecording < 0 && frame.has("identicalToRecording")
                        && !frame.isNull("identicalToRecording")) {
                    entry.identicalToRecording = frame.optBoolean("identicalToRecording") ? 1 : 0;
                }
                if (!MenuReplayVerdict.FRAME_PENDING.equals(entry.verdict)) {
                    mSnapshot.readbacks.put(entry.frame, entry.judge());
                }
            }
        }
    }

    /**
     * Writes the final result (once the native side finished, or, with {@code closedReason}, when GameActivity
     * went away first; a later finish replaces that). Runs on the worker, after every readback before it.
     */
    private void finish(String closedReason) {
        JSONObject galplay;
        synchronized (mLock) {
            if (closedReason != null && mFinishSeen) {
                return; // the native side's result came meanwhile; its own finish writes it
            }
            galplay = mGalplay != null ? mGalplay : new JSONObject();
        }
        adoptNativeBmps();
        mergeNativeFrames(galplay);
        try {
            writeResult(galplay, closedReason);
        } catch (IOException | JSONException | RuntimeException e) {
            Log.e(TAG, "cannot write the menu replay's result", e);
            LauncherLog.get(mContext).log("menu replay " + mRun + ": cannot write the result: " + e);
        }
        changed();
    }

    private void writeResult(JSONObject galplay, String closedReason) throws IOException, JSONException {
        List<MenuReplayVerdict.Frame> frames = new ArrayList<>();
        List<Double> frameMs;
        List<Double> gpuMs;
        String headerError;
        GalTrace header;
        int lastFrame;
        synchronized (mLock) {
            for (MenuReplayVerdict.Frame entry : mFrames.values()) {
                if (MenuReplayVerdict.FRAME_PENDING.equals(entry.verdict)) {
                    entry.error = entry.error.isEmpty() ? "its pixels were never processed" : entry.error;
                }
                entry.judge();
                frames.add(entry);
            }
            frameMs = new ArrayList<>(mFrameMs);
            gpuMs = new ArrayList<>(mGpuFrameMs);
            headerError = mHeaderError;
            header = mHeader;
            lastFrame = mSnapshot.frame;
        }
        String nativeResult = galplay.optString("result", "");
        String nativeMessage = galplay.optString("message", "");
        int exitCode = galplay.optInt("exitCode", -1);
        MenuReplayVerdict verdict = MenuReplayVerdict.judge(frames, nativeResult, exitCode, nativeMessage,
                closedReason != null ? "closed before the end" : null, lastFrame);

        JSONObject timings = galplay.optJSONObject("timings");
        JSONObject caches = galplay.optJSONObject("caches");
        JSONObject device = galplay.optJSONObject("device");
        MenuReplayVerdict.Stats stats = MenuReplayVerdict.Stats.of(frameMs);
        MenuReplayVerdict.Stats gpu = MenuReplayVerdict.Stats.of(gpuMs);
        List<String[]> lines = new ArrayList<>();
        for (MenuReplayVerdict.Frame frame : frames) {
            lines.add(frame.line());
        }
        if (verdict.differs > 0) {
            lines.add(new String[] {"muted", "FAIL means: not byte-identical to the PC frame. Whether such a frame is "
                    + "within the parity rule (max |delta| <= 1 on <= 0.1 % of the pixels) is checked on the PC: "
                    + "gfx_capture.py parity <PC Vulkan run> <this run's folder>, from its frame_*.bmp."});
        }
        if (!nativeMessage.isEmpty() && !verdict.headline.contains(nativeMessage)
                && (verdict.error || verdict.stopped || verdict.differs > 0)) {
            lines.add(new String[] {verdict.error ? "bad" : "warn", "Replay: " + nativeMessage});
        }
        lines.add(new String[] {"body", replayLine(lastFrame, stats, gpu, timings)});
        String shaders = shaderLine(timings, caches);
        if (!shaders.isEmpty()) {
            lines.add(new String[] {"body", shaders});
        }
        String deviceLine = deviceLine(device);
        if (!deviceLine.isEmpty()) {
            lines.add(new String[] {"body", deviceLine});
        }
        for (MenuReplayVerdict.Frame frame : frames) {
            if (frame.hashesDisagree()) {
                lines.add(new String[] {"warn", "Frame " + frame.frame + ": the app's hash " + frame.hash
                        + " is not the native side's " + frame.nativeHash + "."});
            }
            if (!frame.error.isEmpty() && !MenuReplayVerdict.FRAME_DIFFERS.equals(frame.verdict)) {
                lines.add(new String[] {"warn", "Frame " + frame.frame + ": " + frame.error});
            }
        }
        if (!headerError.isEmpty()) {
            lines.add(new String[] {"warn", "The trace's header could not be read: " + headerError});
        }
        lines.add(new String[] {"muted", "Options: " + mOptions.describe() + "."});

        JSONArray frameJson = new JSONArray();
        for (MenuReplayVerdict.Frame frame : frames) {
            frameJson.put(frameJson(frame));
        }
        String ended = Instant.now().toString();
        JSONObject previous = mRunDir != null ? readJson(new File(mRunDir, ReplayTest.RESULT_JSON)) : null;
        String summary = MenuReplay.summaryText(mContext, mRun, verdict.headline, lines);
        JSONObject result = new JSONObject().put("schema", 1).put("kind", MenuReplay.KIND).put("run", mRun)
                .put("started", previous != null ? previous.optString("started", mStarted) : mStarted)
                .put("ended", ended).put(ReplayTest.IN_PROGRESS, false).put(MenuReplay.GAME_PID, Process.myPid())
                .put(MenuReplay.REQUESTED_MILLIS, previous != null ? previous.optLong(MenuReplay.REQUESTED_MILLIS, 0) : 0)
                .put("verdict", verdict.verdict).put("ok", verdict.ok()).put("headline", verdict.headline)
                .put("options", mOptions.toJson()).put("frames", frameJson)
                .put("counts", new JSONObject().put("pass", verdict.pass).put("differs", verdict.differs)
                        .put("no_reference", verdict.noReference).put("not_reached", verdict.notReached)
                        .put("total", verdict.total))
                .put("timing", new JSONObject().put("frame_ms", statsJson(stats)).put("work_ms", statsJson(gpu))
                        .put("native", timings != null ? timings : JSONObject.NULL)
                        .put("caches", caches != null ? caches : JSONObject.NULL))
                .put("galplay", new JSONObject().put("result", nativeResult).put("exit_code", exitCode)
                        .put("message", nativeMessage).put("finished", closedReason == null))
                .put("device", device != null ? device : JSONObject.NULL)
                .put("lines", MenuReplay.linesJson(lines)).put("summary_text", summary);
        if (closedReason != null) {
            result.put("closed", "GameActivity went away before the native side finished");
        }
        if (header != null) {
            result.put("trace", new JSONObject().put("version", header.version).put("presents", header.presents())
                    .put("frame_rate", header.framesPerSecond()).put("file", mTrace != null ? mTrace.getName() : ""));
        }
        if (mRunDir != null) {
            FileOps.writeJson(new File(mRunDir, ReplayTest.RESULT_JSON), result.toString(1));
            FileOps.writeAtomic(new File(mRunDir, ReplayTest.SUMMARY_TXT), summary);
            updateMeta(galplay, ended);
        }
        boolean firstLog;
        synchronized (mLock) {
            firstLog = !mLogged;
            mLogged = true;
            mSnapshot.finished = true;
            mSnapshot.verdict = verdict.verdict;
            mSnapshot.headline = verdict.headline;
            mSnapshot.lines = lines;
        }
        if (firstLog && mRunDir != null) {
            File root = mRunDir.getParentFile() != null ? mRunDir.getParentFile().getParentFile() : null;
            if (root != null) {
                MenuReplay.appendLog(new DataRoot(root), summary);
            }
        }
        LauncherLog.get(mContext).log("menu replay " + mRun + ": " + verdict.headline);
    }

    private void updateMeta(JSONObject galplay, String ended) {
        File file = new File(mRunDir, ReplayTest.META_JSON);
        try {
            JSONObject meta = readJson(file);
            if (meta == null) {
                meta = new JSONObject().put("schema", 1).put("kind", MenuReplay.KIND).put("run", mRun);
            }
            meta.remove(ReplayTest.IN_PROGRESS);
            meta.put("ended", ended).put("game_process", new JSONObject().put("pid", Process.myPid())
                    .put("started", mStarted).put("app_state", new AppState(mContext).describeProcess()));
            JSONObject brief = new JSONObject();
            for (String key : new String[] {"result", "exitCode", "message", "timings", "caches", "device", "trace",
                "data", "replay"}) {
                if (galplay.has(key)) {
                    brief.put(key, galplay.get(key));
                }
            }
            meta.put("galplay", brief);
            FileOps.writeJson(file, meta.toString(1));
        } catch (IOException | JSONException | RuntimeException e) {
            Log.w(TAG, "cannot update meta.json: " + e);
        }
    }

    // ------------------------------------------------------------------ text

    private String replayLine(int frames, MenuReplayVerdict.Stats stats, MenuReplayVerdict.Stats gpu,
            JSONObject timings) {
        StringBuilder out = new StringBuilder("Replay: ").append(frames).append(" frames");
        double seconds = timings != null ? timings.optDouble("replaySeconds", Double.NaN) : Double.NaN;
        if (!Double.isNaN(seconds)) {
            out.append(String.format(Locale.ROOT, " in %.1f s", seconds));
        }
        out.append(mOptions.fast ? " as fast as possible" : " at the recorded pace");
        if (stats.count > 0) {
            out.append(String.format(Locale.ROOT, "; frame time avg %.1f ms, median %.1f, p95 %.1f, max %.1f",
                    stats.mean, stats.p50, stats.p95, stats.max));
        }
        if (gpu.count > 0) {
            out.append(String.format(Locale.ROOT, "; replay work avg %.1f ms, p95 %.1f", gpu.mean, gpu.p95));
        }
        double firstFrame = timings != null ? timings.optDouble("firstFrameMs", Double.NaN) : Double.NaN;
        if (!Double.isNaN(firstFrame)) {
            out.append(String.format(Locale.ROOT, "; first frame after %.0f ms", firstFrame));
        }
        return out.toString();
    }

    /**
     * "Shaders: 31 compiled in 2410 ms, 0 from the cache · pipeline cache cold (0 KB loaded, 412 KB saved) · data
     * 820 ms, device 140 ms": the first launch against a later one (galplay.json "timings" and "caches").
     */
    private static String shaderLine(JSONObject timings, JSONObject caches) {
        if (timings == null) {
            timings = new JSONObject();
        }
        StringBuilder out = new StringBuilder();
        if (timings.has("shadersCompiled") || timings.has("shaderCompileMs") || timings.has("shaderCacheHits")) {
            out.append("Shaders: ").append(timings.optInt("shadersCompiled", 0)).append(" compiled");
            if (timings.has("shaderCompileMs")) {
                out.append(String.format(Locale.ROOT, " in %.0f ms", timings.optDouble("shaderCompileMs", 0)));
            }
            if (timings.has("shaderCacheHits")) {
                out.append(", ").append(timings.optInt("shaderCacheHits", 0)).append(" from the cache");
                if (timings.has("shaderCacheMs") && timings.optInt("shaderCacheHits", 0) > 0) {
                    out.append(String.format(Locale.ROOT, " (%.0f ms)", timings.optDouble("shaderCacheMs", 0)));
                }
            }
            if (timings.optInt("shaderCompileFailures", 0) > 0) {
                out.append(", ").append(timings.optInt("shaderCompileFailures", 0)).append(" failed");
            }
        }
        if (caches != null) {
            String pipeline;
            if (!caches.optBoolean("enabled", true)) {
                pipeline = "caches off";
            } else {
                long loaded = caches.optLong("pipelineCacheLoadedBytes", 0);
                long saved = caches.optLong("pipelineCacheSavedBytes", 0);
                pipeline = "pipeline cache " + (loaded > 0 ? "warm" : "cold") + " (" + FileOps.formatBytes(loaded)
                        + " loaded" + (saved > 0 ? ", " + FileOps.formatBytes(saved) + " saved" : "") + ")";
            }
            out.append(out.length() > 0 ? " · " : "").append(pipeline);
        } else {
            Object pipeline = timings.opt("pipelineCache");
            if (pipeline != null && pipeline != JSONObject.NULL) {
                out.append(out.length() > 0 ? " · " : "").append("pipeline cache ").append(pipeline);
            }
        }
        double dataMs = timings.optDouble("dataMs", Double.NaN);
        double deviceMs = timings.optDouble("deviceMs", Double.NaN);
        if (!Double.isNaN(dataMs)) {
            out.append(out.length() > 0 ? " · " : "").append(String.format(Locale.ROOT, "data %.0f ms", dataMs));
        }
        if (!Double.isNaN(deviceMs)) {
            out.append(out.length() > 0 ? (Double.isNaN(dataMs) ? " · " : ", ") : "")
                    .append(String.format(Locale.ROOT, "device %.0f ms", deviceMs));
        }
        return out.toString();
    }

    private static String deviceLine(JSONObject device) {
        if (device == null) {
            return "";
        }
        List<String> parts = new ArrayList<>();
        String api = device.optString("api", "");
        String adapter = device.optString("adapter", "");
        if (!api.isEmpty() || !adapter.isEmpty()) {
            parts.add(api.isEmpty() ? adapter : adapter.isEmpty() ? api : api + " · " + adapter);
        }
        String swapChain = device.optString("swapChainFormat", "");
        String transform = device.optString("preTransform", "");
        String surface = device.optString("surface", "");
        if (!swapChain.isEmpty() || !transform.isEmpty() || !surface.isEmpty()) {
            parts.add("swap chain " + swapChain + (transform.isEmpty() ? "" : ", pre-transform " + transform)
                    + (surface.isEmpty() ? "" : (swapChain.isEmpty() && transform.isEmpty() ? "" : ", ") + surface));
        }
        String depth = device.optString("depthStencil", "");
        if (!depth.isEmpty()) {
            parts.add("depth " + depth);
        }
        String bc = device.optString("bc", "");
        if (!bc.isEmpty()) {
            parts.add("BC textures " + ("cpu".equalsIgnoreCase(bc) ? "decoded on the CPU" : "sampled by the GPU")
                    + (device.optBoolean("bcForced") ? " (forced)" : ""));
        }
        return parts.isEmpty() ? "" : "Device: " + String.join(" · ", parts);
    }

    private static JSONObject frameJson(MenuReplayVerdict.Frame frame) throws JSONException {
        JSONObject json = new JSONObject().put("frame", frame.frame).put("verdict", frame.verdict)
                .put("hash", frame.hash).put("reference", frame.reference);
        if (frame.width > 0) {
            json.put("width", frame.width).put("height", frame.height).put("alpha_opaque", frame.opaque);
        }
        if (!frame.nativeHash.isEmpty()) {
            json.put("native_hash", frame.nativeHash);
        }
        if (!frame.nativeReference.isEmpty()) {
            json.put("native_reference", frame.nativeReference);
        }
        if (!frame.nativeVerdict.isEmpty()) {
            json.put("native_verdict", frame.nativeVerdict);
        }
        if (frame.identicalToRecording >= 0) {
            json.put("identical_to_recording", frame.identicalToRecording == 1);
        }
        if (!frame.png.isEmpty()) {
            json.put("png", frame.png);
        }
        if (!frame.bmp.isEmpty()) {
            json.put("bmp", frame.bmp).put("bmp_written_by", frame.bmpBy);
        }
        if (!frame.error.isEmpty()) {
            json.put("error", frame.error);
        }
        return json;
    }

    private static JSONObject statsJson(MenuReplayVerdict.Stats stats) throws JSONException {
        return new JSONObject().put("count", stats.count).put("mean", MenuReplayVerdict.Stats.round(stats.mean))
                .put("p50", MenuReplayVerdict.Stats.round(stats.p50))
                .put("p95", MenuReplayVerdict.Stats.round(stats.p95))
                .put("max", MenuReplayVerdict.Stats.round(stats.max));
    }

    // ------------------------------------------------------------------ helpers

    private static JSONObject errorResult(String message) {
        JSONObject json = new JSONObject();
        try {
            json.put("result", "ERROR").put("message", message);
        } catch (JSONException ignored) {
            // constant keys and a string
        }
        return json;
    }

    private static JSONObject readJson(File file) {
        try {
            String text = FileOps.readText(file, 8 * 1024 * 1024);
            return text != null ? new JSONObject(text) : null;
        } catch (IOException | JSONException e) {
            return null;
        }
    }

    private MenuReplayVerdict.Frame frameLocked(int frame) {
        MenuReplayVerdict.Frame entry = mFrames.get(frame);
        if (entry == null) {
            entry = new MenuReplayVerdict.Frame(frame);
            if (mHeader != null) {
                String reference = mHeader.vulkanReferences().get(frame);
                entry.reference = reference != null ? reference : "";
            }
            mFrames.put(frame, entry);
        }
        return entry;
    }

    private void changed() {
        Listener listener;
        synchronized (mLock) {
            listener = mListener;
        }
        if (listener != null) {
            listener.onChanged();
        }
    }

    private static Snapshot copy(Snapshot from) {
        Snapshot to = new Snapshot();
        to.stage = from.stage;
        to.message = from.message;
        to.frame = from.frame;
        to.frames = from.frames;
        to.frameMs = from.frameMs;
        to.avgFrameMs = from.avgFrameMs;
        to.gpuFrameMs = from.gpuFrameMs;
        to.fast = from.fast;
        to.readbacks = new TreeMap<>(from.readbacks);
        to.finished = from.finished;
        to.verdict = from.verdict;
        to.headline = from.headline;
        to.lines = new ArrayList<>(from.lines);
        return to;
    }
}
