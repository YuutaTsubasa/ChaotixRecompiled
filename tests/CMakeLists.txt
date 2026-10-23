# Unit tests (no ROM needed)
add_executable(chaotix_tests
    test_main.cpp
    test_frame_pacing.cpp
    test_pwm_audio.cpp
    test_ym2612.cpp
    test_m68k.cpp
    test_sh2.cpp
    test_viewport.cpp
    test_compositor.cpp
    test_widescreen_render.cpp
    test_vdp_widescreen.cpp
    test_setup.cpp
    test_achievements.cpp
    test_stage_select.cpp
    test_music_mods.cpp
    test_cheats.cpp
    test_volumes.cpp
    test_rom_patch.cpp
    test_tile_replace.cpp
    test_text_replace.cpp
    test_sprite_text.cpp
    test_image_io.cpp
    test_sprite_record.cpp
    test_sprite_replace.cpp
    test_sprite_sheet.cpp
    test_character_mods.cpp
    test_game_font.cpp
    test_save_state.cpp
    test_mod_packs.cpp
)
find_package(Threads REQUIRED)  # test_game_font plays beside a backdrop thread
target_link_libraries(chaotix_tests PRIVATE chaotix_game chaotix_runtime Threads::Threads)

# The in-game menu is part of the frontend, so its test only exists where SDL
# is available. It drives the menu with key presses; nothing is drawn.
if(CHAOTIX_BUILD_FRONTEND)
    target_sources(chaotix_tests PRIVATE
        test_sdl_rom_import.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/sdl_rom_import.cpp
        test_menu.cpp
        test_state_menu.cpp
        test_mod_menu.cpp
        test_time_attack.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/menu.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/ui.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/backdrops.cpp
        ${CMAKE_SOURCE_DIR}/src/frontend/config.cpp
        ${CHAOTIX_EMBEDDED_SOURCES})
    target_link_libraries(chaotix_tests PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)
endif()

chaotix_set_warnings(chaotix_tests)
add_test(NAME unit COMMAND chaotix_tests)

# Integration tests: need the user's ROM (-DCHAOTIX_ROM=...). They verify the
# recompiled build against the reference interpreter and golden frame hashes.
if(CHAOTIX_ROM AND EXISTS "${CHAOTIX_ROM}")
    if(CHAOTIX_BUILD_FRONTEND AND NOT CMAKE_CROSSCOMPILING)
        target_compile_definitions(chaotix_tests PRIVATE CHAOTIX_TEST_ROM="${CHAOTIX_ROM}")
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(NAME rom_startup
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_rom_startup.py"
                        $<TARGET_FILE:chaotix> "${CHAOTIX_ROM}")
            set_tests_properties(rom_startup PROPERTIES TIMEOUT 180)
        endif()
    endif()
    # Issue #35: ROM patches from the Mods folder, over data and over code.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME rom_patches
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_rom_patches.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}" "${CHAOTIX_GENERATED_DIR}/recomp_tables.cpp")
        set_tests_properties(rom_patches PROPERTIES TIMEOUT 900)
        # Mega Drive tile replacement: the level's own tiles as a pack change nothing.
        add_test(NAME tile_pack
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_tile_pack.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}")
        set_tests_properties(tile_pack PROPERTIES TIMEOUT 900)
        # Mega Drive text by tile sequence: OPTIONS on the title menu, found
        # in the dump, replaced there only, in lockstep; no pattern, no change.
        add_test(NAME md_text
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_md_text.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}")
        set_tests_properties(md_text PROPERTIES TIMEOUT 900)
        # Mega Drive text by sprite: DATA LOAD's banner letters, found in the
        # dump, replaced there only, in lockstep; no pattern, no change.
        add_test(NAME md_sprite_text
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_md_sprite_text.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}")
        set_tests_properties(md_sprite_text PROPERTIES TIMEOUT 900)
        # 32X sprite replacement: the graphics dumped in a level, the title,
        # the lobby, the bonus and the special stage, as a pack, change no
        # picture, in widescreen and 4:3, and lockstep holds; changed ones
        # change only the sprites.
        add_test(NAME sprite_pack
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_sprite_pack.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}")
        set_tests_properties(sprite_pack PROPERTIES TIMEOUT 1800)
        # Whole-bank sprite sheets: the characters' sheets exported from the
        # ROM, loaded unchanged, change no picture in a level and the lobby,
        # with lockstep holding; a recoloured sheet changes its character only.
        add_test(NAME sprite_sheets
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_sprite_sheets.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}")
        set_tests_properties(sprite_sheets PROPERTIES TIMEOUT 1800)
        # Character mods (Mods/Characters): colours the ROM's own change no
        # picture, another character's change only its pixels, stats change
        # movement; lockstep holds and the 68K stays native throughout.
        add_test(NAME character_mods
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_character_mods.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}")
        set_tests_properties(character_mods PROPERTIES TIMEOUT 1800)
        # Mod packs (issue #42): two packs replacing the same sprites and
        # tiles, made by the test, resolved by their order; a disabled one has
        # no effect, loose files still load (above every pack), and a
        # disabled pack's patch is not applied. Lockstep holds.
        add_test(NAME mod_packs
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_mod_packs.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}")
        set_tests_properties(mod_packs PROPERTIES TIMEOUT 1800)
        # Save states (issue #40): saved mid-level, in the special stage, in a
        # widescreen level and the widescreen bonus stage, and loaded again,
        # the game carries on with every picture and the whole machine
        # identical to the run that never stopped, and lockstep holds from it.
        add_test(NAME save_states_carry_on_bit_identically
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_save_states.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}")
        set_tests_properties(save_states_carry_on_bit_identically PROPERTIES TIMEOUT 900
            PASS_REGULAR_EXPRESSION "all scenes carry on bit-identically")
    endif()
    # Rewind (issue #40): every snapshot still held is stepped back to and
    # must be exactly the state it was taken from; the buffer keeps to its
    # memory budget (the special stage changes most between frames).
    add_test(NAME rewind_restores_every_snapshot_in_a_level
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2400 --wide 53
                --stage 300:4:2:2:2:7:0 --fuzz 36:700:2400 --rewind 2:64)
    add_test(NAME rewind_keeps_to_its_budget_in_the_special_stage
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2400
                --stage 300:10:3:0:4:3:0 --fuzz 7:700:2400 --rewind 2:16)
    set_tests_properties(rewind_restores_every_snapshot_in_a_level PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "rewind: stepped back through 1200 snapshots, 1200 restored exactly, 0 wrong")
    set_tests_properties(rewind_keeps_to_its_budget_in_the_special_stage PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "held in ((1[0-5]|[0-9])\\.[0-9]|16\\.0) MB of 16 MB.*restored exactly, 0 wrong")
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
    # The margins follow the window, and the player can resize it (or go
    # fullscreen, or change the aspect) while a level is running. The plane
    # ring shift is latched when a level loads and used to be latched from the
    # margin width, so a window widened during a level left that level in 4:3
    # until the next load -- reported as "it goes into 4:3 whenever a level
    # starts" (issue #7). Here the level starts with no margins at all and is
    # widened 600 frames in; the margins must go live straight away.
    add_test(NAME widescreen_follows_a_resize_during_a_level
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1400
                --stage 600:0:1:0:2:7:0 --wide 0 --wide-at 1200:53)
    set_tests_properties(widescreen_follows_a_resize_during_a_level PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "margins live from frame 120[0-9]")

    # Pausing stops the level engine, but the game keeps the level on screen.
    # The margins were dropped eight frames later, so the picture snapped to
    # 4:3 and back every time anyone paused -- the other half of issue #7.
    # One stretch means the margins went live once and stayed live: a pause in
    # the middle would split it in two.
    add_test(NAME widescreen_survives_a_pause
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3400
                --wide 53 --wide-bottom 16 --stage 600:0:1:0:2:7:0
                --press 1400:right:600 --press 2200:start:6 --press 2800:start:6)
    set_tests_properties(widescreen_survives_a_pause PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "margins live for 2[0-9][0-9][0-9] frames in 1 stretches")

    # Finishing a level hands over to the lobby, which runs in the same game
    # mode as a level, so that scene change shows up only in the place and
    # level the engine reads. Without those in the scene identity the margins
    # went on drawing the finished level's ring for 196 frames -- the camera
    # jumping about inside them -- and then flashed 4:3 for four frames when
    # the lobby was finally drawn. The recorded run changes zone at frame 4640.
    add_test(NAME widescreen_lets_go_when_a_level_hands_over
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 5000
                --wide 53 --wide-bottom 16 --trace-wide
                --replay-input ${CMAKE_CURRENT_SOURCE_DIR}/recordings/finished_run.txt)
    set_tests_properties(widescreen_lets_go_when_a_level_hands_over PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "WIDE f=464[0-9] active=0 .*zone=7 level=0")

    # WORLD ENTRANCE stays 4:3 (issue #13: its narrow rooms showed the outdoor
    # entrance beside them, and the catapult outran the shifted ring). This
    # walk crosses the lobby, takes the catapult and lands in SPEED SLIDER at
    # frame 1989; the margins must first go live there, once it has loaded.
    add_test(NAME widescreen_leaves_the_lobby_4_3
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2400 --wide 53 --trace-wide
                --stage 300:7:0:0:2:7:0 --press 600:right:2600)
    set_tests_properties(widescreen_leaves_the_lobby_4_3 PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "margins live from frame 21[0-9][0-9]")

    # ...unless the player asks for it anyway (WidescreenEverywhere, issue #21).
    add_test(NAME widescreen_lobby_when_asked
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1000 --wide 53 --trace-wide
                --wide-everywhere --stage 300:7:0:0:2:7:0 --press 600:right:2600)
    set_tests_properties(widescreen_lobby_when_asked PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "margins live from frame 5[0-9][0-9]")

    # MARINA MADNESS's clouds and sea are four screen-fixed 32X objects, each
    # two copies of one picture that cover only the 4:3 screen (issue #36:
    # the band stopped at the 4:3 edges). Widened, more copies at the game's
    # own spacing must be on the command list for both margins: the clouds
    # (2 x 96 px apart) and the sea below them (2 x 80 px) ...
    add_test(NAME widescreen_marina_madness_backdrop_fills_the_margins
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 901 --wide 53
                --stage 300:4:1:0:2:7:0 --sprites-log 900)
    set_tests_properties(widescreen_marina_madness_backdrop_fills_the_margins PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "bank  34 frame   0 x  448 y   90[^
]*
 *[0-9]+ bank  34 frame   0 x -128 y   90.*bank  34 frame   3 x  400 y  159[^
]*
 *[0-9]+ bank  34 frame   3 x  -80 y  159")
    # ... and the interpreter and the recompiled code must add them alike.
    add_test(NAME lockstep_widescreen_marina_madness
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2000 --wide 53 --lockstep
                --stage 300:4:2:2:2:7:0 --fuzz 36:700:2000)
    set_tests_properties(lockstep_widescreen_marina_madness PROPERTIES TIMEOUT 900)

    # The HUD at the picture's edge (issue #28) patches the SH-2 boot code;
    # the interpreter and the recompiled code must still agree.
    add_test(NAME lockstep_widescreen_hud_at_the_edge
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1800 --wide 53 --hud-edges --lockstep
                --stage 300:0:1:0:2:7:0 --press 1300:right:400)
    set_tests_properties(lockstep_widescreen_hud_at_the_edge PROPERTIES TIMEOUT 900)

    # The special stage is drawn by its own SH-2 polygon code, not the level
    # engine, and stayed 4:3 (issue #18). Its tube is now projected further
    # right and moved back by the host (see patches.h). The patched code must
    # run the same in the interpreter and the recompiled machine; the margins
    # must go live as the stage starts (the stage select starts it at once,
    # its rectangle is set about 40 frames later); and the stage's own picture
    # must match a 4:3 machine's, give or take pixels along polygon edges.
    add_test(NAME lockstep_special_stage_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1600 --wide 53 --lockstep
                --stage 300:10:0:0:2:7:0 --press 1100:left:120)
    add_test(NAME special_stage_is_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1000 --wide 53 --trace-wide
                --stage 300:10:0:0:2:7:0)
    set_tests_properties(special_stage_is_widescreen PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "margins live from frame 4[0-4][0-9]")
    add_test(NAME special_stage_centre_matches_4_3
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2400 --wide 53 --compare-native
                --min-centre-frames 1900 --stage 300:10:0:0:2:7:0
                --press 1100:left:120 --press 1700:right:200)
    set_tests_properties(lockstep_special_stage_widescreen special_stage_centre_matches_4_3 PROPERTIES TIMEOUT 900)
    # Issue #21: played from a level (a giant ring), the special stage runs in
    # the level's game mode, 0x18. Set it so mid-stage: the margins must stay.
    add_test(NAME special_stage_is_widescreen_in_the_level_mode
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1200 --wide 53 --trace-wide
                --stage 300:10:2:0:2:7:0 --poke 450:DFDE:00 --poke 450:DFDF:18)
    set_tests_properties(special_stage_is_widescreen_in_the_level_mode PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "margins live from frame 4[0-4][0-9].*margins live for 7[0-9][0-9] frames in 1 stretches")
    # Issue #31: the special stage ran in the interpreter (no coverage of it,
    # and the widescreen patches invalidated its busiest routines), enough to
    # lag a slow PC. It must run recompiled, widescreen included.
    add_test(NAME special_stage_runs_recompiled
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1500 --wide 53
                --stage 300:10:0:0:2:7:0 --fuzz 99:700:1500)
    set_tests_properties(special_stage_runs_recompiled PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "\\(100\\.0% native\\); MSH2 (100\\.0|9[5-9]\\.[0-9])% native; SSH2 100\\.0% native")
    # Recompiled, the special stage read the words it keeps after its code as
    # the ROM's constants after it had changed them; this stage and input
    # diverged from the interpreter at frame 1104.
    add_test(NAME lockstep_special_stage_variables
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1300 --lockstep
                --stage 300:10:3:0:4:3:0 --fuzz 7:700:2400)
    set_tests_properties(lockstep_special_stage_variables PROPERTIES TIMEOUT 900)

    # Issue #38: the bonus stage stayed 4:3. Its tunnel is a Mega Drive
    # picture of rings, carried on into the margins; its items and polygons
    # are drawn by the special stage's machinery (see patches.h, PolyScene).
    # The margins must go live as the stage appears and stay for all of it;
    # the native 320 columns must match a 4:3 machine's (give or take polygon
    # edges); the interpreter and the recompiled code must agree; and the
    # recorder must still rebuild every picture.
    add_test(NAME bonus_stage_is_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2400 --wide 53 --trace-wide
                --stage 300:9:0:0:2:7:0 --fuzz 3:700:2400)
    set_tests_properties(bonus_stage_is_widescreen PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "margins live from frame 4[5-6][0-9].*margins live for 1[3-9][0-9][0-9] frames in 1 stretches")
    add_test(NAME bonus_stage_centre_matches_4_3
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2400 --wide 53 --compare-native
                --min-centre-frames 1900 --stage 300:9:1:0:4:3:0)
    add_test(NAME lockstep_bonus_stage_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2400 --wide 53 --lockstep
                --stage 300:9:0:0:2:7:0 --fuzz 3:700:2400)
    add_test(NAME sprites_check_bonus_stage_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3000 --stage 300:9:0:0:2:7:0 --fuzz 3:700:3000
                --wide 80 --sprites-check)
    set_tests_properties(bonus_stage_centre_matches_4_3 lockstep_bonus_stage_widescreen PROPERTIES TIMEOUT 900)
    set_tests_properties(sprites_check_bonus_stage_widescreen PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "sprites check: [1-9][0-9]* pictures, [1-9][0-9]* bytes compared, 0 mismatches")
    # Played from a level, the bonus stage keeps the level's game mode.
    add_test(NAME bonus_stage_is_widescreen_in_the_level_mode
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1600 --wide 53 --trace-wide
                --stage 300:9:0:0:2:7:0 --poke 600:DFDE:00 --poke 600:DFDF:18)
    set_tests_properties(bonus_stage_is_widescreen_in_the_level_mode PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "margins live from frame 4[5-6][0-9].*margins live for 11[0-9][0-9] frames in 1 stretches")
    # Issue #39: the results screen after a cleared special stage stayed 4:3.
    # The stage is cut short (its rings, at SDRAM 0x7DF0, run out) and the
    # result it would store on reaching the goal is put where the 68K reads
    # it. The margins must stay live from the stage through the results, in
    # one stretch; the centre must match 4:3; lockstep must hold.
    set(_results --stage 300:10:0:0:2:7:0 --poke-sdram 1000:7DF0:03 --poke-fb 1200:1500:1FF:01)
    add_test(NAME results_screen_is_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1700 --wide 53 --trace-wide ${_results})
    set_tests_properties(results_screen_is_widescreen PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "margins live from frame 4[0-4][0-9].*margins live for 12[0-9][0-9] frames in 1 stretches")
    add_test(NAME results_screen_centre_matches_4_3
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1700 --wide 53 --compare-native
                --min-centre-frames 1200 ${_results})
    add_test(NAME lockstep_results_screen_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1700 --wide 80 --lockstep ${_results})
    set_tests_properties(results_screen_centre_matches_4_3 lockstep_results_screen_widescreen PROPERTIES TIMEOUT 900)

    # 32X sprite recording (issue #35, docs/SPRITES_32X.md). The host-side
    # rebuild of the blitters must be exact: random calls drawn by the game's
    # own blitter code (interpreted) and by the rebuild must write the same
    # bytes ...
    add_test(NAME sprites_rebuild_matches_the_blitters
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --sprites-oracle 4000)
    set_tests_properties(sprites_rebuild_matches_the_blitters PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "sprites oracle: 4000 calls, 0 differ")
    # ... and every picture of these scenes, rebuilt from its draw list, must
    # give back what the blitters left in the frame buffer, byte for byte:
    # a level, the lobby, the bonus stage, the title and its menus, the
    # special stage; with random input, in widescreen and 4:3.
    add_test(NAME sprites_check_level_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3000 --stage 300:0:1:0:2:7:0 --fuzz 1:700:3000
                --wide 53 --sprites-check)
    add_test(NAME sprites_check_lobby
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3000 --stage 300:7:0:0:2:7:0 --fuzz 2:700:3000
                --wide 53 --sprites-check)
    add_test(NAME sprites_check_bonus_stage
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3000 --stage 300:9:0:0:2:7:0 --fuzz 3:700:3000
                --sprites-check)
    add_test(NAME sprites_check_title_and_menus
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3000 --fuzz 4:1500:3000 --wide 53 --sprites-check)
    add_test(NAME sprites_check_special_stage
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3000 --stage 300:10:0:0:2:7:0 --fuzz 5:700:3000
                --wide 53 --sprites-check)
    # A widened level whose fills reach the margins (Marina Madness).
    add_test(NAME sprites_check_level_fills_in_the_margins
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2000 --stage 300:4:1:0:6:3:0 --fuzz 14:700:3000
                --wide 53 --hud-edges --sprites-check)
    set_tests_properties(sprites_check_level_widescreen sprites_check_lobby sprites_check_bonus_stage
        sprites_check_title_and_menus sprites_check_special_stage sprites_check_level_fills_in_the_margins
        PROPERTIES TIMEOUT 600 PASS_REGULAR_EXPRESSION "sprites check: [1-9][0-9]* pictures, [1-9][0-9]* bytes compared, 0 mismatches")
    # The recorder's hooks run in the interpreter and the recompiled code at
    # the same instructions: both machines record the same draw lists, and
    # stay bit-identical.
    add_test(NAME lockstep_sprites_level
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3000 --stage 300:0:1:0:2:7:0 --fuzz 6:700:3000
                --wide 53 --lockstep --sprites-check)
    add_test(NAME lockstep_sprites_bonus_stage
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2000 --stage 300:9:0:0:2:7:0 --fuzz 3:700:3000
                --lockstep --sprites-check)
    set_tests_properties(lockstep_sprites_level lockstep_sprites_bonus_stage PROPERTIES TIMEOUT 900
        PASS_REGULAR_EXPRESSION "frames bit-identical.*sprites check: [1-9][0-9]* pictures, [1-9][0-9]* bytes compared, 0 mismatches")

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

    # The end of a level saves the run to the DATA slot in FFE01C ($890FD2).
    # The title screen leaves that at 0, DATA 1, so a time attack run saved
    # itself over DATA 1 until the request made it NO SAVE, as the game's own
    # stage select does. The same run finishes without touching the save...
    add_test(NAME time_attack_leaves_the_save_alone
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 4700
                --replay-input ${CMAKE_CURRENT_SOURCE_DIR}/recordings/finished_run.txt)
    set_tests_properties(time_attack_leaves_the_save_alone PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "the run ended after 2086 frames.*save RAM changed on 0 frames")

    # ...and played into DATA 1 it saves as it ends, through the routine the
    # save code copies into work RAM, which only the interpreter runs; the
    # recompiled code and the interpreter must agree on it, save included.
    add_test(NAME lockstep_a_finished_level_saves_to_its_data_slot
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 4700 --lockstep
                --replay-input ${CMAKE_CURRENT_SOURCE_DIR}/recordings/finished_run.txt
                --poke 1000:E01C:00 --poke 1000:E01D:00)
    set_tests_properties(lockstep_a_finished_level_saves_to_its_data_slot PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "save RAM changed on 1 frames, the last at frame 4639.*lockstep: 4700 frames bit-identical")

    # Long-running validation (-DCHAOTIX_LONG_TESTS=ON, ctest -L long): full attract sequence.
    if(CHAOTIX_LONG_TESTS)
    add_test(NAME lockstep_attract_long COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 36000 --lockstep)
    add_test(NAME lockstep_attract_widescreen_long COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 36000 --wide 53 --lockstep)
    add_test(NAME render_attract_long
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 36000 --expect-hashes ${CMAKE_CURRENT_SOURCE_DIR}/golden/attract_every50.txt)
    set_tests_properties(lockstep_attract_long lockstep_attract_widescreen_long render_attract_long PROPERTIES LABELS long TIMEOUT 1800)
    endif()
endif()
