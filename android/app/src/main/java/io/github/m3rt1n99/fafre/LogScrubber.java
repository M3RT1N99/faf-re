package io.github.m3rt1n99.fafre;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The game's logs made fit for a public issue (GameLogs, the launcher's game_logs), line by line: what names a player, the
 * player's account, the network's hosts or the user's folders becomes a placeholder (android/README.md, The logs). Nothing
 * marks a player's name where it stands, so the names are learned first from the lines that name players, in every file of
 * one zip (learn), and then replaced wherever they stand, in any case (scrub): the same name by the same playerN within one
 * scrubber, numbered in the order they were learned, and a player's id where the lobby's lines give it by idN, in the order
 * met. No Android in here, for the tests.
 */
final class LogScrubber {
    /** The lines that name a player, the name in the group "name": the engine's lobby, FAF's Lua, the game's replays. */
    private static final Pattern[] NAMES = {
        // FAF's ScenarioUtilities, for each army as the sim begins: a player's, an AI's or the civilians'.
        Pattern.compile("Initialize Armies brain nickname is (?<name>.+)$"),
        // The engine's peer (SPeer::ToString), in most of its lobby's lines.
        Pattern.compile("\"(?<name>[^\"]*)\" \\[[^\\]\\s]*:\\d+, uid=-?\\d+\\]"),
        Pattern.compile("LOBBY: starting with local uid of -?\\d+ \\[(?<name>.*)\\]$"),
        Pattern.compile("LOBBY: Got join \\(name=\"(?<name>.*)\", uid=\\d+\\) from "),
        Pattern.compile("LOBBY: welcomed by host \"(?<name>.*)\" \\(uid=\\d+\\)"),
        Pattern.compile("LOBBY: host renamed us to (?<name>.+)$"),
        // FAF's autolobby, a matchmaker game's lobby: LOG's arguments, which the engine joins with tabs.
        Pattern.compile("(?:CreateLobby|JoinGame)\t[^\t]*\t[^\t]*\t(?<name>[^\t]*)\t"),
        Pattern.compile("ConnectToPeer\t[^\t]*\t(?<name>[^\t]*)\t"),
        // FAF's lobby, and the LAN games its screen finds (lobbyComm logs each one's config with repr).
        Pattern.compile("ConnectToPeer \\(name=(?<name>.*), uid=\\d+, address="),
        Pattern.compile("Peer Disconnected : \\(name=(?<name>.*), uid=\\d+\\)"),
        Pattern.compile("Received data of type .* from -?\\d+ \\((?<name>.*)\\)$"),
        Pattern.compile(">> (?<name>.+) is missing map "),
        Pattern.compile("\\b(?:HostedBy|PlayerName) ?= ?\"(?<name>[^\"]*)\""),
        // The profile's folder of the game's replays: <HOME>/Documents\...\Supreme Commander Forged Alliance\replays\<profile>\.
        Pattern.compile("(?i)Forged Alliance[\\\\/]replays[\\\\/](?<name>[^\\\\/\"]+)[\\\\/]"),
        // A launcher's replay, which FAF's Java client names after the player: gpgnet://<host>/<game id>/<player>.SCFAreplay.
        Pattern.compile("gpgnet://[^/\\s\"]*/\\d+/(?<name>[^/\\s\"]+)\\.(?i:scfareplay)"),
        // FAF's GpgNetSend, which logs each message to the launcher with its arguments: a team kill's victim and instigator.
        Pattern.compile("GpgNetSend\tTeamkill(?:Report|Happened)\t[^\t]*\t[^\t]*\t(?<name>[^\t]*)\t"),
        Pattern.compile("GpgNetSend\tTeamkill(?:Report|Happened)(?:\t[^\t]*){4}\t(?<name>[^\t]*)$"),
    };
    /** The players in the game's stats, which GpgNetSend logs as they go to the launcher (JsonStats, UserSync.lua). */
    private static final Pattern STATS_NAME = Pattern.compile("\"name\" ?: ?\"(?<name>(?:[^\"\\\\]|\\\\.)*)\"");
    /**
     * No player's: an AI's nickname, "<name> (AI: <type>)" as FAF's lobby makes it, the civilians' army's (Civilian, civilian
     * on a map that names it so), and a name without a letter, which FAF's names begin with, where the stand-in would take
     * every such number.
     */
    private static final Pattern NO_PLAYER = Pattern.compile(".* \\(AI: [^)]*\\)|(?i:civilian)|[^\\p{L}]*");

    /**
     * Where the lobby's lines give a player's id, its uid, in the group "uid": in a game of FAF's the uid is the player's id
     * there, whose page names the player.
     */
    private static final Pattern[] IDS = {
        Pattern.compile("(?:uid=|uid of |assigning uid |peer uid |our uid is |we think it is )(?<uid>-?\\d+)(?![\\w-])"),
        Pattern.compile("(?:CreateLobby|JoinGame)\t[^\t]*\t[^\t]*\t[^\t]*\t(?<uid>-?\\d+)"),
        Pattern.compile("ConnectToPeer\t[^\t]*\t[^\t]*\t(?<uid>-?\\d+)"),
        Pattern.compile("DisconnectFromPeer\t(?<uid>-?\\d+)"),
        Pattern.compile("Received data of type .* from (?<uid>-?\\d+) \\("),
        // GpgNetSend's messages that name a player by id: its options, its connections, a team kill's victim and instigator.
        Pattern.compile("GpgNetSend\t(?:PlayerOption|Disconnected|EstablishedPeer|DisconnectedPeer)\t(?<uid>-?\\d+)(?![^\t])"),
        Pattern.compile("GpgNetSend\tTeamkill(?:Report|Happened)\t[^\t]*\t(?<uid>-?\\d+)\t"),
        Pattern.compile("GpgNetSend\tTeamkill(?:Report|Happened)(?:\t[^\t]*){3}\t(?<uid>-?\\d+)\t"),
        // A peer's ICE message (native mode's IceMsg, CandidatesMessage), and the numbers of a GPGNet Test the engine logs.
        Pattern.compile("\"(?:srcId|destId)\" ?: ?(?<uid>-?\\d+)"),
        Pattern.compile("^\\w+: +arg\\[\\d+\\]=(?<uid>-?\\d+) \\[int\\]$"),
    };
    /** The peers a peer of the lobby reached, by their uids. */
    private static final Pattern ID_LIST = Pattern.compile("has established connections to: (?<uids>-?\\d+(?:, -?\\d+)*)");
    /** A replay's game on FAF, whose page names its players: gpgnet://<host>/<game id>/..., and FAF's line of it. */
    private static final Pattern[] GAMES = {
        Pattern.compile("gpgnet://[^/\\s\"]*/(?<game>\\d+)/"),
        Pattern.compile("REPLAY ID: (?<game>\\d+)"),
    };
    /** An ICE session's credentials in a peer's ICE message. */
    private static final Pattern SECRET = Pattern.compile("\"(?:password|ufrag)\" ?: ?\"(?<secret>[^\"]*)\"");
    /** A game's title, which its host chose: FAF's autolobby's HostGame and a LAN game's config (lobbyComm's repr). */
    private static final Pattern[] TITLES = {
        Pattern.compile("HostGame\t(?<title>[^\t]*)\t"),
        Pattern.compile("\\bGameName ?= ?\"(?<title>(?:[^\"\\\\]|\\\\.)*)\""),
    };
    /**
     * A chat message: what FAF's lobby logs when it has no chat to show it in (lobby.lua's AddChatText: "text=" and repr), and
     * the lobby's chat that goes to the launcher (GpgNetSend Chat).
     */
    private static final Pattern[] CHATS = {
        Pattern.compile("^\\w+: text=(?<chat>\".*\")$"),
        Pattern.compile("GpgNetSend\tChat\t(?<chat>.*)$"),
    };
    /** The account's values of a launcher's switches on the game's command line: together they name the player on FAF. */
    private static final Pattern ACCOUNT =
        Pattern.compile("(?<![\\w/])/(?:mean|deviation|numgames|clan|country|division|subdivision) (?<account>[^\\s\"]+)");

    /** The shared storage's roots, as Android and the engine (backslashes, in lower case) write them, an SD card's too. */
    private static final String SHARED = "(?i:[/\\\\](?:storage[/\\\\]emulated[/\\\\]\\d+|sdcard|storage[/\\\\]self[/\\\\]primary"
        + "|mnt[/\\\\]user[/\\\\]\\d+[/\\\\]emulated[/\\\\]\\d+|data[/\\\\]media[/\\\\]\\d+|storage[/\\\\][0-9a-f]{4}-[0-9a-f]{4}))";
    private static final String IPV4 = "(?<![\\w.])\\d{1,3}(?:\\.\\d{1,3}){3}(?!\\w|\\.\\d)";
    /** An IPv6 address's shape, which ipv6 checks: hex groups and colons, with a zone. */
    private static final String IPV6 = "(?<![\\w:.])(?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4}(?:%[\\w.]+)?(?![\\w:.])";
    private static final Pattern IPV6_GROUP = Pattern.compile("[0-9A-Fa-f]{1,4}");

    private final String mTokens;
    /** The players' names as learned, each once in any case, and their stand-ins: player1, player2, ... */
    private final Map<String, String> mNames = new LinkedHashMap<>();
    /** The lobby's player ids as met, and their stand-ins: id1, id2, ... */
    private final Map<String, String> mIds = new LinkedHashMap<>();
    /** mTokens with the names learned so far, built when a line is scrubbed after a new name. */
    private Pattern mPattern;

    /** A scrubber for the logs of the app {@code packageName}, whose folders it shortens. */
    LogScrubber(String packageName) {
        String app = Pattern.quote(packageName);
        mTokens = "(?<external>" + SHARED + "(?i:[/\\\\]android[/\\\\]data[/\\\\]" + app + ")(?![\\w.-]))"
            + "|(?<internal>[/\\\\]data[/\\\\](?:user(?:_de)?[/\\\\]\\d+|data)[/\\\\]" + app + "(?![\\w.-]))"
            + "|(?<apk>[/\\\\]data[/\\\\]app[/\\\\](?:~~[\\w=-]+[/\\\\])?" + app + "-[\\w=-]+)"
            + "|(?<storage>" + SHARED + "(?![\\w.-])(?:[/\\\\][^\\s\"'<>]*)?)"
            + "|content://(?<authority>[^/\\s\"'<>]+)(?<contentPath>/[^\\s\"'<>]*)?"
            + "|(?<ipv4>" + IPV4 + ")"
            + "|(?<ipv6>" + IPV6 + ")";
    }

    /** Learns the players' names {@code line} gives, which scrub then replaces in every line. */
    void learn(String line) {
        for (Pattern shape : NAMES) {
            learn(shape, line);
        }
        if (line.contains("GpgNetSend\tJsonStats\t")) {
            learn(STATS_NAME, line);
        }
    }

    private void learn(Pattern shape, String line) {
        Matcher match = shape.matcher(line);
        while (match.find()) {
            String name = match.group("name");
            if (!NO_PLAYER.matcher(name).matches() && standIn(name) == null) {
                mNames.put(name, "player" + (mNames.size() + 1));
                mPattern = null;
            }
        }
    }

    /** The names learned, each with its stand-in, in the order learned. */
    Map<String, String> names() {
        return new LinkedHashMap<>(mNames);
    }

    /** {@code line} without what names the players, their account, the network's hosts and the user's folders. */
    String scrub(String line) {
        for (Pattern chat : CHATS) {
            line = replace(chat, line, "chat", text -> "<chat>");
        }
        for (Pattern title : TITLES) {
            line = replace(title, line, "title", text -> "<title>");
        }
        for (Pattern game : GAMES) {
            line = replace(game, line, "game", id -> "<game>");
        }
        line = replace(SECRET, line, "secret", value -> "<secret>");
        line = replace(ACCOUNT, line, "account", value -> "<account>");
        for (Pattern ids : IDS) {
            line = replace(ids, line, "uid", this::id);
        }
        line = replace(ID_LIST, line, "uids", list -> {
            StringBuilder out = new StringBuilder();
            for (String uid : list.split(", ")) {
                out.append(out.length() > 0 ? ", " : "").append(id(uid));
            }
            return out.toString();
        });
        return tokens(line);
    }

    /** The paths, addresses and names of {@code line}, in one pass: a stand-in is never read again. */
    private String tokens(String line) {
        if (mPattern == null) {
            StringBuilder names = new StringBuilder();
            List<String> longestFirst = new ArrayList<>(mNames.keySet());
            // A name that another one begins with goes after it ("Bob Lee" before "Bob").
            longestFirst.sort((a, b) -> b.length() - a.length());
            for (String name : longestFirst) {
                names.append(names.length() > 0 ? "|" : "").append(Pattern.quote(name));
            }
            mPattern = Pattern.compile(mTokens + (names.length() > 0
                ? "|(?<name>(?<![\\w-])(?iu:" + names + ")(?![\\w-]))" : ""));
        }
        Matcher match = mPattern.matcher(line);
        if (!match.find()) {
            return line;
        }
        StringBuilder out = new StringBuilder(line.length());
        int last = 0;
        do {
            out.append(line, last, match.start()).append(token(match));
            last = match.end();
        } while (match.find());
        return out.append(line, last, line.length()).toString();
    }

    private String token(Matcher match) {
        if (match.group("external") != null) {
            return "<external>";
        }
        if (match.group("internal") != null) {
            return "<internal>";
        }
        if (match.group("apk") != null) {
            return "<apk>";
        }
        if (match.group("storage") != null) {
            return "<storage>";
        }
        if (match.group("authority") != null) {
            return "content://" + match.group("authority") + (match.group("contentPath") != null ? "/<path>" : "");
        }
        if (match.group("ipv4") != null) {
            return ipv4(match.group());
        }
        if (match.group("ipv6") != null) {
            return ipv6(match.group());
        }
        return standIn(match.group());
    }

    /** A host's IPv4 address, private or public, as <ip>; the loopback, 0.0.0.0, the broadcast and multicast stay, as does a version. */
    private static String ipv4(String text) {
        String[] parts = text.split("\\.");
        int[] octets = new int[4];
        for (int i = 0; i < 4; ++i) {
            octets[i] = Integer.parseInt(parts[i]);
            if (octets[i] > 255) {
                return text;
            }
        }
        boolean noHost = octets[0] == 127 || (octets[0] >= 224 && octets[0] <= 239)
            || (octets[0] | octets[1] | octets[2] | octets[3]) == 0
            || (octets[0] & octets[1] & octets[2] & octets[3]) == 255;
        return noHost ? text : "<ip>";
    }

    /** A host's IPv6 address as <ip>; the loopback and :: stay, as does what is no address (a time, a MAC). */
    private static String ipv6(String text) {
        String address = text.contains("%") ? text.substring(0, text.indexOf('%')) : text;
        int compressed = address.indexOf("::");
        if (compressed != address.lastIndexOf("::") || address.contains(":::")) {
            return text;
        }
        List<String> groups = new ArrayList<>();
        for (String part : address.split(":", -1)) {
            if (!part.isEmpty()) {
                if (!IPV6_GROUP.matcher(part).matches()) {
                    return text;
                }
                groups.add(part);
            }
        }
        boolean whole = compressed < 0 && groups.size() == 8 && !address.startsWith(":") && !address.endsWith(":");
        if (!whole && (compressed < 0 || groups.size() > 7)) {
            return text;
        }
        boolean loopback = compressed >= 0 && (groups.isEmpty() || (groups.size() == 1 && address.equals("::" + groups.get(0))
            && Integer.parseInt(groups.get(0), 16) == 1));
        return loopback ? text : "<ip>";
    }

    /** The stand-in of a name learned, in any case, or null. */
    private String standIn(String name) {
        for (Map.Entry<String, String> known : mNames.entrySet()) {
            if (known.getKey().equalsIgnoreCase(name)) {
                return known.getValue();
            }
        }
        return null;
    }

    private String id(String uid) {
        String known = mIds.get(uid);
        if (known == null) {
            known = "id" + (mIds.size() + 1);
            mIds.put(uid, known);
        }
        return known;
    }

    /** {@code line} with each match's {@code group}, where it matched, replaced by what {@code standIn} makes of it. */
    private static String replace(Pattern pattern, String line, String group, Function<String, String> standIn) {
        Matcher match = pattern.matcher(line);
        StringBuilder out = null;
        int last = 0;
        while (match.find()) {
            if (match.group(group) == null) {
                continue;
            }
            if (out == null) {
                out = new StringBuilder(line.length());
            }
            out.append(line, last, match.start(group)).append(standIn.apply(match.group(group)));
            last = match.end(group);
        }
        return out == null ? line : out.append(line, last, line.length()).toString();
    }
}
