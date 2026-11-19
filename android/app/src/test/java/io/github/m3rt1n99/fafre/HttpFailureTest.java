package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/**
 * Http's failures against servers on this PC: which are the network's, tried again, which are the phone's own, and which
 * are the user's Cancel.
 */
public class HttpFailureTest {
    @Rule
    public final TemporaryFolder folder = new TemporaryFolder();

    @Before
    public void noCancel() {
        Cancellation.reset();
    }

    @After
    public void noCancelLeft() {
        Cancellation.reset();
    }

    /** The log's lines that try {@code url} again, as after a failure of the network. */
    private static List<String> retries(String url) {
        List<String> lines = new ArrayList<>();
        for (String line : TestState.log()) {
            if (line.startsWith("Network: ") && line.contains(url)) {
                lines.add(line);
            }
        }
        return lines;
    }

    /** A Cancel once the first new try of {@code url} is logged: it ends the tries, minutes long, at their first wait. */
    private static Thread cancelAtFirstRetry(String url) {
        Thread cancel = new Thread(() -> {
            while (retries(url).isEmpty()) {
                try {
                    Thread.sleep(20);
                } catch (InterruptedException e) {
                    return;
                }
            }
            Cancellation.request();
        });
        cancel.start();
        return cancel;
    }

    /** A server that answers every request with {@code body}. */
    private static Server serving(byte[] body) throws IOException {
        return new Server(socket -> {
            OutputStream out = socket.getOutputStream();
            out.write(("HTTP/1.1 200 OK\r\nContent-Length: " + body.length + "\r\nConnection: close\r\n\r\n")
                .getBytes(StandardCharsets.US_ASCII));
            out.write(body);
        });
    }

    /**
     * A download the phone cannot write (a full disk, ENOSPC; here a folder where its .part goes, the same failure on the
     * phone's side) fails at once. Tried again as the network's, it takes 7 tries and about 9 minutes, each failing the
     * same way. Here a Cancel ends the tries once the first is logged.
     */
    @Test(timeout = 60_000)
    public void aDownloadThePhoneCannotWriteIsNotTriedAgainAsTheNetworks() throws Exception {
        Server server = serving(new byte[1 << 20]);
        String url = server.url("/env.3840.nx2");
        File dest = new File(folder.getRoot(), "env.3840.nx2");
        assertTrue(new File(dest.getPath() + ".part").mkdir());
        Thread cancel = cancelAtFirstRetry(url);
        try {
            IOException failure = assertThrows(IOException.class, () -> Http.download(url, dest, null));
            assertEquals("not tried again as the network's", new ArrayList<String>(), retries(url));
            assertFalse("the phone's own failure, not a cancel: " + failure, failure instanceof Cancellation.Cancelled);
        } finally {
            cancel.interrupt();
            cancel.join();
            server.close();
        }
    }

    /**
     * A whole download the phone cannot put in its place (here a folder in the way) fails at once too. Tried again, its
     * whole .part would ask for a range past the file's end, and the download would start over from the first byte.
     */
    @Test(timeout = 60_000)
    public void aDownloadThePhoneCannotPutInPlaceIsNotTriedAgain() throws Exception {
        Server server = serving(new byte[1000]);
        String url = server.url("/units.3840.nx2");
        File dest = new File(folder.getRoot(), "units.3840.nx2");
        assertTrue(new File(dest, "in the way").mkdirs());
        Thread cancel = cancelAtFirstRetry(url);
        try {
            assertThrows(Http.StorageException.class, () -> Http.download(url, dest, null));
            assertEquals("not tried again as the network's", new ArrayList<String>(), retries(url));
            assertEquals("the whole download kept", 1000, new File(dest.getPath() + ".part").length());
        } finally {
            cancel.interrupt();
            cancel.join();
            server.close();
        }
    }

    /**
     * A check's request that the user cancels while it waits for its answer ends in a failure of the network (after a
     * stall, the 20 s connect or 60 s read timeout; here the server closes without an answer): that is the user's
     * Cancel, which FafRelease and the job pass on as a Cancelled, not "GitHub is not reachable" or "Getting ... failed".
     */
    @Test(timeout = 60_000)
    public void aCheckCancelledWhileItsRequestWaitsIsACancel() throws Exception {
        // The user's Cancel comes while the request waits; its answer never comes.
        Server server = new Server(socket -> Cancellation.request());
        String url = server.url("/repos/FAForever/fa/releases/latest");
        try {
            IOException text = assertThrows(IOException.class, () -> Http.ONE_TRY.text(url));
            assertTrue("the user's Cancel came while the request waited", Cancellation.requested());
            assertTrue("GitHub's answer: a Cancelled, not " + text, text instanceof Cancellation.Cancelled);

            Cancellation.reset();
            IOException length = assertThrows(IOException.class, () -> Http.ONE_TRY.length(url));
            assertTrue("the user's Cancel came while the request waited", Cancellation.requested());
            assertTrue("a HEAD to FAF's updater: a Cancelled, not " + length, length instanceof Cancellation.Cancelled);
        } finally {
            server.close();
        }
    }

    /**
     * The measurement's last HEAD, during which the user cancels (in its second of pacing or while it runs), gets its
     * answer: the job would go on to ask "Download 682 MB on a metered network?" after the Cancel. A request the user
     * cancelled while it ran is a Cancelled, answered or not.
     */
    @Test(timeout = 60_000)
    public void aRequestTheUserCancelledWhileItRanIsACancelEvenWithItsAnswer() throws Exception {
        Server server = new Server(socket -> {
            Cancellation.request();
            socket.getOutputStream().write("HTTP/1.1 200 OK\r\nContent-Length: 109609228\r\nConnection: close\r\n\r\n"
                .getBytes(StandardCharsets.US_ASCII));
        });
        String url = server.url("/units.3840.nx2");
        try {
            IOException length = assertThrows(IOException.class, () -> Http.ONE_TRY.length(url));
            assertTrue("a Cancelled, not " + length, length instanceof Cancellation.Cancelled);
        } finally {
            server.close();
        }
    }

    /**
     * A wait between tries, of up to 5 minutes, ends at the user's Cancel within half a second: Back on the game's start, or
     * the Progress card's Cancel, while the network is down. The first wait is 2 s.
     */
    @Test(timeout = 60_000)
    public void aCancelEndsTheWaitBetweenTriesAtOnce() throws Exception {
        Server server = new Server(socket -> socket.getOutputStream().write(
            "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".getBytes(StandardCharsets.US_ASCII)));
        String url = server.url("/lua.3840.nx2");
        long[] cancelledAt = {0};
        Thread cancel = new Thread(() -> {
            while (retries(url).isEmpty()) {
                try {
                    Thread.sleep(5);
                } catch (InterruptedException e) {
                    return;
                }
            }
            cancelledAt[0] = System.nanoTime();
            Cancellation.request();
        });
        cancel.start();
        try {
            IOException failure = assertThrows(IOException.class,
                () -> Http.download(url, new File(folder.getRoot(), "lua.3840.nx2"), null));
            long waited = (System.nanoTime() - cancelledAt[0]) / 1_000_000;
            assertTrue("a Cancelled, not " + failure, failure instanceof Cancellation.Cancelled);
            assertTrue("the wait went on for " + waited + " ms after the Cancel", waited < 1000);
        } finally {
            cancel.interrupt();
            cancel.join();
            server.close();
        }
    }

    /** A server on this PC: each request's head is read, then {@code answer} has the connection, which closes after it. */
    private static final class Server {
        interface Answer {
            void to(Socket socket) throws IOException;
        }

        private final ServerSocket mSocket;
        private final Thread mThread;

        Server(Answer answer) throws IOException {
            mSocket = new ServerSocket(0, 50, InetAddress.getLoopbackAddress());
            mThread = new Thread(() -> {
                while (!mSocket.isClosed()) {
                    try (Socket socket = mSocket.accept()) {
                        BufferedReader in = new BufferedReader(new InputStreamReader(socket.getInputStream(), StandardCharsets.US_ASCII));
                        String line;
                        while ((line = in.readLine()) != null && !line.isEmpty()) {
                            // a line of the request's head
                        }
                        answer.to(socket);
                    } catch (IOException e) {
                        // closed, or the client went
                    }
                }
            });
            mThread.start();
        }

        String url(String path) {
            return "http://127.0.0.1:" + mSocket.getLocalPort() + path;
        }

        /** No answer comes after this, nor a Cancel from one. */
        void close() throws Exception {
            mSocket.close();
            mThread.join();
        }
    }
}
