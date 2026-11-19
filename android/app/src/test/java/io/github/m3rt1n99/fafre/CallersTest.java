package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Collections;
import java.util.List;

import org.junit.Test;

/**
 * Which apps may start the game from outside and read its status: those of one signer, whose certificate is on the allowlist,
 * as the package manager answers for Android 9 and later (hasSigningCertificate with the SHA-256) and before it (each signer's
 * certificate, whose SHA-256 this side takes).
 */
public class CallersTest {
    /** The certificate of this PC's debug key (CN=Android Debug), which signs the app and the allowed test caller. */
    private static final byte[] DEBUG_CERTIFICATE = Base64.getDecoder().decode(""
        + "MIIDFDCCAfygAwIBAgIJAIBBrLFPnwySMA0GCSqGSIb3DQEBCwUAMDcxCzAJBgNVBAYTAlVTMRAwDgYDVQQKEwdBbmRyb2lk"
        + "MRYwFAYDVQQDEw1BbmRyb2lkIERlYnVnMCAXDTI2MTAwMzA0NTkzNFoYDzIwNTQwMjE4MDQ1OTM0WjA3MQswCQYDVQQGEwJV"
        + "UzEQMA4GA1UEChMHQW5kcm9pZDEWMBQGA1UEAxMNQW5kcm9pZCBEZWJ1ZzCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoC"
        + "ggEBANX/+t0OL8kx1gfF0O/OykyhrmchguFRhPEWSTyPrkJ14T8JoygxRPxzd0vD+dHljUq2EI1NgK/fiNpBwe8mcUsewSja"
        + "hFwCHtfHFBt+70LvikUh6h3wnuJi9A+EpDZDZ79vg1YYJlYmvieEHp3bHgpj5ToX4doRQA6VG8RWI21njZexsWPVTF03k5jT"
        + "7TsmvsOSk6LuKYYUqFXe9+4mVwr7MxTCqfRUdd3H8oa8dF3x3TlNgr8mpsHO4QBtmPAl7qvhVYcfz3LJHNkZlDoWiuogzCV3"
        + "jkjXEHrwv6vgSgkbKSGaodcZR91OR+Kk9AHmxvDKEfqBS+iAC1h6JOT/YoECAwEAAaMhMB8wHQYDVR0OBBYEFM/EkabXAJGG"
        + "XTLEg57yY1awgwAgMA0GCSqGSIb3DQEBCwUAA4IBAQDJeIytnRcOGSac36uV3EUqc0u2GHbk1oxeFiZH92A16Wrrq931/yp9"
        + "nltV11kDQeLtoe1iUJIDaOA7WylsWvt/SMVsGZhKZ/8Si4wEa/6ypgkoIG1boWQidHohOQQ52I4WlhLa9Spw4F5at3Cp9dfP"
        + "RJJZ5ZSAKoH/CPkqWGBfO9On6iT2pJRhkhTC8g12H1TjTJM/rXnYg84drwl/DFx8rpw+57ryMsEWapD8ys8s+iOKRZ1SHdug"
        + "MdhV1672frvuPkK/2uVk1qx/YVQAEbLRO5hF7s/DJ36IXgUYBfRjYG32SBQiouTJxuTNQNrTOjErl2BNwLlxGechqi915zxd");
    private static final String DEBUG_SHA256 = "d6fc846e55052b8ac69985e5b0692a7a5756f9410533f2d0f3dbf6bfc7d3fe4a";
    /** Another key's certificate, as far as its digest goes: any other bytes. */
    private static final byte[] OTHER_CERTIFICATE = "a throwaway key's certificate".getBytes(StandardCharsets.UTF_8);

    /** The package manager as a test answers for it: the certificates each package is signed by. */
    private static final class Fake implements Callers.Signatures {
        final List<String> asked = new ArrayList<>();
        private final String packageName;
        private final byte[][] certificates;

        Fake(String packageName, byte[]... certificates) {
            this.packageName = packageName;
            this.certificates = certificates;
        }

        /** As Android's SigningDetails.hasSha256Certificate: a package of more than one signer has none of their certificates. */
        @Override
        public boolean hasSigningCertificate(String name, byte[] sha256) {
            asked.add(name + " " + Callers.hex(sha256));
            return name.equals(packageName) && certificates.length == 1
                && Arrays.equals(Callers.sha256(certificates[0]), sha256);
        }

        @Override
        public byte[][] signatures(String name) {
            asked.add(name);
            return name.equals(packageName) ? certificates : null;
        }
    }

    @Test
    public void theAllowlistHoldsThisPcsDebugKeyWhichAlsoSignsTheApp() {
        assertEquals(Collections.singletonList(DEBUG_SHA256), Callers.ALLOWED);
        assertEquals(DEBUG_SHA256, Callers.hex(Callers.sha256(DEBUG_CERTIFICATE)));
    }

    @Test
    public void fromAndroid9TheAllowedKeysDigestIsAskedOfThePackageManager() {
        for (int sdk : new int[] {28, 35}) {
            Fake allowed = new Fake("com.faforever.rustclient", DEBUG_CERTIFICATE);
            assertTrue(Callers.allowed("com.faforever.rustclient", sdk, allowed));
            assertEquals("hasSigningCertificate with the SHA-256, not GET_SIGNATURES",
                Collections.singletonList("com.faforever.rustclient " + DEBUG_SHA256), allowed.asked);
            // Another key: the package manager says whether it is the package's.
            assertFalse(Callers.allowed("com.faforever.rustclient", sdk, new Fake("com.faforever.rustclient", OTHER_CERTIFICATE)));
            // A package that is not there, or that the allowed key signs under another name.
            assertFalse(Callers.allowed("com.example.other", sdk, new Fake("com.faforever.rustclient", DEBUG_CERTIFICATE)));
        }
    }

    @Test
    public void beforeAndroid9EachSignersCertificateIsDigestedHere() {
        for (int sdk : new int[] {26, 27}) {
            Fake allowed = new Fake("com.faforever.rustclient", DEBUG_CERTIFICATE);
            assertTrue(Callers.allowed("com.faforever.rustclient", sdk, allowed));
            assertEquals("GET_SIGNATURES, as hasSigningCertificate is Android 9's",
                Collections.singletonList("com.faforever.rustclient"), allowed.asked);
            assertFalse(Callers.allowed("com.faforever.rustclient", sdk, new Fake("com.faforever.rustclient", OTHER_CERTIFICATE)));
            assertFalse(Callers.allowed("com.faforever.rustclient", sdk, new Fake("com.faforever.rustclient")));
            // No such package: the package manager has no signatures of it.
            assertFalse(Callers.allowed("com.example.other", sdk, new Fake("com.faforever.rustclient", DEBUG_CERTIFICATE)));
        }
    }

    @Test
    public void aPackageOfMoreThanOneSignerIsRefusedOnBothSidesOfAndroid9() {
        // Android 9's hasSigningCertificate answers no for each of its signers, a key's rotation being a single signer's (on
        // the emulator, API 36, an app signed by this PC's debug key and a throwaway one got CALLER): the same rule before it.
        for (int sdk : new int[] {26, 27, 28, 35}) {
            for (Fake twoSigners : new Fake[] {new Fake("com.faforever.rustclient", OTHER_CERTIFICATE, DEBUG_CERTIFICATE),
                new Fake("com.faforever.rustclient", DEBUG_CERTIFICATE, OTHER_CERTIFICATE)}) {
                assertFalse("sdk " + sdk, Callers.allowed("com.faforever.rustclient", sdk, twoSigners));
            }
        }
    }

    @Test
    public void aCallerAndroidNamesNotIsRefusedWithoutAsking() {
        // getCallingPackage is null for a start that waits for no result, the only way to the game's.
        for (int sdk : new int[] {26, 35}) {
            Fake asked = new Fake("com.faforever.rustclient", DEBUG_CERTIFICATE);
            assertFalse(Callers.allowed(null, sdk, asked));
            assertEquals(Collections.emptyList(), asked.asked);
        }
    }

    @Test
    public void theHexIsLowerCaseTwoDigitsAByte() {
        assertEquals("00ff7f80", Callers.hex(new byte[] {0, -1, 127, -128}));
    }
}
