cmake_minimum_required(VERSION 3.20)
# macOS and iOS: the oldest release the programs are built for (ignored
# elsewhere). Without it they would require the system they were built on.
# Apple Silicon Macs start at macOS 11; Xcode 27's libc++ supports nothing
# older than macOS 11 and iOS 15 (it warns below them).
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum iOS version")
else()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()
project(ChaotixRecompiled VERSION 0.9.0 LANGUAGES C CXX)
if(APPLE)
    # SDL's Cocoa and UIKit code, and the iOS document picker.
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(ChaotixOptions)

# ---------------------------------------------------------------------------
# CPU cores: decoders + shared semantics + reference interpreters
# ---------------------------------------------------------------------------
add_library(chaotix_cpu STATIC
    src/cpu/m68k/m68k_decode.cpp
    src/cpu/m68k/m68k_disasm.cpp
    src/cpu/m68k/m68k_interp.cpp
    src/cpu/sh2/sh2_decode.cpp
    src/cpu/sh2/sh2_interp.cpp
    src/cpu/z80/z80.cpp
)
target_include_directories(chaotix_cpu PUBLIC src)
chaotix_set_warnings(chaotix_cpu)

# ---------------------------------------------------------------------------
# Runtime: 32X / Mega Drive compatibility layer (no game code here)
# ---------------------------------------------------------------------------
add_library(chaotix_runtime STATIC
    src/runtime/log.cpp
    src/runtime/rom.cpp
    src/runtime/rom_patch.cpp
    src/runtime/system.cpp
    src/runtime/patches.cpp
    src/runtime/stage_select.cpp
    src/runtime/lobby.cpp
    src/runtime/md_bus.cpp
    src/runtime/sh2_bus.cpp
    src/runtime/sh2_onchip.cpp
    src/runtime/vdp.cpp
    src/runtime/tile_replace.cpp
    src/runtime/text_replace.cpp
    src/runtime/sprite_text.cpp
    src/runtime/sprite_record.cpp
    src/runtime/sprite_replace.cpp
    src/runtime/character_mods.cpp
    src/runtime/mod_packs.cpp
    src/renderer/compositor.cpp
    src/runtime/save.cpp
    src/runtime/save_state.cpp
    src/runtime/rewind.cpp
    src/runtime/lz.cpp
    src/audio/ym2612.cpp
    src/audio/sn76489.cpp
    src/audio/music_mods.cpp
    src/audio/volumes.cpp
    src/renderer/image_io.cpp
    src/renderer/tile_pack.cpp
    src/renderer/sprite_pack.cpp
    src/renderer/sprite_sheet.cpp
    src/renderer/game_font.cpp
    src/renderer/viewport.cpp
    src/frontend/setup.cpp
    src/frontend/time_attack.cpp
    src/frontend/backdrop.cpp
    src/input/input.cpp
    src/input/touch_controls.cpp
)
target_include_directories(chaotix_runtime PUBLIC src)
target_link_libraries(chaotix_runtime PUBLIC chaotix_cpu)
# Save states record the version that wrote them (runtime/save_state.h).
set_source_files_properties(src/runtime/save_state.cpp PROPERTIES
    COMPILE_DEFINITIONS CHAOTIX_VERSION_STRING="${PROJECT_VERSION}")
chaotix_set_warnings(chaotix_runtime)

# ---------------------------------------------------------------------------
# Host tools: ROM analyzer and static recompiler
# ---------------------------------------------------------------------------
if(CHAOTIX_BUILD_TOOLS AND NOT CMAKE_CROSSCOMPILING)
    add_library(chaotix_analysis STATIC tools/rom_analyzer/analysis.cpp)
    target_include_directories(chaotix_analysis PUBLIC tools src)
    target_link_libraries(chaotix_analysis PUBLIC chaotix_runtime)
    chaotix_set_warnings(chaotix_analysis)

    add_executable(rom_analyzer tools/rom_analyzer/main.cpp)
    target_link_libraries(rom_analyzer PRIVATE chaotix_analysis)
    chaotix_set_warnings(rom_analyzer)

    add_executable(chaotix_recomp
        tools/recompiler/main.cpp
        tools/recompiler/emit_m68k.cpp
        tools/recompiler/emit_sh2.cpp
    )
    target_link_libraries(chaotix_recomp PRIVATE chaotix_analysis)
    chaotix_set_warnings(chaotix_recomp)
endif()

# ---------------------------------------------------------------------------
# Game: statically recompiled code + dispatcher
# ---------------------------------------------------------------------------
include(ChaotixGenerated)

add_library(chaotix_game STATIC src/game/recomp_dispatch.cpp src/game/achievements.cpp src/game/cheats.cpp ${CHAOTIX_GENERATED_SOURCES})
target_include_directories(chaotix_game PUBLIC src PRIVATE ${CHAOTIX_GENERATED_DIR})
target_link_libraries(chaotix_game PUBLIC chaotix_runtime)
target_compile_definitions(chaotix_game PRIVATE CHAOTIX_HAVE_GENERATED=$<BOOL:${CHAOTIX_HAVE_GENERATED}>)
chaotix_set_warnings(chaotix_game)
if(CHAOTIX_GENERATED_SOURCES)
    set_source_files_properties(${CHAOTIX_GENERATED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-label;-Wno-unused-parameter>")
endif()

# Headless runner: deterministic frame execution, screenshots, validation.
add_executable(chaotix_headless src/frontend/headless_main.cpp)
target_link_libraries(chaotix_headless PRIVATE chaotix_game)
chaotix_set_warnings(chaotix_headless)

# Windows: UTF-8 as the process code page, so non-ASCII paths work (see the
# comment in the manifest).
# MSVC merges a .manifest source by itself; MinGW needs it as a resource.
set(CHAOTIX_WINDOWS_MANIFEST "")
if(MSVC)
    set(CHAOTIX_WINDOWS_MANIFEST ${CMAKE_SOURCE_DIR}/platforms/windows/app.manifest)
elseif(MINGW)
    set(CHAOTIX_WINDOWS_MANIFEST ${CMAKE_SOURCE_DIR}/platforms/windows/app.rc)
    set_source_files_properties(${CHAOTIX_WINDOWS_MANIFEST} PROPERTIES
        OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/platforms/windows/app.manifest)
endif()
if(CHAOTIX_WINDOWS_MANIFEST)
    target_sources(chaotix_headless PRIVATE ${CHAOTIX_WINDOWS_MANIFEST})
endif()

# ---------------------------------------------------------------------------
# SDL3 frontend (desktop and mobile). Platform code is confined to src/platform.
# ---------------------------------------------------------------------------
if(CHAOTIX_BUILD_FRONTEND)
    # SDL3: use an installed package, or build it from source (Android/iOS or
    # when -DCHAOTIX_SDL3_SOURCE_DIR=<path to SDL3 checkout> is given).
    if(CHAOTIX_SDL3_SOURCE_DIR)
        if(APPLE)
            # Linked in on macOS and iOS: the .app then needs no Homebrew and
            # no frameworks of its own to sign.
            set(SDL_SHARED OFF CACHE BOOL "" FORCE)
            set(SDL_STATIC ON CACHE BOOL "" FORCE)
        endif()
        add_subdirectory(${CHAOTIX_SDL3_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL3 EXCLUDE_FROM_ALL)
    else()
        find_package(SDL3 CONFIG REQUIRED)
    endif()
    # SDL3_ttf draws the setup and achievement screens. Same rule as SDL3:
    # an installed package, or a source checkout for the mobile targets.
    if(CHAOTIX_SDL3_TTF_SOURCE_DIR)
        set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
        set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
        if(APPLE)
            set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)  # as SDL above
        endif()
        add_subdirectory(${CHAOTIX_SDL3_TTF_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL3_ttf EXCLUDE_FROM_ALL)
    else()
        find_package(SDL3_ttf CONFIG REQUIRED)
    endif()

    # Data the program cannot run without is compiled into the binary rather
    # than looked up on disk: the setup screen runs before any asset directory
    # is known, and an Android APK has no filesystem to put assets in.
    set(_embedded "")
    foreach(asset fonts/Inter-Regular.ttf:font_regular
                  fonts/Inter-SemiBold.ttf:font_semibold
                  fonts/PressStart2P-Regular.ttf:font_pixel
                  achievements.ini:achievements_ini
                  icon/icon_64.rgba:app_icon_rgba)
        string(REPLACE ":" ";" asset ${asset})
        list(GET asset 0 _rel)
        list(GET asset 1 _symbol)
        set(_src ${CMAKE_SOURCE_DIR}/assets/${_rel})
        set(_out ${CMAKE_BINARY_DIR}/embedded/${_symbol}.cpp)
        add_custom_command(
            OUTPUT ${_out}
            COMMAND ${CMAKE_COMMAND} -DIN=${_src} -DOUT=${_out} -DSYMBOL=${_symbol}
                    -P ${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake
            DEPENDS ${_src} ${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake
            COMMENT "Embedding ${_rel}")
        list(APPEND _embedded ${_out})
    endforeach()
    # The tests link the same embedded data.
    set(CHAOTIX_EMBEDDED_SOURCES ${_embedded})

    set(_frontend_sources
        src/platform/sdl_main.cpp
        src/platform/sdl_setup.cpp
        src/platform/sdl_rom_import.cpp
        src/platform/menu.cpp
        src/platform/ui.cpp
        src/platform/backdrops.cpp
        src/frontend/config.cpp
        src/frontend/debug_overlay.cpp
        ${_embedded}
    )
    if(ANDROID)
        # SDLActivity loads libmain.so and calls SDL_main.
        add_library(chaotix SHARED ${_frontend_sources})
        set_target_properties(chaotix PROPERTIES OUTPUT_NAME main)
    else()
        add_executable(chaotix WIN32 MACOSX_BUNDLE ${_frontend_sources})
        if(CHAOTIX_WINDOWS_MANIFEST)
            target_sources(chaotix PRIVATE ${CHAOTIX_WINDOWS_MANIFEST})
        endif()
        if(WIN32)
            set(_icon_rc ${CMAKE_SOURCE_DIR}/platforms/windows/icon.rc)
            set_source_files_properties(${_icon_rc} PROPERTIES
                OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/platforms/windows/app.ico)
            target_sources(chaotix PRIVATE ${_icon_rc})
        endif()
        set_target_properties(chaotix PROPERTIES OUTPUT_NAME "ChaotixRecompiled")
        if(UNIX AND NOT APPLE)
            # The Linux download carries the SDL libraries next to the program
            # (issue #23); the build tree's own paths are added as well.
            set_target_properties(chaotix PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
        endif()
    endif()
    # The menu's zone pictures are made on a thread of their own (issue #8).
    find_package(Threads REQUIRED)
    target_link_libraries(chaotix PRIVATE chaotix_game SDL3::SDL3 SDL3_ttf::SDL3_ttf Threads::Threads)
    chaotix_set_warnings(chaotix)
    if(APPLE)
        # The bundle's Info.plist; scripts/package_apple.sh adds the icon,
        # the documents and the signature (docs/BUILDING_APPLE.md).
        set(CHAOTIX_APP_VERSION "${PROJECT_VERSION}" CACHE STRING "Version the Apple bundles carry")
        if(IOS)
            set(_plist ${CMAKE_SOURCE_DIR}/platforms/apple/ios/Info.plist.in)
            if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
                set(CHAOTIX_IOS_PLATFORM "iPhoneSimulator")
            else()
                set(CHAOTIX_IOS_PLATFORM "iPhoneOS")
            endif()
        else()
            set(_plist ${CMAKE_SOURCE_DIR}/platforms/apple/macos/Info.plist.in)
        endif()
        set_target_properties(chaotix PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "Chaotix Recompiled"
            MACOSX_BUNDLE_GUI_IDENTIFIER "org.chaotixrecompiled.app"
            MACOSX_BUNDLE_BUNDLE_VERSION "${CHAOTIX_APP_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${CHAOTIX_APP_VERSION}"
            MACOSX_BUNDLE_INFO_PLIST ${_plist}
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")
    endif()
    if(IOS)
        # SDL 3 has no file dialog on iOS; the system's document picker
        # stands in for it on the setup page.
        target_sources(chaotix PRIVATE src/platform/ios_document_picker.mm)
        set_source_files_properties(src/platform/ios_document_picker.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
        target_link_libraries(chaotix PRIVATE "-framework UIKit" "-framework UniformTypeIdentifiers")
        # iOS 27 stops an app linked against its SDK at launch unless the app
        # adopts the UIScene life cycle, which SDL 3.2 does not. The SDK the
        # program says it was linked against decides that, so it names iOS 18,
        # whose apps keep the classic app delegate SDL uses.
        if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
            target_link_options(chaotix PRIVATE "-Wl,-platform_version,ios-simulator,${CMAKE_OSX_DEPLOYMENT_TARGET},18.0")
        else()
            target_link_options(chaotix PRIVATE "-Wl,-platform_version,ios,${CMAKE_OSX_DEPLOYMENT_TARGET},18.0")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests (unit tests always; ROM-based integration tests when CHAOTIX_ROM is set)
# ---------------------------------------------------------------------------
if(CHAOTIX_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    include(${CMAKE_SOURCE_DIR}/tests/golden.cmake)
    add_subdirectory(tests)
endif()
