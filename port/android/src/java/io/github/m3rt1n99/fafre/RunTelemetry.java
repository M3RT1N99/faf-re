package io.github.m3rt1n99.fafre;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.Deque;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/**
 * Speed telemetry of one runner process (release 0.4.1): where its threads ran, how long they ran,
 * waited for a core or slept, and in which scheduling environment, so a slow run can be told apart as
 * "the sim thread sat on a little core", "it was CPU-bound on the big core" or "it mostly waited".
 *
 * <p>A thread of the app samples the runner, a child of the app process found in /proc by its parent
 * pid and executable:
 * <ul>
 * <li>once: /proc/&lt;pid&gt;/cpuset, cgroup, status (Cpus_allowed_list), timerslack_ns (the kernel shows
 * another process's timer slack only with CAP_SYS_NICE, so this usually reads as unreadable or empty;
 * the launching thread's own slack, which the child inherits at fork, is recorded by the caller), stat
 * (scheduling policy, nice, priority) and oom_score_adj;</li>
 * <li>every {@link #FAST_MS} ms, per thread: task/&lt;tid&gt;/schedstat (ns on a CPU and ns runnable on a run
 * queue) and stat (name, state, utime, stime and the CPU it last ran on). The run time gained since the
 * previous sample is credited to the class of the CPU the thread is on now, so a thread that migrates
 * within one interval is credited to where it ended up;</li>
 * <li>every {@link #FULL_MS} ms: one recorded sample with each thread's totals (utime, stime, schedstat),
 * its Cpus_allowed_list and context switches (status), its migrations (sched, where the kernel has
 * it), the CPUs' current frequencies, the process's CPU time, and every {@link #APP_MS} ms the app's
 * own state (importance, screen, battery saver, thermal status: {@link AppProbe}).</li>
 * </ul>
 *
 * <p>The verdict looks at the busiest thread (by CPU time; in threaded mode the sim thread) during the
 * sim phase (the replay's "sim created" to its RESULT line), or the whole run up to the RESULT line when
 * the sim phase was shorter than three seconds. What follows the RESULT line (the "ending" phase: the
 * runner's shutdown, which before release 0.4.1's fast exit took seconds on the main thread) is sampled
 * and recorded but belongs to neither window.
 *
 * <p>Next to the verdict: the busiest thread's effective clock (each interval's run time weighted with
 * scaling_cur_freq of the CPU it ran on, read every {@link #FAST_MS} ms, against cpuinfo_max_freq), its
 * migrations per second (se.nr_migrations) as a confidence hint for the core-class split (each interval's
 * run time goes to the CPU at its end, so every migration can misplace up to {@link #FAST_MS} ms), and the
 * Cpus_allowed_list it had during the window: what really held, whoever set it (the runner's experiment,
 * FAF's init_faf.lua, Android).
 *
 * <p>Pure Java apart from the {@link AppProbe} the caller passes, so it runs on a host JVM against a copied
 * /proc tree.
 */
final class RunTelemetry {
    /** What the observed step is doing, for splitting the sim phase from loading and teardown. */
    interface Source {
        boolean simulating();

        int beat();

        String phase();
    }

    /** The app's own process state; called on the sampler thread, must not throw, may return null. */
    interface AppProbe {
        JSONObject sample();
    }

    static final long FAST_MS = 200;
    static final long FULL_MS = 1000;
    static final long APP_MS = 5000;
    /** The sim phase must be at least this long for the verdict to use it instead of the whole run. */
    static final long MIN_SIM_NS = 3_000_000_000L;
    /**
     * The core-class split is marked approximate when the busiest thread's migrations times {@link #FAST_MS}
     * (the most each can misplace) exceed this share of its run time.
     */
    static final double MIGRATION_DOUBT = 0.2;
    static final double CPU_BOUND = 0.6;
    static final double STARVED = 0.25;
    /** A core class "holds" the thread when it got at least this share of its CPU time; below: "spread". */
    static final double DOMINANT = 0.6;
    private static final int HEAD_SAMPLES = 600;
    private static final int TAIL_SAMPLES = 300;
    private static final int MAX_THREADS_LISTED = 12;
    private static final int ALL = 0;
    private static final int SIM = 1;

    /** Per-thread accumulators, index ALL or SIM. */
    static final class ThreadAcc {
        final int tid;
        String name = "";
        char state = '?';
        int cpu = -1;
        int nice;
        int policy;
        boolean seen;
        long lastRun;
        long lastWait;
        long lastTicks;
        /** utime and stime in clock ticks, as /proc/<pid>/task/<tid>/stat last said. */
        long utime;
        long stime;
        long lastVcsw = -1;
        long lastNvcsw = -1;
        long lastMigrations = -1;
        final long[] runNs = new long[2];
        final long[] waitNs = new long[2];
        final long[] ticks = new long[2];
        final long[] vcsw = new long[2];
        final long[] nvcsw = new long[2];
        final long[] migrations = {-1, -1};
        final List<Map<String, Long>> classNs = Arrays.<Map<String, Long>>asList(new TreeMap<String, Long>(),
                new TreeMap<String, Long>());
        final List<Map<Integer, Long>> cpuNs = Arrays.<Map<Integer, Long>>asList(new TreeMap<Integer, Long>(),
                new TreeMap<Integer, Long>());
        /** Cpus_allowed_list values seen, in order. */
        final Set<String> allowed = new LinkedHashSet<>();
        String allowedSim;
        /** Cpus_allowed_list values seen in each window (ALL: the run up to the RESULT line), in order. */
        final List<Set<String>> allowedIn = Arrays.<Set<String>>asList(new LinkedHashSet<String>(),
                new LinkedHashSet<String>());
        /** Run ns times the kHz (scaling_cur_freq) of the CPU it was credited to, and the ns with a known kHz. */
        final double[] khzNs = new double[2];
        final long[] khzRunNs = new long[2];
        /** The same with that CPU's cpuinfo_max_freq. */
        final double[] maxKhzNs = new double[2];
        final long[] maxKhzRunNs = new long[2];

        ThreadAcc(int tid) {
            this.tid = tid;
        }

        void credit(int window, long run, long wait, long tickDelta, String cls, int onCpu) {
            runNs[window] += run;
            waitNs[window] += wait;
            ticks[window] += tickDelta;
            if (run > 0) {
                Long have = classNs.get(window).get(cls);
                classNs.get(window).put(cls, (have != null ? have : 0L) + run);
                Long onThis = cpuNs.get(window).get(onCpu);
                cpuNs.get(window).put(onCpu, (onThis != null ? onThis : 0L) + run);
            }
        }

        void creditClock(int window, long run, long khz, long maxKhz) {
            if (run <= 0) {
                return;
            }
            if (khz > 0) {
                khzNs[window] += (double) run * khz;
                khzRunNs[window] += run;
            }
            if (maxKhz > 0) {
                maxKhzNs[window] += (double) run * maxKhz;
                maxKhzRunNs[window] += run;
            }
        }

        double share(int window, String cls) {
            Long ns = classNs.get(window).get(cls);
            return runNs[window] > 0 && ns != null ? (double) ns / runNs[window] : 0;
        }
    }

    private final File mProc;
    private final File mSysCpu;
    private final CpuTopology mTopology;
    private final int mParentPid;
    private final String mExecutable;
    private final Source mSource;
    private final AppProbe mApp;
    private final long mNsPerTick;
    private final Object mLock = new Object();
    private Thread mThread;
    private boolean mStop;

    // Sampler state; written by the sampler thread, read by stop() after the join.
    private int mPid;
    private final long mStartNs = System.nanoTime();
    private long mFoundNs = -1;
    private JSONObject mProcess;
    private final Map<Integer, ThreadAcc> mThreads = new LinkedHashMap<>();
    private long mLastFastNs = -1;
    private boolean mLastSim;
    private long mLastFullNs = -1;
    private long mLastAppNs = -1;
    private final long[] mWallNs = new long[2];
    private int mFastSamples;
    private int mFullSamples;
    private long mProcTicksFirst = -1;
    private long mProcTicksLast = -1;
    private long mProcTicksSim;
    private long mRssMaxKb;
    private long mPageKb = 4;
    private boolean mStatusReadable = true;
    private boolean mSchedReadable = true;
    private boolean mFreqReadable = true;
    private final Map<Integer, long[]> mFreqSim = new TreeMap<>();
    /** scaling_cur_freq per CPU as read during the current fast sample (cleared at its start). */
    private final Map<Integer, Long> mFreqNow = new TreeMap<>();
    private final List<JSONObject> mHead = new ArrayList<>();
    private final Deque<JSONObject> mTail = new ArrayDeque<>();
    private int mDropped;
    private final List<JSONObject> mAppSamples = new ArrayList<>();
    private String mEnded = "";
    private String mError;

    /**
     * @param root "/" on the device (a copied tree on a host): /proc and /sys/devices/system/cpu are read below it
     * @param parentPid the app process, whose child the runner is
     * @param executable the runner's path, as its argv[0]
     */
    RunTelemetry(File root, CpuTopology topology, int parentPid, String executable, Source source, AppProbe app,
            long clockTicksPerSecond) {
        mProc = new File(root, "proc");
        mSysCpu = new File(root, "sys/devices/system/cpu");
        mTopology = topology;
        mParentPid = parentPid;
        mExecutable = executable;
        mSource = source;
        mApp = app;
        mNsPerTick = clockTicksPerSecond > 0 ? 1_000_000_000L / clockTicksPerSecond : 10_000_000L;
    }

    /** The page size, for the RSS in the samples (stat counts pages; 16 KB on some Android 15+ devices). */
    void setPageSize(long bytes) {
        if (bytes >= 1024) {
            mPageKb = bytes / 1024;
        }
    }

    // ---------------------------------------------------------------- thread

    void start() {
        mThread = new Thread(this::loop, "fafre-telemetry");
        mThread.setDaemon(true);
        mThread.start();
    }

    /** Stops the sampler and waits for it; then {@link #summary()} and {@link #details()} are final. */
    void stop() {
        synchronized (mLock) {
            mStop = true;
            mLock.notifyAll();
        }
        if (mThread != null) {
            try {
                mThread.join(3000);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }

    private void loop() {
        try {
            while (true) {
                long now = System.nanoTime();
                if (mPid == 0) {
                    findRunner(now);
                }
                if (mPid != 0 && mEnded.isEmpty()) {
                    sample(now);
                }
                synchronized (mLock) {
                    if (mStop) {
                        break;
                    }
                    long wait = mPid == 0 ? 50 : FAST_MS;
                    mLock.wait(wait);
                    if (mStop) {
                        break;
                    }
                }
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } catch (RuntimeException e) {
            // The telemetry must never break the test; what it had is kept.
            mError = e.toString();
        }
    }

    /**
     * The newest child of the app process that runs the runner: its cmdline's program ({@link
     * ProcFiles#programOf}: argv[0], or argv[1] under the emulator's ARM translation) is the runner's path,
     * or, when the cmdline is unreadable, its comm is the runner's file name (cut to 15 characters).
     */
    private void findRunner(long now) {
        int best = 0;
        long bestStart = -1;
        String name = new File(mExecutable).getName();
        String comm = name.length() > 15 ? name.substring(0, 15) : name;
        for (int pid : ProcFiles.numericEntries(mProc)) {
            ProcFiles.Stat stat = ProcFiles.Stat.parse(ProcFiles.read(new File(mProc, pid + "/stat"), 4096));
            if (stat == null || stat.ppid != mParentPid) {
                continue;
            }
            String program = ProcFiles.programOf(new File(mProc, pid + "/cmdline"));
            boolean match = program != null ? program.equals(mExecutable) || program.endsWith("/" + name)
                    : comm.equals(stat.comm);
            if (!match) {
                continue;
            }
            if (stat.startTime > bestStart) {
                best = pid;
                bestStart = stat.startTime;
            }
        }
        if (best != 0) {
            mPid = best;
            mFoundNs = now;
            mProcess = describeProcess(best, now);
        }
    }

    private JSONObject describeProcess(int pid, long now) {
        JSONObject json = new JSONObject();
        File dir = new File(mProc, String.valueOf(pid));
        try {
            json.put("pid", pid).put("found_after_ms", (now - mStartNs) / 1_000_000);
            ProcFiles.Stat stat = ProcFiles.Stat.parse(ProcFiles.read(new File(dir, "stat"), 4096));
            if (stat != null) {
                json.put("comm", stat.comm).put("policy", ProcFiles.policyName(stat.policy)).put("nice", stat.nice)
                        .put("priority", stat.priority).put("rt_priority", stat.rtPriority);
            }
            json.put("cpuset", ProcFiles.describe(new File(dir, "cpuset")));
            String cgroup = ProcFiles.describe(new File(dir, "cgroup"));
            json.put("cgroup", cgroup.length() > 1024 ? cgroup.substring(0, 1024) + "…" : cgroup);
            String status = ProcFiles.read(new File(dir, "status"));
            json.put("cpus_allowed_list", status != null ? String.valueOf(ProcFiles.statusValue(status,
                    "Cpus_allowed_list")) : "unreadable");
            json.put("timerslack_ns", ProcFiles.describe(new File(dir, "timerslack_ns")));
            json.put("oom_score_adj", ProcFiles.describe(new File(dir, "oom_score_adj")));
        } catch (JSONException e) {
            mError = e.toString();
        }
        return json;
    }

    /** True from the replay's RESULT line on (ReplayOutput's ENDING and DONE phases). */
    static boolean isEnding(String phase) {
        return "ending".equals(phase) || "done".equals(phase);
    }

    private void sample(long now) {
        boolean sim = mSource != null && mSource.simulating();
        boolean intervalSim = sim && mLastSim;
        // The run window stops at the RESULT line: what follows is the runner's shutdown.
        boolean intervalRun = mSource == null || !isEnding(mSource.phase());
        mFreqNow.clear();
        File dir = new File(mProc, String.valueOf(mPid));
        File tasks = new File(dir, "task");
        List<Integer> tids = ProcFiles.numericEntries(tasks);
        if (tids.isEmpty()) {
            mEnded = "the runner ended (" + ((now - mStartNs) / 1_000_000) + " ms after the sampler started)";
            return;
        }
        boolean first = mFastSamples == 0;
        if (mLastFastNs >= 0) {
            long dt = now - mLastFastNs;
            if (intervalRun) {
                mWallNs[ALL] += dt;
            }
            if (intervalSim) {
                mWallNs[SIM] += dt;
            }
        }
        for (int tid : tids) {
            File task = new File(tasks, String.valueOf(tid));
            ProcFiles.Stat stat = ProcFiles.Stat.parse(ProcFiles.read(new File(task, "stat"), 4096));
            if (stat == null) {
                continue;
            }
            long[] schedstat = ProcFiles.parseSchedstat(ProcFiles.read(new File(task, "schedstat"), 256));
            observe(tid, stat.comm, stat.state, stat.processor, stat.nice, stat.policy,
                    schedstat != null ? schedstat[0] : -1, schedstat != null ? schedstat[1] : -1, stat.ticks(),
                    intervalRun, intervalSim, first);
            ThreadAcc acc = mThreads.get(tid);
            acc.utime = stat.utime;
            acc.stime = stat.stime;
        }
        mLastFastNs = now;
        mLastSim = sim;
        ++mFastSamples;
        if (mLastFullNs < 0 || now - mLastFullNs >= FULL_MS * 1_000_000L - FAST_MS * 500_000L) {
            fullSample(now, dir, tasks, tids, intervalRun, intervalSim);
        }
    }

    /** The CPU's scaling_cur_freq in kHz, read once per fast sample; -1 when unreadable. */
    private long currentKhz(int cpu) {
        if (!mFreqReadable || cpu < 0) {
            return -1;
        }
        Long cached = mFreqNow.get(cpu);
        if (cached == null) {
            cached = ProcFiles.readLong(new File(mSysCpu, "cpu" + cpu + "/cpufreq/scaling_cur_freq"), -1);
            mFreqNow.put(cpu, cached);
        }
        return cached;
    }

    /**
     * Credits one thread's run time since its previous observation to the class of the CPU it is on now.
     * {@code runNs} or {@code waitNs} below 0 mean schedstat was unreadable: the run time then comes from
     * utime + stime (clock ticks) and the queue wait is unknown.
     */
    void observe(int tid, String comm, char state, int cpu, int nice, int policy, long runNs, long waitNs,
            long ticks, boolean sim, boolean firstSample) {
        observe(tid, comm, state, cpu, nice, policy, runNs, waitNs, ticks, true, sim, firstSample);
    }

    /** As above; {@code run}: the interval belongs to the run window (it ended before the RESULT line). */
    void observe(int tid, String comm, char state, int cpu, int nice, int policy, long runNs, long waitNs,
            long ticks, boolean run, boolean sim, boolean firstSample) {
        ThreadAcc acc = mThreads.get(tid);
        if (acc == null) {
            acc = new ThreadAcc(tid);
            mThreads.put(tid, acc);
        }
        // schedstat reads "0 0 0" when the kernel keeps no sched_info; the clock ticks still count then.
        boolean schedstat = runNs > 0 || (runNs == 0 && ticks == 0);
        long ran = schedstat ? runNs : ticks * mNsPerTick;
        long wait = schedstat ? Math.max(0, waitNs) : 0;
        if (!acc.seen && !firstSample) {
            // A thread born between two samples: everything it ran so far happened in that interval.
            acc.lastRun = 0;
            acc.lastWait = 0;
            acc.lastTicks = 0;
            acc.seen = true;
        }
        if (acc.seen) {
            long dRun = Math.max(0, ran - acc.lastRun);
            long dWait = Math.max(0, wait - acc.lastWait);
            long dTicks = Math.max(0, ticks - acc.lastTicks);
            String cls = mTopology != null ? mTopology.classOf(cpu) : CpuTopology.UNKNOWN;
            long khz = dRun > 0 && (run || sim) ? currentKhz(cpu) : -1;
            long maxKhz = mTopology != null ? mTopology.maxKhzOf(cpu) : -1;
            if (run) {
                acc.credit(ALL, dRun, dWait, dTicks, cls, cpu);
                acc.creditClock(ALL, dRun, khz, maxKhz);
            }
            if (sim) {
                acc.credit(SIM, dRun, dWait, dTicks, cls, cpu);
                acc.creditClock(SIM, dRun, khz, maxKhz);
            }
        }
        acc.seen = true;
        acc.lastRun = ran;
        acc.lastWait = wait;
        acc.lastTicks = ticks;
        acc.name = comm;
        acc.state = state;
        acc.cpu = cpu;
        acc.nice = nice;
        acc.policy = policy;
    }

    /** Adds wall time to the windows; for host tests that drive {@link #observe} directly. */
    void addWall(long ns, boolean sim) {
        mWallNs[ALL] += ns;
        if (sim) {
            mWallNs[SIM] += ns;
        }
        ++mFastSamples;
    }

    private void fullSample(long now, File dir, File tasks, List<Integer> tids, boolean intervalRun,
            boolean intervalSim) {
        mLastFullNs = now;
        ++mFullSamples;
        JSONObject sample = new JSONObject();
        try {
            sample.put("t", (now - mStartNs) / 1_000_000);
            if (mSource != null) {
                sample.put("phase", mSource.phase()).put("beat", mSource.beat());
            }
            ProcFiles.Stat proc = ProcFiles.Stat.parse(ProcFiles.read(new File(dir, "stat"), 4096));
            if (proc != null) {
                long rssKb = proc.rssPages * mPageKb;
                mRssMaxKb = Math.max(mRssMaxKb, rssKb);
                sample.put("proc", new JSONArray().put(String.valueOf(proc.state)).put(proc.numThreads)
                        .put(proc.ticks()).put(rssKb));
                if (mProcTicksFirst < 0) {
                    mProcTicksFirst = proc.ticks();
                } else if (intervalSim && mProcTicksLast >= 0) {
                    mProcTicksSim += Math.max(0, proc.ticks() - mProcTicksLast);
                }
                mProcTicksLast = proc.ticks();
            }
            JSONArray threads = new JSONArray();
            for (int tid : tids) {
                ThreadAcc acc = mThreads.get(tid);
                if (acc == null) {
                    continue;
                }
                File task = new File(tasks, String.valueOf(tid));
                long vcsw = -1;
                long nvcsw = -1;
                if (mStatusReadable) {
                    String status = ProcFiles.read(new File(task, "status"));
                    if (status == null) {
                        // Another thread may just have ended; only the main thread's file decides.
                        mStatusReadable = tid != mPid;
                    } else {
                        String allowed = ProcFiles.statusValue(status, "Cpus_allowed_list");
                        if (allowed != null) {
                            acc.allowed.add(allowed);
                            if (intervalRun) {
                                acc.allowedIn.get(ALL).add(allowed);
                            }
                            if (intervalSim) {
                                acc.allowedSim = allowed;
                                acc.allowedIn.get(SIM).add(allowed);
                            }
                        }
                        vcsw = ProcFiles.statusLong(status, "voluntary_ctxt_switches", -1);
                        nvcsw = ProcFiles.statusLong(status, "nonvoluntary_ctxt_switches", -1);
                        countDelta(acc.vcsw, acc.lastVcsw, vcsw, intervalRun, intervalSim);
                        countDelta(acc.nvcsw, acc.lastNvcsw, nvcsw, intervalRun, intervalSim);
                        acc.lastVcsw = vcsw;
                        acc.lastNvcsw = nvcsw;
                    }
                }
                if (mSchedReadable) {
                    long migrations = ProcFiles.schedValue(ProcFiles.read(new File(task, "sched"), 16 * 1024),
                            "se.nr_migrations");
                    if (migrations < 0) {
                        mSchedReadable = tid != mPid;
                    } else {
                        if (acc.migrations[ALL] < 0) {
                            acc.migrations[ALL] = 0;
                            acc.migrations[SIM] = 0;
                        }
                        countDelta(acc.migrations, acc.lastMigrations, migrations, intervalRun, intervalSim);
                        acc.lastMigrations = migrations;
                    }
                }
                threads.put(new JSONArray().put(tid).put(acc.name).put(String.valueOf(acc.state)).put(acc.cpu)
                        .put(acc.utime).put(acc.stime).put(acc.lastRun / 1_000_000).put(acc.lastWait / 1_000_000)
                        .put(vcsw).put(nvcsw));
            }
            sample.put("threads", threads);
            if (mFreqReadable && mTopology != null && !mTopology.cpus.isEmpty()) {
                JSONArray freqs = new JSONArray();
                boolean any = false;
                for (CpuTopology.Cpu cpu : mTopology.cpus) {
                    long khz = ProcFiles.readLong(new File(mSysCpu, "cpu" + cpu.index + "/cpufreq/scaling_cur_freq"),
                            -1);
                    freqs.put(khz);
                    any |= khz > 0;
                    if (khz > 0 && intervalSim) {
                        long[] sum = mFreqSim.get(cpu.index);
                        if (sum == null) {
                            sum = new long[4];
                            sum[2] = Long.MAX_VALUE;
                            sum[3] = Long.MAX_VALUE;
                            mFreqSim.put(cpu.index, sum);
                        }
                        sum[0] += khz;
                        ++sum[1];
                        sum[2] = Math.min(sum[2], khz);
                        // The policy's current cap (thermal or power limits lower it below cpuinfo_max_freq).
                        long cap = ProcFiles.readLong(new File(mSysCpu, "cpu" + cpu.index + "/cpufreq/scaling_max_freq"),
                                -1);
                        if (cap > 0) {
                            sum[3] = Math.min(sum[3], cap);
                        }
                    }
                }
                if (any) {
                    sample.put("freq_khz", freqs);
                } else {
                    mFreqReadable = false;
                }
            }
            if (mApp != null && (mLastAppNs < 0 || now - mLastAppNs >= APP_MS * 1_000_000L - FAST_MS * 500_000L)) {
                mLastAppNs = now;
                JSONObject app = mApp.sample();
                if (app != null) {
                    app.put("t", (now - mStartNs) / 1_000_000);
                    mAppSamples.add(app);
                }
            }
        } catch (JSONException e) {
            mError = e.toString();
        }
        if (mHead.size() < HEAD_SAMPLES) {
            mHead.add(sample);
        } else {
            mTail.addLast(sample);
            if (mTail.size() > TAIL_SAMPLES) {
                mTail.removeFirst();
                ++mDropped;
            }
        }
    }

    private static void countDelta(long[] totals, long last, long now, boolean run, boolean sim) {
        if (last < 0 || now < 0) {
            return;
        }
        long delta = Math.max(0, now - last);
        if (run) {
            totals[ALL] += delta;
        }
        if (sim) {
            totals[SIM] += delta;
        }
    }

    // --------------------------------------------------------------- results

    /** The verdict and its numbers. */
    static final class Verdict {
        String code = "unknown";
        String text = "no telemetry";
        String window = "run";
        ThreadAcc hot;
        double busy;
        double queued;
        /** The busiest thread's run-time weighted scaling_cur_freq and cpuinfo_max_freq, kHz; 0 unknown. */
        double effectiveKhz;
        double maxKhz;
        /** Its migrations per second of the window; -1 unknown. */
        double migrationsPerSecond = -1;
        /** Migrations could misplace more than {@link #MIGRATION_DOUBT} of its run time between classes. */
        boolean classSplitApproximate;
        /** The device has more than one core class (else there is no split to doubt). */
        boolean severalClasses;
        /** Its Cpus_allowed_list values during the window, in order. */
        List<String> allowed = new ArrayList<>();
    }

    Verdict judge() {
        Verdict v = new Verdict();
        if (mPid == 0 && mThreads.isEmpty()) {
            v.text = "no telemetry: the runner process was not found (it may have ended within a few milliseconds)";
            return v;
        }
        int window = mWallNs[SIM] >= MIN_SIM_NS ? SIM : ALL;
        v.window = window == SIM ? "sim time" : "run";
        long wall = mWallNs[window];
        ThreadAcc hot = null;
        for (ThreadAcc acc : mThreads.values()) {
            if (hot == null || acc.runNs[window] > hot.runNs[window]) {
                hot = acc;
            }
        }
        if (hot == null || wall < 1_000_000_000L || hot.runNs[window] <= 0) {
            v.text = "no telemetry: under a second of samples" + (mEnded.isEmpty() ? "" : " (" + mEnded + ")");
            return v;
        }
        v.hot = hot;
        v.busy = Math.min(1.0, (double) hot.runNs[window] / wall);
        v.queued = Math.min(1.0, (double) hot.waitNs[window] / wall);
        if (hot.khzRunNs[window] > 0) {
            v.effectiveKhz = hot.khzNs[window] / hot.khzRunNs[window];
        }
        if (hot.maxKhzRunNs[window] > 0) {
            v.maxKhz = hot.maxKhzNs[window] / hot.maxKhzRunNs[window];
        }
        if (hot.migrations[window] >= 0) {
            v.migrationsPerSecond = hot.migrations[window] / (wall / 1e9);
            // Only a split between classes can be wrong: with one class (the emulator) there is none.
            v.severalClasses = mTopology != null && mTopology.known() && mTopology.cpusOf(CpuTopology.UNIFORM).isEmpty();
            v.classSplitApproximate = v.severalClasses && hot.migrations[window] * FAST_MS * 1_000_000.0
                    > MIGRATION_DOUBT * hot.runNs[window];
        }
        v.allowed = new ArrayList<>(hot.allowedIn.get(window));
        String who = "sim thread";
        if (window == ALL) {
            who = "busiest thread";
        }
        String dominant = null;
        for (String cls : new String[] {CpuTopology.BIG, CpuTopology.MID, CpuTopology.LITTLE, CpuTopology.UNIFORM}) {
            if (hot.share(window, cls) >= DOMINANT) {
                dominant = cls;
            }
        }
        boolean known = mTopology != null && mTopology.known();
        StringBuilder text = new StringBuilder();
        if (v.queued >= STARVED) {
            v.code = "starved";
            text.append("waiting for a free core: the ").append(who).append(" was runnable but not running ")
                    .append(pct(v.queued)).append(" of the ").append(v.window).append(" (ran ").append(pct(v.busy))
                    .append(')');
            appendWhere(text, window, hot, dominant, known);
        } else if (v.busy < CPU_BOUND) {
            v.code = "waiting";
            text.append("mostly waiting: the ").append(who).append(" ran ").append(pct(v.busy)).append(" of the ")
                    .append(v.window).append(", slept or blocked ")
                    .append(pct(Math.max(0, 1 - v.busy - v.queued)));
            appendWhere(text, window, hot, dominant, known);
        } else if (!known) {
            v.code = "cpu-bound";
            text.append(who).append(" CPU-bound (ran ").append(pct(v.busy)).append(" of the ").append(v.window)
                    .append("); core classes unknown");
        } else if (CpuTopology.UNIFORM.equals(dominant)) {
            v.code = "cpu-bound";
            text.append(who).append(" CPU-bound on equal cores (ran ").append(pct(v.busy)).append(" of the ")
                    .append(v.window).append(')');
        } else if (dominant != null) {
            v.code = dominant;
            String names = mTopology.coreNames(dominant);
            List<Integer> members = mTopology.cpusOf(dominant);
            String where;
            if (CpuTopology.BIG.equals(dominant)) {
                where = members.size() == 1 ? "on the big core" : "mostly on big cores";
            } else {
                where = "mostly on " + dominant + " cores";
            }
            text.append(who).append(' ').append(where).append(" (").append(names.isEmpty() ? "" : names + ", ")
                    .append(mTopology.cpuRange(dominant)).append(", ").append(pct(hot.share(window, dominant)))
                    .append(" of its CPU time), CPU-bound (ran ").append(pct(v.busy)).append(" of the ")
                    .append(v.window).append(')');
        } else {
            v.code = "mixed";
            text.append(who).append(" spread over core classes (").append(classShares(hot, window))
                    .append("), CPU-bound (ran ").append(pct(v.busy)).append(" of the ").append(v.window).append(')');
        }
        String pinning = pinning(hot, window);
        if (!pinning.isEmpty()) {
            text.append("; ").append(pinning);
        }
        v.text = text.toString();
        return v;
    }

    private void appendWhere(StringBuilder text, int window, ThreadAcc hot, String dominant, boolean known) {
        if (!known || dominant == null || CpuTopology.UNIFORM.equals(dominant)) {
            return;
        }
        String names = mTopology.coreNames(dominant);
        text.append(", mostly on ").append(dominant).append(" cores (").append(names.isEmpty() ? "" : names + ", ")
                .append(pct(hot.share(window, dominant))).append(')');
    }

    /** "pinned to cpu1 (little)" or "allowed cpu0-3 only (no big core)", from Cpus_allowed_list. */
    private String pinning(ThreadAcc hot, int window) {
        String allowed = window == SIM && hot.allowedSim != null ? hot.allowedSim
                : hot.allowed.isEmpty() ? null : new ArrayList<>(hot.allowed).get(hot.allowed.size() - 1);
        if (allowed == null || mTopology == null) {
            return "";
        }
        List<Integer> cpus = ProcFiles.parseCpuList(allowed);
        if (cpus.isEmpty() || cpus.size() >= mTopology.cpus.size()) {
            return "";
        }
        if (cpus.size() == 1) {
            String cls = mTopology.classOf(cpus.get(0));
            return "pinned to cpu" + cpus.get(0) + (CpuTopology.UNKNOWN.equals(cls) ? "" : " (" + cls + ")");
        }
        boolean hasBig = false;
        for (int cpu : cpus) {
            hasBig |= CpuTopology.BIG.equals(mTopology.classOf(cpu));
        }
        return "allowed cpu" + CpuTopology.formatList(cpus) + " only" + (hasBig || !mTopology.known()
                || mTopology.cpusOf(CpuTopology.BIG).isEmpty() ? "" : " (no big core)");
    }

    private static String classShares(ThreadAcc acc, int window) {
        StringBuilder out = new StringBuilder();
        for (String cls : new String[] {CpuTopology.BIG, CpuTopology.MID, CpuTopology.LITTLE}) {
            double share = acc.share(window, cls);
            if (share > 0) {
                out.append(out.length() > 0 ? ", " : "").append(cls).append(' ').append(pct(share));
            }
        }
        return out.toString();
    }

    static String pct(double fraction) {
        return String.format(Locale.ROOT, "%.0f%%", fraction * 100);
    }

    /**
     * The compact result for result.json and meta.json's step entry. Never throws: if the sampler is still
     * running (stop() waits only 3 s, a slow /proc or binder read can outlast that), a concurrent change shows
     * as "error" and the keys put so far stay.
     */
    JSONObject summary() {
        JSONObject json = new JSONObject();
        try {
            Verdict v = judge();
            json.put("verdict", v.text).put("verdict_code", v.code).put("window", v.window);
            json.put("method", "schedstat run time per " + FAST_MS + " ms credited to the CPU the thread is on at the "
                    + "end of each interval; recorded samples every " + FULL_MS + " ms");
            json.put("fast_samples", mFastSamples).put("samples", mFullSamples)
                    .put("wall_s", round(mWallNs[ALL] / 1e9)).put("sim_wall_s", round(mWallNs[SIM] / 1e9));
            if (mProcess != null) {
                json.put("process", mProcess);
            }
            if (!mEnded.isEmpty()) {
                json.put("ended", mEnded);
            }
            if (mProcTicksFirst >= 0 && mProcTicksLast >= 0) {
                double cpu = (mProcTicksLast - mProcTicksFirst) * mNsPerTick / 1e9;
                json.put("process_cpu_s", round(cpu)).put("process_sim_cpu_s", round(mProcTicksSim * mNsPerTick / 1e9));
                if (mWallNs[SIM] > 0) {
                    json.put("process_cores_sim", round(mProcTicksSim * mNsPerTick / (double) mWallNs[SIM]));
                }
            }
            if (mRssMaxKb > 0) {
                json.put("rss_max_mb", round(mRssMaxKb / 1024.0));
            }
            int window = "sim time".equals(v.window) ? SIM : ALL;
            if (v.hot != null) {
                json.put("hot_thread", describeThread(v.hot, window));
                json.put("busy", round(v.busy)).put("queued", round(v.queued));
                if (!v.allowed.isEmpty()) {
                    json.put("cpus_allowed_window", new JSONArray(v.allowed));
                }
                JSONObject clock = new JSONObject();
                if (v.effectiveKhz > 0) {
                    clock.put("effective_mhz", Math.round(v.effectiveKhz / 1000));
                }
                if (v.maxKhz > 0) {
                    clock.put("max_mhz", Math.round(v.maxKhz / 1000));
                }
                if (clock.length() > 0) {
                    clock.put("method", "run time per " + FAST_MS + " ms weighted with scaling_cur_freq (and "
                            + "cpuinfo_max_freq) of the CPU it was credited to");
                    json.put("hot_clock", clock);
                }
                if (v.migrationsPerSecond >= 0) {
                    json.put("hot_migrations_per_s", round(v.migrationsPerSecond))
                            .put("class_split", !v.severalClasses ? "one class" : v.classSplitApproximate ? "approximate"
                                    : "reliable");
                }
            }
            List<ThreadAcc> threads = new ArrayList<>(mThreads.values());
            final int w = window;
            Collections.sort(threads, new Comparator<ThreadAcc>() {
                @Override
                public int compare(ThreadAcc a, ThreadAcc b) {
                    return Long.compare(b.runNs[ALL], a.runNs[ALL]);
                }
            });
            JSONArray list = new JSONArray();
            for (int i = 0; i < threads.size() && i < MAX_THREADS_LISTED; ++i) {
                list.put(describeThread(threads.get(i), w));
            }
            json.put("threads", list).put("thread_count", threads.size());
            if (!mFreqSim.isEmpty()) {
                JSONObject freq = new JSONObject();
                for (Map.Entry<Integer, long[]> entry : mFreqSim.entrySet()) {
                    long[] sum = entry.getValue();
                    JSONObject cpu = new JSONObject().put("avg_khz", sum[0] / Math.max(1, sum[1])).put("min_khz", sum[2]);
                    if (sum[3] != Long.MAX_VALUE) {
                        cpu.put("cap_min_khz", sum[3]);
                    }
                    freq.put(String.valueOf(entry.getKey()), cpu);
                }
                json.put("freq_sim", freq);
            } else if (!mFreqReadable) {
                json.put("freq_sim", "scaling_cur_freq unreadable");
            }
            if (!mStatusReadable) {
                json.put("status_readable", false);
            }
            json.put("sched_readable", mSchedReadable);
            json.put("app", summarizeApp());
            if (mError != null) {
                json.put("error", mError);
            }
        } catch (JSONException | RuntimeException e) {
            // The summary is best effort; an unexpected value (or the sampler still changing its state)
            // leaves the keys put so far.
            try {
                json.put("error", "summary: " + e);
            } catch (JSONException ignored) {
                // Nothing more to keep.
            }
        }
        return json;
    }

    private JSONObject describeThread(ThreadAcc acc, int window) throws JSONException {
        long wall = Math.max(1, mWallNs[window]);
        JSONObject json = new JSONObject().put("tid", acc.tid).put("name", acc.name)
                .put("run_s", round(acc.runNs[window] / 1e9)).put("wait_s", round(acc.waitNs[window] / 1e9))
                .put("busy", round((double) acc.runNs[window] / wall)).put("queued", round((double) acc.waitNs[window] / wall))
                .put("cpu_ticks", acc.ticks[window]).put("run_total_s", round(acc.runNs[ALL] / 1e9))
                .put("nice", acc.nice).put("policy", ProcFiles.policyName(acc.policy))
                .put("last_cpu", acc.cpu).put("state", String.valueOf(acc.state));
        JSONObject classes = new JSONObject();
        for (Map.Entry<String, Long> entry : acc.classNs.get(window).entrySet()) {
            classes.put(entry.getKey(), round((double) entry.getValue() / Math.max(1, acc.runNs[window])));
        }
        json.put("classes", classes);
        JSONObject cpus = new JSONObject();
        for (Map.Entry<Integer, Long> entry : acc.cpuNs.get(window).entrySet()) {
            cpus.put(String.valueOf(entry.getKey()), round((double) entry.getValue() / Math.max(1, acc.runNs[window])));
        }
        json.put("cpus", cpus);
        if (!acc.allowed.isEmpty()) {
            json.put("cpus_allowed", new JSONArray(new ArrayList<>(acc.allowed)));
        }
        if (acc.lastVcsw >= 0) {
            json.put("voluntary_switches", acc.vcsw[window]).put("involuntary_switches", acc.nvcsw[window]);
        }
        if (acc.migrations[window] >= 0) {
            json.put("migrations", acc.migrations[window]);
        }
        if (!acc.allowedIn.get(window).isEmpty()) {
            json.put("cpus_allowed_window", new JSONArray(new ArrayList<>(acc.allowedIn.get(window))));
        }
        return json;
    }

    private JSONObject summarizeApp() throws JSONException {
        JSONObject json = new JSONObject().put("samples", mAppSamples.size());
        if (mAppSamples.isEmpty()) {
            return json;
        }
        Set<String> importance = new LinkedHashSet<>();
        Set<String> cpusets = new LinkedHashSet<>();
        int interactive = 0;
        int powerSave = 0;
        int thermalMax = -1;
        double tempMin = Double.MAX_VALUE;
        double tempMax = -Double.MAX_VALUE;
        for (JSONObject sample : mAppSamples) {
            if (sample.has("importance")) {
                importance.add(sample.optString("importance"));
            }
            if (sample.has("cpuset")) {
                cpusets.add(sample.optString("cpuset"));
            }
            interactive += sample.optBoolean("interactive", false) ? 1 : 0;
            powerSave += sample.optBoolean("power_save", false) ? 1 : 0;
            thermalMax = Math.max(thermalMax, sample.optInt("thermal_status", -1));
            double temp = sample.optDouble("battery_temp_c", Double.NaN);
            if (!Double.isNaN(temp)) {
                tempMin = Math.min(tempMin, temp);
                tempMax = Math.max(tempMax, temp);
            }
        }
        json.put("importance", new JSONArray(new ArrayList<>(importance)))
                .put("cpuset", new JSONArray(new ArrayList<>(cpusets)))
                .put("interactive_fraction", round((double) interactive / mAppSamples.size()))
                .put("power_save_fraction", round((double) powerSave / mAppSamples.size()));
        if (thermalMax >= 0) {
            json.put("thermal_status_max", thermalMax);
        }
        if (tempMax > -Double.MAX_VALUE) {
            json.put("battery_temp_c", new JSONArray().put(tempMin).put(tempMax));
        }
        return json;
    }

    /** The recorded samples and app states, for meta.json. Never throws, like {@link #summary()}. */
    JSONObject details() {
        JSONObject json = new JSONObject();
        try {
            json.put("legend", new JSONObject()
                    .put("t", "ms since the sampler started")
                    .put("proc", "[state, threads, utime+stime clock ticks, rss kB]")
                    .put("threads", "[tid, name, state, last cpu, utime, stime (clock ticks, stat), ms on a cpu, "
                            + "ms runnable on a run queue (schedstat), voluntary switches, involuntary switches "
                            + "(status)], totals since the thread started")
                    .put("clock_ticks_per_s", 1_000_000_000L / mNsPerTick)
                    .put("freq_khz", "scaling_cur_freq per cpu, in the order of cpu_topology.cpus"));
            JSONArray samples = new JSONArray();
            for (JSONObject sample : mHead) {
                samples.put(sample);
            }
            if (mDropped > 0) {
                samples.put(new JSONObject().put("dropped", mDropped));
            }
            for (JSONObject sample : mTail) {
                samples.put(sample);
            }
            json.put("samples", samples);
            JSONArray app = new JSONArray();
            for (JSONObject sample : mAppSamples) {
                app.put(sample);
            }
            json.put("app", app);
        } catch (JSONException | RuntimeException e) {
            // Best effort, as the summary.
            try {
                json.put("error", "details: " + e);
            } catch (JSONException ignored) {
                // Nothing more to keep.
            }
        }
        return json;
    }

    int pid() {
        return mPid;
    }

    private static double round(double value) {
        if (Double.isNaN(value) || Double.isInfinite(value)) {
            return 0;
        }
        return Math.round(value * 1000) / 1000.0;
    }
}
