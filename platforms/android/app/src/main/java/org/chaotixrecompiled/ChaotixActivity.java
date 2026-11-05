package org.chaotixrecompiled;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

import org.libsdl.app.SDLActivity;

/** Loads SDL3 and the recompiled game (libmain.so, built from the root CMakeLists.txt). */
public class ChaotixActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    // The Mods folder on shared storage (issue #29): the app's own folders
    // (Android/data/...) cannot be opened by file managers on Android 11 and
    // later. Called from native code (src/platform/sdl_main.cpp).

    /** Whether this app may read and write ordinary folders on shared storage. */
    public static boolean hasSharedStorage() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        Context c = SDLActivity.getContext();
        return c != null && c.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED;
    }

    /** The Mods folder on shared storage, with a trailing slash. */
    public static String sharedModsDir() {
        return Environment.getExternalStorageDirectory().getAbsolutePath() + "/ChaotixRecompiled/Mods/";
    }

    /** Asks for that access: the system's All files access page, or the permission prompt. */
    public static void requestSharedStorage() {
        final Context c = SDLActivity.getContext();
        if (!(c instanceof Activity)) return;
        final Activity a = (Activity) c;
        a.runOnUiThread(() -> {
            if (Build.VERSION.SDK_INT >= 30) {
                try {
                    a.startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                               Uri.parse("package:" + a.getPackageName())));
                } catch (Exception e) {
                    a.startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                }
            } else {
                a.requestPermissions(new String[] {Manifest.permission.WRITE_EXTERNAL_STORAGE,
                                                   Manifest.permission.READ_EXTERNAL_STORAGE}, 29);
            }
        });
    }

    /**
     * The new version notice (issue #90, src/platform/http_get.cpp): one HTTPS
     * GET, the body of a 200 answer (at most 1 MB) or null. Called from a
     * thread of the game's own, never the UI thread.
     */
    public static String httpGet(String address) {
        HttpURLConnection c = null;
        try {
            URL url = new URL(address);
            if (!"https".equals(url.getProtocol())) return null;
            c = (HttpURLConnection) url.openConnection();
            c.setConnectTimeout(8000);
            c.setReadTimeout(8000);
            c.setRequestProperty("User-Agent", "ChaotixRecompiled");
            if (c.getResponseCode() != 200) return null;
            try (InputStream in = c.getInputStream()) {
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] buf = new byte[8192];
                for (int n; (n = in.read(buf)) > 0; ) {
                    out.write(buf, 0, n);
                    if (out.size() > (1 << 20)) return null;
                }
                return out.toString("UTF-8");
            }
        } catch (Exception e) {
            return null;
        } finally {
            if (c != null) c.disconnect();
        }
    }
}
