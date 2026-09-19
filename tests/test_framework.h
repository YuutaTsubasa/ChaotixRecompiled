// Minimal dependency-free test framework.
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tf {

struct Test {
    const char* suite;
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Test>& registry() {
    static std::vector<Test> r;
    return r;
}
inline int& failures() {
    static int f = 0;
    return f;
}
inline const char*& current() {
    static const char* c = "";
    return c;
}

struct Registrar {
    Registrar(const char* s, const char* n, std::function<void()> f) { registry().push_back({s, n, std::move(f)}); }
};

inline void fail(const char* file, int line, const std::string& msg) {
    std::printf("  FAIL %s (%s:%d): %s\n", current(), file, line, msg.c_str());
    ++failures();
}

} // namespace tf

#define TF_CAT2(a, b) a##b
#define TF_CAT(a, b) TF_CAT2(a, b)
#define TEST(suite, name)                                                                   \
    static void TF_CAT(test_, TF_CAT(suite, TF_CAT(_, name)))();                            \
    static tf::Registrar TF_CAT(reg_, TF_CAT(suite, TF_CAT(_, name)))(#suite, #name,        \
        TF_CAT(test_, TF_CAT(suite, TF_CAT(_, name))));                                     \
    static void TF_CAT(test_, TF_CAT(suite, TF_CAT(_, name)))()

#define CHECK(cond)                                                              \
    do { if (!(cond)) tf::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        auto va_ = (a); auto vb_ = (b);                                                             \
        if (!(va_ == vb_)) {                                                                        \
            char buf_[256];                                                                         \
            std::snprintf(buf_, sizeof buf_, "%s == %s  (0x%llX vs 0x%llX)", #a, #b,               \
                          (unsigned long long)(va_), (unsigned long long)(vb_));                    \
            tf::fail(__FILE__, __LINE__, buf_);                                                     \
        }                                                                                           \
    } while (0)
