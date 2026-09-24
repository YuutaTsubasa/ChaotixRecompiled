# Turns a binary file into a C++ source defining a byte array, so that assets
# the program cannot run without (the UI font) travel inside the executable
# instead of having to be found on disk on five different platforms.
#
# Used at build time:
#   cmake -DIN=<file> -DOUT=<file.cpp> -DSYMBOL=<name> -P cmake/embed_file.cmake

file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" len)
math(EXPR count "${len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
get_filename_component(name "${IN}" NAME)
file(WRITE "${OUT}"
"// Generated from ${name} by cmake/embed_file.cmake. Do not edit.\n"
"#include <cstddef>\n"
"namespace chaotix {\n"
"extern const unsigned char ${SYMBOL}[];\n"
"extern const size_t ${SYMBOL}_len;\n"
"const unsigned char ${SYMBOL}[] = {${bytes}};\n"
"const size_t ${SYMBOL}_len = ${count};\n"
"} // namespace chaotix\n")
