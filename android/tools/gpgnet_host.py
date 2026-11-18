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

The wire format is the engine's (src/moho/net/CGpgNetInterface.cpp; tools/gpgnet/codec.go is the Go codec),
little-endian: a message is its command as a string, the count of its arguments as an int32 and each argument
as a tag byte (0 an int32, 1 a string, 2 data) and its body; a string is its length as an int32 and its bytes.
"""

import argparse
import socket
import struct
import sys
import time

TAG_INT, TAG_STRING, TAG_DATA = 0, 1, 2


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


class Connection:
    """One game's GPGNet connection."""

    def __init__(self, sock):
        self.sock = sock

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
        self.sock.sendall(encode(command, list(args)))
        say("host", command, args)


START = time.monotonic()


def say(who, command, args=()):
    shown = " ".join(f"<data {len(a)} bytes>" if isinstance(a, Data) else repr(a) for a in args)
    print(f"{time.monotonic() - START:8.2f} {who}: {command} {shown}".rstrip(), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--port", type=int, default=7237, help="the port to listen on (default 7237)")
    parser.add_argument("--timeout", type=float, default=180,
                        help="seconds to wait for the game's connection (default 180)")
    parser.add_argument("--mode", type=int, choices=(0, 1), default=0,
                        help="CreateLobby's mode: 0 a lobby, 1 an automatch lobby (default 0)")
    parser.add_argument("--lobby-port", type=int, default=0,
                        help="CreateLobby's UDP port for the game's traffic, 0 any (default 0)")
    parser.add_argument("--name", default="tester", help="CreateLobby's player name (default tester)")
    parser.add_argument("--uid", type=int, default=1, help="CreateLobby's player id (default 1)")
    parser.add_argument("--hold", type=float, default=0,
                        help="seconds to hold the connection after the game's lobby, 0 until the game closes it")
    args = parser.parse_args()

    with socket.create_server(("127.0.0.1", args.port)) as server:
        server.settimeout(args.timeout)
        print(f"listening on 127.0.0.1:{args.port}", flush=True)
        try:
            sock, peer = server.accept()
        except socket.timeout:
            print(f"no game connected in {args.timeout:g} s", flush=True)
            return 1
    print(f"{time.monotonic() - START:8.2f} the game connected from {peer[0]}:{peer[1]}", flush=True)

    game = Connection(sock)
    lobby_at = None
    with sock:
        while True:
            if lobby_at is not None and args.hold:
                left = lobby_at + args.hold - time.monotonic()
                if left <= 0:
                    print(f"{time.monotonic() - START:8.2f} held the lobby {args.hold:g} s", flush=True)
                    break
                sock.settimeout(left)
            try:
                message = game.receive()
            except socket.timeout:
                continue
            except (EOFError, ConnectionError, ValueError) as error:
                print(f"{time.monotonic() - START:8.2f} the connection broke: {error}", flush=True)
                break
            if message is None:
                print(f"{time.monotonic() - START:8.2f} the game closed the connection", flush=True)
                break
            command, values = message
            say("game", command, values)
            if command == "GameState" and values == ["Idle"]:
                game.send("CreateLobby", args.mode, args.lobby_port, args.name, args.uid, 1)
            elif command == "GameState" and values == ["Lobby"] and lobby_at is None:
                lobby_at = time.monotonic()
    print("the game reached its lobby" if lobby_at is not None else "the game did not reach its lobby", flush=True)
    return 0 if lobby_at is not None else 1


if __name__ == "__main__":
    sys.exit(main())
