package org.chaotixrecompiled;

import org.libsdl.app.SDLActivity;

/** Loads SDL3 and the recompiled game (libmain.so, built from the root CMakeLists.txt). */
public class ChaotixActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }
}
