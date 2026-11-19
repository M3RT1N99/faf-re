package io.github.m3rt1n99.fafre;

import android.annotation.TargetApi;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.os.Build;

import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/**
 * The apps that may start the game from outside (LaunchGameActivity) and read its status (StatusProvider): those signed by
 * a certificate on the allowlist, the one place that names them. Android's permission with knownSigner would say the same
 * from Android 12 on only, and the app runs from Android 8. The decision is pure, for the tests: the package manager's
 * answers come through {@link Signatures}.
 */
final class Callers {
    /**
     * The SHA-256 of each certificate whose apps are allowed, in lower-case hex: this PC's debug key, which also signs this
     * app itself. The FAF client's release certificate comes here once it has one.
     */
    static final List<String> ALLOWED = Collections.unmodifiableList(Arrays.asList(
        "d6fc846e55052b8ac69985e5b0692a7a5756f9410533f2d0f3dbf6bfc7d3fe4a"));

    /** What the package manager says of a package's signing certificates. */
    interface Signatures {
        /**
         * Android 9 and later: whether the package is signed by the certificate of this SHA-256, now or in its key's past;
         * never for a package of more than one signer, as a key's rotation is a single signer's (SigningDetails).
         */
        boolean hasSigningCertificate(String packageName, byte[] sha256);

        /** Before Android 9: the encoded certificate of each of the package's signers; null when there is no such package. */
        byte[][] signatures(String packageName);
    }

    private Callers() {
    }

    /**
     * Whether {@code packageName}, the caller as Android names it (null when it does not), is signed by an allowed
     * certificate: by one signer, whose certificate is on the allowlist (from Android 9 also one of its key's past), as
     * Android 9's hasSigningCertificate allows a package of more than one signer none of theirs. Before it the same rule.
     */
    static boolean allowed(String packageName, int sdk, Signatures signatures) {
        if (packageName == null) {
            return false;
        }
        if (sdk >= 28) {
            for (String digest : ALLOWED) {
                if (signatures.hasSigningCertificate(packageName, bytes(digest))) {
                    return true;
                }
            }
            return false;
        }
        byte[][] certificates = signatures.signatures(packageName);
        return certificates != null && certificates.length == 1 && ALLOWED.contains(hex(sha256(certificates[0])));
    }

    /** The package manager's answers for {@code packageName}, which an app that started us for a result or asks our provider is visible to. */
    static boolean allowed(Context context, String packageName) {
        PackageManager manager = context.getPackageManager();
        return allowed(packageName, Build.VERSION.SDK_INT, new Signatures() {
            @Override
            @TargetApi(28)
            public boolean hasSigningCertificate(String name, byte[] sha256) {
                return manager.hasSigningCertificate(name, sha256, PackageManager.CERT_INPUT_SHA256);
            }

            @Override
            @SuppressWarnings("deprecation")
            public byte[][] signatures(String name) {
                Signature[] signed;
                try {
                    signed = manager.getPackageInfo(name, PackageManager.GET_SIGNATURES).signatures;
                } catch (PackageManager.NameNotFoundException e) {
                    return null;
                }
                if (signed == null) {
                    return null;
                }
                List<byte[]> certificates = new ArrayList<>();
                for (Signature signature : signed) {
                    certificates.add(signature.toByteArray());
                }
                return certificates.toArray(new byte[0][]);
            }
        });
    }

    static byte[] sha256(byte[] data) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(data);
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }

    static String hex(byte[] bytes) {
        StringBuilder text = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) {
            text.append(String.format(Locale.ROOT, "%02x", b & 0xff));
        }
        return text.toString();
    }

    private static byte[] bytes(String hex) {
        byte[] bytes = new byte[hex.length() / 2];
        for (int i = 0; i < bytes.length; ++i) {
            bytes[i] = (byte) Integer.parseInt(hex.substring(2 * i, 2 * i + 2), 16);
        }
        return bytes;
    }
}
