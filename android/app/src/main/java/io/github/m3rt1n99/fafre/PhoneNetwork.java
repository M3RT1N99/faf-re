package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.Build;

/**
 * The phone's network for a download of FAF's game files, the Game card's job's (TestService) and a start's (Preparation),
 * by Android's rule for a download that should not run on a metered one. A default-network callback hears of the network
 * as it is right after it is registered, then of its changes.
 */
final class PhoneNetwork implements NewestGameFilesJob.Connectivity {
    private final ConnectivityManager mManager;

    PhoneNetwork(Context context) {
        mManager = context.getSystemService(ConnectivityManager.class);
    }

    @Override
    public boolean metered() {
        Network network = mManager.getActiveNetwork();
        return network == null || metered(mManager.getNetworkCapabilities(network));
    }

    @Override
    public NewestGameFilesJob.Watch watch(Runnable onMetered) {
        ConnectivityManager.NetworkCallback callback = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onCapabilitiesChanged(Network network, NetworkCapabilities capabilities) {
                if (metered(capabilities)) {
                    onMetered.run();
                }
            }
        };
        mManager.registerDefaultNetworkCallback(callback);
        return () -> mManager.unregisterNetworkCallback(callback);
    }

    /** Android's rule for a download that should not run on a metered network, also for a plan unmetered for a while. */
    private static boolean metered(NetworkCapabilities capabilities) {
        return capabilities == null
            || !capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED)
                && !(Build.VERSION.SDK_INT >= 30
                    && capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_TEMPORARILY_NOT_METERED));
    }
}
