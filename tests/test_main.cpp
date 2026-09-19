#include "test_framework.h"
#include <cstring>

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const auto& t : tf::registry()) {
        if (filter && !std::strstr(t.suite, filter) && !std::strstr(t.name, filter)) continue;
        std::string full = std::string(t.suite) + "." + t.name;
        tf::current() = full.c_str();
        int before = tf::failures();
        t.fn();
        std::printf("%s %s\n", tf::failures() == before ? "[ OK ]" : "[FAIL]", full.c_str());
        ++run;
    }
    std::printf("%d tests, %d failed checks\n", run, tf::failures());
    return tf::failures() ? 1 : 0;
}
