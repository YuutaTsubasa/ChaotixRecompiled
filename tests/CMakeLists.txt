# Unit tests (no ROM needed)
add_executable(chaotix_tests
    test_main.cpp
    test_m68k.cpp
    test_sh2.cpp
    test_viewport.cpp
)
target_link_libraries(chaotix_tests PRIVATE chaotix_runtime)
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
    set_tests_properties(lockstep_boot_to_level golden_frames PROPERTIES TIMEOUT 600)
endif()
