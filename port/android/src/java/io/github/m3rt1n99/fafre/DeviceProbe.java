package io.github.m3rt1n99.fafre;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * The device probe's result as the replay card shows it (release 0.4.1). The probe is
 * {@code libfafdeviceprobe.so --out <run dir>} (port/deviceprobe/DeviceProbe.cpp): it runs each section
 * (vulkan, vulkan_render, gles, gles_render, glslang) in a child process of its own, prints
 * "[probe] <section>: <status>: <summary>" per section and "[probe] RESULT ... exit=N", and writes
 * {@code deviceprobe.json} ({"probe", "device", "summary": {section: "status: line"}, "sections":
 * {section: {...}}}) plus deviceprobe-vulkan.png and deviceprobe-gles.png into the run directory.
 *
 * <p>Exit 0: every section ran to its end, "unsupported" and "error" included (those are findings);
 * exit 1: a section crashed, hung or wrote nothing; exit 2: usage.
 *
 * <p>A section's child sends its result before it tears its driver objects down; how that teardown went
 * is in sections.&lt;name&gt;.process.teardown ({"status": "ok"|"crashed"|"timeout"|"no_output", ...}). A
 * teardown that crashed or hung costs nothing the section found and does not change the exit code; it is
 * shown as a warning and the app saves logcat for it.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class DeviceProbe {
    static final String REPORT = "deviceprobe.json";
    /** Longer than the probe's own longest section limit (glslang, 300 s), which prints nothing meanwhile. */
    static final long WATCHDOG_MS = 360_000;

    private static final String[][] TITLES = {
        {"vulkan", "Vulkan"},
        {"vulkan_render", "Vulkan render"},
        {"gles", "OpenGL ES"},
        {"gles_render", "OpenGL ES render"},
        {"glslang", "Shader compile"},
    };

    /** What the card, result.json and the summary text get. */
    static final class Result {
        boolean reportFound;
        String reportError = "";
        String headline = "";
        /** {tone, text} lines for the result block. */
        final List<String[]> lines = new ArrayList<>();
        /** PNG files in the run directory that the probe named. */
        final List<String> images = new ArrayList<>();
        /** section → status. */
        final Map<String, String> statuses = new LinkedHashMap<>();
        /** section → its driver teardown's status, for the sections whose teardown was not "ok". */
        final Map<String, String> teardownProblems = new LinkedHashMap<>();
        String resultLine = "";

        boolean failedSections() {
            for (String status : statuses.values()) {
                if (!"ok".equals(status) && !"unsupported".equals(status) && !"error".equals(status)) {
                    return true;
                }
            }
            return false;
        }

        JSONObject toJson() throws JSONException {
            JSONObject json = new JSONObject().put("report", reportFound ? REPORT : JSONObject.NULL)
                    .put("headline", headline).put("images", new JSONArray(images))
                    .put("sections", new JSONObject(statuses));
            if (!reportError.isEmpty()) {
                json.put("report_error", reportError);
            }
            if (!resultLine.isEmpty()) {
                json.put("result_line", resultLine);
            }
            if (!teardownProblems.isEmpty()) {
                json.put("teardown_problems", new JSONObject(teardownProblems));
            }
            return json;
        }
    }

    private DeviceProbe() {
    }

    /** "[probe] RESULT ..." from the probe's output, or "". */
    static String resultLine(List<String> output) {
        for (int i = output.size() - 1; i >= 0; --i) {
            String line = output.get(i);
            if (line.startsWith("[probe] RESULT")) {
                return line.substring("[probe] ".length());
            }
        }
        return "";
    }

    /**
     * Reads {@code deviceprobe.json} in {@code runDir}; without it, falls back on the "[probe] section: status:
     * summary" lines of the output.
     */
    static Result read(File runDir, List<String> output) {
        Result result = new Result();
        result.resultLine = resultLine(output);
        JSONObject report = null;
        try {
            String text = FileOps.readText(new File(runDir, REPORT), 8 * 1024 * 1024);
            if (text != null) {
                report = new JSONObject(text);
            } else {
                result.reportError = REPORT + " was not written";
            }
        } catch (IOException | JSONException e) {
            result.reportError = REPORT + " is unreadable: " + e.getMessage();
        }
        Map<String, String> summaries = new LinkedHashMap<>();
        JSONObject sections = null;
        if (report != null) {
            result.reportFound = true;
            sections = report.optJSONObject("sections");
            JSONObject summary = report.optJSONObject("summary");
            if (summary != null) {
                for (Iterator<String> keys = summary.keys(); keys.hasNext();) {
                    String key = keys.next();
                    summaries.put(key, summary.optString(key, ""));
                }
            }
            if (sections != null) {
                for (Iterator<String> keys = sections.keys(); keys.hasNext();) {
                    String key = keys.next();
                    JSONObject section = sections.optJSONObject(key);
                    if (section == null) {
                        continue;
                    }
                    result.statuses.put(key, section.optString("status", "?"));
                    JSONObject process = section.optJSONObject("process");
                    JSONObject teardown = process != null ? process.optJSONObject("teardown") : null;
                    if (teardown != null && !"ok".equals(teardown.optString("status", "ok"))) {
                        String what = teardown.optString("status");
                        if (teardown.has("signal")) {
                            what += " (signal " + teardown.optInt("signal") + ")";
                        }
                        result.teardownProblems.put(key, what);
                    }
                    if (!summaries.containsKey(key)) {
                        summaries.put(key, section.optString("status", "?") + ": " + section.optString("error", ""));
                    }
                    String png = section.optString("png", "");
                    if (png.matches("[A-Za-z0-9._-]+\\.png") && new File(runDir, png).isFile()
                            && !result.images.contains(png)) {
                        result.images.add(png);
                    }
                }
            }
        } else {
            // "[probe] vulkan: ok: Vulkan 1.3 ..." lines.
            for (String line : output) {
                for (String[] title : TITLES) {
                    String prefix = "[probe] " + title[0] + ": ";
                    if (line.startsWith(prefix)) {
                        String rest = line.substring(prefix.length());
                        summaries.put(title[0], rest);
                        int colon = rest.indexOf(':');
                        result.statuses.put(title[0], colon > 0 ? rest.substring(0, colon) : rest);
                    }
                }
            }
        }
        for (Map.Entry<String, String> entry : summaries.entrySet()) {
            String key = entry.getKey();
            String text = entry.getValue();
            String status = result.statuses.containsKey(key) ? result.statuses.get(key) : statusOf(text);
            result.statuses.put(key, status);
            String body = text.startsWith(status + ": ") ? text.substring(status.length() + 2) : text;
            JSONObject section = sections != null ? sections.optJSONObject(key) : null;
            if ("glslang".equals(key) && (section == null || section.optBoolean("placeholder", true))
                    && !body.toLowerCase(Locale.ROOT).contains("placeholder")) {
                body += " (placeholder shaders, not FA's)";
            }
            String tone = "ok".equals(status) ? "muted" : "unsupported".equals(status) ? "warn" : "bad";
            result.lines.add(new String[] {tone, titleOf(key) + ": " + ("ok".equals(status) ? "" : status.toUpperCase(
                    Locale.ROOT) + " · ") + body});
        }
        for (Map.Entry<String, String> entry : result.teardownProblems.entrySet()) {
            result.lines.add(new String[] {"warn", titleOf(entry.getKey()) + ": the driver's teardown "
                    + entry.getValue() + " after the result was saved (logcat saved with the run)"});
        }
        if (!result.reportError.isEmpty()) {
            result.lines.add(new String[] {"bad", "Report: " + result.reportError});
        }
        if (!result.images.isEmpty()) {
            result.lines.add(new String[] {"muted", "Images: " + String.join(", ", result.images)
                    + " (Probe images; also in the run zip)"});
        }
        result.headline = headline(result, sections);
        return result;
    }

    private static String headline(Result result, JSONObject sections) {
        StringBuilder out = new StringBuilder();
        for (String[] title : TITLES) {
            String status = result.statuses.get(title[0]);
            if (status == null) {
                continue;
            }
            JSONObject section = sections != null ? sections.optJSONObject(title[0]) : null;
            String part;
            if (title[0].endsWith("_render")) {
                String pattern = section != null ? section.optString("pattern", "") : "";
                part = "render " + ("ok".equals(status) && !pattern.isEmpty() ? pattern : status);
            } else if (title[0].equals("glslang")) {
                double ms = section != null ? section.optDouble("first_total_ms", Double.NaN) : Double.NaN;
                part = "glslang " + ("ok".equals(status) && !Double.isNaN(ms) ? String.format(Locale.ROOT, "%.0f ms",
                        ms) : status);
            } else {
                part = title[1] + " " + status;
            }
            // "Vulkan ok, render exact · OpenGL ES ok, render exact · glslang 812 ms"
            boolean joinsPrevious = title[0].endsWith("_render") && out.length() > 0;
            out.append(out.length() == 0 ? "" : joinsPrevious ? ", " : " · ").append(part);
        }
        return out.toString();
    }

    private static String statusOf(String summary) {
        int colon = summary.indexOf(':');
        return colon > 0 ? summary.substring(0, colon).trim() : "?";
    }

    static String titleOf(String section) {
        for (String[] title : TITLES) {
            if (title[0].equals(section)) {
                return title[1];
            }
        }
        return section;
    }
}
