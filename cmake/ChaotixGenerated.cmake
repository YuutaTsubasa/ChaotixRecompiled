# Integration of the statically recompiled game code.
#
# The recompiler (chaotix_recomp) reads the user's ROM and coverage traces and
# writes C++ into CHAOTIX_GENERATED_DIR. The file names are fixed (sharded) so
# they can be declared as build outputs.
#
#  * CHAOTIX_ROM set          -> generate during the build (host builds)
#  * files already generated  -> use them (e.g. cross builds for Android/iOS,
#                                generated on the host beforehand)
#  * neither                  -> interpreter-only build

set(_m68k_shards 16)
set(_sh2_shards 8)
set(CHAOTIX_GENERATED_SOURCES "")
math(EXPR _m68k_last "${_m68k_shards} - 1")
math(EXPR _sh2_last "${_sh2_shards} - 1")
foreach(i RANGE 0 ${_m68k_last})
    if(i LESS 10)
        set(n "00${i}")
    else()
        set(n "0${i}")
    endif()
    list(APPEND CHAOTIX_GENERATED_SOURCES "${CHAOTIX_GENERATED_DIR}/recomp_m68k_${n}.cpp")
endforeach()
foreach(i RANGE 0 ${_sh2_last})
    list(APPEND CHAOTIX_GENERATED_SOURCES "${CHAOTIX_GENERATED_DIR}/recomp_sh2_00${i}.cpp")
endforeach()
list(APPEND CHAOTIX_GENERATED_SOURCES "${CHAOTIX_GENERATED_DIR}/recomp_tables.cpp")
set(CHAOTIX_GENERATED_HEADER "${CHAOTIX_GENERATED_DIR}/recomp_decls.h")

file(GLOB CHAOTIX_COVERAGE_FILES "${CMAKE_SOURCE_DIR}/coverage/*.cov")

set(CHAOTIX_HAVE_GENERATED OFF)
if(CHAOTIX_ROM AND EXISTS "${CHAOTIX_ROM}" AND TARGET chaotix_recomp)
    set(_cov_args "")
    foreach(c ${CHAOTIX_COVERAGE_FILES})
        list(APPEND _cov_args --coverage "${c}")
    endforeach()
    add_custom_command(
        OUTPUT ${CHAOTIX_GENERATED_SOURCES} ${CHAOTIX_GENERATED_HEADER}
        COMMAND chaotix_recomp --rom "${CHAOTIX_ROM}" ${_cov_args} --out "${CHAOTIX_GENERATED_DIR}"
        DEPENDS chaotix_recomp "${CHAOTIX_ROM}" ${CHAOTIX_COVERAGE_FILES}
        COMMENT "Recompiling Knuckles' Chaotix (68000 + SH-2) to C++"
        VERBATIM)
    set(CHAOTIX_HAVE_GENERATED ON)
elseif(EXISTS "${CHAOTIX_GENERATED_DIR}/recomp_tables.cpp")
    set(CHAOTIX_HAVE_GENERATED ON)
    message(STATUS "Chaotix: using pre-generated code in ${CHAOTIX_GENERATED_DIR}")
else()
    set(CHAOTIX_GENERATED_SOURCES "")
    message(STATUS "Chaotix: no ROM given (-DCHAOTIX_ROM=...) and no generated code: building interpreter-only runtime")
endif()
