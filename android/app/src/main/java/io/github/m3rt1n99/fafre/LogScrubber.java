package io.github.m3rt1n99.fafre;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The game's logs made fit for a public issue (GameLogs, the launcher's game_logs), line by line: what names a player, the
 * player's account, the network's hosts or the user's folders becomes a placeholder (android/README.md, The logs). Nothing
 * marks a player's name or id where it stands, so they are learned first from the lines that give them, in every file of
 * one zip (learn), and then replaced (scrub), each by the same stand-in within one scrubber, numbered in the order learned:
 * a name by playerN wherever it stands, in any case; an id by idN where those lines give it, and one of FAF's size
 * (FAF_ID) wherever it stands as a number of its own. No Android in here, for the tests.
 */
final class LogScrubber {
    /** The lines that name a player, the name in the group "name": the engine's lobby, FAF's Lua, the game's replays. */
    private static final Pattern[] NAMES = {
        // FAF's ScenarioUtilities, for each army as the sim begins: a player's, an AI's or the civilians'.
        Pattern.compile("Initialize Armies brain nickname is (?<name>.+)$"),
        // The engine's peer (SPeer::ToString), in most of its lobby's lines.
        Pattern.compile("\"(?<name>[^\"]*)\" \\[[^\\]\\s]*:\\d+, uid=-?\\d+\\]"),
        Pattern.compile("LOBBY: starting with local uid of -?\\d+ \\[(?<name>.*)\\]$"),
        Pattern.compile("LOBBY: (?:Got|ignoring unexpected) join \\(name=\"(?<name>.*)\", uid=-?\\d+\\) from "),
        Pattern.compile("LOBBY: welcomed by host \"(?<name>.*)\" \\(uid=\\d+\\)"),
        Pattern.compile("LOBBY: host renamed us to (?<name>.+)$"),
        // The lobby of a game on a game server (ServerLobbyUi): this player's, the host's and a peer's.
        Pattern.compile("SERVER LOBBY: (?:lobby made for|joining|peer) \"(?<name>.*)\" \\(uid -?\\d+\\)"),
        // The engine's word on each lobby player's build and style (LobbyPeerFeatures).
        Pattern.compile("PEERS: lobby player -?\\d+:(?<name>.*?) (?:runs our engine|gave a style proof|has style)"),
        // FAF's autolobby, a matchmaker game's lobby: LOG's arguments, which the engine joins with tabs.
        Pattern.compile("(?:CreateLobby|JoinGame)\t[^\t]*\t[^\t]*\t(?<name>[^\t]*)\t"),
        Pattern.compile("ConnectToPeer\t[^\t]*\t(?<name>[^\t]*)\t"),
        // FAF's lobby, and the LAN games its screen finds (lobbyComm logs each one's config with repr, which quotes a string
        // with " and no ' in single quotes, as it is).
        Pattern.compile("ConnectToPeer \\(name=(?<name>.*), uid=\\d+, address="),
        Pattern.compile("Peer Disconnected : \\(name=(?<name>.*), uid=\\d+\\)"),
        Pattern.compile("Received data of type .* from -?\\d+ \\((?<name>.*)\\)$"),
        Pattern.compile(">> (?<name>.+) is missing map "),
        Pattern.compile("\\b(?:HostedBy|PlayerName) ?= ?\"(?<name>[^\"]*)\""),
        Pattern.compile("\\b(?:HostedBy|PlayerName) ?= ?'(?<name>[^']*)'"),
        // The profile's folder of the game's replays: <HOME>/Documents\...\Supreme Commander Forged Alliance\replays\<profile>\.
        Pattern.compile("(?i)Forged Alliance[\\\\/]replays[\\\\/](?<name>[^\\\\/\"]+)[\\\\/]"),
        // A launcher's replay, which FAF's Java client names after the player: gpgnet://<host>/<game id>/<player>.SCFAreplay.
        Pattern.compile("gpgnet://[^/\\s\"]*/\\d+/(?<name>[^/\\s\"]+)\\.(?i:scfareplay)"),
        // FAF's GpgNetSend, which logs each message to the launcher with its arguments: a team kill's victim and instigator.
        Pattern.compile("GpgNetSend\tTeamkill(?:Report|Happened)\t[^\t]*\t[^\t]*\t(?<name>[^\t]*)\t"),
        Pattern.compile("GpgNetSend\tTeamkill(?:Report|Happened)(?:\t[^\t]*){4}\t(?<name>[^\t]*)$"),
        // The same in a game, on every player's sim as well: a moderator event, its arguments each followed by a comma.
        Pattern.compile("GpgNetSend with command 'Teamkill(?:Report|Happened)' and data '(?:[^,]*,){2}(?<name>[^,]*),"),
        Pattern.compile("GpgNetSend with command 'Teamkill(?:Report|Happened)' and data '(?:[^,]*,){4}(?<name>[^,]*),"),
    };
    /**
     * The players in the game's stats, which GpgNetSend logs as they go to the launcher (JsonStats, UserSync.lua), and every
     * player's sim again as a moderator event.
     */
    private static final Pattern STATS_NAME = Pattern.compile("\"name\" ?: ?\"(?<name>(?:[^\"\\\\]|\\\\.)*)\"");
    /**
     * No player's: an AI's nickname, "<name> (AI: <type>)" as FAF's lobby makes it, the civilians' army's (Civilian, civilian
     * on a map that names it so), and a name without a letter, which FAF's names begin with, where the stand-in would take
     * every such number.
     */
    private static final Pattern NO_PLAYER = Pattern.compile(".* \\(AI: [^)]*\\)|(?i:civilian)|[^\\p{L}]*");

    /**
     * Where the lines give a player's id, its uid, in the group "uid": in a game of FAF's the uid is the player's id there,
     * whose page names the player.
     */
    private static final Pattern[] IDS = {
        // The engine's lobby (CLobby, LobbyPeerFeatures), a game server's (ServerLobbyUi, the server driver, ServerConnect):
        // uid=N, uid of N, our uid is N, assigning uid N, (uid N), ...
        Pattern.compile("\\buid(?:=| of | is | )(?<uid>-?\\d+)(?![\\w-])"),
        Pattern.compile("we think it is (?<uid>-?\\d+)(?![\\w-])"),
        Pattern.compile("PEERS: lobby player (?<uid>-?\\d+):"),
        Pattern.compile("Server session requested: \\S* as player (?<uid>-?\\d+)"),
        // FAF's autolobby and lobby.
        Pattern.compile("(?:CreateLobby|JoinGame)\t[^\t]*\t[^\t]*\t[^\t]*\t(?<uid>-?\\d+)"),
        Pattern.compile("ConnectToPeer\t[^\t]*\t[^\t]*\t(?<uid>-?\\d+)"),
        Pattern.compile("DisconnectFromPeer\t(?<uid>-?\\d+)"),
        Pattern.compile("Received data of type .* from (?<uid>-?\\d+) \\("),
        // GpgNetSend's messages that name a player by id: its options, its connections, a team kill's victim and instigator.
        Pattern.compile("GpgNetSend\t(?:PlayerOption|Disconnected|EstablishedPeer|DisconnectedPeer)\t(?<uid>-?\\d+)(?![^\t])"),
        Pattern.compile("GpgNetSend\tTeamkill(?:Report|Happened)\t[^\t]*\t(?<uid>-?\\d+)\t"),
        Pattern.compile("GpgNetSend\tTeamkill(?:Report|Happened)(?:\t[^\t]*){3}\t(?<uid>-?\\d+)\t"),
        Pattern.compile("GpgNetSend with command 'Teamkill(?:Report|Happened)' and data '[^,]*,(?<uid>-?\\d+),"),
        Pattern.compile("GpgNetSend with command 'Teamkill(?:Report|Happened)' and data '(?:[^,]*,){3}(?<uid>-?\\d+),"),
        // A peer's ICE message (native mode's IceMsg, CandidatesMessage), and the numbers of a GPGNet Test the engine logs.
        Pattern.compile("\"(?:srcId|destId)\" ?: ?(?<uid>-?\\d+)"),
        Pattern.compile("^\\w+: +arg\\[\\d+\\]=(?<uid>-?\\d+) \\[int\\]$"),
    };
    /** A rejoin's seats, {@code <player id>=<source>,...}, in the group "list". */
    private static final String SEATS = "(?<list>\\d+=\\d+(?:,\\d+=\\d+)*)";
    /**
     * Lists of players' ids, in the group "list": the peers a peer of the lobby reached, and a rejoin's seats as the game
     * offers them to its launcher (PeerFeatures), the launcher gives them back (/rejoin on the command line, or REJOIN_PEERS
     * where the start refused them) and the rejoin takes them (CRejoinFeed).
     */
    private static final Pattern[] ID_LISTS = {
        Pattern.compile("has established connections to: (?<list>-?\\d+(?:, -?\\d+)*)"),
        Pattern.compile("could come back: source -?\\d+, " + SEATS),
        Pattern.compile("/rejoin " + SEATS),
        Pattern.compile("REJOIN_PEERS is \"" + SEATS),
        Pattern.compile("back from peers " + SEATS),
    };
    /** An id in such a list: each number but a seat's source, which follows its "=". */
    private static final Pattern LISTED_ID = Pattern.compile("(?<![=\\d])(?<uid>-?\\d+)");
    /**
     * An id that is replaced wherever it stands: one of FAF's size. Only FAF's first 99 accounts have fewer digits, and a
     * number of one or two digits, as a LAN game's uids 0, 1, 2, ... are, stands for too much else in the logs.
     */
    private static final Pattern FAF_ID = Pattern.compile("\\d{3,}");
    /** A replay's game on FAF, whose page names its players: gpgnet://<host>/<game id>/..., and FAF's line of it. */
    private static final Pattern[] GAMES = {
        Pattern.compile("gpgnet://[^/\\s\"]*/(?<game>\\d+)/"),
        Pattern.compile("REPLAY ID: (?<game>\\d+)"),
    };
    /** An ICE session's credentials in a peer's ICE message. */
    private static final Pattern SECRET = Pattern.compile("\"(?:password|ufrag)\" ?: ?\"(?<secret>[^\"]*)\"");
    /**
     * A game's title, which its host chose: FAF's autolobby's HostGame, the title the host sets in FAF's lobby, which
     * GpgNetSend logs (SetGameOption), and a LAN game's config (lobbyComm's repr, in either of its quotes).
     */
    private static final Pattern[] TITLES = {
        Pattern.compile("HostGame\t(?<title>[^\t]*)\t"),
        Pattern.compile("GpgNetSend\tGameOption\tTitle\t(?<title>.*)$"),
        Pattern.compile("\\bGameName ?= ?\"(?<title>(?:[^\"\\\\]|\\\\.)*)\""),
        Pattern.compile("\\bGameName ?= ?'(?<title>[^']*)'"),
    };
    /**
     * A chat message: what FAF's lobby logs when it has no chat to show it in (lobby.lua's AddChatText: "text=" and repr, in
     * either of its quotes), and the lobby's chat that goes to the launcher (GpgNetSend Chat).
     */
    private static final Pattern[] CHATS = {
        Pattern.compile("^\\w+: text=(?<chat>\".*\"|'.*')$"),
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
    /** The players' ids as learned, and their stand-ins: id1, id2, ... */
    private final Map<String, String> mIds = new LinkedHashMap<>();
    /** mTokens with the names and FAF's ids learned so far, built when a line is scrubbed after a new one. */
    private Pattern mPattern;
    /**
     * A matcher of each pattern that the lines go through, reset for each line: on Android making one (ICU's) costs twice
     * what its find does on a line of the logs.
     */
    private final Map<Pattern, Matcher> mMatchers = new HashMap<>();

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

    /** Learns the players' names and ids {@code line} gives, which scrub then replaces in every line. */
    void learn(String line) {
        for (Pattern shape : NAMES) {
            learn(shape, line);
        }
        if (line.contains("GpgNetSend\tJsonStats\t") || line.contains("GpgNetSend with command 'JsonStats'")) {
            learn(STATS_NAME, line);
        }
        // The ids it gives, each learned as withIds first gives its stand-in; the line stays as it is.
        withIds(line);
    }

    private void learn(Pattern shape, String line) {
        Matcher match = matcher(shape, line);
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
        return tokens(withIds(line));
    }

    /** {@code line} with the stand-ins of the ids where the lines give them. */
    private String withIds(String line) {
        for (Pattern shape : IDS) {
            line = replace(shape, line, "uid", this::id);
        }
        for (Pattern shape : ID_LISTS) {
            line = replace(shape, line, "list", list -> replace(LISTED_ID, list, "uid", this::id));
        }
        return line;
    }

    /** The paths, addresses, names and FAF's ids of {@code line}, in one pass: a stand-in is never read again. */
    private String tokens(String line) {
        if (mPattern == null) {
            List<String> fafIds = new ArrayList<>();
            for (String uid : mIds.keySet()) {
                if (FAF_ID.matcher(uid).matches()) {
                    fafIds.add(uid);
                }
            }
            String names = alternatives(mNames.keySet());
            String ids = alternatives(fafIds);
            mPattern = Pattern.compile(mTokens
                + (names.isEmpty() ? "" : "|(?<name>(?<![\\w-])(?iu:" + names + ")(?![\\w-]))")
                // A number of its own: no part of a word, a decimal, a version or a mod's uid.
                + (ids.isEmpty() ? "" : "|(?<![\\w.-])(?:" + ids + ")(?![\\w-]|\\.\\d)"));
        }
        Matcher match = matcher(mPattern, line);
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

    /** {@code texts} quoted as a pattern's alternatives, a text that another begins with after it ("Bob Lee" before "Bob"). */
    private static String alternatives(Collection<String> texts) {
        List<String> longestFirst = new ArrayList<>(texts);
        longestFirst.sort((a, b) -> b.length() - a.length());
        StringBuilder out = new StringBuilder();
        for (String text : longestFirst) {
            out.append(out.length() > 0 ? "|" : "").append(Pattern.quote(text));
        }
        return out.toString();
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
        // A name or an id learned: an id is digits alone, a name has a letter (NO_PLAYER).
        String id = mIds.get(match.group());
        return id != null ? id : standIn(match.group());
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

    /** The stand-in of the id {@code uid}, a new one for an id not met before. */
    private String id(String uid) {
        String known = mIds.get(uid);
        if (known == null) {
            known = "id" + (mIds.size() + 1);
            mIds.put(uid, known);
            if (FAF_ID.matcher(uid).matches()) {
                mPattern = null;
            }
        }
        return known;
    }

    /**
     * {@code line} with each match's {@code group}, where it matched, replaced by what {@code standIn} makes of it, which may
     * replace with another pattern.
     */
    private String replace(Pattern pattern, String line, String group, Function<String, String> standIn) {
        Matcher match = matcher(pattern, line);
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

    /** This scrubber's matcher of {@code pattern}, on {@code line}. */
    private Matcher matcher(Pattern pattern, String line) {
        return mMatchers.computeIfAbsent(pattern, shape -> shape.matcher("")).reset(line);
    }
}
