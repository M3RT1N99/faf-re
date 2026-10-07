package io.github.m3rt1n99.fafre;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

/**
 * The device's CPUs grouped into core classes, so each telemetry sample can say whether a thread ran on
 * a big, mid or little core ({@link RunTelemetry}).
 *
 * <p>The grouping key is {@code /sys/devices/system/cpu/cpuN/cpu_capacity} (the scheduler's own
 * capacity scale, 1024 for the fastest core) when every CPU has one, else
 * {@code cpufreq/cpuinfo_max_freq}. One distinct value means one class ("uniform": an emulator, or a
 * phone whose sysfs the app may not read); two give little and big; three or more give little (lowest),
 * big (highest) and mid (everything between). The core names come from /proc/cpuinfo's "CPU part"
 * (Arm's part numbers), when the device shows it.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM against copied sysfs files.
 */
final class CpuTopology {
    static final String LITTLE = "little";
    static final String MID = "mid";
    static final String BIG = "big";
    static final String UNIFORM = "uniform";
    static final String UNKNOWN = "unknown";

    static final class Cpu {
        final int index;
        long capacity = -1;
        long maxFreqKhz = -1;
        long minFreqKhz = -1;
        String part = "";
        String related = "";
        String online = "";
        String cls = UNKNOWN;

        Cpu(int index) {
            this.index = index;
        }
    }

    final List<Cpu> cpus = new ArrayList<>();
    /** "cpu_capacity", "cpuinfo_max_freq" or "none": what the classes are based on. */
    String basis = "none";
    final List<String> notes = new ArrayList<>();

    private CpuTopology() {
    }

    /** Reads the topology; {@code sysCpu} is /sys/devices/system/cpu, {@code cpuinfo} /proc/cpuinfo. */
    static CpuTopology read(File sysCpu, File cpuinfo, int fallbackCount) {
        CpuTopology topology = new CpuTopology();
        List<Integer> indices = ProcFiles.parseCpuList(ProcFiles.read(new File(sysCpu, "possible"), 256));
        if (indices.isEmpty()) {
            indices = ProcFiles.parseCpuList(ProcFiles.read(new File(sysCpu, "present"), 256));
        }
        if (indices.isEmpty()) {
            String[] names = sysCpu.list();
            TreeSet<Integer> found = new TreeSet<>();
            if (names != null) {
                for (String name : names) {
                    if (name.matches("cpu[0-9]+")) {
                        found.add(Integer.parseInt(name.substring(3)));
                    }
                }
            }
            indices = new ArrayList<>(found);
            if (indices.isEmpty()) {
                topology.notes.add("no CPU list in " + sysCpu + "; assuming " + fallbackCount + " CPUs");
                for (int i = 0; i < fallbackCount; ++i) {
                    indices.add(i);
                }
            }
        }
        Map<Integer, String> parts = parseParts(ProcFiles.read(cpuinfo, 256 * 1024));
        for (int index : indices) {
            Cpu cpu = new Cpu(index);
            File dir = new File(sysCpu, "cpu" + index);
            cpu.capacity = ProcFiles.readLong(new File(dir, "cpu_capacity"), -1);
            cpu.maxFreqKhz = ProcFiles.readLong(new File(dir, "cpufreq/cpuinfo_max_freq"), -1);
            cpu.minFreqKhz = ProcFiles.readLong(new File(dir, "cpufreq/cpuinfo_min_freq"), -1);
            String related = ProcFiles.read(new File(dir, "cpufreq/related_cpus"), 256);
            cpu.related = related != null ? related.trim() : "";
            String online = ProcFiles.read(new File(dir, "online"), 16);
            cpu.online = online != null ? online.trim() : "";
            String part = parts.get(index);
            cpu.part = part != null ? part : "";
            topology.cpus.add(cpu);
        }
        topology.classify();
        return topology;
    }

    private void classify() {
        boolean allCapacity = !cpus.isEmpty();
        boolean allFreq = !cpus.isEmpty();
        for (Cpu cpu : cpus) {
            allCapacity &= cpu.capacity > 0;
            allFreq &= cpu.maxFreqKhz > 0;
        }
        if (!allCapacity && !allFreq) {
            basis = "none";
            notes.add("neither cpu_capacity nor cpuinfo_max_freq is readable for every CPU; core classes unknown");
            return;
        }
        basis = allCapacity ? "cpu_capacity" : "cpuinfo_max_freq";
        TreeSet<Long> keys = new TreeSet<>();
        for (Cpu cpu : cpus) {
            keys.add(key(cpu));
        }
        long lowest = keys.first();
        long highest = keys.last();
        for (Cpu cpu : cpus) {
            long key = key(cpu);
            if (keys.size() == 1) {
                cpu.cls = UNIFORM;
            } else if (key == highest) {
                cpu.cls = BIG;
            } else if (key == lowest) {
                cpu.cls = LITTLE;
            } else {
                cpu.cls = MID;
            }
        }
    }

    private long key(Cpu cpu) {
        return "cpu_capacity".equals(basis) ? cpu.capacity : cpu.maxFreqKhz;
    }

    /** The class of a CPU number ("big", "mid", "little", "uniform", or "unknown"). */
    String classOf(int index) {
        for (Cpu cpu : cpus) {
            if (cpu.index == index) {
                return cpu.cls;
            }
        }
        return UNKNOWN;
    }

    /** A CPU's cpuinfo_max_freq in kHz, or -1 when unknown. */
    long maxKhzOf(int index) {
        for (Cpu cpu : cpus) {
            if (cpu.index == index) {
                return cpu.maxFreqKhz;
            }
        }
        return -1;
    }

    boolean known() {
        return !"none".equals(basis);
    }

    /** The CPU numbers of a class, ascending. */
    List<Integer> cpusOf(String cls) {
        List<Integer> out = new ArrayList<>();
        for (Cpu cpu : cpus) {
            if (cpu.cls.equals(cls)) {
                out.add(cpu.index);
            }
        }
        return out;
    }

    /** "Cortex-X2" / "Cortex-A510 + Cortex-A55" for a class, or "" when /proc/cpuinfo names none. */
    String coreNames(String cls) {
        Set<String> names = new LinkedHashSet<>();
        for (Cpu cpu : cpus) {
            if (cpu.cls.equals(cls) && !cpu.part.isEmpty()) {
                names.add(cpu.part);
            }
        }
        return String.join(" + ", names);
    }

    /** "cpu7" or "cpu4-6" for a class. */
    String cpuRange(String cls) {
        return "cpu" + formatList(cpusOf(cls));
    }

    static String formatList(List<Integer> cpus) {
        StringBuilder out = new StringBuilder();
        int i = 0;
        while (i < cpus.size()) {
            int start = cpus.get(i);
            int end = start;
            while (i + 1 < cpus.size() && cpus.get(i + 1) == end + 1) {
                end = cpus.get(++i);
            }
            out.append(out.length() > 0 ? "," : "").append(start).append(end > start ? "-" + end : "");
            ++i;
        }
        return out.toString();
    }

    /** "big (Cortex-X2, cpu7)" for the verdict line. */
    String describeClass(String cls) {
        String names = coreNames(cls);
        List<Integer> list = cpusOf(cls);
        String where = list.isEmpty() ? "" : cpuRange(cls);
        if (names.isEmpty()) {
            return cls + (where.isEmpty() ? "" : " (" + where + ")");
        }
        return cls + " (" + names + (where.isEmpty() ? "" : ", " + where) + ")";
    }

    JSONObject toJson() throws JSONException {
        JSONObject json = new JSONObject().put("basis", basis);
        JSONArray list = new JSONArray();
        for (Cpu cpu : cpus) {
            JSONObject entry = new JSONObject().put("cpu", cpu.index).put("class", cpu.cls);
            if (cpu.capacity > 0) {
                entry.put("capacity", cpu.capacity);
            }
            if (cpu.maxFreqKhz > 0) {
                entry.put("max_freq_khz", cpu.maxFreqKhz);
            }
            if (cpu.minFreqKhz > 0) {
                entry.put("min_freq_khz", cpu.minFreqKhz);
            }
            if (!cpu.part.isEmpty()) {
                entry.put("core", cpu.part);
            }
            if (!cpu.related.isEmpty()) {
                entry.put("related_cpus", cpu.related);
            }
            if (!cpu.online.isEmpty()) {
                entry.put("online", cpu.online);
            }
            list.put(entry);
        }
        json.put("cpus", list);
        JSONObject classes = new JSONObject();
        for (String cls : new String[] {BIG, MID, LITTLE, UNIFORM}) {
            List<Integer> members = cpusOf(cls);
            if (!members.isEmpty()) {
                classes.put(cls, describeClass(cls));
            }
        }
        json.put("classes", classes);
        if (!notes.isEmpty()) {
            json.put("notes", new JSONArray(notes));
        }
        return json;
    }

    /** One line for the card or the summary: "big cpu7 (Cortex-X2) · mid cpu4-6 · little cpu0-3". */
    String describe() {
        if (!known()) {
            return "core classes unknown (" + cpus.size() + " CPUs)";
        }
        StringBuilder out = new StringBuilder();
        for (String cls : new String[] {BIG, MID, LITTLE, UNIFORM}) {
            if (cpusOf(cls).isEmpty()) {
                continue;
            }
            String names = coreNames(cls);
            out.append(out.length() > 0 ? " · " : "").append(cls).append(' ').append(cpuRange(cls))
                    .append(names.isEmpty() ? "" : " (" + names + ")");
        }
        return out.toString();
    }

    /** processor index → core name, from /proc/cpuinfo's "CPU implementer" and "CPU part" lines. */
    static Map<Integer, String> parseParts(String cpuinfo) {
        Map<Integer, String> out = new LinkedHashMap<>();
        if (cpuinfo == null) {
            return out;
        }
        int processor = -1;
        String implementer = "";
        for (String raw : cpuinfo.split("\n")) {
            int colon = raw.indexOf(':');
            if (colon < 0) {
                continue;
            }
            String key = raw.substring(0, colon).trim().toLowerCase(Locale.ROOT);
            String value = raw.substring(colon + 1).trim().toLowerCase(Locale.ROOT);
            if (key.equals("processor")) {
                try {
                    processor = Integer.parseInt(value);
                } catch (NumberFormatException e) {
                    processor = -1;
                }
                implementer = "";
            } else if (key.equals("cpu implementer")) {
                implementer = value;
            } else if (key.equals("cpu part") && processor >= 0) {
                out.put(processor, partName(implementer, value));
            }
        }
        return out;
    }

    /** Arm's (0x41) part numbers of the cores phones use; anything else as "implementer/part". */
    static String partName(String implementer, String part) {
        if ("0x41".equals(implementer)) {
            switch (part) {
                case "0xd03": return "Cortex-A53";
                case "0xd04": return "Cortex-A35";
                case "0xd05": return "Cortex-A55";
                case "0xd07": return "Cortex-A57";
                case "0xd08": return "Cortex-A72";
                case "0xd09": return "Cortex-A73";
                case "0xd0a": return "Cortex-A75";
                case "0xd0b": return "Cortex-A76";
                case "0xd0d": return "Cortex-A77";
                case "0xd41": return "Cortex-A78";
                case "0xd44": return "Cortex-X1";
                case "0xd46": return "Cortex-A510";
                case "0xd47": return "Cortex-A710";
                case "0xd48": return "Cortex-X2";
                case "0xd4d": return "Cortex-A715";
                case "0xd4e": return "Cortex-X3";
                case "0xd80": return "Cortex-A520";
                case "0xd81": return "Cortex-A720";
                case "0xd82": return "Cortex-X4";
                case "0xd85": return "Cortex-X925";
                case "0xd87": return "Cortex-A725";
                default: break;
            }
        }
        return (implementer.isEmpty() ? "?" : implementer) + "/" + part;
    }
}
