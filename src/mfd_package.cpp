#include "mfd_package.h"
#include "log.h"
#include "config.h"

#include <nlohmann/json.hpp>
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace edvr::mfd {

namespace {

constexpr size_t kMaxManifestFileSize = 1024 * 1024; // 1 MB limit

std::string wideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return {};
    std::string result(sizeNeeded, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &result[0], sizeNeeded, nullptr, nullptr);
    return result;
}

std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (sizeNeeded <= 0) return {};
    std::wstring result(sizeNeeded, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), &result[0], sizeNeeded);
    return result;
}

std::wstring getDirectoryNameFromPath(const std::wstring& path) {
    size_t lastSep = path.find_last_of(L"\\/");
    if (lastSep == std::wstring::npos) return path;
    return path.substr(lastSep + 1);
}

std::wstring getParentDirectory(const std::wstring& path) {
    size_t lastSep = path.find_last_of(L"\\/");
    if (lastSep == std::wstring::npos) return L"";
    return path.substr(0, lastSep);
}

} // namespace

bool isValidPackageId(const std::string& id) {
    // Regex: ^[a-z0-9][a-z0-9_-]{1,31}$
    // Length: 1 leading char + 1 to 31 following chars = 2 to 32 chars total
    if (id.size() < 2 || id.size() > 32) {
        return false;
    }
    char first = id[0];
    if (!((first >= 'a' && first <= 'z') || (first >= '0' && first <= '9'))) {
        return false;
    }
    for (size_t i = 1; i < id.size(); ++i) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool isPathSafeRelative(const std::string& pathStr) {
    if (pathStr.empty()) return false;
    if (pathStr.find('\0') != std::string::npos) return false;

    // Check for absolute path indicators (Windows drive letter or leading slashes)
    if (pathStr.size() >= 2 && std::isalpha(static_cast<unsigned char>(pathStr[0])) && pathStr[1] == ':') {
        return false;
    }
    if (pathStr[0] == '/' || pathStr[0] == '\\') {
        return false;
    }

    // Tokenize path and track relative directory depth to prevent escaping root
    int depth = 0;
    size_t start = 0;
    while (start < pathStr.size()) {
        size_t nextSep = pathStr.find_first_of("/\\", start);
        std::string token;
        if (nextSep == std::string::npos) {
            token = pathStr.substr(start);
            start = pathStr.size();
        } else {
            token = pathStr.substr(start, nextSep - start);
            start = nextSep + 1;
        }

        if (token.empty() || token == ".") {
            continue;
        } else if (token == "..") {
            if (depth <= 0) {
                return false; // Traversal escapes package root
            }
            depth--;
        } else {
            depth++;
        }
    }

    return true;
}

bool isExePathSafe(const std::string& exeStr) {
    if (exeStr.empty()) return false;
    if (exeStr.find('\0') != std::string::npos) return false;

    // Absolute paths are explicitly allowed per §4.2 (for apps with their own updaters)
    bool isAbsolute = false;
    if (exeStr.size() >= 2 && std::isalpha(static_cast<unsigned char>(exeStr[0])) && exeStr[1] == ':') {
        isAbsolute = true;
    } else if (exeStr[0] == '/' || exeStr[0] == '\\') {
        isAbsolute = true;
    }

    if (isAbsolute) {
        return true;
    }

    // Relative exe paths must not escape package root
    return isPathSafeRelative(exeStr);
}

const std::vector<std::string>& getDefaultScreenContexts() {
    static const std::vector<std::string> s_defaultContexts = {
        "ship_cockpit", "fighter", "srv", "on_foot", "taxi", "multicrew"
    };
    return s_defaultContexts;
}

bool parseManifestJson(const std::string& jsonText,
                       MfdPackageManifest& outManifest,
                       const std::string& expectedDirName,
                       std::string* outError) {
    auto fail = [&](const std::string& msg) {
        if (outError) *outError = msg;
        edvr::Log::get().note("mfd_package: manifest parse failure: %s\n", msg.c_str());
        return false;
    };

    if (jsonText.empty()) {
        return fail("Manifest file is empty");
    }
    if (jsonText.size() > kMaxManifestFileSize) {
        return fail("Manifest file exceeds maximum size of 1 MB");
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonText);
    } catch (const nlohmann::json::parse_error& e) {
        return fail(std::string("JSON parse error: ") + e.what());
    }

    if (!j.is_object()) {
        return fail("Root element must be a JSON object");
    }

    // 1. manifestVersion: required int (must be 1)
    if (!j.contains("manifestVersion")) {
        return fail("Missing required 'manifestVersion'");
    }
    if (!j["manifestVersion"].is_number_integer()) {
        return fail("'manifestVersion' must be an integer");
    }
    int version = j["manifestVersion"].get<int>();
    if (version != 1) {
        std::ostringstream ss;
        ss << "Unsupported manifestVersion " << version << " (expected 1)";
        return fail(ss.str());
    }
    outManifest.manifestVersion = version;

    // 2. id: required string, must match regex ^[a-z0-9][a-z0-9_-]{1,31}$
    if (!j.contains("id")) {
        return fail("Missing required 'id'");
    }
    if (!j["id"].is_string()) {
        return fail("'id' must be a string");
    }
    std::string id = j["id"].get<std::string>();
    if (!isValidPackageId(id)) {
        return fail("Invalid package ID '" + id + "': must match regex ^[a-z0-9][a-z0-9_-]{1,31}$");
    }
    if (!expectedDirName.empty() && id != expectedDirName) {
        return fail("Package ID '" + id + "' does not match directory name '" + expectedDirName + "'");
    }
    outManifest.id = id;

    // 3. name: required string
    if (!j.contains("name")) {
        return fail("Missing required 'name'");
    }
    if (!j["name"].is_string()) {
        return fail("'name' must be a string");
    }
    std::string name = j["name"].get<std::string>();
    if (name.empty()) {
        return fail("'name' must not be empty");
    }
    outManifest.name = name;

    // 4. Optional metadata fields
    if (j.contains("version") && j["version"].is_string()) {
        outManifest.version = j["version"].get<std::string>();
    } else {
        outManifest.version.clear();
    }

    if (j.contains("author") && j["author"].is_string()) {
        outManifest.author = j["author"].get<std::string>();
    } else {
        outManifest.author.clear();
    }

    if (j.contains("description") && j["description"].is_string()) {
        outManifest.description = j["description"].get<std::string>();
    } else {
        outManifest.description.clear();
    }

    if (j.contains("minPluginVersion") && j["minPluginVersion"].is_string()) {
        outManifest.minPluginVersion = j["minPluginVersion"].get<std::string>();
    } else {
        outManifest.minPluginVersion.clear();
    }

    // 5. screens: required array of objects
    if (!j.contains("screens")) {
        return fail("Missing required 'screens' array");
    }
    if (!j["screens"].is_array()) {
        return fail("'screens' must be an array");
    }

    outManifest.screens.clear();
    std::set<std::string> screenIds;

    for (const auto& sElem : j["screens"]) {
        if (!sElem.is_object()) {
            return fail("Each item in 'screens' must be an object");
        }

        MfdScreenManifest screen;

        // screen.id: required string
        if (!sElem.contains("id") || !sElem["id"].is_string()) {
            return fail("Screen missing required string 'id'");
        }
        screen.id = sElem["id"].get<std::string>();
        if (screen.id.empty()) {
            return fail("Screen 'id' must not be empty");
        }
        if (screenIds.find(screen.id) != screenIds.end()) {
            return fail("Duplicate screen ID '" + screen.id + "' in package '" + id + "'");
        }
        screenIds.insert(screen.id);

        // screen.file: required relative path
        if (!sElem.contains("file") || !sElem["file"].is_string()) {
            return fail("Screen '" + screen.id + "' missing required string 'file'");
        }
        screen.file = sElem["file"].get<std::string>();
        if (!isPathSafeRelative(screen.file)) {
            return fail("Screen '" + screen.id + "' file '" + screen.file + "' must be a relative path inside package folder");
        }

        // screen.title: optional string
        if (sElem.contains("title") && sElem["title"].is_string()) {
            screen.title = sElem["title"].get<std::string>();
        } else {
            screen.title = screen.id;
        }

        // screen.pose: optional object
        screen.pose = MfdManifestPose();
        if (sElem.contains("pose") && !sElem["pose"].is_null()) {
            const auto& pElem = sElem["pose"];
            if (!pElem.is_object()) {
                return fail("Screen '" + screen.id + "' pose must be an object");
            }
            if (!pElem.contains("position") || !pElem["position"].is_array() || pElem["position"].size() != 3) {
                return fail("Screen '" + screen.id + "' pose missing valid 'position' array of 3 numbers");
            }
            for (size_t i = 0; i < 3; ++i) {
                if (!pElem["position"][i].is_number()) {
                    return fail("Screen '" + screen.id + "' pose 'position' elements must be numbers");
                }
                screen.pose.position[i] = pElem["position"][i].get<float>();
            }

            if (!pElem.contains("pitchDeg") || !pElem["pitchDeg"].is_number()) {
                return fail("Screen '" + screen.id + "' pose missing number 'pitchDeg'");
            }
            screen.pose.pitchDeg = pElem["pitchDeg"].get<float>();

            if (!pElem.contains("yawDeg") || !pElem["yawDeg"].is_number()) {
                return fail("Screen '" + screen.id + "' pose missing number 'yawDeg'");
            }
            screen.pose.yawDeg = pElem["yawDeg"].get<float>();

            if (pElem.contains("rollDeg") && pElem["rollDeg"].is_number()) {
                screen.pose.rollDeg = pElem["rollDeg"].get<float>();
            } else {
                screen.pose.rollDeg = 0.0f;
            }

            if (!pElem.contains("widthM") || !pElem["widthM"].is_number() || pElem["widthM"].get<float>() <= 0.0f) {
                return fail("Screen '" + screen.id + "' pose missing positive number 'widthM'");
            }
            screen.pose.widthM = pElem["widthM"].get<float>();

            if (!pElem.contains("heightM") || !pElem["heightM"].is_number() || pElem["heightM"].get<float>() <= 0.0f) {
                return fail("Screen '" + screen.id + "' pose missing positive number 'heightM'");
            }
            screen.pose.heightM = pElem["heightM"].get<float>();

            screen.pose.hasPose = true;
        }

        // screen.contexts: optional array of strings (default to §7.3)
        if (sElem.contains("contexts") && !sElem["contexts"].is_null()) {
            if (!sElem["contexts"].is_array()) {
                return fail("Screen '" + screen.id + "' 'contexts' must be an array");
            }
            screen.contexts.clear();
            for (const auto& cElem : sElem["contexts"]) {
                if (!cElem.is_string()) {
                    return fail("Screen '" + screen.id + "' context element must be a string");
                }
                screen.contexts.push_back(cElem.get<std::string>());
            }
        } else {
            screen.contexts = getDefaultScreenContexts();
        }

        // screen.excludeContexts: optional array of strings
        if (sElem.contains("excludeContexts") && !sElem["excludeContexts"].is_null()) {
            if (!sElem["excludeContexts"].is_array()) {
                return fail("Screen '" + screen.id + "' 'excludeContexts' must be an array");
            }
            screen.excludeContexts.clear();
            for (const auto& ecElem : sElem["excludeContexts"]) {
                if (!ecElem.is_string()) {
                    return fail("Screen '" + screen.id + "' excludeContexts element must be a string");
                }
                screen.excludeContexts.push_back(ecElem.get<std::string>());
            }
        } else {
            screen.excludeContexts.clear();
        }

        // screen.autoHideUntilGaze: optional boolean (default false)
        if (sElem.contains("autoHideUntilGaze") && sElem["autoHideUntilGaze"].is_boolean()) {
            screen.autoHideUntilGaze = sElem["autoHideUntilGaze"].get<bool>();
        } else {
            screen.autoHideUntilGaze = false;
        }

        outManifest.screens.push_back(std::move(screen));
    }

    // 6. companions: optional array of objects
    outManifest.companions.clear();
    std::set<std::string> companionIds;

    if (j.contains("companions") && !j["companions"].is_null()) {
        if (!j["companions"].is_array()) {
            return fail("'companions' must be an array");
        }

        for (const auto& cElem : j["companions"]) {
            if (!cElem.is_object()) {
                return fail("Each item in 'companions' must be an object");
            }

            MfdCompanionManifest comp;

            // comp.id: required string
            if (!cElem.contains("id") || !cElem["id"].is_string()) {
                return fail("Companion missing required string 'id'");
            }
            comp.id = cElem["id"].get<std::string>();
            if (comp.id.empty()) {
                return fail("Companion 'id' must not be empty");
            }
            if (companionIds.find(comp.id) != companionIds.end()) {
                return fail("Duplicate companion ID '" + comp.id + "' in package '" + id + "'");
            }
            companionIds.insert(comp.id);

            // comp.exe: required string (must resolve inside package or be absolute)
            if (!cElem.contains("exe") || !cElem["exe"].is_string()) {
                return fail("Companion '" + comp.id + "' missing required string 'exe'");
            }
            comp.exe = cElem["exe"].get<std::string>();
            if (!isExePathSafe(comp.exe)) {
                return fail("Companion '" + comp.id + "' exe '" + comp.exe + "' must resolve inside package or be absolute");
            }

            // comp.args: optional array of strings
            comp.args.clear();
            if (cElem.contains("args") && !cElem["args"].is_null()) {
                if (!cElem["args"].is_array()) {
                    return fail("Companion '" + comp.id + "' args must be an array");
                }
                for (const auto& aElem : cElem["args"]) {
                    if (!aElem.is_string()) {
                        return fail("Companion '" + comp.id + "' arg element must be a string");
                    }
                    comp.args.push_back(aElem.get<std::string>());
                }
            }

            // comp.env: optional map of string -> string
            comp.env.clear();
            if (cElem.contains("env") && !cElem["env"].is_null()) {
                if (!cElem["env"].is_object()) {
                    return fail("Companion '" + comp.id + "' env must be an object");
                }
                for (auto it = cElem["env"].begin(); it != cElem["env"].end(); ++it) {
                    if (!it.value().is_string()) {
                        return fail("Companion '" + comp.id + "' env value for key '" + it.key() + "' must be a string");
                    }
                    comp.env[it.key()] = it.value().get<std::string>();
                }
            }

            // comp.restart: optional enum string ("on-failure" | "always" | "never")
            if (cElem.contains("restart") && !cElem["restart"].is_null()) {
                if (!cElem["restart"].is_string()) {
                    return fail("Companion '" + comp.id + "' restart must be a string");
                }
                std::string r = cElem["restart"].get<std::string>();
                if (r == "on-failure") {
                    comp.restart = r;
                    comp.restartPolicy = MfdCompanionRestart::kOnFailure;
                } else if (r == "always") {
                    comp.restart = r;
                    comp.restartPolicy = MfdCompanionRestart::kAlways;
                } else if (r == "never") {
                    comp.restart = r;
                    comp.restartPolicy = MfdCompanionRestart::kNever;
                } else {
                    return fail("Companion '" + comp.id + "' invalid restart policy '" + r + "': expected on-failure, always, or never");
                }
            } else {
                comp.restart = "on-failure";
                comp.restartPolicy = MfdCompanionRestart::kOnFailure;
            }

            outManifest.companions.push_back(std::move(comp));
        }
    }

    return true;
}

bool loadPackageManifest(const std::wstring& manifestPath,
                         MfdPackageManifest& outManifest,
                         const std::string& expectedDirName,
                         std::string* outError) {
    HANDLE hFile = CreateFileW(
        manifestPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        std::string err = "Failed to open manifest file";
        if (outError) *outError = err;
        edvr::Log::get().note("mfd_package: rejected package at '%ls': %s\n", manifestPath.c_str(), err.c_str());
        return false;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) {
        CloseHandle(hFile);
        std::string err = "Failed to query manifest file size";
        if (outError) *outError = err;
        edvr::Log::get().note("mfd_package: rejected package at '%ls': %s\n", manifestPath.c_str(), err.c_str());
        return false;
    }

    if (fileSize.QuadPart > static_cast<LONGLONG>(kMaxManifestFileSize)) {
        CloseHandle(hFile);
        std::string err = "Manifest file exceeds 1 MB limit";
        if (outError) *outError = err;
        edvr::Log::get().note("mfd_package: rejected package at '%ls': %s\n", manifestPath.c_str(), err.c_str());
        return false;
    }

    std::string buffer(static_cast<size_t>(fileSize.QuadPart), '\0');
    DWORD bytesRead = 0;
    if (fileSize.QuadPart > 0) {
        if (!ReadFile(hFile, &buffer[0], static_cast<DWORD>(fileSize.QuadPart), &bytesRead, nullptr) ||
            bytesRead != static_cast<DWORD>(fileSize.QuadPart)) {
            CloseHandle(hFile);
            std::string err = "Failed to read manifest file content";
            if (outError) *outError = err;
            edvr::Log::get().note("mfd_package: rejected package at '%ls': %s\n", manifestPath.c_str(), err.c_str());
            return false;
        }
    }
    CloseHandle(hFile);

    std::wstring packageDir = getParentDirectory(manifestPath);
    std::string dirName = expectedDirName;
    if (dirName.empty()) {
        dirName = wideToUtf8(getDirectoryNameFromPath(packageDir));
    }

    bool ok = parseManifestJson(buffer, outManifest, dirName, outError);
    if (!ok) {
        edvr::Log::get().note("mfd_package: rejected package at '%ls': %s\n",
                              manifestPath.c_str(), outError ? outError->c_str() : "unknown error");
        return false;
    }

    outManifest.packageDir = packageDir;
    return true;
}

std::vector<MfdPackageManifest> discoverPackages(const std::wstring& mfdsDir) {
    std::vector<MfdPackageManifest> packages;

    std::wstring searchPattern = mfdsDir + L"\\*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        edvr::Log::get().note("mfd_package: mfds directory not found at '%ls'\n", mfdsDir.c_str());
        return packages;
    }

    do {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
            continue;
        }

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::wstring folderName = findData.cFileName;
            std::string folderNameUtf8 = wideToUtf8(folderName);

            // Package ID validation regex check on directory name
            if (!isValidPackageId(folderNameUtf8)) {
                edvr::Log::get().note("mfd_package: skipping directory '%ls': invalid package ID format\n", folderName.c_str());
                continue;
            }

            std::wstring packageFolder = mfdsDir + L"\\" + folderName;
            std::wstring manifestPath = packageFolder + L"\\manifest.json";

            MfdPackageManifest manifest;
            std::string error;
            if (loadPackageManifest(manifestPath, manifest, folderNameUtf8, &error)) {
                packages.push_back(std::move(manifest));
            }
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

    // Sort packages by package id ascending, preserving screen order (§4.2)
    std::sort(packages.begin(), packages.end(), [](const MfdPackageManifest& a, const MfdPackageManifest& b) {
        return a.id < b.id;
    });

    edvr::Log::get().note("mfd_package: discovered %zu valid package(s) in '%ls'\n", packages.size(), mfdsDir.c_str());
    return packages;
}

std::wstring getDefaultMfdsDirectory() {
    std::wstring standard = edvr::executableDirectory() + L"\\plugins\\edvr_mfd\\mfds";
    if (GetFileAttributesW(standard.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return standard;
    }
    std::wstring repoPackages = edvr::executableDirectory() + L"\\..\\packages";
    if (GetFileAttributesW(repoPackages.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return repoPackages;
    }
    std::wstring localPackages = edvr::executableDirectory() + L"\\packages";
    if (GetFileAttributesW(localPackages.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return localPackages;
    }
    return standard;
}

} // namespace edvr::mfd
