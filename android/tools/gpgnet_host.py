#!/usr/bin/env python3
"""A launcher's GPGNet side for the game on the emulator or a phone, as the FAF client in the app will be.

It listens on 127.0.0.1:<port>, prints what the game sends and what it answers, and answers the game's
GameState Idle with CreateLobby, as FAF's ICE adapter and a launcher in native mode do
(docs/GPGNET_MESSAGES.md, docs/LAUNCHER_NATIVE_NETWORK.md). The game on a device reaches it through adb:

    adb reverse tcp:7237 tcp:7237
    python -I android/tools/gpgnet_host.py --port 7237
    adb shell am start -S -n io.github.m3rt1n99.fafre/.GameActivity \\
        --esa io.github.m3rt1n99.fafre.extra.LAUNCHER_ARGUMENTS /gpgnet,127.0.0.1:7237

It ends when the game closes the connection, or --hold seconds after the game's lobby, and its status says
whether the game reached the lobby (GameState Lobby after CreateLobby): 0 yes, 1 no.

With --native it is a launcher in native mode, for a game started with /nativenet as well, which runs its peer
links itself: before CreateLobby it sends the ICE servers, --ice-servers, as IceServers.

With --port more than once it is also the lobby server, for one native game on each port (each device's own port
reversed to its own on the PC; it implies --native): the first game hosts (HostGame with --map) and each other joins
it as FAF's server has them do, JoinGame [host's name, id] to the joining game and ConnectToPeer [its name, id, 1] to
the host, with ConnectToPeer between the joining games, the newer offering; it relays each game's IceMsg
[receiver id, text] to the receiver's game as IceMsg [sender id, text], as the server routes them. The players are
--name with their number, their ids --uid, --uid + 1, ... It ends when a game closes its connection, or --hold
seconds after the host's lobby seated every other player, and its status is 0 when it did: when the host's game sent
a PlayerOption for each of the others' ids, which FAF's lobby sends once a player is in a slot.

With --play as well the games go on from the lobby into a game, which the host launches: the session ends when every
game has closed its connection, and its status is 0 when the host's lobby seated every other player, every game
launched (GameState Launching), a game reported the game's end (GameEnded) and none a desync (Desync). As FAF's server
does, a game whose connection closes before the host launched leaves the others' lobbies (DisconnectFromPeer [its id]
to each); after the launch the games handle a lost peer themselves, and an IceMsg for a gone game is dropped. A long
IceMsg's line names its CandidatesMessage's candidates by type, and its SHA-256 is the same where it goes on.

With --ice-msg BYTES (one game) it sends, before CreateLobby, a peer's ICE message of BYTES bytes as a launcher in
native mode relays one (IceMsg [sender id, CandidatesMessage JSON]), and the same arguments as Test, whose arguments
the game logs: the game reaches its lobby only when it has read both whole. It prints the text's length and SHA-256,
which the Test's line in the game's log has to match.

The wire format is the engine's (src/moho/net/CGpgNetInterface.cpp; tools/gpgnet/codec.go is the Go codec),
little-endian: a message is its command as a string, the count of its arguments as an int32 and each argument
as a tag byte (0 an int32, 1 a string, 2 data) and its body; a string is its length as an int32 and its bytes.
"""

import argparse
import collections
import hashlib
import json
import socket
import struct
import sys
import threading
import time

TAG_INT, TAG_STRING, TAG_DATA = 0, 1, 2
CANDIDATE_TYPES = ("HOST_CANDIDATE", "SERVER_REFLEXIVE_CANDIDATE", "RELAYED_CANDIDATE")


class Data(bytes):
    """A data argument (tag 2), as against a string."""


def encode(command, args):
    def string(value):
        return struct.pack("<I", len(value)) + value

    out = [string(command.encode()), struct.pack("<I", len(args))]
    for arg in args:
        if isinstance(arg, bool) or not isinstance(arg, (int, str, Data)):
            raise TypeError(f"{command}: GPGNet has no argument of type {type(arg).__name__}")
        if isinstance(arg, int):
            out.append(struct.pack("<Bi", TAG_INT, arg))
        elif isinstance(arg, Data):
            out.append(struct.pack("<B", TAG_DATA) + string(arg))
        else:
            out.append(struct.pack("<B", TAG_STRING) + string(arg.encode()))
    return b"".join(out)


def ice_message(size, sender, receiver):
    """A peer's CandidatesMessage as the Java adapter writes one (docs/LAUNCHER_ADAPTER_CONTRACT.md, Signalling
    message), size bytes of JSON: as many candidates as fit, the last one's id taking up the rest. The addresses are
    documentation addresses (RFC 5737)."""
    def candidate(index):
        kind = CANDIDATE_TYPES[index % len(CANDIDATE_TYPES)]
        host = kind == "HOST_CANDIDATE"
        return {"foundation": str(index), "protocol": "udp", "priority": 2130706431 - index,
                "ip": f"203.0.113.{index % 254 + 1}", "port": 6112 + index, "type": kind, "generation": 0, "id": "",
                "relAddr": None if host else "192.0.2.1", "relPort": 0 if host else 6112}

    message = {"srcId": sender, "destId": receiver, "password": "6cmvb8d7e03sma550p6dtta5vn",
               "ufrag": "eb31i1k68fj1m5", "candidates": [candidate(0)]}

    def text():
        return json.dumps(message, separators=(",", ":"))

    if len(text()) > size:
        raise ValueError(f"an ICE message with one candidate has {len(text())} bytes, more than {size}")
    while len(text()) <= size:
        message["candidates"].append(candidate(len(message["candidates"])))
    message["candidates"].pop()
    rest = size - len(text())
    message["candidates"][-1]["id"] = ("0123456789abcdef" * (rest // 16 + 1))[:rest]
    return text()


START = time.monotonic()


def candidates(text):
    """A CandidatesMessage's candidates by type, as ', 3 candidates: 3 HOST_CANDIDATE'; '' for any other text."""
    try:
        message = json.loads(text)
    except ValueError:
        return ""
    found = message.get("candidates") if isinstance(message, dict) else None
    if not isinstance(found, list):
        return ""
    kinds = collections.Counter(str(c.get("type")) if isinstance(c, dict) else "?" for c in found)
    return f", {len(found)} candidates: " + ", ".join(f"{count} {kind}" for kind, count in kinds.items())


def shown(arg):
    if isinstance(arg, Data):
        return f"<data {len(arg)} bytes>"
    if isinstance(arg, str) and len(arg) > 200:
        text = arg.encode()
        return f"<string {len(text)} bytes, SHA-256 {hashlib.sha256(text).hexdigest()}{candidates(arg)}>"
    return repr(arg)


def say(who, command, args=()):
    print(f"{time.monotonic() - START:8.2f} {who}: {command} {' '.join(map(shown, args))}".rstrip(), flush=True)


def note(text):
    print(f"{time.monotonic() - START:8.2f} {text}", flush=True)


class Connection:
    """One game's GPGNet connection. Its messages are read on one thread and sent from any."""

    def __init__(self, sock, label):
        self.sock = sock
        self.label = label
        self.send_lock = threading.Lock()

    def exact(self, count):
        data = bytearray()
        while len(data) < count:
            chunk = self.sock.recv(count - len(data))
            if not chunk:
                raise EOFError(f"the game closed the connection {len(data)} of {count} bytes into a field")
            data += chunk
        return bytes(data)

    def uint32(self):
        return struct.unpack("<I", self.exact(4))[0]

    def string(self):
        return self.exact(self.uint32())

    def receive(self):
        """The next message as (command, args), or None when the game closed the connection between two."""
        first = self.sock.recv(1)
        if not first:
            return None
        length = struct.unpack("<I", first + self.exact(3))[0]
        command = self.exact(length).decode(errors="replace")
        args = []
        for _ in range(self.uint32()):
            tag = self.exact(1)[0]
            if tag == TAG_INT:
                args.append(struct.unpack("<i", self.exact(4))[0])
            elif tag == TAG_STRING:
                args.append(self.string().decode(errors="replace"))
            elif tag == TAG_DATA:
                args.append(Data(self.string()))
            else:
                raise ValueError(f"{command}: an argument with the unknown tag {tag}")
        return command, args

    def send(self, command, *args):
        """Sends a message; a connection that broke is its reader's to report."""
        try:
            with self.send_lock:
                self.sock.sendall(encode(command, list(args)))
        except OSError as error:
            note(f"{command} not sent to {self.label}: {error}")
            return
        say(f"host>{self.label}" if self.label != "game" else "host", command, args)


class Seat:
    """One game: its listener's port, its player, and how far its lobby and its game have come."""

    def __init__(self, label, port, name, uid):
        self.label = label
        self.port = port
        self.name = name
        self.uid = uid
        self.server = None
        self.game = None
        self.gone = None  # how its connection ended, once it has
        self.lobby = False
        self.hosted = False  # the host: HostGame sent
        self.joined = False  # another game: JoinGame sent, and ConnectToPeer to the games before it
        self.seated = False  # another game: the host's PlayerOption named it
        self.launched = False  # its GameState Launching
        self.ended = False  # its GameEnded: its simulation reached the game's end
        self.desyncs = 0  # its Desync messages


class Session:
    """The launchers of the games and, with more than one, the lobby server between them."""

    def __init__(self, args, seats, ice_text):
        self.args = args
        self.seats = seats
        self.ice_text = ice_text
        self.lock = threading.Lock()
        self.finished = threading.Event()
        self.succeeded_at = None
        self.launched = False  # the host's GameState Launching, from which FAF's server has the game live

    def succeeded(self):
        if not all(seat.lobby for seat in self.seats):
            return False
        return all(seat.seated for seat in self.seats[1:])

    def played(self):
        """--play's goal: the lobby's, then every game launched, one reported the game's end and none a desync."""
        return (self.succeeded_at is not None and all(seat.launched for seat in self.seats)
                and any(seat.ended for seat in self.seats) and not any(seat.desyncs for seat in self.seats))

    def by_uid(self, uid):
        return next((seat for seat in self.seats if seat.uid == uid), None)

    def on_message(self, seat, command, values):
        """What the launcher, and the lobby server with more than one game, does with a message of the game's."""
        with self.lock:
            if command == "GameState" and values == ["Idle"]:
                if self.args.native:
                    seat.game.send("IceServers", self.args.ice_servers)
                if self.ice_text is not None:
                    seat.game.send("IceMsg", seat.uid + 1, self.ice_text)
                    seat.game.send("Test", seat.uid + 1, self.ice_text)
                seat.game.send("CreateLobby", self.args.mode, self.args.lobby_port, seat.name, seat.uid, 1)
            elif command == "GameState" and values == ["Lobby"] and not seat.lobby:
                seat.lobby = True
                if len(self.seats) > 1:
                    self.match()
            elif command == "GameState" and values == ["Launching"] and not seat.launched:
                seat.launched = True
                if seat is self.seats[0]:
                    self.launched = True
            elif command == "GameEnded":
                seat.ended = True
            elif command == "Desync":
                seat.desyncs += 1
            elif command == "IceMsg" and len(self.seats) > 1 and len(values) == 2 and isinstance(values[0], int):
                receiver = self.by_uid(values[0])
                if receiver is None or receiver.game is None or receiver.gone is not None:
                    note(f"{seat.label}'s IceMsg for player {values[0]} not delivered: no game of that player's")
                else:
                    receiver.game.send("IceMsg", seat.uid, values[1])
            elif command == "PlayerOption" and seat is self.seats[0] and values and str(values[0]).isdigit():
                other = self.by_uid(int(values[0]))
                if other is not None and other is not seat and not other.seated:
                    other.seated = True
                    note(f"the host's lobby seated {other.name} ({other.uid})")
            if self.succeeded_at is None and self.succeeded():
                self.succeeded_at = time.monotonic()
                if len(self.seats) > 1:
                    note("every game is in the host's lobby")
                if self.args.hold:
                    # A daemon, so that a session a game ended first does not wait for it to exit.
                    hold = threading.Timer(self.args.hold, self.finish, (f"held the lobby {self.args.hold:g} s",))
                    hold.daemon = True
                    hold.start()

    def match(self):
        """The server's part once games are in their lobbies (lock held): the host hosts, the others join it."""
        host = self.seats[0]
        if not host.lobby:
            return
        if not host.hosted:
            host.game.send("HostGame", self.args.map)
            host.hosted = True
        for seat in self.seats[1:]:
            if not seat.lobby or seat.joined:
                continue
            seat.game.send("JoinGame", host.name, host.uid)
            host.game.send("ConnectToPeer", seat.name, seat.uid, 1)
            for other in self.seats[1:]:
                if other.joined:
                    seat.game.send("ConnectToPeer", other.name, other.uid, 1)
                    other.game.send("ConnectToPeer", seat.name, seat.uid, 0)
            seat.joined = True

    def finish(self, why):
        if not self.finished.is_set():
            note(why)
            self.finished.set()

    def serve(self, seat):
        """One game's thread: its connection, then its messages until it or the session ends."""
        try:
            sock, peer = seat.server.accept()
        except socket.timeout:
            self.finish(f"no game connected to port {seat.port} in {self.args.timeout:g} s")
            return
        except OSError:
            return  # the session ended first, and closed the listener
        finally:
            seat.server.close()
        note(f"{seat.label} connected from {peer[0]}:{peer[1]}")
        with self.lock:
            seat.game = Connection(sock, seat.label)
        how = f"{seat.label}'s connection ended"
        with sock:
            try:
                while not self.finished.is_set():
                    message = seat.game.receive()
                    if message is None:
                        how = f"{seat.label} closed the connection"
                        break
                    command, values = message
                    say(seat.label, command, values)
                    self.on_message(seat, command, values)
            except (EOFError, ConnectionError, ValueError) as error:
                how = f"{seat.label}'s connection broke: {error}"
            except OSError:
                pass
        self.on_gone(seat, how)

    def on_gone(self, seat, how):
        """A game's connection ended: without --play the session ends; with it the session ends with the last game,
        and before the host's launch the games in the host's game drop the player, as FAF's server has them do."""
        with self.lock:
            if self.finished.is_set():
                seat.gone = f"the session's end closed it at {time.monotonic() - START:.2f} s"
                return
            seat.gone = f"{how} at {time.monotonic() - START:.2f} s"
            if not self.args.play:
                self.finish(how)
                return
            note(how)
            if not self.launched:
                for other in self.seats:
                    if other is not seat and (other.hosted or other.joined) and other.gone is None:
                        other.game.send("DisconnectFromPeer", seat.uid)
            if all(other.gone is not None for other in self.seats):
                self.finish("every game closed its connection")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--port", type=int, action="append",
                        help="the port to listen on (default 7237); more than once, one game on each")
    parser.add_argument("--timeout", type=float, default=180,
                        help="seconds to wait for the games' connections (default 180)")
    parser.add_argument("--mode", type=int, choices=(0, 1), default=0,
                        help="CreateLobby's mode: 0 a lobby, 1 an automatch lobby (default 0)")
    parser.add_argument("--lobby-port", type=int, default=0,
                        help="CreateLobby's UDP port for the game's traffic, 0 any (default 0)")
    parser.add_argument("--name", default="tester",
                        help="CreateLobby's player name (default tester), with several games followed by their number")
    parser.add_argument("--uid", type=int, default=1,
                        help="CreateLobby's player id (default 1), with several games the first one's")
    parser.add_argument("--hold", type=float, default=0,
                        help="seconds to hold the connections after the lobby, 0 until a game closes its own")
    parser.add_argument("--play", action="store_true",
                        help="the games go on into a game, and the session ends with the last game's connection "
                             "(several games, no --hold)")
    parser.add_argument("--native", action="store_true",
                        help="a launcher in native mode (/nativenet): IceServers before CreateLobby")
    parser.add_argument("--ice-servers", default="[]", metavar="JSON",
                        help="the IceServers text: a JSON list of {urls, username, credential}, or the API's ICE "
                             "session with it under servers (default [])")
    parser.add_argument("--map", default="scmp_009", help="the host's HostGame map, a folder's name (default scmp_009)")
    parser.add_argument("--ice-msg", type=int, default=0, metavar="BYTES",
                        help="before CreateLobby, send a peer's ICE message of BYTES bytes as IceMsg and as Test "
                             "(default 0: none; one game only)")
    args = parser.parse_args()
    ports = args.port or [7237]
    if len(set(ports)) != len(ports):
        parser.error("--port: each game needs a port of its own")
    if len(ports) > 1:
        args.native = True
        if args.ice_msg:
            parser.error("--ice-msg: with one game only")
    elif args.play:
        parser.error("--play: with several games only")
    if args.play and args.hold:
        parser.error("--play: the session lasts as long as the games, without --hold")
    try:
        if not isinstance(json.loads(args.ice_servers), (list, dict)):
            raise ValueError("neither a list nor an object")
    except ValueError as error:
        parser.error(f"--ice-servers: {error}")
    ice_text = None
    if args.ice_msg:
        try:
            # The peer's id is the next after the player's, as a lobby's second player's would be.
            ice_text = ice_message(args.ice_msg, args.uid + 1, args.uid)
        except ValueError as error:
            parser.error(f"--ice-msg: {error}")

    if len(ports) == 1:
        seats = [Seat("game", ports[0], args.name, args.uid)]
    else:
        seats = [Seat(f"game{i + 1}", port, f"{args.name}{i + 1}", args.uid + i) for i, port in enumerate(ports)]
    session = Session(args, seats, ice_text)
    for seat in seats:
        seat.server = socket.create_server(("127.0.0.1", seat.port))
        seat.server.settimeout(args.timeout)
        note(f"listening on 127.0.0.1:{seat.port}" + ("" if len(seats) == 1 else f" for {seat.name} ({seat.uid})"))
    threads = [threading.Thread(target=session.serve, args=(seat,), daemon=True) for seat in seats]
    for thread in threads:
        thread.start()
    session.finished.wait()
    with session.lock:
        for seat in seats:
            seat.server.close()
            if seat.game is not None:
                try:
                    seat.game.sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                seat.game.sock.close()
    for thread in threads:
        thread.join(5)

    if len(seats) == 1:
        print("the game reached its lobby" if seats[0].lobby else "the game did not reach its lobby", flush=True)
    else:
        for seat in seats:
            line = (f"{seat.label} ({seat.name}, {seat.uid}): {'in its lobby' if seat.lobby else 'no lobby'}"
                    + ("" if seat is seats[0] else ", seated by the host" if seat.seated else ", not seated"))
            if args.play:
                line += (", launched" if seat.launched else ", not launched") \
                    + (", GameEnded" if seat.ended else ", no GameEnded") \
                    + f", {seat.desyncs} Desync; " + (seat.gone or "never connected")
            print(line, flush=True)
    if args.play:
        return 0 if session.played() else 1
    return 0 if session.succeeded_at is not None else 1


if __name__ == "__main__":
    sys.exit(main())
