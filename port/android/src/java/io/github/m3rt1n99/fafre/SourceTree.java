package io.github.m3rt1n99.fafre;

import java.io.IOException;
import java.io.InputStream;
import java.util.List;

/**
 * A read-only folder tree the importer copies from. On the device it is a
 * Storage Access Framework tree the user picked ({@link SafTree}); host tests
 * plug in a plain directory. Keeping the importer behind this interface keeps
 * the selection logic free of Android APIs.
 */
interface SourceTree {
    /** One file or directory. {@code size} is -1 when the provider does not report it. */
    final class Node {
        final String id;
        final String name;
        final boolean directory;
        final long size;

        Node(String id, String name, boolean directory, long size) {
            this.id = id;
            this.name = name;
            this.directory = directory;
            this.size = size;
        }
    }

    Node root() throws IOException;

    List<Node> list(Node directory) throws IOException;

    InputStream open(Node file) throws IOException;

    /** Human-readable location for logs and messages. */
    String describe();
}
