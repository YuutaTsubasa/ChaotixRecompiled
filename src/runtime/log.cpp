#include "log.h"
#include <cstdio>

namespace chaotix {

static LogLevel g_level = LogLevel::Info;

void log_set_level(LogLevel lvl) { g_level = lvl; }
LogLevel log_get_level() { return g_level; }

void log_msg(LogLevel lvl, const char* category, const char* fmt, ...) {
    if (lvl > g_level) return;
    static const char* names[] = {"E", "W", "I", "D", "T"};
    std::fprintf(stderr, "[%s][%s] ", names[int(lvl)], category);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
}

} // namespace chaotix
