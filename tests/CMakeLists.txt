# Unit tests (no ROM needed)
add_executable(chaotix_tests
    test_main.cpp
    test_m68k.cpp
    test_sh2.cpp
    test_viewport.cpp
    test_setup.cpp
    test_achievements.cpp
    test_stage_select.cpp
)
target_link_libraries(chaotix_tests PRIVATE chaotix_game chaotix_runtime)

# The in-game menu is part of the frontend, so its test only exists where SDL
# is available. It drives the menu with key presses; nothing is drawn.
if(CHAOTIX_BUILD_FRONTEND)
    target_sources(chaotix_tests PRIVATE
        test_menu.cpp
        test_time_attack.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/menu.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/ui.cpp
        ${CMAKE_SOURCE_DIR}/src/frontend/config.cpp
        ${CHAOTIX_EMBEDDED_SOURCES})
    target_link_libraries(chaotix_tests PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)
endif()

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
    # The attract demo plays the game by itself, so it must earn nothing. This
    # test used to assert the opposite and passed for exactly that reason.
    add_test(NAME achievements_not_earned_by_the_demo
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 12000
                --achievements ${CMAKE_SOURCE_DIR}/assets/achievements.ini)
    set_tests_properties(achievements_not_earned_by_the_demo PROPERTIES TIMEOUT 300
        FAIL_REGULAR_EXPRESSION "unlocked")

    # ... and playing must earn something, or the conditions are simply dead.
    add_test(NAME achievements_earned_by_playing
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 4200
                --achievements ${CMAKE_SOURCE_DIR}/assets/achievements.ini
                --press 1750:start:5 --press 1950:start:5 --press 2300:start:5
                --press 2600:right:400)
    set_tests_properties(achievements_earned_by_playing PROPERTIES TIMEOUT 300
        PASS_REGULAR_EXPRESSION "unlocked")

    # TIME ATTACK: the front end asks for a stage and the game's own stage
    # select code starts it. The chosen place, level, characters and player
    # count must all come out the other side, and the run stays bit-identical
    # between the interpreter and the recompiled code (the request is applied
    # by a patch hook, so both must take it at the same instruction).
    # It must also be prompt. The request is taken at the mode dispatcher, and
    # the title screen only goes back there once it is done with itself, so the
    # frontend taps Start until it is taken; without that the stage waits out
    # the whole title screen (about 500 frames of PUSH START) and the player
    # watches the game's own screens go by first.
    add_test(NAME stage_select_starts_the_chosen_level
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2000 --lockstep
                --stage 1200:4:3:4:5:7:1)
    set_tests_properties(stage_select_starts_the_chosen_level PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION
        "stage: taken after ([0-9]|[1-9][0-9]|1[0-9][0-9]) frames, pending=0 mode=0018 zone=4 level=3 player=20 combi=28 padoff=10")

    # A run has to end somewhere, and be timed. The game does not tell anyone
    # a run is over -- it just moves the player on -- so the frontend watches
    # the zone and level. INTRODUCTION's first level ends by itself, which
    # makes it the one run a script can actually finish.
    add_test(NAME time_attack_ends_a_finished_run
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 5200
                --stage 600:6:0:0:2:7:0 --press 1400:right:3600
                --press 1440:c:14 --press 1560:c:14 --press 1680:c:14 --press 1800:c:14
                --press 1920:c:14 --press 2040:c:14 --press 2160:c:14 --press 2280:c:14
                --press 2400:c:14 --press 2520:c:14 --press 2640:c:14 --press 2760:c:14
                --press 2880:c:14 --press 3000:c:14 --press 3120:c:14 --press 3240:c:14
                --press 3360:c:14 --press 3480:c:14 --press 3600:c:14 --press 3720:c:14)
    # A plausible time, and not the level's own limit: the clock this reads
    # must be the one the HUD draws, not FFE052, which the player resets by
    # pressing anything (the presses above are what caught that).
    set_tests_properties(time_attack_ends_a_finished_run PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "stage: the run ended")

    # Frames the game was stopped for are not the player's time. Pausing is
    # the one such stop a script can reach; the other is the tally at the end
    # of a level, which is what made a run come out 25 seconds long. The clock
    # must read the same before and after 600 frames of pause.
    add_test(NAME time_attack_does_not_count_a_pause
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3400
                --stage 600:0:1:0:2:7:0 --press 1400:right:600
                --press 2200:start:6 --press 2800:start:6 --shot 2400,2700)
    set_tests_properties(time_attack_does_not_count_a_pause PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "at frame 2400 the run clock reads 0'18\"06.*at frame 2700 the run clock reads 0'18\"06")

    # A real run, finished at the goal, recorded by the frontend and replayed
    # here. The game holds the finished time on its results screen while its
    # frame counter runs on through twenty-odd seconds of tally, so a run used
    # to be recorded that much too long. The time below is what the player saw
    # on the screen when they finished.
    add_test(NAME time_attack_matches_what_the_player_saw
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 4700
                --replay-input ${CMAKE_CURRENT_SOURCE_DIR}/recordings/finished_run.txt)
    set_tests_properties(time_attack_matches_what_the_player_saw PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "the run ended after 2086 frames .0.34.66.")

    # Long-running validation (-DCHAOTIX_LONG_TESTS=ON, ctest -L long): full attract sequence.
    if(CHAOTIX_LONG_TESTS)
    add_test(NAME lockstep_attract_long COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 36000 --lockstep)
    add_test(NAME lockstep_attract_widescreen_long COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 36000 --wide 53 --lockstep)
    add_test(NAME render_attract_long
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 36000 --expect-hashes ${CMAKE_CURRENT_SOURCE_DIR}/golden/attract_every50.txt)
    set_tests_properties(lockstep_attract_long lockstep_attract_widescreen_long render_attract_long PROPERTIES LABELS long TIMEOUT 1800)
    endif()
endif()
