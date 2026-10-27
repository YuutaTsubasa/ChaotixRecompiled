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
    test_lobby.cpp
    test_tether.cpp
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
    test_ghost.cpp
    test_jukebox.cpp
    test_speedrun.cpp
    test_fps_counter.cpp
    test_divide.cpp
    test_idle_skip.cpp
    test_netplay.cpp
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
        test_music_menu.cpp
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

# LiveSplit (issue #47): the block an auto-splitter scans for is in the
# programs, once, with the documented layout. No ROM needed.
if(CHAOTIX_BUILD_FRONTEND AND NOT CMAKE_CROSSCOMPILING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME livesplit_signature_is_in_the_programs
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_livesplit_signature.py"
                    $<TARGET_FILE:chaotix> $<TARGET_FILE:chaotix_headless>)
        set_tests_properties(livesplit_signature_is_in_the_programs PROPERTIES
            PASS_REGULAR_EXPRESSION "livesplit signature: OK")
    endif()
endif()

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
            # MENU SIZE (issue #49) in the program itself: each size's touch
            # targets taller than the last, and all of them on the screen, in
            # a phone-shaped, a portrait and a desktop window.
            add_test(NAME menu_size_rows_grow_and_stay_on_the_screen
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_menu_size.py"
                        $<TARGET_FILE:chaotix> "${CHAOTIX_ROM}")
            set_tests_properties(menu_size_rows_grow_and_stay_on_the_screen PROPERTIES TIMEOUT 900)
            # SHOW FPS (issue #54): the counter is drawn by the host inside
            # its box at the top right; the game's picture and every window
            # pixel outside the box are as with it off.
            add_test(NAME fps_counter_stays_in_its_box
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_fps_counter.py"
                        $<TARGET_FILE:chaotix> "${CHAOTIX_ROM}")
            set_tests_properties(fps_counter_stays_in_its_box PROPERTIES TIMEOUT 1200)
        endif()
    endif()
    # Issue #35: ROM patches from the Mods folder, over data and over code.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME rom_patches
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_rom_patches.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}" "${CHAOTIX_GENERATED_DIR}/recomp_tables.cpp")
        set_tests_properties(rom_patches PROPERTIES TIMEOUT 900)
        # Issue #62: wait loops fast-forwarded change nothing (state hashes
        # with it on and off) and take most of the SH-2s' cycles.
        add_test(NAME idle_skip_changes_nothing
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_idle_skip.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}")
        set_tests_properties(idle_skip_changes_nothing PROPERTIES TIMEOUT 900)
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
        # TIME ATTACK ghosts (issue #41): a finished run recorded as a ghost
        # and raced by another run of the stage replays to the recorded time,
        # is drawn only where it is, and leaves the live run, its pictures
        # and lockstep exactly as they are without it.
        add_test(NAME time_attack_ghosts
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_ghosts.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}" "${CMAKE_CURRENT_SOURCE_DIR}/recordings")
        set_tests_properties(time_attack_ghosts PROPERTIES TIMEOUT 900)  # fails by its exit code
        # The music player (issue #48): the SOUND TEST's song list read from
        # the ROM, tracks rendered on the player's own machine (music, each
        # its own, the same on the interpreters), a music mod's file playing
        # the track it replaces, and the game left exactly as it was when the
        # player is used mid-level, lockstep included.
        add_test(NAME music_player
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_music_player.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}")
        set_tests_properties(music_player PROPERTIES TIMEOUT 900)  # fails by its exit code
        # Netplay over real UDP (issue #46): two processes on this machine,
        # host and client, end a stretch of a 2P level with the same state as
        # each other and as one machine fed the confirmed pads.
        add_test(NAME netplay_udp_localhost
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_netplay_udp.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}")
        set_tests_properties(netplay_udp_localhost PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)  # fails by its exit code
        # The game's own SOUND TEST (issue #50): a song and a sound effect
        # are heard after A (the Z80 runs up to the 68K's time when its bus
        # is requested, so it takes its V-blank interrupt first), the same
        # on the interpreters, lockstep included.
        add_test(NAME sound_test_plays
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_sound_test.py"
                    $<TARGET_FILE:chaotix_headless> "${CHAOTIX_ROM}")
        set_tests_properties(sound_test_plays PROPERTIES TIMEOUT 600)  # fails by its exit code
    endif()
    # Netplay (issue #46, docs/NETPLAY.md): both peers in one process over a
    # simulated network, playing a level in two-player mode with fuzzed pads:
    # 50-100 ms each way (3-6 frames), 5% of datagrams lost; then with no
    # input delay at all, widescreen, and a worse network, so that nearly
    # every remote pad is mispredicted at first, and the second player at
    # other music and effects levels (which shape only the sound), the host's
    # ring tether STRONG (issue #43; taken over by the client). Both must
    # end with the same state as each other and as a single machine fed the
    # confirmed pads.
    add_test(NAME netplay_loopback_level
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3000 --stage 300:4:2:2:2:7:1
                --netplay-loopback 75:25:5 --netplay-fuzz 3)
    add_test(NAME netplay_loopback_no_delay_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2400 --stage 300:0:1:0:2:7:1 --wide 53
                --netplay-loopback 100:40:8 --netplay-delay 0 --netplay-fuzz 11 --netplay-seed 4
                --netplay-client-volume 30 --tether-strength 2)
    # A divergence (a byte of the second player's work RAM changed) is caught
    # by the hashes and reported on both sides, with the states written out.
    add_test(NAME netplay_desync_is_detected
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1200 --stage 300:4:2:2:2:7:1
                --netplay-loopback 60:20:5 --netplay-inject-desync 500
                --netplay-dump "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(netplay_loopback_level netplay_loopback_no_delay_widescreen PROPERTIES TIMEOUT 900
        PASS_REGULAR_EXPRESSION "netplay: frame [0-9]+: player 1 [0-9a-f]+, player 2 [0-9a-f]+, reference [0-9a-f]+: identical")
    set_tests_properties(netplay_desync_is_detected PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "DESYNC at frame 540.*state at that frame: .*netplay_desync_player1_frame540\\.state.*injected divergence at frame 500 detected by both players")
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

    # WORLD ENTRANCE is widescreen (issue #61; it was kept 4:3 for issues #13
    # and #56). This walk crosses the lobby, takes the catapult and lands in
    # SPEED SLIDER at frame 1989: the margins are live from the lobby's first
    # frame, and the centre is the 4:3 picture on every one of them. Before,
    # with the camera clamped and plane B shifted, the catapult landed four
    # frames early and blocks of the sky were missing inside the 4:3 view.
    add_test(NAME widescreen_lobby_centre_matches_4_3
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2400 --wide 53
                --stage 300:7:0:0:2:7:0 --press 600:right:2600 --compare-native --min-centre-frames 1600)
    set_tests_properties(widescreen_lobby_centre_matches_4_3 PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "margins live from frame 5[0-9][0-9].*centre check: 16[0-9][0-9] widescreen frames")
    # The same walk at frame 858: the right margin shows the pillar before the
    # COMBI CATCHER in its own area's colours (orange), a frame before the
    # game loads them; it showed blue-grey (patches::lobby_margins).
    add_test(NAME widescreen_lobby_margins_in_the_next_area_colours
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 860 --wide 53
                --stage 300:7:0:0:2:7:0 --press 600:right:2600 --expect-hash 858:5d17e048ee2e9f47)
    # The Combi Catcher scrolls down into a room narrower than the view: black
    # margins, the 4:3 centre throughout. Its TIME UP, scaled up past the
    # screen, used to flip a frame late when the SH-2 drew it into margins
    # that show nothing there.
    add_test(NAME widescreen_lobby_combi_catcher_centre_matches_4_3
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3300 --wide 53
                --stage 300:7:0:0:2:7:0 --press 600:right:200 --press 1100:left:35 --press 1300:down:10
                --compare-native --min-centre-frames 2700)
    set_tests_properties(widescreen_lobby_margins_in_the_next_area_colours
        widescreen_lobby_combi_catcher_centre_matches_4_3 PROPERTIES TIMEOUT 600
        FAIL_REGULAR_EXPRESSION "DOES NOT MATCH")
    add_test(NAME lockstep_lobby_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2400 --wide 53 --lockstep
                --stage 300:7:0:0:2:7:0 --press 600:right:2600)
    set_tests_properties(lockstep_lobby_widescreen PROPERTIES TIMEOUT 900
        PASS_REGULAR_EXPRESSION "lockstep: 2400 frames bit-identical")

    # The final boss, WORLD ENTRANCE level 2 (issues #56, #61): its arena is
    # drawn by the special stage's polygon code, widened as the stage is, so
    # its floor and pillars reach the margins (they stopped at the 4:3 edges,
    # which showed them as stretched bars). Margins live from its first frame.
    add_test(NAME widescreen_final_boss
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2000 --wide 53 --trace-wide
                --level-select --stage 300:7:2:0:4:7:0)
    set_tests_properties(widescreen_final_boss PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "level select: reached scene 0018:0007:0002.*margins live from frame 48[0-9]")
    add_test(NAME lockstep_final_boss_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2400 --wide 53 --lockstep
                --level-select --stage 300:7:2:0:4:7:0
                --press 1600:right:300 --press 1700:c:10 --press 2000:left:200 --press 2300:c:10)
    set_tests_properties(lockstep_final_boss_widescreen PROPERTIES TIMEOUT 900
        PASS_REGULAR_EXPRESSION "lockstep: 2400 frames bit-identical")
    # ...but not the ending's scenes (levels 3-6): their sprites are staged
    # for the 4:3 screen, and widened their left margin was black (left of the
    # layout) beside a white picture. No margins at all.
    add_test(NAME widescreen_ending_stays_4_3
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1200 --wide 53 --trace-wide
                --level-select --stage 300:7:3:0:4:7:0)
    set_tests_properties(widescreen_ending_stays_4_3 PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "level select: reached scene 0018:0007:0003"
        FAIL_REGULAR_EXPRESSION "margins live from")

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
    # Issue #62: the wait loops the recompiled code fast-forwards -- the
    # master SH-2 polling the 32X VDP status for V-blank, whose H-blank bits
    # change during a slice, the slave's delay loop, the 68000 waiting on work
    # RAM and COMM -- against the interpreter running every iteration, in a
    # level and the lobby with input.
    add_test(NAME lockstep_idle_skip_level
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2000 --lockstep
                --stage 300:0:1:0:2:7:0 --fuzz 62:200:2000)
    add_test(NAME lockstep_idle_skip_lobby
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1500 --lockstep
                --stage 300:7:0:0:2:7:0 --fuzz 63:200:1500)
    set_tests_properties(lockstep_idle_skip_level lockstep_idle_skip_lobby PROPERTIES TIMEOUT 900)

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
    # Issue #52: entered from a level, the bonus stage stayed 4:3. It keeps
    # the level's scroll mode, line scroll in most zones, which the tunnel
    # check refused. Its palette set is the level's time of day (FFDFF6,
    # $8BCFCC). A giant ring (object 0x30, $8927CC) is put on the player in
    # BOTANIC BASE 1 at SUNSET, with 50 rings: the margins are live in the
    # level, in the bonus stage (frames 1080 to 3767) and in the level again.
    # Before the fix: 1094 frames in 2 stretches.
    set(BONUS_FROM_LEVEL --stage 300:0:1:4:2:7:0 --poke 1000:8046:00 --poke 1000:8047:30 --poke 1000:8068:00
        --poke 1000:8048:01 --poke 1000:8049:08 --poke 1000:804C:06 --poke 1000:804D:6C
        --poke 1000:E008:00 --poke 1000:E009:32)
    add_test(NAME bonus_stage_from_a_level_is_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 4500 --wide 53 --trace-wide ${BONUS_FROM_LEVEL})
    set_tests_properties(bonus_stage_from_a_level_is_widescreen PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "margins live from frame 5[5-6][0-9].*WIDE f=10[7-9][0-9] active=1.*WIDE f=37[0-9][0-9] active=0.*margins live for 3[6-9][0-9][0-9] frames in 3 stretches")
    # SPEED SLIDER 1 at NIGHT: its VRAM left in plane B's unused columns has
    # high-priority cells, which must not show over the rings in the margins
    # (see also vdp_widescreen.bonus_stage_rings_hide_plane_b_in_the_margins).
    add_test(NAME bonus_stage_from_speed_slider_is_widescreen
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 4500 --wide 53 --trace-wide
                --stage 300:1:1:6:2:7:0 --poke 1000:95C6:00 --poke 1000:95C7:30 --poke 1000:95E8:00
                --poke 1000:95C8:01 --poke 1000:95C9:56 --poke 1000:95CC:08 --poke 1000:95CD:CC
                --poke 1000:E008:00 --poke 1000:E009:32)
    set_tests_properties(bonus_stage_from_speed_slider_is_widescreen PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "margins live from frame 5[8-9][0-9].*WIDE f=10[7-9][0-9] active=1.*WIDE f=37[0-9][0-9] active=0.*margins live for 3[6-9][0-9][0-9] frames in 3 stretches")
    # The centre matches a 4:3 machine's, the level and then the bonus stage,
    # until frame 1270: there the SH-2's extra work beyond the 4:3 edges
    # delays a frame buffer flip, as in the special stage and the bonus stage
    # with random input (see docs/ISSUE_38_BONUS_STAGE.md), and the two runs
    # are a frame apart.
    add_test(NAME bonus_stage_from_a_level_centre_matches_4_3
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1260 --wide 53 --compare-native
                --min-centre-frames 640 ${BONUS_FROM_LEVEL})
    set_tests_properties(bonus_stage_from_a_level_centre_matches_4_3 PROPERTIES TIMEOUT 600)
    # With random input from frame 1100 the player leaves the bonus stage at
    # frame 2061 and is back in the level.
    add_test(NAME lockstep_bonus_stage_from_a_level
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3000 --wide 53 --lockstep ${BONUS_FROM_LEVEL}
                --fuzz 5:1100:3000)
    set_tests_properties(lockstep_bonus_stage_from_a_level PROPERTIES TIMEOUT 900)
    # Every palette set (MORNING is bonus_stage_is_widescreen above), in both
    # bonus stages.
    foreach(bonus_time 2 4 6)
        foreach(bonus_level 0 1)
            add_test(NAME bonus_stage_${bonus_level}_at_time_${bonus_time}_is_widescreen
                COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2400 --wide 53 --trace-wide
                        --stage 300:9:${bonus_level}:${bonus_time}:2:7:0 --fuzz 3:700:2400)
            set_tests_properties(bonus_stage_${bonus_level}_at_time_${bonus_time}_is_widescreen PROPERTIES TIMEOUT 600
                PASS_REGULAR_EXPRESSION "margins live from frame 4[5-6][0-9].*margins live for 1[3-9][0-9][0-9] frames in 1 stretches")
        endforeach()
    endforeach()
    add_test(NAME bonus_stage_at_sunset_centre_matches_4_3
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2400 --wide 53 --compare-native
                --min-centre-frames 1900 --stage 300:9:0:4:4:3:0)
    set_tests_properties(bonus_stage_at_sunset_centre_matches_4_3 PROPERTIES TIMEOUT 900)
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

    # LEVEL SELECT (issue #51, frontend/level_select.h): TIME ATTACK's way in,
    # over more places. Each zone's first level (with a different time of day
    # and pair of characters each), the hub, the last special stage and the
    # bonus stage: the game reaches the scene asked for (mode:zone:level, as
    # patches::scene_key has it) NO SAVE, writes nothing to the cartridge
    # save, and the interpreter and the recompiled code agree.
    foreach(_ls "0:1:0:2:7:0:0018:0000:0001" "1:1:2:0:3:0:0018:0001:0001" "2:1:4:3:4:0:0018:0002:0001"
                "3:1:6:4:5:0:0018:0003:0001" "4:1:0:5:6:0:0018:0004:0001" "5:0:2:6:7:0:0018:0005:0000"
                "6:0:0:7:2:0:0018:0006:0000" "7:1:0:2:7:0:0018:0007:0000" "10:5:0:2:7:0:0020:0007:0005"
                "9:0:0:2:7:0:0050:0007:0000")
        string(REPLACE ":" ";" _f "${_ls}")
        list(GET _f 0 _place)
        list(GET _f 1 _level)
        list(SUBLIST _f 0 6 _req)
        list(SUBLIST _f 6 3 _scene)
        string(REPLACE ";" ":" _req "${_req}")
        string(REPLACE ";" ":" _scene "${_scene}")
        add_test(NAME level_select_starts_place_${_place}_level_${_level}
            COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 900 --lockstep --level-select
                    --stage 300:${_req})
        set_tests_properties(level_select_starts_place_${_place}_level_${_level} PROPERTIES TIMEOUT 600
            PASS_REGULAR_EXPRESSION
            "level select: reached scene ${_scene} at frame [0-9]+, save slot FFFF.*save RAM changed on 0 frames.*lockstep: 900 frames bit-identical"
            FAIL_REGULAR_EXPRESSION "level is over")
    endforeach()
    # A level played to its goal from LEVEL SELECT (the recorded run below,
    # BOTANIC BASE 1) is over when the game reaches its lobby, which is when
    # the frontend takes the player back to the page -- and nothing was saved:
    # the slot is still NO SAVE and the cartridge save never changed, where
    # the same run played into DATA 1 saves at frame 4639. Lockstep holds.
    add_test(NAME level_select_a_finished_level_saves_nothing
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 4800 --lockstep --level-select
                --replay-input ${CMAKE_CURRENT_SOURCE_DIR}/recordings/finished_run.txt --peek-every 4800:E01C:2)
    set_tests_properties(level_select_a_finished_level_saves_nothing PROPERTIES TIMEOUT 900
        PASS_REGULAR_EXPRESSION
        "level select: reached scene 0018:0000:0001 at frame [0-9]+, save slot FFFF\n.*level select: the level is over at frame 4639 \\(mode 0018 zone 7 level 0\\).*peek 4800 E01C FFFF.*save RAM changed on 0 frames.*lockstep: 4800 frames bit-identical")
    # A special stage is over when it is left: started this way the game goes
    # on to its title screen (mode 8). Cut short as the results test does.
    add_test(NAME level_select_a_special_stage_ends_at_the_title
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1800 --level-select
                --stage 300:10:0:0:2:7:0 --poke-sdram 1000:7DF0:03 --poke-fb 1200:1500:1FF:01)
    set_tests_properties(level_select_a_special_stage_ends_at_the_title PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION
        "level select: reached scene 0020:0007:0000.*level select: the level is over at frame 1643 \\(mode 0008.*save RAM changed on 0 frames")

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

    # Lobby shortcuts (issue #44, runtime/lobby.h, docs/LOBBY_SHORTCUTS.md).
    # WORLD ENTRANCE as story mode leaves it after INTRODUCTION: DATA 1
    # (FFE01C = 0) and every attraction at level 1 (FFDFFE-FFE002 = 1, as
    # $890F28 sets them). The stage request only stands in for the walk through
    # INTRODUCTION; from there it is the game's own lobby, walked into
    # BOTANIC BASE. With VECTOR and NIGHT chosen the level starts with them --
    # partner FFE03A = 4 x 4, FFDFF6 = 6 -- the DATA slot is left alone, and
    # the interpreter and the recompiled code agree.
    set(LOBBY_WALK --stage 300:7:0:0:2:7:0 --press 600:right:650 --press 1270:right:1130
                   --poke 500:E01C:00 --poke 500:E01D:00 --poke 500:DFFE:01 --poke 500:DFFF:01
                   --poke 500:E000:01 --poke 500:E001:01 --poke 500:E002:01)
    add_test(NAME lobby_shortcuts_pick_the_partner_and_the_time
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2400 --lockstep ${LOBBY_WALK}
                --lobby-partner 4 --lobby-time 3
                --peek-every 2400:E038:4 --peek-every 2400:DFF2:6 --peek-every 2400:E01C:2)
    set_tests_properties(lobby_shortcuts_pick_the_partner_and_the_time PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION
        "peek 2400 E038 00080010.*peek 2400 DFF2 000000010006.*peek 2400 E01C 0000.*lockstep: 2400 frames bit-identical")
    # Off, the same walk and the Combi Catcher (Down on its box: the claw
    # comes up with HEAVY, FFE03A = 6 x 4) give exactly the frames they gave
    # before the shortcuts existed.
    add_test(NAME lobby_shortcuts_off_leave_the_walk_as_it_was
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2600 ${LOBBY_WALK}
                --expect-hashes ${CMAKE_CURRENT_SOURCE_DIR}/golden/lobby_walk_every50.txt)
    add_test(NAME lobby_shortcuts_off_leave_the_combi_catcher_as_it_was
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3300 --stage 300:7:0:0:2:7:0
                --press 600:right:200 --press 1100:left:35 --press 1300:down:10
                --peek-every 3300:E038:4
                --expect-hashes ${CMAKE_CURRENT_SOURCE_DIR}/golden/lobby_catcher_every50.txt)
    set_tests_properties(lobby_shortcuts_off_leave_the_walk_as_it_was
        lobby_shortcuts_off_leave_the_combi_catcher_as_it_was PROPERTIES TIMEOUT 600
        FAIL_REGULAR_EXPRESSION "DOES NOT MATCH")
    set_tests_properties(lobby_shortcuts_off_leave_the_combi_catcher_as_it_was PROPERTIES
        PASS_REGULAR_EXPRESSION "peek 3300 E038 00080018")
    # A level finished with the shortcuts on saves into its DATA slot exactly
    # as before (the same frame, and the slot's record in work RAM at FFFA04
    # byte for byte what the same run writes with them off), and the lobby it
    # hands over to then starts with
    # the choice: partner VECTOR, and DAY with the game's half step kept
    # (FFDFF6 7 -> 3).
    add_test(NAME lobby_shortcuts_a_finished_level_still_saves_to_its_data_slot
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 4800 --lockstep
                --replay-input ${CMAKE_CURRENT_SOURCE_DIR}/recordings/finished_run.txt
                --poke 1000:E01C:00 --poke 1000:E01D:00 --lobby-partner 4 --lobby-time 1
                --peek-every 4700:E038:4 --peek-every 4700:DFF2:6 --peek-every 4800:FA00:36)
    set_tests_properties(lobby_shortcuts_a_finished_level_still_saves_to_its_data_slot PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION
        "peek 4700 E038 00140010.*peek 4700 DFF2 000700000003.*peek 4800 FA00 0000C69E4000BC00000037DC000008A80000.*save RAM changed on 1 frames, the last at frame 4639.*lockstep: 4800 frames bit-identical")

    # Ring tether options (issue #43, runtime/tether.h, docs/RING_TETHER.md).
    # Off, BOTANIC BASE 1 and TECHNO TOWER 2 played with random input give
    # exactly the frames they gave before the options existed (golden hashes
    # from that build).
    add_test(NAME tether_off_leaves_botanic_base_as_it_was
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 6000 --stage 300:0:1:0:2:7:0 --fuzz 43:700:6000
                --expect-hashes ${CMAKE_CURRENT_SOURCE_DIR}/golden/tether_off_bb1_fuzz_every50.txt)
    add_test(NAME tether_off_leaves_techno_tower_as_it_was
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 4000 --stage 300:3:2:0:4:3:0 --fuzz 11:700:4000
                --expect-hashes ${CMAKE_CURRENT_SOURCE_DIR}/golden/tether_off_tt2_fuzz_every50.txt)
    set_tests_properties(tether_off_leaves_botanic_base_as_it_was tether_off_leaves_techno_tower_as_it_was
        PROPERTIES TIMEOUT 600 PASS_REGULAR_EXPRESSION "matches golden" FAIL_REGULAR_EXPRESSION "DOES NOT MATCH")
    # STRENGTH: Knuckles holds Espio (B) and walks right for 90 frames. The
    # ring gives way to x 0x168 when WEAK, 0x155 as the game has it, 0x14E
    # when STRONG; Espio braces in place (state $16) every time. Lockstep
    # holds with each.
    set(TETHER_STRETCH --frames 990 --lockstep --stage 300:0:1:0:2:7:0 --press 900:right+b:90
                       --peek-every 990:AD50:2 --peek-every 990:AE0F:1)
    foreach(_s weak:0168 normal:0155 strong:014E)
        string(REPLACE ":" ";" _p "${_s}")
        list(GET _p 0 _name)
        list(GET _p 1 _x)
        add_test(NAME tether_strength_${_name}_stretches_the_ring
            COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" ${TETHER_STRETCH} --tether-strength ${_name})
        set_tests_properties(tether_strength_${_name}_stretches_the_ring PROPERTIES TIMEOUT 600
            PASS_REGULAR_EXPRESSION "peek 990 AD50 ${_x}\npeek 990 AE0F 16\n.*lockstep: 990 frames bit-identical")
    endforeach()
    # FOLLOW: Espio is put 250 px behind a running Knuckles. As the game has
    # it the ring pulls Knuckles back to x 0xCE on the next frame; following,
    # Knuckles runs on (0x110) and Espio is put back on the ring's circle
    # instead (0x62 rather than 0x20).
    set(TETHER_BEHIND --frames 901 --lockstep --stage 300:0:1:0:2:7:0 --press 880:right:200
                      --poke 900:AE10:00 --poke 900:AE11:10 --peek-every 901:AD50:2 --peek-every 901:AE10:2)
    add_test(NAME tether_normal_pulls_the_player_back
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" ${TETHER_BEHIND})
    set_tests_properties(tether_normal_pulls_the_player_back PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "peek 901 AD50 00CE\npeek 901 AE10 0020\n.*lockstep: 901 frames bit-identical")
    add_test(NAME tether_follow_never_holds_the_player_back
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" ${TETHER_BEHIND} --tether-partner follow)
    set_tests_properties(tether_follow_never_holds_the_player_back PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "peek 901 AD50 0110\npeek 901 AE10 0062\n.*lockstep: 901 frames bit-identical")
    # SOLO: once Espio has landed it is sent away as the game does it (state
    # $14 kept for good, hidden and intangible: +4/+5 = 34 60) and FFFCEA
    # bit 0 drops, so the camera follows Knuckles alone and the tether is off:
    # no Espio (banks 7 and 15) and no ring between them (bank 29) is drawn.
    add_test(NAME tether_solo_leaves_the_player_alone
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1000 --lockstep --stage 300:0:1:0:2:7:0
                --press 880:right:200 --tether-partner solo --sprites-log 1000
                --peek-every 1000:AE0C:2 --peek-every 1000:AE0F:1 --peek-every 1000:FCEA:1)
    set_tests_properties(tether_solo_leaves_the_player_alone PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "bank   2 frame .*peek 1000 AE0C 3460\npeek 1000 AE0F 14\npeek 1000 FCEA 04\n.*lockstep: 1000 frames bit-identical"
        FAIL_REGULAR_EXPRESSION "bank   7 frame|bank  15 frame|bank  29 frame")
    # Holding CALL (A) for half a second brings Espio back the game's way:
    # in play again (state 2, FFFCEA = 5) by frame 1400.
    add_test(NAME tether_solo_hold_call_brings_the_partner_back
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1400 --lockstep --stage 300:0:1:0:2:7:0
                --press 880:right:200 --press 1100:a:40 --tether-partner solo
                --peek-every 1090:AE0F:1 --peek-every 1400:AE0F:1 --peek-every 1400:FCEA:1)
    set_tests_properties(tether_solo_hold_call_brings_the_partner_back PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "peek 1090 AE0F 14\n.*peek 1400 AE0F 02\npeek 1400 FCEA 05\n.*lockstep: 1400 frames bit-identical")
    # TECHNO TOWER's lift waits for both characters to stand in it. SOLO, it
    # counts Knuckles twice and carries him up all the same (y 0xC4D -> 0xB4C;
    # without that he would wait at the bottom for ever).
    add_test(NAME tether_solo_takes_the_lift_meant_for_two
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1500 --lockstep --stage 300:3:1:0:2:7:0
                --press 850:right:1000 --tether-partner solo --peek-every 1500:AD54:2 --peek-every 1500:AE0F:1)
    set_tests_properties(tether_solo_takes_the_lift_meant_for_two PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "peek 1500 AD54 0B4C\npeek 1500 AE0F 14\n.*lockstep: 1500 frames bit-identical")
    # The recorded run that finishes BOTANIC BASE 1, SOLO from frame 3000:
    # the goal is reached (FFFDC5 up by frame 3500), the level hands over to
    # the lobby as before (zone 7 by 4700), where the partner is the game's
    # again, and lockstep holds throughout.
    add_test(NAME tether_solo_finishes_a_level
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 4700 --lockstep
                --replay-input ${CMAKE_CURRENT_SOURCE_DIR}/recordings/finished_run.txt --tether-at 3000:0:2
                --peek-every 3100:AE0F:1 --peek-every 3500:FDC5:1 --peek-every 4700:DFF2:2)
    set_tests_properties(tether_solo_finishes_a_level PROPERTIES TIMEOUT 900
        PASS_REGULAR_EXPRESSION "peek 3100 AE0F 14\n.*peek 3500 FDC5 FF\n.*peek 4700 DFF2 0007\n.*lockstep: 4700 frames bit-identical")
    # PARTNER NONE (issue #55). From LEVEL SELECT, BOTANIC BASE 1 alone: the
    # session plays SOLO whatever GAMEPLAY says (FOLLOW here), so Espio is
    # sent away and not drawn, as in tether_solo_leaves_the_player_alone, and
    # lockstep holds.
    add_test(NAME level_select_partner_none_plays_alone
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 1000 --lockstep --level-select-none
                --stage 300:0:1:0:2:7:0 --press 880:right:200 --tether-partner follow --sprites-log 1000
                --peek-every 1000:AE0C:2 --peek-every 1000:AE0F:1 --peek-every 1000:FCEA:1)
    set_tests_properties(level_select_partner_none_plays_alone PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "tether: partner SOLO from frame 300\n.*bank   2 frame .*peek 1000 AE0C 3460\npeek 1000 AE0F 14\npeek 1000 FCEA 04\n.*lockstep: 1000 frames bit-identical"
        FAIL_REGULAR_EXPRESSION "bank   7 frame|bank  15 frame|bank  29 frame")
    # And once the session is over, GAMEPLAY's own setting is back. Alone, a
    # hit without rings is fatal, and a death sends the player to the lobby
    # (Chaotix has no lives), which ends the session: random play with the
    # rings kept at none dies at frame 1948, and from then the partner is
    # FOLLOW again.
    set(_no_rings "")
    foreach(_f RANGE 400 2000 50)
        list(APPEND _no_rings --poke ${_f}:E009:00)
    endforeach()
    add_test(NAME level_select_partner_none_ends_with_the_session
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2100 --lockstep --level-select-none
                --stage 300:0:1:0:2:7:0 --fuzz 5:700:6000 ${_no_rings} --tether-partner follow)
    set_tests_properties(level_select_partner_none_ends_with_the_session PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "tether: partner SOLO from frame 300\n.*level select: the level is over at frame 1948 \\(mode 0018 zone 7 level 0\\).*tether: partner FOLLOW from frame 1948\n.*lockstep: 2100 frames bit-identical")
    # The lobby shortcut's NONE: the walk into BOTANIC BASE above, and the
    # level starts with the game's partner (Espio, FFE03A = 7 x 4) sent away
    # as SOLO does it, whatever GAMEPLAY says.
    add_test(NAME lobby_shortcut_partner_none_plays_the_attractions_alone
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2600 --lockstep ${LOBBY_WALK}
                --lobby-partner none --lobby-time 3 --tether-partner follow --sprites-log 2600
                --peek-every 2600:E038:4 --peek-every 2600:DFF2:6 --peek-every 2600:AE0F:1 --peek-every 2600:FCEA:1)
    set_tests_properties(lobby_shortcut_partner_none_plays_the_attractions_alone PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "tether: partner SOLO from frame 0\n.*bank   2 frame .*peek 2600 E038 0008001C\npeek 2600 DFF2 000000010006\npeek 2600 AE0F 14\npeek 2600 FCEA 04\n.*lockstep: 2600 frames bit-identical"
        FAIL_REGULAR_EXPRESSION "bank   7 frame|bank  15 frame|bank  29 frame")

    # Long play in each mode with random input: no divergence between the
    # interpreter and the recompiled code, and the level still running at the
    # end (game mode $18, BOTANIC BASE 1).
    foreach(_m "weak:follow" "strong:solo" "strong:normal")
        string(REPLACE ":" ";" _p "${_m}")
        list(GET _p 0 _st)
        list(GET _p 1 _pa)
        add_test(NAME tether_${_st}_${_pa}_plays_6000_frames
            COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 6000 --lockstep --stage 300:0:1:0:2:7:0
                    --fuzz 43:700:6000 --tether-strength ${_st} --tether-partner ${_pa}
                    --peek-every 6000:DFDE:2 --peek-every 6000:DFF2:4)
        set_tests_properties(tether_${_st}_${_pa}_plays_6000_frames PROPERTIES TIMEOUT 900
            PASS_REGULAR_EXPRESSION "peek 6000 DFDE 0018\npeek 6000 DFF2 00000001\n.*lockstep: 6000 frames bit-identical")
    endforeach()

    # Speedrun tools (issue #47, frontend/speedrun.h, docs/SPEEDRUN.md).
    # The recorded run finishes BOTANIC BASE 1 at the goal: the goal flag
    # FFFDC5 goes up on frame 3423 and the clock on the HUD stops there (the
    # time attack test above: 0'34"66). The run's split is taken once, as
    # that clock settles, with that time as the level's and the game time;
    # the progress byte the level end moves on later does not split again.
    # The timer only reads the machine: lockstep holds.
    add_test(NAME speedrun_splits_a_finished_level_once
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 4800 --lockstep --speedrun-start 800
                --replay-input ${CMAKE_CURRENT_SOURCE_DIR}/recordings/finished_run.txt)
    set_tests_properties(speedrun_splits_a_finished_level_once PROPERTIES TIMEOUT 900
        PASS_REGULAR_EXPRESSION
        "speedrun: split 1 BOTANIC BASE 1 at frame 3426, level 0'34\"66 \\(2080 frames\\), game time 0'34\"66 \\(2080 frames\\).*speedrun: 1 split\\(s\\), game time 0'34\"66.*livesplit: block \"CHAOTIX_LIVESPLIT\" version 1 size 72, work RAM found, state 1, run 1, 1 split\\(s\\), game time 2080 frames.*lockstep: 4800 frames bit-identical"
        FAIL_REGULAR_EXPRESSION "split 2 ")
    # A run starts on its own when START on DATA LOAD hands over to the game
    # (mode 0x60 -> INTRODUCTION's 0x58 on frame 2301), and the game runs as
    # it did: lockstep bit-identical with the timer following it.
    add_test(NAME speedrun_starts_when_data_load_hands_over
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 3200 --lockstep --speedrun ${_script})
    set_tests_properties(speedrun_starts_when_data_load_hands_over PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "speedrun: run started at frame 2301\n.*lockstep: 3200 frames bit-identical"
        FAIL_REGULAR_EXPRESSION "run reset")
    # The overlays are drawn over a copy of the picture: every frame of the
    # lobby walk hashes as it always has with the timer running and the input
    # display drawn into the shots, and the display changes no pixel outside
    # its box.
    add_test(NAME speedrun_overlays_leave_the_frames_alone
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 2600 ${LOBBY_WALK} --speedrun-start 400
                --input-display --shot 700,1500 --out ${CMAKE_CURRENT_BINARY_DIR}
                --expect-hashes ${CMAKE_CURRENT_SOURCE_DIR}/golden/lobby_walk_every50.txt)
    set_tests_properties(speedrun_overlays_leave_the_frames_alone PROPERTIES TIMEOUT 600
        PASS_REGULAR_EXPRESSION "input display: drawn on 2 shots, [1-9][0-9]* pixels inside its box, 0 outside"
        FAIL_REGULAR_EXPRESSION "DOES NOT MATCH")

    # Long-running validation (-DCHAOTIX_LONG_TESTS=ON, ctest -L long): full attract sequence.
    if(CHAOTIX_LONG_TESTS)
    add_test(NAME lockstep_attract_long COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 36000 --lockstep)
    add_test(NAME lockstep_attract_widescreen_long COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 36000 --wide 53 --lockstep)
    add_test(NAME render_attract_long
        COMMAND chaotix_headless --rom "${CHAOTIX_ROM}" --frames 36000 --expect-hashes ${CMAKE_CURRENT_SOURCE_DIR}/golden/attract_every50.txt)
    set_tests_properties(lockstep_attract_long lockstep_attract_widescreen_long render_attract_long PROPERTIES LABELS long TIMEOUT 1800)
    endif()
endif()
