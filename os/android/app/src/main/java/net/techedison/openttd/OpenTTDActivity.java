/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

package net.techedison.openttd;

import org.libsdl.app.SDLActivity;

import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Entry point for the game. Storage configuration is handled by the launcher
 * before the game starts; the game itself uses only OpenTTD's own menus.
 */
public class OpenTTDActivity extends SDLActivity {
    private static final String TAG = "OpenTTD Handheld";
    private static volatile boolean running = false;

    static boolean isRunning() {
        return running;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        /* The process may be recreated straight into this activity; make sure the
         * game data is in place before SDL starts the native thread. */
        if (!DataInstaller.isInstalled(this)) {
            super.onCreate(savedInstanceState);
            startActivity(new Intent(this, LauncherActivity.class));
            finish();
            return;
        }

        /* Must be done before the native code starts. */
        StorageLocation.applyForNative(this);
        Log.v(TAG, "Starting OpenTTD Handheld");
        running = true;
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onDestroy() {
        Log.v(TAG, "Stopping OpenTTD Handheld");
        running = false;
        super.onDestroy();
    }

    @Override
    protected String[] getLibraries() {
        /* SDL2 and all other dependencies are linked statically into libmain.so. */
        return new String[] { "main" };
    }

    /**
     * Command line arguments for OpenTTD. Debug arguments can be passed with
     * {@code adb shell am start -n net.techedison.openttd/.LauncherActivity --es args "-d driver=3"}.
     * The bundled soundfont is appended for the FluidSynth driver.
     */
    @Override
    protected String[] getArguments() {
        List<String> args = new ArrayList<>();
        String extra = getIntent().getStringExtra(LauncherActivity.EXTRA_ARGS);
        if (extra != null && !extra.trim().isEmpty()) {
            Collections.addAll(args, extra.trim().split("\\s+"));
        }
        String soundfont = SoundFont.path(this);
        if (soundfont != null) {
            args.add("-m");
            args.add("fluidsynth:soundfont=" + soundfont);
        }
        return args.toArray(new String[0]);
    }
}
