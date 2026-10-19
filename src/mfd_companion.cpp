#include "mfd_companion.h"
#include "mfd_display_schema.h"
#include "mfd_context.h"
#include "log.h"
#include "mfd_version.h"

#include <windows.h>
#include <wincrypt.h>
#include <shlwapi.h>
#include <algorithm>
#include <string>
#include <vector>
#include <map>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace edvr::mfd {

namespace {

static std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
    if (count <= 0) return L"";
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &result[0], count);
    return result;
}

static std::vector<wchar_t> buildEnvironmentBlock(const std::map<std::wstring, std::wstring>& newVars) {
    LPWCH envStrings = GetEnvironmentStringsW();
    std::map<std::wstring, std::wstring> envMap;
    if (envStrings) {
        LPCWSTR curr = envStrings;
        while (*curr) {
            std::wstring entry(curr);
            size_t eq = entry.find(L'=');
            if (eq != std::wstring::npos && eq > 0) {
                std::wstring key = entry.substr(0, eq);
                std::wstring val = entry.substr(eq + 1);
                envMap[key] = val;
            }
            curr += entry.size() + 1;
        }
        FreeEnvironmentStringsW(envStrings);
    }

    for (const auto& kv : newVars) {
        envMap[kv.first] = kv.second;
    }

    std::vector<wchar_t> block;
    for (const auto& kv : envMap) {
        std::wstring line = kv.first + L"=" + kv.second;
        block.insert(block.end(), line.begin(), line.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

} // namespace

std::string computeFileSha256(const std::wstring& filePath) {
    HANDLE hFile = CreateFileW(
        filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        return "";
    }

    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        CloseHandle(hFile);
        return "";
    }

    HCRYPTHASH hHash = 0;
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);
        return "";
    }

    BYTE buffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        CryptHashData(hHash, buffer, bytesRead, 0);
    }
    CloseHandle(hFile);

    BYTE hash[32];
    DWORD hashLen = sizeof(hash);
    std::string result;
    if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
        static const char kHexChars[] = "0123456789abcdef";
        result.reserve(hashLen * 2);
        for (DWORD i = 0; i < hashLen; ++i) {
            result.push_back(kHexChars[(hash[i] >> 4) & 0xF]);
            result.push_back(kHexChars[hash[i] & 0xF]);
        }
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return result;
}

CompanionSupervisor& CompanionSupervisor::instance() {
    static CompanionSupervisor s_instance;
    return s_instance;
}

CompanionSupervisor::CompanionSupervisor() = default;

CompanionSupervisor::~CompanionSupervisor() {
    shutdown();
}

bool CompanionSupervisor::initialize(const std::wstring& mfdsDir, const std::string& sessionUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running.load()) {
        return true;
    }

    m_mfdsDir = mfdsDir.empty() ? getDefaultMfdsDirectory() : mfdsDir;
    m_sessionUuid = sessionUuid.empty() ? (EventWriter::instance().isRunning() ? EventWriter::instance().sessionUuid() : generateUuidV4()) : sessionUuid;

    // Create plugin-wide Win32 Job Object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE (§8.1)
    m_hJob = CreateJobObjectW(nullptr, nullptr);
    if (m_hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(m_hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    } else {
        Log::get().note("mfd_companion: WARNING failed to create Job Object (err=%u)\n", GetLastError());
    }

    m_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    m_running.store(true);
    m_workerThread = std::thread(&CompanionSupervisor::supervisorThreadFunc, this);

    Log::get().note("mfd_companion: CompanionSupervisor initialized (session=%s)\n", m_sessionUuid.c_str());
    return true;
}

void CompanionSupervisor::shutdown() {
    if (!m_running.exchange(false)) {
        return;
    }

    if (m_wakeEvent) {
        SetEvent(m_wakeEvent);
    }

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Terminate all managed companion processes
    for (auto& kv : m_packages) {
        if (kv.second) {
            terminatePackageLocked(*kv.second);
        }
    }
    m_packages.clear();

    if (m_hJob) {
        CloseHandle(m_hJob);
        m_hJob = nullptr;
    }

    if (m_wakeEvent) {
        CloseHandle(m_wakeEvent);
        m_wakeEvent = nullptr;
    }

    Log::get().note("mfd_companion: CompanionSupervisor shut down successfully\n");
}

void CompanionSupervisor::registerPackage(const MfdPackageManifest& manifest) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (manifest.id.empty()) return;

    auto pkg = std::make_unique<ManagedPackage>();
    pkg->id = manifest.id;
    pkg->packageDir = manifest.packageDir;
    pkg->companionsEnabled.store(true);

    for (const auto& compManifest : manifest.companions) {
        ManagedCompanion comp;
        comp.packageId = manifest.id;
        comp.companionId = compManifest.id;
        comp.packageDir = manifest.packageDir;
        comp.manifest = compManifest;

        // Resolve executable path
        std::wstring exeW = utf8ToWide(compManifest.exe);
        std::replace(exeW.begin(), exeW.end(), L'/', L'\\');
        if (PathIsRelativeW(exeW.c_str())) {
            comp.resolvedExe = manifest.packageDir + L"\\" + exeW;
        } else {
            comp.resolvedExe = exeW;
        }

        // Precompute SHA-256 if file exists
        comp.sha256 = computeFileSha256(comp.resolvedExe);

        pkg->companions.push_back(std::move(comp));
    }

    m_packages[manifest.id] = std::move(pkg);

    // Launch companions if supervisor is running and package is enabled
    if (m_running.load()) {
        auto* p = m_packages[manifest.id].get();
        if (p && p->companionsEnabled.load()) {
            for (auto& comp : p->companions) {
                launchCompanionLocked(*p, comp);
            }
        }
        if (m_wakeEvent) SetEvent(m_wakeEvent);
    }
}

void CompanionSupervisor::registerPackages(const std::vector<MfdPackageManifest>& manifests) {
    for (const auto& m : manifests) {
        registerPackage(m);
    }
}

void CompanionSupervisor::unregisterPackage(const std::string& packageId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_packages.find(packageId);
    if (it != m_packages.end()) {
        terminatePackageLocked(*it->second);
        m_packages.erase(it);
        if (m_wakeEvent) SetEvent(m_wakeEvent);
    }
}

void CompanionSupervisor::clearPackages() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& kv : m_packages) {
        if (kv.second) {
            terminatePackageLocked(*kv.second);
        }
    }
    m_packages.clear();
    if (m_wakeEvent) SetEvent(m_wakeEvent);
}

void CompanionSupervisor::setPackageCompanionsEnabled(const std::string& packageId, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_packages.find(packageId);
    if (it == m_packages.end() || !it->second) return;

    auto& pkg = *it->second;
    bool previous = pkg.companionsEnabled.exchange(enabled);
    if (previous == enabled) return;

    if (!enabled) {
        // §8.3 Toggling off: terminate package's processes, clear OFFLINE badge -> PAUSED
        for (auto& comp : pkg.companions) {
            terminateCompanionLocked(comp);
            comp.hasScheduledRestart = false;
            comp.state = CompanionProcessState::kPaused;
        }
        Log::get().note("mfd_companion: package '%s' companions disabled (PAUSED)\n", packageId.c_str());
    } else {
        // §8.3 Toggling on: launch, reset crash counters
        Log::get().note("mfd_companion: package '%s' companions enabled (launching)\n", packageId.c_str());
        for (auto& comp : pkg.companions) {
            comp.crashTimes.clear();
            comp.currentAttempt = 0;
            comp.exitCode = 0;
            comp.state = CompanionProcessState::kNotStarted;
            comp.hasScheduledRestart = false;
            launchCompanionLocked(pkg, comp);
        }
    }

    if (m_wakeEvent) SetEvent(m_wakeEvent);
}

bool CompanionSupervisor::isPackageCompanionsEnabled(const std::string& packageId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_packages.find(packageId);
    if (it != m_packages.end() && it->second) {
        return it->second->companionsEnabled.load();
    }
    return true;
}

bool CompanionSupervisor::isCompanionRunning(const std::string& packageId, const std::string& companionId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_packages.find(packageId);
    if (it == m_packages.end() || !it->second) return false;

    const auto& pkg = *it->second;
    for (const auto& comp : pkg.companions) {
        if (companionId.empty() || comp.companionId == companionId) {
            if (comp.isRunning()) return true;
        }
    }
    return false;
}

bool CompanionSupervisor::hasCompanionGaveUp(const std::string& packageId, const std::string& companionId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_packages.find(packageId);
    if (it == m_packages.end() || !it->second) return false;

    const auto& pkg = *it->second;
    for (const auto& comp : pkg.companions) {
        if (companionId.empty() || comp.companionId == companionId) {
            if (comp.state == CompanionProcessState::kGaveUp) return true;
        }
    }
    return false;
}

int CompanionSupervisor::getCompanionCrashCount(const std::string& packageId, const std::string& companionId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_packages.find(packageId);
    if (it == m_packages.end() || !it->second) return 0;

    const auto& pkg = *it->second;
    for (const auto& comp : pkg.companions) {
        if (companionId.empty() || comp.companionId == companionId) {
            return static_cast<int>(comp.crashTimes.size());
        }
    }
    return 0;
}

int CompanionSupervisor::getCompanionRestartAttempt(const std::string& packageId, const std::string& companionId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_packages.find(packageId);
    if (it == m_packages.end() || !it->second) return 0;

    const auto& pkg = *it->second;
    for (const auto& comp : pkg.companions) {
        if (companionId.empty() || comp.companionId == companionId) {
            return comp.currentAttempt;
        }
    }
    return 0;
}

bool CompanionSupervisor::getStatusBadgeOverride(const std::string& packageId,
                                                std::string& outBadge,
                                                MfdNamedColor& outColor,
                                                std::string& outColorName) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_packages.find(packageId);
    if (it == m_packages.end() || !it->second) return false;

    const auto& pkg = *it->second;
    if (pkg.companions.empty()) return false;

    // Check if user disabled companions for this package (§8.3)
    if (!pkg.companionsEnabled.load()) {
        outBadge = "PAUSED";
        outColor = MfdNamedColor::kWarn;
        outColorName = "warn";
        return true;
    }

    // While companions are not running (crashed / waiting restart / gave up / failed to start),
    // status badge is COMPANION OFFLINE (color crit) (§8.2)
    bool anyOffline = false;
    for (const auto& comp : pkg.companions) {
        if (comp.state == CompanionProcessState::kCrashed ||
            comp.state == CompanionProcessState::kWaitingRestart ||
            comp.state == CompanionProcessState::kGaveUp ||
            comp.state == CompanionProcessState::kNotStarted) {
            anyOffline = true;
            break;
        }
    }

    if (anyOffline) {
        outBadge = "COMPANION OFFLINE";
        outColor = MfdNamedColor::kCrit;
        outColorName = "crit";
        return true;
    }

    return false;
}

bool CompanionSupervisor::applyStatusBadgeOverride(const std::string& packageId, MfdViewModel& vm) const {
    std::string badge;
    MfdNamedColor col = MfdNamedColor::kNormal;
    std::string colName;
    if (getStatusBadgeOverride(packageId, badge, col, colName)) {
        vm.statusBadge = badge;
        vm.statusBadgeNamedColor = col;
        vm.statusBadgeColorName = colName;
        vm.statusBadgeColor = resolveNamedColor(col, palette::getTheme(MfdColorTheme::kDefaultAmber));
        return true;
    }
    return false;
}

void CompanionSupervisor::setBackoffSchedule(const std::vector<int>& delaySeconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_backoffSchedule = delaySeconds;
}

void CompanionSupervisor::setCrashWindowDuration(std::chrono::milliseconds duration) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_crashWindowDuration = duration;
}

void CompanionSupervisor::setMaxCrashesInWindow(int maxCrashes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxCrashesInWindow = maxCrashes;
}

void CompanionSupervisor::tick() {
    std::lock_guard<std::mutex> lock(m_mutex);
    processTickLocked();
}

std::vector<CompanionInstanceInfo> CompanionSupervisor::getInstances() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<CompanionInstanceInfo> results;
    for (const auto& pkgKv : m_packages) {
        if (!pkgKv.second) continue;
        for (const auto& c : pkgKv.second->companions) {
            CompanionInstanceInfo info;
            info.packageId = c.packageId;
            info.companionId = c.companionId;
            info.resolvedExe = c.resolvedExe;
            info.pid = c.pid;
            info.exitCode = c.exitCode;
            info.crashCount = static_cast<int>(c.crashTimes.size());
            info.restartAttempt = c.currentAttempt;
            info.state = c.state;
            info.sha256 = c.sha256;
            results.push_back(info);
        }
    }
    return results;
}

void CompanionSupervisor::terminateCompanionLocked(ManagedCompanion& comp) {
    if (comp.hProcess != INVALID_HANDLE_VALUE && comp.hProcess != nullptr) {
        TerminateProcess(comp.hProcess, 0);
        WaitForSingleObject(comp.hProcess, 200);
        CloseHandle(comp.hProcess);
        comp.hProcess = INVALID_HANDLE_VALUE;
    }
    comp.pid = 0;
    comp.hasScheduledRestart = false;
}

void CompanionSupervisor::terminatePackageLocked(ManagedPackage& pkg) {
    for (auto& comp : pkg.companions) {
        terminateCompanionLocked(comp);
    }
}

bool CompanionSupervisor::launchCompanionLocked(ManagedPackage& pkg, ManagedCompanion& comp) {
    terminateCompanionLocked(comp);

    if (comp.sha256.empty()) {
        comp.sha256 = computeFileSha256(comp.resolvedExe);
    }

    // Build command line: "<exe>" [args...]
    std::wstring cmd = L"\"" + comp.resolvedExe + L"\"";
    for (const auto& arg : comp.manifest.args) {
        std::wstring argW = utf8ToWide(arg);
        if (!argW.empty() && argW.find_first_of(L" \t") != std::wstring::npos && argW.front() != L'"') {
            cmd += L" \"" + argW + L"\"";
        } else {
            cmd += L" " + argW;
        }
    }
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    // Build environment block per §8.1
    std::map<std::wstring, std::wstring> envVars;
    envVars[L"EDVR_MFD_PACKAGE_DIR"] = pkg.packageDir;
    envVars[L"EDVR_MFD_JOURNAL_DIR"] = ContextEvaluator::getJournalDir();
    envVars[L"EDVR_MFD_SESSION"] = utf8ToWide(m_sessionUuid);
    envVars[L"EDVR_MFD_EVENTS"] = (EventWriter::instance().isRunning() ?
        EventWriter::instance().getPackageEventsPath(pkg.id) : (pkg.packageDir + L"\\events.jsonl"));
    envVars[L"EDVR_MFD_PLUGIN_VERSION"] = EDVR_MFD_VERSION_W;

    for (const auto& kv : comp.manifest.env) {
        envVars[utf8ToWide(kv.first)] = utf8ToWide(kv.second);
    }
    std::vector<wchar_t> envBlock = buildEnvironmentBlock(envVars);

    // Stdout/stderr redirection to <package_dir>/companion.log (truncated per session) (§8.1)
    std::wstring logPath = pkg.packageDir + L"\\companion.log";
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    DWORD creationDisposition = pkg.logTruncated ? OPEN_ALWAYS : CREATE_ALWAYS;
    HANDLE hLog = CreateFileW(
        logPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        creationDisposition,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hLog != INVALID_HANDLE_VALUE && pkg.logTruncated) {
        SetFilePointer(hLog, 0, nullptr, FILE_END);
    }
    pkg.logTruncated = true;

    HANDLE hNullIn = CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = (hLog != INVALID_HANDLE_VALUE) ? hLog : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = (hLog != INVALID_HANDLE_VALUE) ? hLog : GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = (hNullIn != INVALID_HANDLE_VALUE) ? hNullIn : GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    DWORD creationFlags = CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;

    BOOL ok = CreateProcessW(
        comp.resolvedExe.c_str(),
        cmdBuf.data(),
        nullptr,
        nullptr,
        TRUE, // Inherit stdout/stderr log handle
        creationFlags,
        envBlock.data(),
        pkg.packageDir.c_str(),
        &si,
        &pi
    );

    if (hLog != INVALID_HANDLE_VALUE) {
        CloseHandle(hLog);
    }
    if (hNullIn != INVALID_HANDLE_VALUE) {
        CloseHandle(hNullIn);
    }

    if (!ok) {
        DWORD err = GetLastError();
        Log::get().note("mfd_companion: failed to launch %S (error %u)\n", comp.resolvedExe.c_str(), err);
        comp.exitCode = static_cast<int>(err);
        comp.state = CompanionProcessState::kCrashed;

        // Crash history tracking & backoff
        auto now = std::chrono::steady_clock::now();
        comp.crashTimes.push_back(now);
        auto cutoff = now - m_crashWindowDuration;
        comp.crashTimes.erase(
            std::remove_if(comp.crashTimes.begin(), comp.crashTimes.end(), [&](const auto& t) { return t < cutoff; }),
            comp.crashTimes.end()
        );

        if (comp.crashTimes.size() >= static_cast<size_t>(m_maxCrashesInWindow)) {
            comp.state = CompanionProcessState::kGaveUp;
            comp.hasScheduledRestart = false;
            if (EventWriter::instance().isRunning()) {
                EventWriter::instance().enqueue(MfdEventRecord::createCompanionGaveUp(
                    comp.packageId, comp.companionId, 0, comp.exitCode, comp.currentAttempt));
            }
        } else {
            if (comp.manifest.restartPolicy != MfdCompanionRestart::kNever) {
                int crashIdx = static_cast<int>(comp.crashTimes.size()) - 1;
                int delaySec = 30;
                if (crashIdx >= 0 && crashIdx < static_cast<int>(m_backoffSchedule.size())) {
                    delaySec = m_backoffSchedule[crashIdx];
                } else if (!m_backoffSchedule.empty()) {
                    delaySec = m_backoffSchedule.back();
                }
                comp.hasScheduledRestart = true;
                comp.nextRestartTime = now + std::chrono::seconds(delaySec);
                comp.state = CompanionProcessState::kWaitingRestart;
            }
        }
        return false;
    }

    // Assign to single plugin-wide Job Object before resuming thread (§8.1)
    if (m_hJob) {
        AssignProcessToJobObject(m_hJob, pi.hProcess);
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    comp.hProcess = pi.hProcess;
    comp.pid = pi.dwProcessId;
    comp.state = CompanionProcessState::kRunning;
    comp.currentAttempt++;
    comp.hasScheduledRestart = false;

    Log::get().note("mfd_companion: launched %S (pid=%u, sha256=%s)\n",
                    comp.resolvedExe.c_str(), comp.pid, comp.sha256.c_str());

    if (EventWriter::instance().isRunning()) {
        EventWriter::instance().enqueue(MfdEventRecord::createCompanionStarted(
            comp.packageId, comp.companionId, comp.pid, comp.currentAttempt));
    }

    return true;
}

void CompanionSupervisor::processTickLocked() {
    auto now = std::chrono::steady_clock::now();

    for (auto& pkgKv : m_packages) {
        auto& pkg = *pkgKv.second;
        if (!pkg.companionsEnabled.load()) continue;

        for (auto& comp : pkg.companions) {
            // Check running process
            if (comp.isRunning()) {
                DWORD waitRes = WaitForSingleObject(comp.hProcess, 0);
                if (waitRes == WAIT_OBJECT_0) {
                    DWORD exitCode = 0;
                    GetExitCodeProcess(comp.hProcess, &exitCode);
                    CloseHandle(comp.hProcess);
                    comp.hProcess = INVALID_HANDLE_VALUE;
                    comp.exitCode = static_cast<int>(exitCode);

                    // §8.1: Exit code 0 is treated as "intentional, do not restart"
                    if (exitCode == 0 && comp.manifest.restartPolicy != MfdCompanionRestart::kAlways) {
                        comp.state = CompanionProcessState::kExitedCleanly;
                        comp.hasScheduledRestart = false;
                        Log::get().note("mfd_companion: companion %s/%s exited cleanly (code 0), not restarting\n",
                                        comp.packageId.c_str(), comp.companionId.c_str());
                    } else {
                        // Crash!
                        comp.crashTimes.push_back(now);
                        auto cutoff = now - m_crashWindowDuration;
                        comp.crashTimes.erase(
                            std::remove_if(comp.crashTimes.begin(), comp.crashTimes.end(), [&](const auto& t) { return t < cutoff; }),
                            comp.crashTimes.end()
                        );

                        if (comp.crashTimes.size() >= static_cast<size_t>(m_maxCrashesInWindow)) {
                            comp.state = CompanionProcessState::kGaveUp;
                            comp.hasScheduledRestart = false;
                            Log::get().note("mfd_companion: companion %s/%s crashed %zu times within window, gave up\n",
                                            comp.packageId.c_str(), comp.companionId.c_str(), comp.crashTimes.size());
                            if (EventWriter::instance().isRunning()) {
                                EventWriter::instance().enqueue(MfdEventRecord::createCompanionGaveUp(
                                    comp.packageId, comp.companionId, comp.pid, comp.exitCode, comp.currentAttempt));
                            }
                        } else {
                            comp.state = CompanionProcessState::kCrashed;
                            Log::get().note("mfd_companion: companion %s/%s crashed with code %d (attempt %d)\n",
                                            comp.packageId.c_str(), comp.companionId.c_str(), comp.exitCode, comp.currentAttempt);
                            if (EventWriter::instance().isRunning()) {
                                EventWriter::instance().enqueue(MfdEventRecord::createCompanionCrashed(
                                    comp.packageId, comp.companionId, comp.pid, comp.exitCode, comp.currentAttempt));
                            }

                            if (comp.manifest.restartPolicy != MfdCompanionRestart::kNever) {
                                int crashIdx = static_cast<int>(comp.crashTimes.size()) - 1;
                                int delaySec = 30;
                                if (crashIdx >= 0 && crashIdx < static_cast<int>(m_backoffSchedule.size())) {
                                    delaySec = m_backoffSchedule[crashIdx];
                                } else if (!m_backoffSchedule.empty()) {
                                    delaySec = m_backoffSchedule.back();
                                }
                                comp.hasScheduledRestart = true;
                                comp.nextRestartTime = now + std::chrono::seconds(delaySec);
                                comp.state = CompanionProcessState::kWaitingRestart;
                                Log::get().note("mfd_companion: scheduled restart for %s/%s in %d s\n",
                                                comp.packageId.c_str(), comp.companionId.c_str(), delaySec);
                            }
                        }
                    }
                }
            } else if (comp.hasScheduledRestart) {
                if (now >= comp.nextRestartTime) {
                    comp.hasScheduledRestart = false;
                    launchCompanionLocked(pkg, comp);
                }
            }
        }
    }
}

void CompanionSupervisor::supervisorThreadFunc() {
    while (m_running.load()) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            processTickLocked();
        }

        // Wait up to 100 ms or until signaled by wakeEvent
        WaitForSingleObject(m_wakeEvent, 100);
    }
}

} // namespace edvr::mfd
