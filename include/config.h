#pragma once

#include <windows.h>
#include <string>

namespace edvr {

inline std::wstring executableDirectory() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    wchar_t* p = wcsrchr(buf, L'\\');
    if (p) *p = L'\0';
    return buf;
}

class Config {
public:
    static Config& get() {
        static Config s_instance;
        return s_instance;
    }

    void init(const std::wstring& dir) {
        m_dir = dir;
        m_path = dir + L"\\edvr.ini";
    }

    bool reloadIfChanged() { return false; }

    bool getBool(const char* key, bool def) const {
        return getInt(key, def ? 1 : 0) != 0;
    }

    int getInt(const char* key, int def) const {
        char buf[64];
        if (GetPrivateProfileStringA("fix", key, "", buf, sizeof(buf), iniPathA().c_str()) > 0) {
            return atoi(buf);
        }
        return def;
    }

    float getFloat(const char* key, float def) const {
        char buf[64];
        if (GetPrivateProfileStringA("fix", key, "", buf, sizeof(buf), iniPathA().c_str()) > 0) {
            return static_cast<float>(atof(buf));
        }
        return def;
    }

    std::string getString(const char* key, const char* def) const {
        char buf[256];
        if (GetPrivateProfileStringA("fix", key, "", buf, sizeof(buf), iniPathA().c_str()) > 0) {
            return buf;
        }
        return def ? def : "";
    }

private:
    std::string iniPathA() const {
        char mb[MAX_PATH]{};
        WideCharToMultiByte(CP_UTF8, 0, m_path.c_str(), -1, mb, sizeof(mb), nullptr, nullptr);
        return std::string(mb);
    }

    std::wstring m_dir;
    std::wstring m_path;
};

} // namespace edvr
