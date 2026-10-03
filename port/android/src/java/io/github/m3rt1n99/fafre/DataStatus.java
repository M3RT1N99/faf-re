package io.github.m3rt1n99.fafre;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.EnumMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * What of the manifest is present under the data root, per tier. Only sizes
 * and names are checked here (cheap enough for every onResume); checksums are
 * the import service's "Verify" job.
 *
 * <p>SCFA entries with wildcards ("fonts/*.ttf") cannot be checked against the
 * manifest alone, because what they match depends on the user's install. The
 * deploy record (.deploy/deployed.json) says what was actually placed, with
 * sizes; without it an entry counts as present when anything matches.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class DataStatus {
    static final class FafFileState {
        final DataManifest.FafFile file;
        final boolean present;

        FafFileState(DataManifest.FafFile file, boolean present) {
            this.file = file;
            this.present = present;
        }
    }

    static final class EntryState {
        final DataManifest.Entry entry;
        final String destDir;
        /** Matching names found under destDir. */
        final int presentCount;
        /** Names known to be needed (deploy record + literal includes); 0 when only wildcards are known. */
        final int expectedCount;
        final boolean complete;
        final long presentBytes;
        final List<String> problems;

        EntryState(DataManifest.Entry entry, String destDir, int presentCount, int expectedCount, boolean complete,
                long presentBytes, List<String> problems) {
            this.entry = entry;
            this.destDir = destDir;
            this.presentCount = presentCount;
            this.expectedCount = expectedCount;
            this.complete = complete;
            this.presentBytes = presentBytes;
            this.problems = problems;
        }

        String counts() {
            String unit = entry.kind == DataManifest.Kind.DIR ? " folders" : " files";
            if (expectedCount > 0) {
                return Math.min(presentCount, expectedCount) + "/" + expectedCount + unit;
            }
            return presentCount + unit;
        }
    }

    static final class TierSummary {
        int fafPresent;
        int fafTotal;
        int entriesComplete;
        int entriesTotal;

        boolean complete() {
            return fafPresent == fafTotal && entriesComplete == entriesTotal;
        }
    }

    final FafVersion fafVersion;
    final List<FafFileState> faf;
    final List<EntryState> scfa;
    final List<EntryState> vault;
    final Map<DataManifest.Tier, TierSummary> tiers;
    /** Bytes present / roughly expected for the required and recommended tiers (the default deploy). */
    final long presentBytes;
    final long expectedBytes;

    private DataStatus(FafVersion fafVersion, List<FafFileState> faf, List<EntryState> scfa,
            List<EntryState> vault, Map<DataManifest.Tier, TierSummary> tiers, long presentBytes,
            long expectedBytes) {
        this.fafVersion = fafVersion;
        this.faf = faf;
        this.scfa = scfa;
        this.vault = vault;
        this.tiers = tiers;
        this.presentBytes = presentBytes;
        this.expectedBytes = expectedBytes;
    }

    static DataStatus check(DataManifest manifest, DataRoot root, LogSink log) throws IOException {
        Map<DataManifest.Tier, TierSummary> tiers = new EnumMap<>(DataManifest.Tier.class);
        for (DataManifest.Tier tier : DataManifest.Tier.values()) {
            tiers.put(tier, new TierSummary());
        }
        long presentBytes = 0;
        long expectedBytes = 0;

        List<FafFileState> faf = new ArrayList<>();
        for (DataManifest.FafFile file : manifest.fafFiles) {
            File existing = root.find(file.dest);
            boolean present = existing != null && existing.isFile() && existing.length() == file.size;
            faf.add(new FafFileState(file, present));
            TierSummary summary = tiers.get(file.tier);
            ++summary.fafTotal;
            if (present) {
                ++summary.fafPresent;
            }
            if (file.tier != DataManifest.Tier.OPTIONAL) {
                expectedBytes += file.size;
                presentBytes += present ? file.size : 0;
            }
        }

        DeployRecord record = DeployRecord.read(root.file(manifest.layout.deployRecord), log);
        List<EntryState> scfa = new ArrayList<>();
        for (DataManifest.Entry entry : manifest.scfaEntries) {
            EntryState state = checkEntry(entry, manifest.layout.scfa, root, record);
            scfa.add(state);
            TierSummary summary = tiers.get(entry.tier);
            ++summary.entriesTotal;
            if (state.complete) {
                ++summary.entriesComplete;
            }
            if (entry.tier != DataManifest.Tier.OPTIONAL) {
                presentBytes += state.presentBytes;
                expectedBytes += Math.max(0, entry.approxBytes);
            }
        }
        // The vault is the user's own optional content; it never gates a start
        // and is not part of the tier totals.
        List<EntryState> vault = new ArrayList<>();
        for (DataManifest.Entry entry : manifest.userEntries) {
            vault.add(checkEntry(entry, manifest.layout.vault, root, record));
        }
        return new DataStatus(FafVersion.read(root.file(manifest.layout.fafVersion)),
                Collections.unmodifiableList(faf), Collections.unmodifiableList(scfa),
                Collections.unmodifiableList(vault), tiers, presentBytes, expectedBytes);
    }

    private static EntryState checkEntry(DataManifest.Entry entry, String prefix, DataRoot root,
            DeployRecord record) throws IOException {
        String destDir = prefix + "/" + entry.src;
        boolean isDir = entry.kind == DataManifest.Kind.DIR;

        // What the deploy record says this entry expanded to: name -> files below it with sizes.
        Map<String, List<DeployRecord.Item>> recorded = new LinkedHashMap<>();
        for (DeployRecord.Item item : record.under(destDir)) {
            String rest = item.dest.substring(destDir.length() + 1);
            int slash = rest.indexOf('/');
            String name = slash < 0 ? rest : rest.substring(0, slash);
            if ((slash >= 0) == isDir && entry.matches(name, isDir)) {
                String key = Names.toLowerAscii(name);
                List<DeployRecord.Item> items = recorded.get(key);
                if (items == null) {
                    items = new ArrayList<>();
                    recorded.put(key, items);
                }
                items.add(item);
            }
        }
        Set<String> expected = new HashSet<>(recorded.keySet());
        for (String name : entry.literalNames()) {
            expected.add(Names.toLowerAscii(name));
        }

        Set<String> present = new HashSet<>();
        long bytes = 0;
        File dir = root.find(destDir);
        String[] names = dir != null && dir.isDirectory() ? dir.list() : null;
        if (names != null) {
            for (String name : names) {
                if (name.endsWith(FileOps.PART_SUFFIX) || name.endsWith(".tmp")) {
                    continue;
                }
                File file = new File(dir, name);
                if (entry.matches(name, file.isDirectory())) {
                    present.add(Names.toLowerAscii(name));
                    bytes += FileOps.sizeRecursive(file);
                }
            }
        }

        List<String> problems = new ArrayList<>();
        boolean complete;
        if (expected.isEmpty()) {
            complete = !present.isEmpty();
            if (!complete) {
                problems.add("nothing matching in " + destDir);
            }
        } else {
            for (String name : expected) {
                if (!present.contains(name)) {
                    problems.add("missing " + destDir + "/" + displayName(name, recorded, entry));
                }
            }
            for (List<DeployRecord.Item> items : recorded.values()) {
                for (DeployRecord.Item item : items) {
                    if (item.size < 0) {
                        continue;
                    }
                    File file = root.find(item.dest);
                    if (file != null && file.isFile() && file.length() != item.size) {
                        problems.add(item.dest + " has " + file.length() + " bytes, expected " + item.size);
                    } else if (file == null && present.contains(firstName(item.dest, destDir))) {
                        problems.add("missing " + item.dest);
                    }
                }
            }
            complete = problems.isEmpty();
        }
        int presentExpected = 0;
        for (String name : expected) {
            if (present.contains(name)) {
                ++presentExpected;
            }
        }
        int presentCount = expected.isEmpty() ? present.size() : presentExpected;
        return new EntryState(entry, destDir, presentCount, expected.size(), complete, bytes,
                Collections.unmodifiableList(problems));
    }

    private static String firstName(String dest, String destDir) {
        String rest = dest.substring(destDir.length() + 1);
        int slash = rest.indexOf('/');
        return Names.toLowerAscii(slash < 0 ? rest : rest.substring(0, slash));
    }

    private static String displayName(String key, Map<String, List<DeployRecord.Item>> recorded,
            DataManifest.Entry entry) {
        List<DeployRecord.Item> items = recorded.get(key);
        if (items != null && !items.isEmpty()) {
            return items.get(0).dest.substring(items.get(0).dest.lastIndexOf('/') + 1);
        }
        for (String name : entry.literalNames()) {
            if (Names.toLowerAscii(name).equals(key)) {
                return name;
            }
        }
        return key;
    }

    /** Everything the main menu needs is in place. */
    boolean requiredComplete() {
        return tiers.get(DataManifest.Tier.REQUIRED).complete();
    }

    /** Labels of required items that are missing, for the "why can't I start" line. */
    List<String> missingRequired() {
        List<String> out = new ArrayList<>();
        for (FafFileState state : faf) {
            if (state.file.tier == DataManifest.Tier.REQUIRED && !state.present) {
                out.add("FAF " + state.file.name);
            }
        }
        for (EntryState state : scfa) {
            if (state.entry.tier == DataManifest.Tier.REQUIRED && !state.complete) {
                out.add("SCFA " + state.entry.label);
            }
        }
        return out;
    }

    int fafPresentCount() {
        int count = 0;
        for (FafFileState state : faf) {
            if (state.present) {
                ++count;
            }
        }
        return count;
    }
}
