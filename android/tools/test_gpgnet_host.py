#!/usr/bin/env python3
"""Tests of gpgnet_host.py as the lobby server of two fake games on the loopback: what its relay passes on and what
--play's status says of a game. The fake games have a codec of their own, so that a fault of the script's codec on
both sides cannot hide. Run: python -I android/tools/test_gpgnet_host.py"""

import os
import socket
import struct
import subprocess
import sys
import unittest

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gpgnet_host.py")
STATS = '{"stats":[]}'


def free_ports(count):
    """That many free ports, all different."""
    socks = [socket.socket() for _ in range(count)]
    try:
        for sock in socks:
            sock.bind(("127.0.0.1", 0))
        return [sock.getsockname()[1] for sock in socks]
    finally:
        for sock in socks:
            sock.close()


class FakeGame:
    """A game's GPGNet side: ints as int32, strings as their bytes, either way."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=10)

    def send(self, command, *args):
        def string(raw):
            return struct.pack("<I", len(raw)) + raw

        out = string(command.encode()) + struct.pack("<I", len(args))
        for arg in args:
            if isinstance(arg, int):
                out += struct.pack("<Bi", 0, arg)
            else:
                out += b"\x01" + string(arg if isinstance(arg, bytes) else arg.encode())
        self.sock.sendall(out)

    def exact(self, count):
        data = b""
        while len(data) < count:
            chunk = self.sock.recv(count - len(data))
            if not chunk:
                raise EOFError("the script closed the connection")
            data += chunk
        return data

    def string(self):
        return self.exact(struct.unpack("<I", self.exact(4))[0])

    def expect(self, command):
        """The arguments of the script's next message of that command, the ones before it skipped."""
        while True:
            name = self.string().decode()
            args = []
            for _ in range(struct.unpack("<I", self.exact(4))[0]):
                tag = self.exact(1)[0]
                args.append(struct.unpack("<i", self.exact(4))[0] if tag == 0 else self.string())
            if name == command:
                return args

    def close(self):
        """Ends the connection as a game's process does through adb's reverse: what it sent comes first, then the end.
        It reads to the script's own close, which follows the end it read, so that nothing it did not read resets it."""
        self.sock.shutdown(socket.SHUT_WR)
        while self.sock.recv(65536):
            pass
        self.sock.close()


class Script:
    """gpgnet_host.py with those options: the launcher of a game on each of its ports, of several their lobby server."""

    def __init__(self, test, *options, games=2):
        self.ports = free_ports(games)
        command = [sys.executable, "-I", SCRIPT, "--timeout", "20", *options]
        for port in self.ports:
            command += ["--port", str(port)]
        self.process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                        errors="backslashreplace")
        test.addCleanup(self.process.kill)
        test.addCleanup(self.process.stdout.close)
        # Its listeners' lines: then both ports take a connection.
        self.out = "".join(self.process.stdout.readline() for _ in self.ports)

    def lobby(self):
        """The two games in the host's lobby, as FAF's has them: the host seats the other player."""
        host = FakeGame(self.ports[0])
        host.send("GameState", "Idle")
        host.expect("CreateLobby")
        host.send("GameState", "Lobby")
        host.expect("HostGame")
        joiner = FakeGame(self.ports[1])
        joiner.send("GameState", "Idle")
        joiner.expect("CreateLobby")
        joiner.send("GameState", "Lobby")
        joiner.expect("JoinGame")
        host.expect("ConnectToPeer")
        host.send("PlayerOption", "2", "Faction", 1)
        return host, joiner

    def game(self):
        """The two games launched from the host's lobby."""
        host, joiner = self.lobby()
        host.send("GameState", "Launching")
        joiner.send("GameState", "Launching")
        return host, joiner

    def status(self):
        """Its exit status, once the games have closed their connections; out has everything it printed."""
        out, _ = self.process.communicate(timeout=30)
        self.out += out
        return self.process.returncode


def win(host):
    """The host's game wins and ends, as FAF's does once the other player is gone, and its player leaves it."""
    host.send("GameResult", 2, "defeat -10")
    host.send("GameResult", 1, "victory 10")
    host.send("JsonStats", STATS)
    host.send("GameEnded")
    host.close()


class Play(unittest.TestCase):
    def misses(self, script):
        return [line for line in script.out.splitlines() if line.startswith("not played to its end: ")]

    def test_a_game_its_player_left_and_one_that_won_played_to_the_end(self):
        script = Script(self, "--play")
        host, joiner = script.game()
        joiner.send("JsonStats", STATS)  # its player's Exit to Windows
        joiner.close()
        win(host)
        self.assertEqual(script.status(), 0, script.out)
        self.assertEqual(self.misses(script), [], script.out)
        self.assertIn("played to its end", script.out.splitlines())

    def test_a_game_killed_in_the_game_is_named(self):
        script = Script(self, "--play")
        host, joiner = script.game()
        joiner.close()  # its process killed: nothing more
        win(host)
        self.assertEqual(script.status(), 1, script.out)
        self.assertEqual(self.misses(script), ["not played to its end: game2 left the game without JsonStats or "
                                               "GameEnded, as a game that crashed or was killed does"], script.out)

    def test_a_game_with_gameended_alone_ended_its_session(self):
        script = Script(self, "--play")
        host, joiner = script.game()
        joiner.send("GameEnded")
        joiner.close()
        win(host)
        self.assertEqual(script.status(), 0, script.out)

    def test_a_desync_is_named(self):
        script = Script(self, "--play")
        host, joiner = script.game()
        host.send("Desync")
        joiner.send("JsonStats", STATS)
        joiner.close()
        win(host)
        self.assertEqual(script.status(), 1, script.out)
        self.assertEqual(self.misses(script), ["not played to its end: game1 sent 1 Desync"], script.out)

    def test_a_game_gone_in_the_lobby_leaves_the_others_lobbies(self):
        script = Script(self, "--play")
        host, joiner = script.lobby()
        joiner.close()
        self.assertEqual(host.expect("DisconnectFromPeer"), [2])
        host.close()
        self.assertEqual(script.status(), 1, script.out)
        self.assertEqual(self.misses(script), ["not played to its end: game1 did not launch",
                                               "not played to its end: game2 did not launch",
                                               "not played to its end: no game reported the game's end (GameEnded)"],
                         script.out)


if __name__ == "__main__":
    unittest.main()
