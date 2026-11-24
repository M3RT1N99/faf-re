package io.github.m3rt1n99.fafre;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.Log;

import java.io.File;
import java.util.HashMap;
import java.util.Map;

/**
 * The game's only way in from another app, the FAF client (action PLAY, android/README.md): no UI. It checks the caller
 * (Callers, and the token an allowed app got for the start: PlayTokens; CALLER) and the start's extras (Launch), then starts
 * the game's activity, which is not exported, with what the checks let through and what this side adds, and ends.
 * FLAG_ACTIVITY_FORWARD_RESULT hands the game's result straight to the client; a start this refuses ends with its own, the
 * same RESULT_FIRST_USER with "failure" and "message".
 */
public final class LaunchGameActivity extends Activity {
    private static final String TAG = "faf-launch";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        try {
            Intent game = game();
            try {
                startActivity(game);
            } catch (SecurityException e) {
                // The grant this start passes on is the client's, which Android checked at its start: gone since.
                throw GameStart.Failure.replay("the game may not read it: the launcher grants that with "
                    + "FLAG_GRANT_READ_URI_PERMISSION (" + e.getMessage() + ")");
            }
        } catch (GameStart.Failure failure) {
            Log.w(TAG, getCallingPackage() + ": " + failure.code + ": " + failure.getMessage());
            setResult(RESULT_FIRST_USER, result(failure));
        }
        finish();
    }

    /**
     * A failure's result, with RESULT_FIRST_USER: its code and its line, and NO_ROOM's bytes to free. This activity's
     * refusals end with it, and so do the game's failed starts and SetupActivity's.
     */
    static Intent result(GameStart.Failure failure) {
        Intent result = new Intent().putExtra(GameStart.RESULT_FAILURE, failure.code.name())
            .putExtra(GameStart.RESULT_MESSAGE, failure.getMessage());
        if (failure.code == GameStart.Code.NO_ROOM) {
            result.putExtra(GameStart.RESULT_BYTES_TO_FREE, failure.bytesToFree);
        }
        return result;
    }

    /** The game's start for this one, which the checks let through. */
    private Intent game() throws GameStart.Failure {
        // The app the result goes to, set only for a start that waits for one, which is also the only way to the game's.
        if (!Callers.allowed(this, getCallingPackage())) {
            throw GameStart.Failure.caller();
        }
        Intent intent = getIntent();
        Map<String, Object> extras = extras(intent);
        // The app that started PLAY, which need not be that one: an app the client started for a result can forward PLAY
        // with its own extras (FLAG_ACTIVITY_FORWARD_RESULT). Only an allowed app gets a token, from the status provider.
        if (!PlayTokens.APP.spend(extras.get(Launch.EXTRA_TOKEN), SystemClock.elapsedRealtime())) {
            throw GameStart.Failure.token();
        }
        Uri replay = intent.getData();
        Launch launch = Launch.check(extras, replay != null, replay != null ? replay.getScheme() : null,
            replay != null ? replay.getAuthority() : null, (intent.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION) != 0,
            new File(getNoBackupFilesDir(), Launch.REJOIN_KEY));
        // LAUNCHER_ARGUMENTS also when empty: it makes the start a launcher's, which GameActivity refuses beside a running
        // game and in Android's restore after its process ended (GameStart.refusal).
        Intent game = new Intent(this, GameActivity.class).addFlags(Intent.FLAG_ACTIVITY_FORWARD_RESULT)
            .putExtra(GameStart.EXTRA_LAUNCHER_ARGUMENTS, launch.arguments);
        if (launch.version != null) {
            game.putExtra(GameStart.EXTRA_GAME_VERSION, launch.version.intValue());
        }
        if (launch.map != null) {
            game.putExtra(Launch.EXTRA_MAP, launch.map);
        }
        if (launch.mods.length > 0) {
            game.putExtra(Launch.EXTRA_MODS, launch.mods);
        }
        if (launch.modFiles != null) {
            game.putExtra(Launch.EXTRA_MOD_FILES, launch.modFiles);
        }
        if (launch.replay) {
            // The client's grant goes on to the game's activity, which copies the replay before the program starts.
            game.setData(replay).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        }
        return game;
    }

    /** A start's extras by name, each as the client gave it; ARGUMENTS when they cannot be read, such as a class of its own. */
    @SuppressWarnings("deprecation")
    static Map<String, Object> extras(Intent intent) throws GameStart.Failure {
        Map<String, Object> extras = new HashMap<>();
        try {
            Bundle bundle = intent.getExtras();
            if (bundle != null) {
                for (String name : bundle.keySet()) {
                    extras.put(name, bundle.get(name));
                }
            }
        } catch (RuntimeException e) {
            throw GameStart.Failure.arguments("the start's extras cannot be read: " + e);
        }
        return extras;
    }
}
