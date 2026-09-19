#pragma once
#include <cstdarg>
#include <cstdint>

namespace chaotix {

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3, Trace = 4 };

void log_set_level(LogLevel lvl);
LogLevel log_get_level();
void log_msg(LogLevel lvl, const char* category, const char* fmt, ...)
#if defined(__MINGW32__) && !defined(__clang__)
    __attribute__((format(gnu_printf, 3, 4)))
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

// Rate-limited logging for unimplemented hardware accesses: prints the first
// few occurrences per call site.
#define CHAOTIX_LOG_LIMITED(lvl, cat, max, ...)                          \
    do {                                                                 \
        static int chaotix_log_count_ = 0;                               \
        if (chaotix_log_count_ < (max)) {                                \
            ++chaotix_log_count_;                                        \
            ::chaotix::log_msg(lvl, cat, __VA_ARGS__);                   \
        }                                                                \
    } while (0)

#define LOGE(cat, ...) ::chaotix::log_msg(::chaotix::LogLevel::Error, cat, __VA_ARGS__)
#define LOGW(cat, ...) ::chaotix::log_msg(::chaotix::LogLevel::Warn, cat, __VA_ARGS__)
#define LOGI(cat, ...) ::chaotix::log_msg(::chaotix::LogLevel::Info, cat, __VA_ARGS__)
#define LOGD(cat, ...) do { if (::chaotix::log_get_level() >= ::chaotix::LogLevel::Debug) ::chaotix::log_msg(::chaotix::LogLevel::Debug, cat, __VA_ARGS__); } while (0)

} // namespace chaotix
