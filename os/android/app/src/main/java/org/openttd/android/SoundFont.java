/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://opensource.org/licenses/GPL-2.0>.
 */

package org.openttd.android;

import android.content.Context;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Makes the General-MIDI soundfont bundled in the APK available to OpenTTD's
 * FluidSynth music driver. The asset lives outside the {@code openttd} asset root,
 * so the game data installer leaves it alone.
 */
final class SoundFont {
    /** Asset name inside the APK. */
    private static final String ASSET_NAME = "soundfont.sf2";
    /** File name used in the app's private storage once extracted. */
    private static final String FILE_NAME = "soundfont.sf2";

    private SoundFont() {
    }

    /**
     * Absolute path of the extracted soundfont, extracted on first use.
     * @return the path, or {@code null} when the asset is missing or cannot be written.
     */
    static String path(Context context) {
        File file = new File(context.getFilesDir(), FILE_NAME);
        if (file.isFile() && file.length() > 0) return file.getAbsolutePath();

        try (InputStream in = context.getAssets().open(ASSET_NAME);
             OutputStream out = new FileOutputStream(file)) {
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) > 0) out.write(buffer, 0, read);
            return file.getAbsolutePath();
        } catch (Exception e) {
            file.delete();
            return null;
        }
    }
}
