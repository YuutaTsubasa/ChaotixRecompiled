# Unit tests (no ROM needed)
add_executable(chaotix_tests
    test_main.cpp
    test_m68k.cpp
    test_sh2.cpp
    test_viewport.cpp
    test_setup.cpp
    test_achievements.cpp
)
target_link_libraries(chaotix_tests PRIVATE chaotix_game chaotix_runtime)
chaotix_set_warnings(chaotix_tests)
add_test(NAME unit COMMAND chaotix_tests)

# Integration tests: need the user's ROM (-DCHAOTIX_ROM=...). They verify the
# recompiled build against the reference interpreter and golden frame hashes.
if(CHAOTIX_ROM AND EXISTS "${CHAOTIX_ROM}")
    # Boot -> title -> menus -> DATA LOAD -> level, with gameplay input.
    set(_script
        --press 1750:start:5 --press 1950:start:5 --press 2300:start:5
        --press 2600:right:200 --press 2750:c:10 --press 2900:left:100 --press 3050:c:5)
    add_test(NAME lockstep_boot_to_level
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3200 --lockstep ${_script})
    # Rendering regression: frame hashes of the title screen and the first level.
    add_test(NAME golden_frames
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2600 ${_script}
                --expect-hash 1700:${CHAOTIX_GOLDEN_TITLE} --expect-hash 2600:${CHAOTIX_GOLDEN_LEVEL})
    # True widescreen: the level patches run identically in the interpreter and
    # the recompiled code (lockstep), and the widened frames are stable.
    add_test(NAME lockstep_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3200 --wide 53 --lockstep ${_script})
    add_test(NAME golden_frames_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2800 --wide 64 ${_script}
                --expect-hash 1700:${CHAOTIX_GOLDEN_WIDE_TITLE} --expect-hash 2800:${CHAOTIX_GOLDEN_WIDE_LEVEL})
    # The widescreen patches must not change the 4:3 image: a 4:3 machine and a
    # widescreen machine run side by side and the centre 320 px must match
    # pixel for pixel (this caught 8 ring columns never being filled).
    add_test(NAME widescreen_centre_matches_4_3
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 4000 --wide 53 --wide-bottom 16
                --compare-native --min-centre-frames 1000)
    add_test(NAME golden_frames_16_9
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3200 --wide 53 ${_script}
                --expect-hash 2800:${CHAOTIX_GOLDEN_169_A} --expect-hash 3200:${CHAOTIX_GOLDEN_169_B})
    set_tests_properties(lockstep_boot_to_level golden_frames lockstep_widescreen golden_frames_widescreen golden_frames_16_9 widescreen_centre_matches_4_3 PROPERTIES TIMEOUT 900)

    # The shipped achievement definitions must still unlock while the attract
    # demos play (a definition that never fires is silent otherwise).
    add_test(NAME achievements_attract
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 12000
                --achievements ${CMAKE_SOURCE_DIR}/assets/achievements.ini)
    set_tests_properties(achievements_attract PROPERTIES TIMEOUT 300
        PASS_REGULAR_EXPRESSION "unlocked First Ring")

    # Long-running validation (-DCHAOTIX_LONG_TESTS=ON, ctest -L long): full attract sequence.
    if(CHAOTIX_LONG_TESTS)
    add_test(NAME lockstep_attract_long COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 36000 --lockstep)
    add_test(NAME lockstep_attract_widescreen_long COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 36000 --wide 53 --lockstep)
    add_test(NAME render_attract_long
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 36000 --expect-hashes ${CMAKE_CURRENT_SOURCE_DIR}/golden/attract_every50.txt)
    set_tests_properties(lockstep_attract_long lockstep_attract_widescreen_long render_attract_long PROPERTIES LABELS long TIMEOUT 1800)
    endif()
endif()
