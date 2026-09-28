#pragma once

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <mutex>

namespace edvr {

class Log {
public:
    static Log& get() {
        static Log s_instance;
        return s_instance;
    }

    void init(const std::wstring& logDir) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_file) return;
        CreateDirectoryW(logDir.c_str(), nullptr);
        std::wstring logPath = logDir + L"\\edvr_mfd.log";
        _wfopen_s(&m_file, logPath.c_str(), L"w, ccs=UTF-8");
        if (m_file) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fprintf(m_file, "[%02d:%02d:%02d.%03d] [edvr-mfd] Log session started\n",
                    st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            fflush(m_file);
        }
    }

    void note(const char* fmt, ...) {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        OutputDebugStringA(buf);

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_file) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fprintf(m_file, "[%02d:%02d:%02d.%03d] %s", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
            fflush(m_file);
        }
    }

    ~Log() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_file) {
            fclose(m_file);
            m_file = nullptr;
        }
    }

private:
    FILE* m_file = nullptr;
    std::mutex m_mutex;
};

} // namespace edvr
