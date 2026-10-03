package io.github.m3rt1n99.fafre;

/** Where jobs report what they did; {@link LauncherLog} on the device, stdout in host tests. */
interface LogSink {
    void log(String line);
}
