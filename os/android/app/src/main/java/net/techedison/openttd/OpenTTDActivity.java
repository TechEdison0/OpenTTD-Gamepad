/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

package net.techedison.openttd;

import org.libsdl.app.SDLActivity;

import android.os.Bundle;
import android.util.Log;

/**
 * Entry point for the game. Storage configuration is handled by the launcher
 * before the game starts; the game itself uses only OpenTTD's own menus.
 */
public class OpenTTDActivity extends SDLActivity {
    private static final String TAG = "OpenTTD Handheld";
    private static OpenTTDActivity instance = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.v(TAG, "Starting OpenTTD Handheld");
        super.onCreate(savedInstanceState);
        instance = this;
    }

    @Override
    protected void onDestroy() {
        Log.v(TAG, "Stopping OpenTTD Handheld");
        instance = null;
        super.onDestroy();
    }

    static boolean isRunning() {
        return instance != null;
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "hidapi", "main" };
    }

    @Override
    protected String getArguments() {
        String args = StorageLocation.getArguments(this);
        Log.v(TAG, "Arguments: " + args);
        return args;
    }
}
