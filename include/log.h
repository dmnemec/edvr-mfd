#pragma once

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <string>

namespace edvr {

class Log {
public:
    static Log& get() {
        static Log s_instance;
        return s_instance;
    }

    void note(const char* fmt, ...) {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        OutputDebugStringA(buf);
    }
};

} // namespace edvr
