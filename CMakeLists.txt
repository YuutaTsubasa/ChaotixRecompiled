cmake_minimum_required(VERSION 3.20)
project(ChaotixRecompiled VERSION 0.1.0 LANGUAGES C CXX)

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
    src/runtime/system.cpp
    src/runtime/patches.cpp
    src/runtime/md_bus.cpp
    src/runtime/sh2_bus.cpp
    src/runtime/sh2_onchip.cpp
    src/runtime/vdp.cpp
    src/renderer/compositor.cpp
    src/runtime/save.cpp
    src/audio/ym2612.cpp
    src/audio/sn76489.cpp
    src/renderer/image_io.cpp
    src/renderer/viewport.cpp
    src/frontend/setup.cpp
    src/input/input.cpp
    src/input/touch_controls.cpp
)
target_include_directories(chaotix_runtime PUBLIC src)
target_link_libraries(chaotix_runtime PUBLIC chaotix_cpu)
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

add_library(chaotix_game STATIC src/game/recomp_dispatch.cpp src/game/achievements.cpp ${CHAOTIX_GENERATED_SOURCES})
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

# ---------------------------------------------------------------------------
# SDL3 frontend (desktop and mobile). Platform code is confined to src/platform.
# ---------------------------------------------------------------------------
if(CHAOTIX_BUILD_FRONTEND)
    # SDL3: use an installed package, or build it from source (Android/iOS or
    # when -DCHAOTIX_SDL3_SOURCE_DIR=<path to SDL3 checkout> is given).
    if(CHAOTIX_SDL3_SOURCE_DIR)
        add_subdirectory(${CHAOTIX_SDL3_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL3 EXCLUDE_FROM_ALL)
    else()
        find_package(SDL3 CONFIG REQUIRED)
    endif()
    # SDL3_ttf draws the setup and achievement screens. Same rule as SDL3:
    # an installed package, or a source checkout for the mobile targets.
    if(CHAOTIX_SDL3_TTF_SOURCE_DIR)
        set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
        set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
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
                  achievements.ini:achievements_ini)
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

    set(_frontend_sources
        src/platform/sdl_main.cpp
        src/platform/sdl_setup.cpp
        src/platform/ui.cpp
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
        set_target_properties(chaotix PROPERTIES OUTPUT_NAME "ChaotixRecompiled")
    endif()
    target_link_libraries(chaotix PRIVATE chaotix_game SDL3::SDL3 SDL3_ttf::SDL3_ttf)
    chaotix_set_warnings(chaotix)
    if(APPLE)
        set_target_properties(chaotix PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "Chaotix Recompiled"
            MACOSX_BUNDLE_GUI_IDENTIFIER "org.chaotixrecompiled.app"
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/platforms/apple/Info.plist.in
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")
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
