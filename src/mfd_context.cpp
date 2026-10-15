#include "mfd_context.h"
#include "log.h"

#include <nlohmann/json.hpp>
#include <windows.h>
#include <chrono>
#include <thread>
#include <algorithm>

namespace edvr::mfd {

namespace {

struct ContextNameEntry {
    GameContextBit bit;
    GameContextMask mask;
    const char* name;
};

constexpr ContextNameEntry kContextEntries[] = {
    { GameContextBit::MainMenu,        GameContext::MainMenu,        "main_menu" },
    { GameContextBit::ShipCockpit,     GameContext::ShipCockpit,     "ship_cockpit" },
    { GameContextBit::Fighter,         GameContext::Fighter,         "fighter" },
    { GameContextBit::Srv,             GameContext::Srv,             "srv" },
    { GameContextBit::OnFoot,          GameContext::OnFoot,          "on_foot" },
    { GameContextBit::Taxi,            GameContext::Taxi,            "taxi" },
    { GameContextBit::Multicrew,       GameContext::Multicrew,       "multicrew" },
    { GameContextBit::Docked,          GameContext::Docked,          "docked" },
    { GameContextBit::Landed,          GameContext::Landed,          "landed" },
    { GameContextBit::Supercruise,     GameContext::Supercruise,     "supercruise" },
    { GameContextBit::NormalSpace,     GameContext::NormalSpace,     "normal_space" },
    { GameContextBit::Hardpoints,      GameContext::Hardpoints,      "hardpoints" },
    { GameContextBit::AnalysisMode,    GameContext::AnalysisMode,    "analysis_mode" },
    { GameContextBit::CombatMode,      GameContext::CombatMode,      "combat_mode" },
    { GameContextBit::GalaxyMap,       GameContext::GalaxyMap,       "galaxy_map" },
    { GameContextBit::SystemMap,       GameContext::SystemMap,       "system_map" },
    { GameContextBit::Orrery,          GameContext::Orrery,          "orrery" },
    { GameContextBit::Fss,             GameContext::Fss,             "fss" },
    { GameContextBit::Saa,             GameContext::Saa,             "saa" },
    { GameContextBit::Codex,           GameContext::Codex,           "codex" },
    { GameContextBit::StationServices, GameContext::StationServices, "station_services" },
    { GameContextBit::Paused,          GameContext::Paused,          "paused" }
};

// Safe Win32 file read into std::string (no CRT stdio)
bool readTextFileWin32(const std::wstring& filePath, std::string& outContent) {
    HANDLE hFile = CreateFileW(
        filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE || fileSize > 2 * 1024 * 1024) {
        CloseHandle(hFile);
        return false;
    }

    outContent.resize(fileSize);
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, &outContent[0], fileSize, &bytesRead, nullptr);
    CloseHandle(hFile);

    if (!ok || bytesRead == 0) {
        outContent.clear();
        return false;
    }

    outContent.resize(bytesRead);
    return true;
}

} // namespace

std::wstring ContextEvaluator::s_journalDirOverride;
std::mutex ContextEvaluator::s_overrideMutex;

ContextEvaluator& ContextEvaluator::instance() {
    static ContextEvaluator s_instance;
    return s_instance;
}

ContextEvaluator::ContextEvaluator() {
    m_activeContexts = GameContext::None;
    m_activeContextNames.clear();
}

ContextEvaluator::~ContextEvaluator() {
    stopWatcher();
}

void ContextEvaluator::startWatcher(uint32_t pollIntervalMs) {
    if (m_running.exchange(true)) {
        return; // Already running
    }
    m_workerThread = std::thread(&ContextEvaluator::workerLoop, this, pollIntervalMs);
}

void ContextEvaluator::stopWatcher() {
    if (!m_running.exchange(false)) {
        return;
    }
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

bool ContextEvaluator::isWatcherRunning() const {
    return m_running.load();
}

void ContextEvaluator::workerLoop(uint32_t pollIntervalMs) {
    while (m_running.load()) {
        pollOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
    }
}

bool ContextEvaluator::pollOnce() {
    std::wstring statusPath = getStatusJsonPath();
    if (statusPath.empty()) {
        return updateFromStatus(0, 0, 0);
    }

    // Check modification time
    WIN32_FILE_ATTRIBUTE_DATA fileAttr;
    if (!GetFileAttributesExW(statusPath.c_str(), GetFileExInfoStandard, &fileAttr)) {
        // File does not exist -> main_menu
        return updateFromStatus(0, 0, 0);
    }

    ULARGE_INTEGER fileTime;
    fileTime.LowPart = fileAttr.ftLastWriteTime.dwLowDateTime;
    fileTime.HighPart = fileAttr.ftLastWriteTime.dwHighDateTime;
    uint64_t currentFileTime = fileTime.QuadPart;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (currentFileTime == m_lastFileTime && m_snapshot.valid) {
            return false; // No change on disk
        }
    }

    uint32_t flags = 0;
    uint32_t flags2 = 0;
    int guiFocus = 0;

    bool parsed = readAndParseStatus(statusPath, flags, flags2, guiFocus);
    if (!parsed) {
        // File unreadable / empty / partial write -> evaluate as main_menu
        return updateFromStatus(0, 0, 0);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastFileTime = currentFileTime;
    }

    return updateFromStatus(flags, flags2, guiFocus);
}

bool ContextEvaluator::updateFromStatus(uint32_t flags, uint32_t flags2, int guiFocus) {
    GameContextMask newCtx = evaluate(flags, flags2, guiFocus);
    GameContextMask oldCtx = GameContext::None;
    ContextChangeCallback callbackToInvoke = nullptr;
    GameStatusSnapshot snapshotCopy;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        oldCtx = m_activeContexts;
        m_snapshot.flags = flags;
        m_snapshot.flags2 = flags2;
        m_snapshot.guiFocus = guiFocus;
        m_snapshot.valid = (flags != 0 || flags2 != 0);

        if (newCtx == oldCtx) {
            return false;
        }

        m_activeContexts = newCtx;
        m_activeContextNames = formatContexts(newCtx);
        callbackToInvoke = m_changeCallback;
        snapshotCopy = m_snapshot;
    }

    if (callbackToInvoke) {
        callbackToInvoke(oldCtx, newCtx, snapshotCopy);
    }

    return true;
}

GameContextMask ContextEvaluator::getActiveContexts() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeContexts;
}

std::vector<std::string> ContextEvaluator::getActiveContextNames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeContextNames;
}

GameStatusSnapshot ContextEvaluator::getSnapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_snapshot;
}

bool ContextEvaluator::isContextActive(GameContextMask mask) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return (m_activeContexts & mask) != 0;
}

bool ContextEvaluator::isVisible(GameContextMask screenContexts, GameContextMask excludeContexts) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return isScreenVisible(screenContexts, excludeContexts, m_activeContexts);
}

bool ContextEvaluator::isVisible(const std::vector<std::string>& contexts, const std::vector<std::string>& excludeContexts) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return isScreenVisible(contexts, excludeContexts, m_activeContexts);
}

void ContextEvaluator::setContextChangeCallback(ContextChangeCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_changeCallback = std::move(cb);
}

GameContextMask ContextEvaluator::evaluate(uint32_t flags, uint32_t flags2, int guiFocus) {
    if (flags == 0 && flags2 == 0) {
        return GameContext::MainMenu;
    }

    GameContextMask result = GameContext::None;

    // Vehicle classification
    bool shipCockpit = (flags & kFlagInMainShip) != 0;
    bool fighter     = (flags & kFlagInFighter) != 0;
    bool srv         = (flags & kFlagInSRV) != 0;
    bool onFoot      = (flags2 & kFlag2OnFoot) != 0;
    bool taxi        = (flags2 & kFlag2InTaxi) != 0;
    bool multicrew   = (flags2 & kFlag2InMulticrew) != 0;

    if (shipCockpit) result |= GameContext::ShipCockpit;
    if (fighter)     result |= GameContext::Fighter;
    if (srv)         result |= GameContext::Srv;
    if (onFoot)      result |= GameContext::OnFoot;
    if (taxi)        result |= GameContext::Taxi;
    if (multicrew)   result |= GameContext::Multicrew;

    // Flight & physical movement state
    bool docked      = (flags & kFlagDocked) != 0;
    bool landed      = (flags & kFlagLanded) != 0;
    bool supercruise = (flags & kFlagSupercruise) != 0;

    if (docked)      result |= GameContext::Docked;
    if (landed)      result |= GameContext::Landed;
    if (supercruise) result |= GameContext::Supercruise;

    // normal_space: in vehicle && !supercruise && !docked && !landed
    bool inVehicle = shipCockpit || fighter || srv || taxi || multicrew;
    if (inVehicle && !supercruise && !docked && !landed) {
        result |= GameContext::NormalSpace;
    }

    // Weapons / systems
    if (flags & kFlagHardpointsDeployed) {
        result |= GameContext::Hardpoints;
    }

    // HUD mode (Combat vs Analysis)
    bool analysisMode = (flags & kFlagHudInAnalysisMode) != 0;
    if (analysisMode) {
        result |= GameContext::AnalysisMode;
    }

    // combat_mode: in ship && !analysis_mode
    bool inShip = shipCockpit || fighter;
    if (inShip && !analysisMode) {
        result |= GameContext::CombatMode;
    }

    // GuiFocus mappings
    switch (guiFocus) {
        case kGuiFocusStationServices: result |= GameContext::StationServices; break;
        case kGuiFocusGalaxyMap:       result |= GameContext::GalaxyMap; break;
        case kGuiFocusSystemMap:       result |= GameContext::SystemMap; break;
        case kGuiFocusOrrery:          result |= GameContext::Orrery; break;
        case kGuiFocusFSS:             result |= GameContext::Fss; break;
        case kGuiFocusSAA:             result |= GameContext::Saa; break;
        case kGuiFocusCodex:           result |= GameContext::Codex; break;
        default: break;
    }

    return result;
}

bool ContextEvaluator::isScreenVisible(GameContextMask screenContexts,
                                       GameContextMask excludeContexts,
                                       GameContextMask activeContexts) {
    if (screenContexts == 0) {
        screenContexts = GameContext::DefaultScreenContexts;
    }

    // 1. (screen.contexts ∩ active) != empty
    if ((screenContexts & activeContexts) == 0) {
        return false;
    }

    // 2. AND (screen.excludeContexts ∩ active) == empty
    if ((excludeContexts & activeContexts) != 0) {
        return false;
    }

    // 3. AND no member of global exclusion set is active unless the screen lists it explicitly in contexts.
    // If any active global exclusion bit is NOT present in screenContexts, screen is hidden.
    GameContextMask activeGlobalExclusions = activeContexts & GameContext::GlobalExclusionSet;
    if ((activeGlobalExclusions & ~screenContexts) != 0) {
        return false;
    }

    return true;
}

bool ContextEvaluator::isScreenVisible(const std::vector<std::string>& contexts,
                                       const std::vector<std::string>& excludeContexts,
                                       GameContextMask activeContexts) {
    GameContextMask screenMask = contexts.empty() ? GameContext::DefaultScreenContexts : parseContexts(contexts);
    GameContextMask excludeMask = parseContexts(excludeContexts);
    return isScreenVisible(screenMask, excludeMask, activeContexts);
}

const char* ContextEvaluator::contextToString(GameContextBit bit) {
    for (const auto& entry : kContextEntries) {
        if (entry.bit == bit) {
            return entry.name;
        }
    }
    return "";
}

GameContextMask ContextEvaluator::contextFromString(const std::string& name) {
    for (const auto& entry : kContextEntries) {
        if (name == entry.name) {
            return entry.mask;
        }
    }
    return GameContext::None;
}

GameContextMask ContextEvaluator::parseContexts(const std::vector<std::string>& names) {
    GameContextMask mask = GameContext::None;
    for (const auto& name : names) {
        mask |= contextFromString(name);
    }
    return mask;
}

std::vector<std::string> ContextEvaluator::formatContexts(GameContextMask mask) {
    std::vector<std::string> result;
    for (const auto& entry : kContextEntries) {
        if ((mask & entry.mask) != 0) {
            result.push_back(entry.name);
        }
    }
    return result;
}

std::wstring ContextEvaluator::getJournalDir() {
    {
        std::lock_guard<std::mutex> lock(s_overrideMutex);
        if (!s_journalDirOverride.empty()) {
            return s_journalDirOverride;
        }
    }

    wchar_t envBuf[MAX_PATH * 2];
    DWORD len = GetEnvironmentVariableW(L"EDVR_MFD_JOURNAL_DIR", envBuf, _countof(envBuf));
    if (len > 0 && len < _countof(envBuf)) {
        return std::wstring(envBuf);
    }

    wchar_t userProfile[MAX_PATH];
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
        std::wstring p = userProfile;
        p += L"\\Saved Games\\Frontier Developments\\Elite Dangerous";
        return p;
    }

    return L"";
}

void ContextEvaluator::setJournalDirOverride(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(s_overrideMutex);
    s_journalDirOverride = path;
}

std::wstring ContextEvaluator::getStatusJsonPath() {
    std::wstring dir = getJournalDir();
    if (dir.empty()) return L"";
    if (dir.back() == L'\\' || dir.back() == L'/') {
        return dir + L"Status.json";
    }
    return dir + L"\\Status.json";
}

bool ContextEvaluator::readAndParseStatus(const std::wstring& filePath,
                                          uint32_t& outFlags,
                                          uint32_t& outFlags2,
                                          int& outGuiFocus) {
    std::string text;
    constexpr int kMaxAttempts = 3;

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (readTextFileWin32(filePath, text)) {
            if (parseStatusJson(text, outFlags, outFlags2, outGuiFocus)) {
                return true;
            }
        }
        // Write race mitigation: sleep briefly before retry
        if (attempt + 1 < kMaxAttempts) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    return false;
}

bool ContextEvaluator::parseStatusJson(const std::string& jsonText,
                                       uint32_t& outFlags,
                                       uint32_t& outFlags2,
                                       int& outGuiFocus) {
    if (jsonText.empty()) {
        return false;
    }

    try {
        auto j = nlohmann::json::parse(jsonText, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            return false;
        }

        outFlags = j.value("Flags", 0u);
        outFlags2 = j.value("Flags2", 0u);
        outGuiFocus = j.value("GuiFocus", 0);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace edvr::mfd
