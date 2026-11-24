package io.github.m3rt1n99.fafre;

import java.util.function.Consumer;

/**
 * The game's end when its activity is finished while the program runs, or the start's preparation before it
 * (GameActivity): by its task, as the FAF client's own activity, brought back by its launcher icon, clears what is above
 * it in the task (launchMode singleTask); by its launcher (Activity.finishActivity); or by the task's end in Recents. No
 * activity can show the program again then. The activity asks the program to quit, as SDL's onDestroy does, and the
 * program's end ends the process (android/native/src/app/GameMainAndroid.cpp); a program that has not ended QUIT_GRACE_MS
 * later has its process ended by the app, and so has a preparation, at once, as its program would start with no activity.
 * The report says each step, and the end. No Android in here, for the tests.
 */
final class ProgramEnd {
    /**
     * How long a program that was asked to quit has for its end, before the app ends its process: well under the 10 s
     * after which Android's freezer stops a cached process, as the activity's end leaves this one, and this wait with it.
     */
    static final long QUIT_GRACE_MS = 5_000;
    /** The report's end when the app ended the process of a program that its quit did not end. */
    static final String QUIT_ENDED = GameReport.END + "the app ended the process: its activity was finished while the program "
        + "ran, and the program had not ended " + QUIT_GRACE_MS / 1000 + " s after it was asked to quit";
    /** The report's end when the app ended the process of a start whose activity was finished during its preparation. */
    static final String PREPARATION_ENDED = GameReport.END + "the app ended the process: its activity was finished while the "
        + "start's preparation ran, before the program";

    private ProgramEnd() {
    }

    /** The report's line when the activity, finished, {@code step}s ("stops", "is destroyed") while the program or its preparation runs. */
    static String finished(String step, boolean preparing) {
        return "game: the activity " + step + ", finished while " + (preparing ? "the start's preparation" : "the program")
            + " runs (its task cleared or ended, or its launcher finished it)";
    }

    /**
     * On a thread of its own: unless {@code program}, asked to quit, has ended within {@code graceMs}, which ends the process,
     * {@code report} gets QUIT_ENDED and {@code end} ends the process.
     */
    static Thread watch(Thread program, long graceMs, Consumer<String> report, Runnable end) {
        Thread watch = new Thread(() -> {
            try {
                program.join(graceMs);
            } catch (InterruptedException e) {
                return;
            }
            if (program.isAlive()) {
                report.accept(QUIT_ENDED);
                end.run();
            }
        }, "faf-program-end");
        watch.setDaemon(true);
        watch.start();
        return watch;
    }
}
