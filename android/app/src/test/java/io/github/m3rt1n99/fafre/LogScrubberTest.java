package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.junit.Test;

/**
 * The scrubber of the game's logs for a public issue (LogScrubber), on the shapes the logs' lines have: the report's (game.txt),
 * the engine's log's (its own lines and FAF's Lua's), the crash report's and a tombstone's summary, as the game on the emulator
 * and the engine's and FAF's sources write them, with invented names and documentation addresses.
 */
public class LogScrubberTest {
    private static final String APP = "io.github.m3rt1n99.fafre";
    private static final String EXTERNAL = "/storage/emulated/0/Android/data/" + APP;

    /** {@code lines} scrubbed as one zip's: every line learned first, then each scrubbed. */
    private static List<String> scrubbed(String... lines) {
        LogScrubber scrubber = new LogScrubber(APP);
        for (String line : lines) {
            scrubber.learn(line);
        }
        List<String> out = new ArrayList<>();
        for (String line : lines) {
            out.add(scrubber.scrub(line));
        }
        return out;
    }

    /** Each pair: a line, then what the scrubber makes of it, all of them one zip's. */
    private static void assertScrubbed(String... lineThenScrubbed) {
        String[] lines = new String[lineThenScrubbed.length / 2];
        List<String> expected = new ArrayList<>();
        for (int i = 0; i < lines.length; ++i) {
            lines[i] = lineThenScrubbed[2 * i];
            expected.add(lineThenScrubbed[2 * i + 1]);
        }
        assertEquals(expected, scrubbed(lines));
    }

    @Test
    public void aLaunchersCommandLineKeepsItsSwitchesWithoutTheAccountTheGameAndTheFolders() {
        assertScrubbed(
            "game: command line " + EXTERNAL + "/files/parity/game/3841/bin/ForgedAlliance.exe /init " + EXTERNAL
                + "/files/parity/game/3841/bin/init_game.lua /log " + EXTERNAL + "/files/game/engine.sclog /notelemetry "
                + "/fafre_hud_scale 1.67 /fafre_hud_most 1.67 /gpgnet 127.0.0.1:7237 /savereplay "
                + "gpgnet://127.0.0.1:41011/24000001/Kestrel.SCFAreplay /mean 1523.75 /deviation 87.5 /numgames 312 /country DE "
                + "/clan AB-1_ /division gold /subdivision II /team 2 /startspot 1 /players 4 /fullscreen 1707 768",
            "game: command line <external>/files/parity/game/3841/bin/ForgedAlliance.exe /init "
                + "<external>/files/parity/game/3841/bin/init_game.lua /log <external>/files/game/engine.sclog /notelemetry "
                + "/fafre_hud_scale 1.67 /fafre_hud_most 1.67 /gpgnet 127.0.0.1:7237 /savereplay "
                + "gpgnet://127.0.0.1:41011/<game>/player1.SCFAreplay /mean <account> /deviation <account> /numgames <account> "
                + "/country <account> /clan <account> /division <account> /subdivision <account> /team 2 /startspot 1 /players 4 "
                + "/fullscreen 1707 768",
            "game: command line /data/app/x/bin/ForgedAlliance.exe /replay gpgnet://localhost:15000/24000002/Marlin.SCFAreplay",
            "game: command line /data/app/x/bin/ForgedAlliance.exe /replay gpgnet://localhost:15000/<game>/player2.SCFAreplay",
            // FAF's Lua names the game it saves the replay of.
            "info: REPLAY ID: 24000001", "info: REPLAY ID: <game>",
            "game: command line " + EXTERNAL + "/files/parity/game/3835/bin/ForgedAlliance.exe /replay /data/user/0/" + APP
                + "/files/launcher-replay.SCFAreplay /fullscreen 1707 768",
            "game: command line <external>/files/parity/game/3835/bin/ForgedAlliance.exe /replay "
                + "<internal>/files/launcher-replay.SCFAreplay /fullscreen 1707 768",
            // A launcher's replay that could not be read, in Android's words.
            "game: the start failed, REPLAY: the replay: java.io.FileNotFoundException: "
                + "content://com.example.client.replays/replays/24000001-Kestrel.SCFAreplay",
            "game: the start failed, REPLAY: the replay: java.io.FileNotFoundException: content://com.example.client.replays/<path>",
            // FAF's Lua names the switches it found, without their values.
            "info: /clan", "info: /clan");
    }

    @Test
    public void aSkirmishsPlayerIsAStandInAndItsAisAndCiviliansStay() {
        assertScrubbed(
            "info: Saving replay to \"" + EXTERNAL + "/files/game/home/Documents\\My Games\\Gas Powered Games\\Supreme Commander "
                + "Forged Alliance\\replays\\Kestrel\\LastGame.SCFAReplay\"",
            "info: Saving replay to \"<external>/files/game/home/Documents\\My Games\\Gas Powered Games\\Supreme Commander Forged "
                + "Alliance\\replays\\player1\\LastGame.SCFAReplay\"",
            "info: Initialize Armies brain nickname is Kestrel", "info: Initialize Armies brain nickname is player1",
            "info: Initialize Armies brain nickname is Wyxe (AI: Rush)", "info: Initialize Armies brain nickname is Wyxe (AI: Rush)",
            "info: Initialize Skirmish for Wyxe (AI: Rush)", "info: Initialize Skirmish for Wyxe (AI: Rush)",
            "info: Initialize Armies brain nickname is Civilian", "info: Initialize Armies brain nickname is Civilian",
            // Desert Arena's civilians.
            "info: Initialize Armies brain nickname is civilian", "info: Initialize Armies brain nickname is civilian",
            "info: *AI DEBUG: ARMY \t6\t: Initiating Archetype using SetonsCustom",
            "info: *AI DEBUG: ARMY \t6\t: Initiating Archetype using SetonsCustom",
            "info: LOBBY: assigning ourselves the uid of 0", "info: LOBBY: assigning ourselves the uid of id1",
            "debug: GpgNetSend\tAIOption\tJagels (AI: Easy)\tArmy\t2", "debug: GpgNetSend\tAIOption\tJagels (AI: Easy)\tArmy\t2");
    }

    @Test
    public void fafsMessagesToTheLauncherHaveStandInsForTheirPlayersAndTheirChatGoes() {
        // FAF's GpgNetSend logs each message to the launcher, its arguments tab-separated.
        assertScrubbed(
            "debug: GpgNetSend\tPlayerOption\t104857\tFaction\t2", "debug: GpgNetSend\tPlayerOption\tid1\tFaction\t2",
            "debug: GpgNetSend\tEstablishedPeer\t2", "debug: GpgNetSend\tEstablishedPeer\tid2",
            "debug: GpgNetSend\tDisconnected\t2", "debug: GpgNetSend\tDisconnected\tid2",
            "debug: GpgNetSend\tTeamkillReport\t312.5\t2\tMarlin\t104857\tKestrel",
            "debug: GpgNetSend\tTeamkillReport\t312.5\tid2\tplayer1\tid1\tplayer2",
            "debug: GpgNetSend\tChat\tmeet me at 10.0.2.18, Marlin", "debug: GpgNetSend\tChat\t<chat>",
            "debug: GpgNetSend\tJsonStats\t{\"stats\":[{\"type\":\"Human\",\"name\":\"Heron\",\"faction\":2},{\"type\":\"AI\","
                + "\"name\":\"Sleepy (AI: Easy)\"},{\"name\":\"civilian\"}]}",
            "debug: GpgNetSend\tJsonStats\t{\"stats\":[{\"type\":\"Human\",\"name\":\"player3\",\"faction\":2},{\"type\":\"AI\","
                + "\"name\":\"Sleepy (AI: Easy)\"},{\"name\":\"civilian\"}]}",
            "debug: GpgNetSend\tGameState\tLobby", "debug: GpgNetSend\tGameState\tLobby",
            "debug: GpgNetSend\tGameResult\t1\tdefeat -10", "debug: GpgNetSend\tGameResult\t1\tdefeat -10",
            "info: Heron's stats sent", "info: player3's stats sent");
    }

    @Test
    public void aReplaysPlayersAreStandInsWhereverTheirNamesStand() {
        assertScrubbed(
            "info: FAF game version 3835 (replay), sim fixes 0x00", "info: FAF game version 3835 (replay), sim fixes 0x00",
            "info: Initialize Armies brain nickname is Kestrel", "info: Initialize Armies brain nickname is player1",
            "info: Initialize Armies brain nickname is Marlin", "info: Initialize Armies brain nickname is player2",
            // Anywhere else, in any case, as a word of its own.
            "warning: M28 Marlin's ACU at 120,80; kestrel scouts", "warning: M28 player2's ACU at 120,80; player1 scouts",
            "info: KESTREL Kestrelfish kestrel_2 Kestrel-x (Kestrel).", "info: player1 Kestrelfish kestrel_2 Kestrel-x (player1).");
    }

    @Test
    public void aNameThatAnotherBeginsWithGoesAfterItAndAnAiOrANumberIsNoName() {
        LogScrubber scrubber = new LogScrubber(APP);
        for (String line : new String[] {"info: Initialize Armies brain nickname is Bob", "info: LOBBY: Adding peer \"Bob Lee\" "
            + "[192.0.2.5:6112, uid=3]", "info: Initialize Armies brain nickname is 1234", "info:   PlayerName=\"Flounder (AI: Easy)\",",
            "info: Initialize Armies brain nickname is  "}) {
            scrubber.learn(line);
        }
        Map<String, String> names = new LinkedHashMap<>();
        names.put("Bob", "player1");
        names.put("Bob Lee", "player2");
        assertEquals(names, scrubber.names());
        assertEquals("info: player2 and player1, Bobby 1234", scrubber.scrub("info: Bob Lee and Bob, Bobby 1234"));
    }

    @Test
    public void theLobbysPeersHaveStandInsForTheirNamesIdsAndAddresses() {
        assertScrubbed(
            "info: LOBBY: starting with local uid of 104857 [Kestrel]", "info: LOBBY: starting with local uid of id1 [player1]",
            "info: LOBBY: Game port 58404[UDP] opened.", "info: LOBBY: Game port 58404[UDP] opened.",
            "info: LOBBY: Got join (name=\"Marlin\", uid=2) from 10.0.2.17:16010",
            "info: LOBBY: Got join (name=\"player2\", uid=id2) from <ip>:16010",
            "info: LOBBY: Adding peer \"Marlin\" [10.0.2.17:16010, uid=2]", "info: LOBBY: Adding peer \"player2\" [<ip>:16010, uid=id2]",
            "info: LOBBY: \"Marlin\" [10.0.2.17:16010, uid=2] has established connections to: 104857, 2",
            "info: LOBBY: \"player2\" [<ip>:16010, uid=id2] has established connections to: id1, id2",
            "info: LOBBY: welcomed by host \"Osprey\" (uid=0)", "info: LOBBY: welcomed by host \"player3\" (uid=id3)",
            "info: LOBBY: assigned uid of 104857 by host", "info: LOBBY: assigned uid of id1 by host",
            "info: LOBBY: host thinks our uid is 104857, but we think it is 5", "info: LOBBY: host thinks our uid is id1, but we think it is id4",
            "info: LOBBY: received LOBMSG_DeletePeer, uid=2", "info: LOBBY: received LOBMSG_DeletePeer, uid=id2",
            "info: LOBBY: received discovery request from 10.0.2.16:15000", "info: LOBBY: received discovery request from <ip>:15000",
            "info: LOBBY: connection to \"Osprey\" [198.51.100.9:16010, uid=0] failed, retrying...",
            "info: LOBBY: connection to \"player3\" [<ip>:16010, uid=id3] failed, retrying...",
            "info: GPGNET: received nat packet from 203.0.113.7:6112", "info: GPGNET: received nat packet from <ip>:6112",
            // A mod's uid is no player's.
            "info: ModsManager activated: M28AI uid=1234abcd-0000-4000-8000-000000000001",
            "info: ModsManager activated: M28AI uid=1234abcd-0000-4000-8000-000000000001");
    }

    @Test
    public void fafsLobbiesLinesHaveStandInsAndTheTitleTheDiscoveryAndTheChatGo() {
        assertScrubbed(
            // FAF's autolobby: LOG's arguments, tab-separated.
            "info: CreateLobby\tUDP\t0\tKestrel\t104857", "info: CreateLobby\tUDP\t0\tplayer1\tid1",
            "info: HostGame\tKestrel's 2v2\t/maps/scmp_009/scmp_009_scenario.lua\tfalse",
            "info: HostGame\t<title>\t/maps/scmp_009/scmp_009_scenario.lua\tfalse",
            "info: JoinGame\t10.0.2.17:16010\tfalse\tMarlin\t2", "info: JoinGame\t<ip>:16010\tfalse\tplayer2\tid2",
            "info: ConnectToPeer\t198.51.100.4:6112\tHeron\t77", "info: ConnectToPeer\t<ip>:6112\tplayer3\tid3",
            "info: DisconnectFromPeer\t77\tfalse", "info: DisconnectFromPeer\tid3\tfalse",
            // FAF's lobby.
            "info: ConnectToPeer (name=Marlin, uid=2, address=10.0.2.17:16010, USE PROXY)",
            "info: ConnectToPeer (name=player2, uid=id2, address=<ip>:16010, USE PROXY)",
            "info: Peer Disconnected : (name=Heron, uid=77)", "info: Peer Disconnected : (name=player3, uid=id3)",
            "debug: Received data of type SetPlayerOptions from 2 (Marlin)", "debug: Received data of type SetPlayerOptions from id2 (player2)",
            "info: >> Osprey is missing map /maps/scmp_009/scmp_009_scenario.lua",
            "info: >> player4 is missing map /maps/scmp_009/scmp_009_scenario.lua",
            // A LAN game the screen found, lobbyComm's repr of its config line by line.
            "info: DiscoveryService.GameFound(0)", "info: DiscoveryService.GameFound(0)",
            "info:   Address=\"10.0.2.18:16010\",", "info:   Address=\"<ip>:16010\",",
            "info:   GameName=\"Plover's \\\"open\\\" game\",", "info:   GameName=\"<title>\",",
            "info:   HostedBy=\"Plover\",", "info:   HostedBy=\"player5\",",
            "info:   Hostname=\"10.0.2.18\",", "info:   Hostname=\"<ip>\",",
            // A chat line lobby.lua logs when it has no chat to show it in.
            "info: Can't add chat text -- no chat display", "info: Can't add chat text -- no chat display",
            "info: text=\"Plover: meet at 10.0.2.18\"", "info: text=<chat>");
    }

    @Test
    public void aTitleTheHostSetsAndTheTextsReprPutsInSingleQuotesGo() {
        assertScrubbed(
            // The title the host sets in FAF's lobby (SetGameOption), as GpgNetSend logs it: a name only it gives goes with it.
            "debug: GpgNetSend\tGameOption\tTitle\tKestrel and Osprey 2v2 no noobs", "debug: GpgNetSend\tGameOption\tTitle\t<title>",
            "debug: GpgNetSend\tGameOption\tSlots\t8", "debug: GpgNetSend\tGameOption\tSlots\t8",
            // repr puts a text with " and no ' in single quotes, as it is: a LAN game's config, the chat lobby.lua logs.
            "info:   GameName='Plover \"pro\" game',", "info:   GameName='<title>',",
            "info:   HostedBy='Plover \"P\" Smith',", "info:   HostedBy='player1',",
            "info:   PlayerName='Plover \"P\" Smith',", "info:   PlayerName='player1',",
            "info: text='Plover: he said \"gg\"'", "info: text=<chat>");
    }

    @Test
    public void aRejoinsSeatsTheLobbysPlayersAndAGameServersLobbyHaveStandInsForTheirIdsAndNames() {
        assertScrubbed(
            // A rejoin after a crash: the seats the launcher gives back on the command line, the game's offer and its return.
            "game: command line " + EXTERNAL + "/files/parity/game/3841/bin/ForgedAlliance.exe /gpgnet 127.0.0.1:7237 /rejoinoffer "
                + "/rejoinkeyfile /data/user/0/" + APP + "/no_backup/faf_rejoin_seat.key /con net_PeerFeatures=1;net_RejoinAccept=1 "
                + "/rejoin 104857=0,2=1,31415=12 1 0 /fullscreen 1707 768",
            "game: command line <external>/files/parity/game/3841/bin/ForgedAlliance.exe /gpgnet 127.0.0.1:7237 /rejoinoffer "
                + "/rejoinkeyfile <internal>/no_backup/faf_rejoin_seat.key /con net_PeerFeatures=1;net_RejoinAccept=1 "
                + "/rejoin id1=0,id2=1,id3=12 1 0 /fullscreen 1707 768",
            "info: REJOIN: told the launcher this client could come back: source 0, 104857=0,2=1,31415=12",
            "info: REJOIN: told the launcher this client could come back: source 0, id1=0,id2=1,id3=12",
            "info: REJOIN: taking source 1 back from peers 104857=0,2=1,31415=12",
            "info: REJOIN: taking source 1 back from peers id1=0,id2=1,id3=12",
            "game: the start failed, ARGUMENTS: REJOIN_PEERS is \"104857=0,2=1,\", not the RejoinOffer's <player id>=<source>,...",
            "game: the start failed, ARGUMENTS: REJOIN_PEERS is \"id1=0,id2=1,\", not the RejoinOffer's <player id>=<source>,...",
            // The engine's lobby: each player's build and style, a join it ignored, a peer it deleted.
            "info: PEERS: lobby player 104857:Kestrel runs our engine, features 000000000000003f, build 550e330",
            "info: PEERS: lobby player id1:player1 runs our engine, features 000000000000003f, build 550e330",
            "info: PEERS: lobby player 2:Marlin has style ff8800", "info: PEERS: lobby player id2:player2 has style ff8800",
            "info: LOBBY: assigning uid 77", "info: LOBBY: assigning uid id4",
            "info: LOBBY: ignoring unexpected join (name=\"Heron\", uid=77) from \"Marlin\" [198.51.100.7:6112, uid=2]",
            "info: LOBBY: ignoring unexpected join (name=\"player3\", uid=id4) from \"player2\" [<ip>:6112, uid=id2]",
            "info: LOBBY: deleting unknown peer uid 77.", "info: LOBBY: deleting unknown peer uid id4.",
            // A game on a game server, whose peers' addresses are 240.x.y.z.
            "info: Server session requested: 203.0.113.9:7001 as player 104857", "info: Server session requested: <ip>:7001 as player id1",
            "info: SERVER LOBBY: lobby made for \"Kestrel\" (uid 104857).", "info: SERVER LOBBY: lobby made for \"player1\" (uid id1).",
            "info: SERVER LOBBY: joining \"Osprey\" (uid 7) at 240.0.0.7:1000.",
            "info: SERVER LOBBY: joining \"player4\" (uid id5) at <ip>:1000.",
            "info: SERVER LOBBY: peer \"Marlin\" (uid 2) at 240.0.0.2:1000.", "info: SERVER LOBBY: peer \"player2\" (uid id2) at <ip>:1000.",
            "info: server driver: on, as client 0 (uid 104857, source 0), 2 armies",
            "info: server driver: on, as client 0 (uid id1, source 0), 2 armies",
            "info: SERVER LOBBY: uid 2 left.", "info: SERVER LOBBY: uid id2 left.",
            // A uid is a word of its own.
            "warning: a mod's words: fluid 100, guid=7", "warning: a mod's words: fluid 100, guid=7");
    }

    @Test
    public void aTeamKillsModeratorEventOnEveryPlayersSimHasStandInsForItsIdsAndNames() {
        // FAF's GpgNetSend sends each message of a game to the sims as well (ModeratorEvent), and each sim logs it: here
        // alone, as the log's last lines in an earlier game's crash report can have them.
        assertScrubbed(
            "debug: Moderator event for Kestrel: GpgNetSend with command 'TeamkillHappened' and data '312.5,2,Marlin,104857,Kestrel,'",
            "debug: Moderator event for player2: GpgNetSend with command 'TeamkillHappened' and data '312.5,id1,player1,id2,player2,'",
            "debug: Moderator event for Marlin: GpgNetSend with command 'TeamkillReport' and data '312.5,2,Marlin,104857,Kestrel,'",
            "debug: Moderator event for player1: GpgNetSend with command 'TeamkillReport' and data '312.5,id1,player1,id2,player2,'",
            "debug: Moderator event for Kestrel: GpgNetSend with command 'JsonStats' and data '{\"stats\":[{\"type\":\"Human\",\"name\":"
                + "\"Heron\",\"faction\":2},{\"type\":\"AI\",\"name\":\"Sleepy (AI: Easy)\"}]},'",
            "debug: Moderator event for player2: GpgNetSend with command 'JsonStats' and data '{\"stats\":[{\"type\":\"Human\",\"name\":"
                + "\"player3\",\"faction\":2},{\"type\":\"AI\",\"name\":\"Sleepy (AI: Easy)\"}]},'",
            "debug: Moderator event for Observer (source 3): GpgNetSend with command 'GameResult' and data '1,defeat -10,'",
            "debug: Moderator event for Observer (source 3): GpgNetSend with command 'GameResult' and data '1,defeat -10,'");
    }

    @Test
    public void anIdOfFafsSizeIsAStandInWhereverItStandsAsANumberOfItsOwnAndAShorterOneWhereTheLinesGiveIt() {
        assertScrubbed(
            "info: CreateLobby\tUDP\t0\tKestrel\t104857", "info: CreateLobby\tUDP\t0\tplayer1\tid1",
            "info: ConnectToPeer\t198.51.100.4:6112\tMarlin\t2", "info: ConnectToPeer\t<ip>:6112\tplayer2\tid2",
            // Lines that no shape knows, the in-engine links' of the desktop build (NetlinkPeers).
            "info: NETLINK: peer Kestrel(104857) closed", "info: NETLINK: peer player1(id1) closed",
            "info: NETLINK: ICE message for unknown peer 2", "info: NETLINK: ICE message for unknown peer 2",
            "info: 104857, (104857) 104857. 104857:6112 x=104857 '104857'", "info: id1, (id1) id1. id1:6112 x=id1 'id1'",
            // No part of a word, a decimal, a version or a mod's uid.
            "info: 1048570 1104857 104857.5 0.104857 2.104857.1 v104857 104857a 0x104857 mod-104857 104857-b",
            "info: 1048570 1104857 104857.5 0.104857 2.104857.1 v104857 104857a 0x104857 mod-104857 104857-b");
    }

    @Test
    public void theAddressesOfHostsGoAndTheLoopbackTheBroadcastVersionsAndTimesStay() {
        assertScrubbed(
            // A GPGNet Test of a peer's ICE message (gpgnet_host.py --ice-msg), which the engine logs: its ids, credentials and
            // candidates.
            "info: GPGNET: test message, 2 args", "info: GPGNET: test message, 2 args",
            "info:  arg[0]=4243 [int]", "info:  arg[0]=id1 [int]",
            "info:  arg[1]=\"{\"srcId\":4243,\"destId\":4242,\"password\":\"6cmvb8d7e03sma550p6dtta5vn\",\"ufrag\":\"eb31i1k68fj1m5\","
                + "\"candidates\":[{\"protocol\":\"udp\",\"ip\":\"203.0.113.1\",\"port\":6112,\"relAddr\":\"10.0.2.20\"},"
                + "{\"ip\":\"2001:db8::42\"},{\"ip\":\"fe80::5054:ff:fe12:3456%wlan0\"}]}\" [str]",
            "info:  arg[1]=\"{\"srcId\":id1,\"destId\":id2,\"password\":\"<secret>\",\"ufrag\":\"<secret>\",\"candidates\":"
                + "[{\"protocol\":\"udp\",\"ip\":\"<ip>\",\"port\":6112,\"relAddr\":\"<ip>\"},{\"ip\":\"<ip>\"},{\"ip\":\"<ip>\"}]}\" [str]",
            "info: 10.0.2.16 10.0.2.19. [2001:db8:0:0:0:0:0:7]:6112 ::ffff:192.0.2.1",
            "info: <ip> <ip>. [<ip>]:6112 ::ffff:<ip>",
            "info: 127.0.0.1:7237 0.0.0.0:0 255.255.255.255:15000 224.0.0.251 ::1 [::]:0",
            "info: 127.0.0.1:7237 0.0.0.0:0 255.255.255.255:15000 224.0.0.251 ::1 [::]:0",
            "info: Vulkan 1.3.275.0, API 1.3.0, build 999.1.1.1, v1.2.3.4.5, mac 52:54:00:12:34:56",
            "info: Vulkan 1.3.275.0, API 1.3.0, build 999.1.1.1, v1.2.3.4.5, mac 52:54:00:12:34:56",
            "debug: Session time: 00:00:17\tGame time: 00:00:10\tHeap: 197.8M / 197.8M",
            "debug: Session time: 00:00:17\tGame time: 00:00:10\tHeap: 197.8M / 197.8M",
            "  #04 libfafgame.so+0x9ac48d  Moho::CScApp::Main()+52 fa::thread::Run dead::beef",
            "  #04 libfafgame.so+0x9ac48d  Moho::CScApp::Main()+52 fa::thread::Run <ip>");
    }

    @Test
    public void theAppsFoldersAreShortenedAsAndroidAndTheEngineWriteThemAndTheUsersGo() {
        assertScrubbed(
            "info: Loading custom vault path: " + EXTERNAL + "/files/game/vault",
            "info: Loading custom vault path: <external>/files/game/vault",
            "info: DISK: AddSearchPath: '\\storage\\emulated\\0\\android\\data\\" + APP + "\\files\\scfa\\gamedata\\\\units.scd', "
                + "mounted as '/'",
            "info: DISK: AddSearchPath: '<external>\\files\\scfa\\gamedata\\\\units.scd', mounted as '/'",
            "info:   shader cache: " + EXTERNAL + "/files/game/home/.local/share/Gas Powered Games/Supreme Commander Forged "
                + "Alliance/cache (0 files, 0.0 MB)",
            "info:   shader cache: <external>/files/game/home/.local/share/Gas Powered Games/Supreme Commander Forged "
                + "Alliance/cache (0 files, 0.0 MB)",
            "/sdcard/Android/data/" + APP + "/files /mnt/user/0/emulated/0/Android/data/" + APP + " /data/media/0/Android/data/"
                + APP + "/files", "<external>/files <external> <external>/files",
            "info: Replay timeline: keyframes go in /data/user/0/" + APP + "/cache/faf_replay_keyframes /data/data/" + APP
                + "/files/game.txt", "info: Replay timeline: keyframes go in <internal>/cache/faf_replay_keyframes <internal>/files/game.txt",
            // The crash report's modules: the app's install folder goes, Android's own stay.
            "/data/app/~~Qm3xT8bCzA0eYf3lJmIuWg==/" + APP + "-9ZtjYw7eYgUlDqVb2i5n1A==/lib/x86_64/libfafgame.so",
            "<apk>/lib/x86_64/libfafgame.so",
            "exe       : /system/bin/app_process64 /system/lib64/libc.so /vendor/lib64/hw/vulkan.ranchu.so /proc/self/maps",
            "exe       : /system/bin/app_process64 /system/lib64/libc.so /vendor/lib64/hw/vulkan.ranchu.so /proc/self/maps",
            // The user's own folders, and another app's.
            "from /storage/emulated/0/Download/SCFA.zip, /sdcard/Documents/a.txt and /storage/1A2B-3C4D/Games/SCFA\"",
            "from <storage> <storage> and <storage>\"",
            "/storage/emulated/0/Android/data/" + APP + ".other/files/x /storage/emulated/10 /sdcard",
            "<storage> <storage> <storage>");
    }

    @Test
    public void aCrashReportsLuaStringsAndATombstonesAbortLoseTheNamesTheZipLearned() {
        assertScrubbed(
            "info: Initialize Armies brain nickname is Kestrel", "info: Initialize Armies brain nickname is player1",
            "  VM 1   [0] \"attempt to index a nil value (field 'Kestrel')\"", "  VM 1   [0] \"attempt to index a nil value (field 'player1')\"",
            "  [UI] ....fafre\\files\\parity\\game\\3835\\bin\\..\\gamedata\\\\lua.nx2\\lua\\ui\\uimain.lua(315): unfinished string",
            "  [UI] ....fafre\\files\\parity\\game\\3835\\bin\\..\\gamedata\\\\lua.nx2\\lua\\ui\\uimain.lua(315): unfinished string",
            "abort: Assert failed: player KESTREL at 10.0.2.15", "abort: Assert failed: player player1 at <ip>");
    }

    @Test
    public void aLineWithNothingOfThatStaysAsItIs() {
        List<String> lines = Arrays.asList("info: Engine build: d8ee086 x64-android Release (android-client-upstream-56e06b3, "
            + "android-v0.14.0-1075-gd8ee086)", "debug: SND: Loaded WaveBank 'aeonselect' at 0x7a0d00013130",
            "1574 ms: renderer: Vulkan: BC textures are the GPU's", "END: the program returned 0 after 84.4 s", "", "\t");
        assertEquals(lines, scrubbed(lines.toArray(new String[0])));
    }
}
