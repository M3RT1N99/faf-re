package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;
import android.os.Build;
import android.os.PerformanceHintManager;
import android.os.PowerManager;
import android.system.Os;
import android.system.OsConstants;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeSet;

/**
 * Where the headless simulation ran: once a second while a replay plays, each of its threads' CPU
 * time and last CPU (/proc/PID/task/TID/stat), that CPU's clock and clock cap (cpufreq
 * scaling_cur_freq and scaling_max_freq), Android's thermal status, the battery's temperature and
 * charger, the screen and whether the app was in front. Beats per second alone cannot tell a slow
 * core from a capped clock: in 0.4.1's runs a Galaxy S22 Ultra's X2 core ran at 1.5 to 1.9 of its
 * 2.8 GHz. If asked, it also holds an Android performance hint session for the simulation thread
 * (hintLoop) and records whether the phone granted it.
 */
final class SpeedSampler {
    private static final String[] THERMAL = {"none", "light", "moderate", "severe", "critical", "emergency", "shutdown"};

    /** The cores by rated clock (cpuinfo_max_freq): little, mid and big on a three-cluster phone. */
    static final class Cores {
        final int[] classOf;
        final List<String> names = new ArrayList<>();
        final List<Integer> ratedMhz = new ArrayList<>();
        final List<Integer> counts = new ArrayList<>();

        Cores() {
            List<Integer> rated = new ArrayList<>();
            for (int cpu = 0; ; ++cpu) {
                File dir = new File("/sys/devices/system/cpu/cpu" + cpu);
                if (!dir.isDirectory()) {
                    break;
                }
                rated.add(readInt(new File(dir, "cpufreq/cpuinfo_max_freq")) / 1000);
            }
            TreeSet<Integer> distinct = new TreeSet<>(rated);
            List<Integer> order = new ArrayList<>(distinct);
            String[] labels = order.size() == 1 ? new String[] {"all"}
                : order.size() == 2 ? new String[] {"little", "big"}
                : order.size() == 3 ? new String[] {"little", "mid", "big"} : null;
            for (int i = 0; i < order.size(); ++i) {
                names.add(labels != null ? labels[i] : "class" + i);
                ratedMhz.add(order.get(i));
                counts.add(0);
            }
            classOf = new int[rated.size()];
            for (int cpu = 0; cpu < rated.size(); ++cpu) {
                classOf[cpu] = order.indexOf(rated.get(cpu));
                counts.set(classOf[cpu], counts.get(classOf[cpu]) + 1);
            }
        }

        int classOf(int cpu) {
            return cpu >= 0 && cpu < classOf.length ? classOf[cpu] : -1;
        }

        /** "4 little 1.82 GHz, 3 mid 2.52 GHz, 1 big 2.80 GHz". */
        String describe() {
            StringBuilder out = new StringBuilder();
            for (int i = 0; i < names.size(); ++i) {
                out.append(i == 0 ? "" : ", ").append(counts.get(i)).append(' ').append(names.get(i))
                    .append(String.format(Locale.ROOT, " %.2f GHz", ratedMhz.get(i) / 1000.0));
            }
            return out.toString();
        }
    }

    /** What one replay's run showed, for its card line and the results' summary.json. */
    static final class Summary {
        String thread = "";
        double seconds;
        double cpuSeconds;
        final Map<String, Double> classShare = new HashMap<>();
        List<String> classOrder = new ArrayList<>();
        double averageMhz;
        int lowestCapMhz;
        int ratedMhz;
        String thermal = "none";
        int screenOffSeconds;
        int hiddenSeconds;
        double batteryStartC = Double.NaN;
        double batteryMaxC = Double.NaN;
        int chargingSeconds;
        /** The performance hint: off, requested, on, or why not. */
        String hint = "off";

        double busy() {
            return seconds > 0 ? cpuSeconds / seconds : 0;
        }

        /** "sim thread 96% busy: big 82%, mid 18%; 2.31 GHz (capped at 1.92 of 2.80); thermal light; battery 31 to 38 °C". */
        String line() {
            if (seconds <= 0) {
                return "";
            }
            StringBuilder out = new StringBuilder();
            out.append(String.format(Locale.ROOT, "sim thread %.0f%% busy", 100 * busy()));
            String separator = ": ";
            for (String name : classOrder.size() > 1 ? classOrder : new ArrayList<String>()) {
                Double share = classShare.get(name);
                if (share != null && share >= 0.005) {
                    out.append(separator).append(name).append(String.format(Locale.ROOT, " %.0f%%", 100 * share));
                    separator = ", ";
                }
            }
            if (averageMhz > 0) {
                out.append(String.format(Locale.ROOT, "; %.2f GHz", averageMhz / 1000));
                if (ratedMhz > 0) {
                    out.append(lowestCapMhz > 0 && lowestCapMhz < ratedMhz
                        ? String.format(Locale.ROOT, " (capped at %.2f of %.2f)", lowestCapMhz / 1000.0, ratedMhz / 1000.0)
                        : String.format(Locale.ROOT, " (of %.2f)", ratedMhz / 1000.0));
                }
            }
            out.append("; thermal ").append(thermal);
            out.append("; performance hint ").append(hint);
            if (!Double.isNaN(batteryStartC)) {
                out.append(String.format(Locale.ROOT, "; battery %.0f to %.0f °C", batteryStartC, batteryMaxC));
            }
            if (chargingSeconds > 0) {
                out.append("; charging ").append(chargingSeconds).append(" s");
            }
            if (screenOffSeconds > 0) {
                out.append("; screen off ").append(screenOffSeconds).append(" s");
            }
            if (hiddenSeconds > 0) {
                out.append("; app not in front ").append(hiddenSeconds).append(" s");
            }
            return out.toString();
        }
    }

    private static final class Task {
        String name = "";
        long ticks;
        long[] classTicks;
        double mhzTicks;
        long mhzWeight;
        int lowestCapMhz;
        int ratedMhz;
    }

    /** The performance hint's reporting window and target: a busy thread reports twice its target. */
    private static final long HINT_WINDOW_MS = 100;
    private static final long HINT_TARGET_NS = 50_000_000L;

    private final Context mContext;
    private final String mProgram;
    private final TestState.Replay mReplay;
    private final File mCsv;
    private final Cores mCores = new Cores();
    private final Map<Integer, Task> mTasks = new HashMap<>();
    private final Summary mSummary = new Summary();
    private final Thread mThread;
    private final Thread mHintThread;
    private volatile boolean mStopped;
    private volatile int mPid = -1;
    private volatile int mBusiestTid = -1;

    private SpeedSampler(Context context, String program, TestState.Replay replay, File csv, boolean hint) {
        mContext = context;
        mProgram = program;
        mReplay = replay;
        mCsv = csv;
        mThread = new Thread(this::loop, "faf-re speed sampler");
        mThread.setDaemon(true);
        mHintThread = hint ? new Thread(this::hintLoop, "faf-re performance hint") : null;
        if (mHintThread != null) {
            mHintThread.setDaemon(true);
        }
        mSummary.hint = hint ? "requested" : "off";
    }

    static Cores cores() {
        return new Cores();
    }

    /**
     * Starts sampling the child process running {@code program}; its rows go to {@code csv}. With
     * {@code hint}, the simulation thread also gets an Android performance hint session.
     */
    static SpeedSampler start(Context context, String program, TestState.Replay replay, File csv, boolean hint) {
        SpeedSampler sampler = new SpeedSampler(context, program, replay, csv, hint);
        sampler.mThread.start();
        if (sampler.mHintThread != null) {
            sampler.mHintThread.start();
        }
        return sampler;
    }

    /** Stops sampling and gives the busiest thread's (the simulation's) figures. */
    Summary stop() {
        mStopped = true;
        mThread.interrupt();
        if (mHintThread != null) {
            mHintThread.interrupt();
        }
        try {
            mThread.join(5000);
            if (mHintThread != null) {
                mHintThread.join(5000);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        synchronized (this) {
            return mSummary;
        }
    }

    /**
     * Android's performance hint (ADPF, Android 12 and later) for the simulation thread, the one
     * a game would use: every 100 ms the thread's run time (schedstat) is reported against a 50 ms
     * target, so the system sees it behind and may raise its clock. Whether a phone does, and
     * whether it lets an app hint the thread of a process it started, is what this measures.
     */
    private void hintLoop() {
        if (Build.VERSION.SDK_INT < 31) {
            hint("off: needs Android 12");
            return;
        }
        PerformanceHintManager manager = mContext.getSystemService(PerformanceHintManager.class);
        if (manager == null || manager.getPreferredUpdateRateNanos() < 0) {
            hint("not supported by this phone");
            return;
        }
        int tid;
        while ((tid = mBusiestTid) <= 0) {
            if (!pause(200)) {
                return;
            }
        }
        PerformanceHintManager.Session session;
        try {
            session = manager.createHintSession(new int[] {tid}, HINT_TARGET_NS);
        } catch (RuntimeException e) {
            hint("refused: " + e.getMessage());
            return;
        }
        if (session == null) {
            hint("not supported by this phone (no session)");
            return;
        }
        hint("on");
        File schedstat = new File("/proc/" + mPid + "/task/" + tid + "/schedstat");
        long last = firstLong(readString(schedstat));
        try {
            while (pause(HINT_WINDOW_MS)) {
                long run = firstLong(readString(schedstat));
                if (run <= 0) {
                    break; // the thread ended
                }
                if (run > last) {
                    session.reportActualWorkDuration(run - last);
                }
                last = run;
            }
        } catch (RuntimeException e) {
            hint("stopped: " + e.getMessage());
        } finally {
            session.close();
        }
    }

    private void hint(String state) {
        synchronized (this) {
            mSummary.hint = state;
        }
    }

    /** Sleeps; false once the sampler is stopped. */
    private boolean pause(long ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException e) {
            return false;
        }
        return !mStopped;
    }

    private static long firstLong(String text) {
        int end = text.indexOf(' ');
        try {
            return Long.parseLong(end < 0 ? text : text.substring(0, end));
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private void loop() {
        long ticksPerSecond = Os.sysconf(OsConstants._SC_CLK_TCK);
        if (ticksPerSecond <= 0) {
            ticksPerSecond = 100;
        }
        PowerManager power = mContext.getSystemService(PowerManager.class);
        int pid = -1;
        try (OutputStream csv = new FileOutputStream(mCsv)) {
            csv.write("t_s,beat,thread,cpu_share,cpu,class,mhz,cap_mhz,thermal,battery_c,charging,screen_on,app_in_front\n"
                .getBytes(StandardCharsets.US_ASCII));
            long started = System.nanoTime();
            long previous = started;
            int maxThermal = 0;
            while (!mStopped) {
                try {
                    Thread.sleep(1000);
                } catch (InterruptedException e) {
                    break;
                }
                long now = System.nanoTime();
                double interval = (now - previous) / 1e9;
                previous = now;
                if (pid < 0 && (pid = findChild(mProgram)) < 0) {
                    continue;
                }
                mPid = pid;
                int thermal = Build.VERSION.SDK_INT >= 29 ? power.getCurrentThermalStatus() : -1;
                boolean screenOn = power.isInteractive();
                boolean inFront = TestState.visible();
                // The battery's temperature: how warm the phone is (the sticky broadcast; nothing registers).
                Intent battery = mContext.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
                int batteryTenths = battery != null ? battery.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Integer.MIN_VALUE) : Integer.MIN_VALUE;
                double batteryC = batteryTenths != Integer.MIN_VALUE ? batteryTenths / 10.0 : Double.NaN;
                boolean charging = battery != null && battery.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0;
                Task top = null;
                long topDelta = 0;
                int topCpu = -1;
                int topMhz = 0;
                int topCap = 0;
                synchronized (this) {
                    File[] tasks = new File("/proc/" + pid + "/task").listFiles();
                    if (tasks == null) {
                        continue; // the process ended
                    }
                    boolean baseline = mTasks.isEmpty();
                    for (File taskDir : tasks) {
                        String stat = readString(new File(taskDir, "stat"));
                        int close = stat.lastIndexOf(')');
                        if (close < 0) {
                            continue;
                        }
                        String[] fields = stat.substring(close + 2).split(" ");
                        if (fields.length < 37) {
                            continue;
                        }
                        int tid = Integer.parseInt(taskDir.getName());
                        long ticks = Long.parseLong(fields[11]) + Long.parseLong(fields[12]); // utime, stime
                        int cpu = Integer.parseInt(fields[36]); // processor
                        Task task = mTasks.get(tid);
                        if (task == null) {
                            // The first sight is the baseline: the time before it is not sampled.
                            task = new Task();
                            task.name = stat.substring(stat.indexOf('(') + 1, close);
                            task.classTicks = new long[mCores.names.size()];
                            task.ticks = ticks;
                            mTasks.put(tid, task);
                            continue;
                        }
                        long delta = ticks - task.ticks;
                        task.ticks = ticks;
                        if (delta <= 0) {
                            continue;
                        }
                        int coreClass = mCores.classOf(cpu);
                        if (coreClass >= 0) {
                            task.classTicks[coreClass] += delta;
                        }
                        int mhz = readInt(new File("/sys/devices/system/cpu/cpu" + cpu + "/cpufreq/scaling_cur_freq")) / 1000;
                        int cap = readInt(new File("/sys/devices/system/cpu/cpu" + cpu + "/cpufreq/scaling_max_freq")) / 1000;
                        if (mhz > 0) {
                            task.mhzTicks += (double) mhz * delta;
                            task.mhzWeight += delta;
                        }
                        if (cap > 0 && (task.lowestCapMhz == 0 || cap < task.lowestCapMhz)) {
                            task.lowestCapMhz = cap;
                        }
                        if (coreClass >= 0) {
                            task.ratedMhz = Math.max(task.ratedMhz, mCores.ratedMhz.get(coreClass));
                        }
                        if (delta > topDelta) {
                            top = task;
                            topDelta = delta;
                            topCpu = cpu;
                            topMhz = mhz;
                            topCap = cap;
                        }
                    }
                    if (baseline) {
                        continue;
                    }
                    maxThermal = Math.max(maxThermal, thermal);
                    mSummary.seconds += interval;
                    if (!screenOn) {
                        ++mSummary.screenOffSeconds;
                    }
                    if (!inFront) {
                        ++mSummary.hiddenSeconds;
                    }
                    if (charging) {
                        ++mSummary.chargingSeconds;
                    }
                    if (!Double.isNaN(batteryC)) {
                        if (Double.isNaN(mSummary.batteryStartC)) {
                            mSummary.batteryStartC = batteryC;
                        }
                        mSummary.batteryMaxC = Double.isNaN(mSummary.batteryMaxC) ? batteryC : Math.max(mSummary.batteryMaxC, batteryC);
                    }
                    summarize(ticksPerSecond, maxThermal);
                }
                int coreClass = mCores.classOf(topCpu);
                String row = String.format(Locale.ROOT, "%.0f,%d,%s,%.2f,%d,%s,%d,%d,%s,%s,%d,%d,%d\n",
                    (now - started) / 1e9, mReplay.beat, top != null ? top.name.replace(',', ' ') : "",
                    topDelta / (double) ticksPerSecond / interval, topCpu, coreClass >= 0 ? mCores.names.get(coreClass) : "",
                    topMhz, topCap, thermal >= 0 && thermal < THERMAL.length ? THERMAL[thermal] : "",
                    Double.isNaN(batteryC) ? "" : String.format(Locale.ROOT, "%.1f", batteryC), charging ? 1 : 0,
                    screenOn ? 1 : 0, inFront ? 1 : 0);
                csv.write(row.getBytes(StandardCharsets.US_ASCII));
            }
        } catch (IOException | RuntimeException e) {
            TestState.line("speed sampler: " + e);
        }
    }

    /** The busiest thread so far is the simulation's; with one sim worker it carries the game. */
    private void summarize(long ticksPerSecond, int maxThermal) {
        Task busiest = null;
        int busiestTid = -1;
        for (Map.Entry<Integer, Task> entry : mTasks.entrySet()) {
            if (busiest == null || entry.getValue().ticks > busiest.ticks) {
                busiest = entry.getValue();
                busiestTid = entry.getKey();
            }
        }
        if (busiest == null) {
            return;
        }
        if (mSummary.seconds >= 2 && mBusiestTid < 0) {
            mBusiestTid = busiestTid; // the performance hint's thread, once loading has shown which it is
        }
        long classified = 0;
        for (long ticks : busiest.classTicks) {
            classified += ticks;
        }
        mSummary.thread = busiest.name;
        mSummary.cpuSeconds = busiest.ticks / (double) ticksPerSecond;
        mSummary.classOrder = new ArrayList<>(mCores.names);
        Collections.reverse(mSummary.classOrder); // big first
        for (int i = 0; i < mCores.names.size(); ++i) {
            mSummary.classShare.put(mCores.names.get(i), classified > 0 ? busiest.classTicks[i] / (double) classified : 0);
        }
        mSummary.averageMhz = busiest.mhzWeight > 0 ? busiest.mhzTicks / busiest.mhzWeight : 0;
        mSummary.lowestCapMhz = busiest.lowestCapMhz;
        mSummary.ratedMhz = busiest.ratedMhz;
        mSummary.thermal = maxThermal >= 0 && maxThermal < THERMAL.length ? THERMAL[maxThermal] : "unknown";
    }

    /** The child process this app started for {@code program} (the parent is this process). */
    private static int findChild(String program) {
        int self = android.os.Process.myPid();
        File[] entries = new File("/proc").listFiles();
        if (entries == null) {
            return -1;
        }
        for (File entry : entries) {
            String name = entry.getName();
            if (name.isEmpty() || !Character.isDigit(name.charAt(0))) {
                continue;
            }
            String stat = readString(new File(entry, "stat"));
            int close = stat.lastIndexOf(')');
            if (close < 0) {
                continue;
            }
            String[] fields = stat.substring(close + 2).split(" ");
            if (fields.length < 2 || !String.valueOf(self).equals(fields[1])) {
                continue;
            }
            if (readString(new File(entry, "cmdline")).startsWith(program)) {
                return Integer.parseInt(name);
            }
        }
        return -1;
    }

    private static String readString(File file) {
        try {
            return new String(Files.readAllBytes(file.toPath()), StandardCharsets.ISO_8859_1).trim();
        } catch (IOException | RuntimeException e) {
            return "";
        }
    }

    private static int readInt(File file) {
        try {
            return Integer.parseInt(readString(file));
        } catch (NumberFormatException e) {
            return 0;
        }
    }
}
