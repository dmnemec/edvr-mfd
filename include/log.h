#pragma once

#include <windows.h>
#include <cstdarg>
#include <string>
#include <mutex>

namespace edvr {

// Win32 HANDLE-based logger.
// Using CreateFileW/WriteFile instead of CRT stdio (FILE*) to bypass the CRT's
// stdio atexit handlers. When built with /MT (static CRT), the CRT registers
// its own _flushall atexit that conflicts with ~Log()'s fclose() call, causing
// a crash in is_stream_flushable_or_commitable during DLL unload.
// Win32 HANDLE has no such ordering dependency.
class Log {
public:
    static Log& get() {
        static Log s_instance;
        return s_instance;
    }

    void init(const std::wstring& logDir) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_handle != INVALID_HANDLE_VALUE) return;

        CreateDirectoryW(logDir.c_str(), nullptr);
        std::wstring logPath = logDir + L"\\edvr_mfd.log";

        m_handle = CreateFileW(
            logPath.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (m_handle != INVALID_HANDLE_VALUE) {
            // Write UTF-8 BOM so editors recognise the encoding
            static const char kBom[] = "\xEF\xBB\xBF";
            DWORD written = 0;
            WriteFile(m_handle, kBom, 3, &written, nullptr);

            writeLineLocked("Log session started");
        }
    }

    void note(const char* fmt, ...) {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
        va_end(args);

        OutputDebugStringA(buf);

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_handle != INVALID_HANDLE_VALUE) {
            writeLineLocked(buf);
        }
    }

    // Explicit close for the shutdown path — call before DLL static destructors run.
    void close() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_handle != INVALID_HANDLE_VALUE) {
            writeLineLocked("Log session ended");
            CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
        }
    }

    ~Log() {
        close();
    }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
    std::mutex m_mutex;

    // Must be called with m_mutex already held.
    void writeLineLocked(const char* msg) {
        SYSTEMTIME st;
        GetLocalTime(&st);

        char header[64];
        int hlen = _snprintf_s(header, sizeof(header), _TRUNCATE,
            "[%02d:%02d:%02d.%03d] [edvr-mfd] ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        if (hlen < 0) hlen = 0;

        DWORD written = 0;
        WriteFile(m_handle, header, static_cast<DWORD>(hlen), &written, nullptr);
        size_t msgLen = strlen(msg);
        WriteFile(m_handle, msg, static_cast<DWORD>(msgLen), &written, nullptr);
        // Ensure line ends with \n (safe even for empty msg since msgLen==0 check comes first)
        if (msgLen == 0 || msg[msgLen - 1] != '\n') {
            WriteFile(m_handle, "\n", 1, &written, nullptr);
        }
    }
};

} // namespace edvr
