#pragma once

#include "mfd_types.h"
#include "mfd_package.h"
#include "mfd_view_model.h"
#include "mfd_event_log.h"

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

namespace edvr::mfd {

// Process runtime state
enum class CompanionProcessState : uint8_t {
    kNotStarted = 0,
    kRunning,
    kExitedCleanly,
    kCrashed,
    kWaitingRestart,
    kGaveUp,
    kPaused
};

// Information snapshot for an active/managed companion instance
struct CompanionInstanceInfo {
    std::string packageId;
    std::string companionId;
    std::wstring resolvedExe;
    uint32_t pid = 0;
    int exitCode = 0;
    int crashCount = 0;
    int restartAttempt = 0;
    CompanionProcessState state = CompanionProcessState::kNotStarted;
    std::string sha256;
};

// Background thread managing companion executables per §8
class CompanionSupervisor {
public:
    static CompanionSupervisor& instance();

    CompanionSupervisor();
    ~CompanionSupervisor();

    CompanionSupervisor(const CompanionSupervisor&) = delete;
    CompanionSupervisor& operator=(const CompanionSupervisor&) = delete;

    // Lifecycle (§8.1, §8.4)
    bool initialize(const std::wstring& mfdsDir = L"", const std::string& sessionUuid = "");
    void shutdown();
    bool isRunning() const { return m_running.load(); }

    // Package & companion registration (§8.1)
    void registerPackage(const MfdPackageManifest& manifest);
    void registerPackages(const std::vector<MfdPackageManifest>& manifests);
    void unregisterPackage(const std::string& packageId);
    void clearPackages();

    // Runtime toggle (§8.3)
    void setPackageCompanionsEnabled(const std::string& packageId, bool enabled);
    bool isPackageCompanionsEnabled(const std::string& packageId) const;

    // Status queries
    bool isCompanionRunning(const std::string& packageId, const std::string& companionId = "") const;
    bool hasCompanionGaveUp(const std::string& packageId, const std::string& companionId = "") const;
    int getCompanionCrashCount(const std::string& packageId, const std::string& companionId = "") const;
    int getCompanionRestartAttempt(const std::string& packageId, const std::string& companionId = "") const;

    // Status badge overrides (§8.2, §8.3)
    // Returns true if there is an active badge override for this package.
    // - If companion offline (crashed / waiting restart / gave up): "COMPANION OFFLINE" (crit)
    // - If companion disabled/paused by user: "PAUSED" (warn)
    // - If running or no companions: false (no override)
    bool getStatusBadgeOverride(const std::string& packageId,
                                std::string& outBadge,
                                MfdNamedColor& outColor,
                                std::string& outColorName) const;

    // Direct helper to apply badge override to view model
    bool applyStatusBadgeOverride(const std::string& packageId, MfdViewModel& vm) const;

    // Testing and configuration helpers
    void setBackoffSchedule(const std::vector<int>& delaySeconds);
    void setCrashWindowDuration(std::chrono::milliseconds duration);
    void setMaxCrashesInWindow(int maxCrashes);
    void tick(); // Synchronous supervisor cycle

    // Inspection
    HANDLE jobHandle() const { return m_hJob; }
    std::vector<CompanionInstanceInfo> getInstances() const;

private:
    struct ManagedCompanion {
        std::string packageId;
        std::string companionId;
        std::wstring packageDir;
        std::wstring resolvedExe;
        MfdCompanionManifest manifest;

        // Process runtime
        HANDLE hProcess = INVALID_HANDLE_VALUE;
        DWORD pid = 0;
        int exitCode = 0;
        int currentAttempt = 0;
        CompanionProcessState state = CompanionProcessState::kNotStarted;
        std::string sha256;

        // Crash history within sliding window
        std::vector<std::chrono::steady_clock::time_point> crashTimes;

        // Restart timing
        std::chrono::steady_clock::time_point nextRestartTime{};
        bool hasScheduledRestart = false;

        bool isRunning() const {
            return state == CompanionProcessState::kRunning && hProcess != INVALID_HANDLE_VALUE;
        }
    };

    struct ManagedPackage {
        std::string id;
        std::wstring packageDir;
        std::atomic<bool> companionsEnabled{true};
        std::vector<ManagedCompanion> companions;
        bool logTruncated = false;
    };

    void supervisorThreadFunc();
    void processTickLocked();
    bool launchCompanionLocked(ManagedPackage& pkg, ManagedCompanion& comp);
    void terminateCompanionLocked(ManagedCompanion& comp);
    void terminatePackageLocked(ManagedPackage& pkg);

    mutable std::mutex m_mutex;
    std::atomic<bool> m_running{false};
    std::thread m_workerThread;
    HANDLE m_wakeEvent = nullptr;
    HANDLE m_hJob = nullptr;

    std::wstring m_mfdsDir;
    std::string m_sessionUuid;

    // Configurable backoff
    std::vector<int> m_backoffSchedule = {1, 5, 30, 30};
    std::chrono::milliseconds m_crashWindowDuration = std::chrono::minutes(10);
    int m_maxCrashesInWindow = 5;

    std::map<std::string, std::unique_ptr<ManagedPackage>> m_packages;
};

// Global helper to compute SHA-256 of file using CryptoAPI (no CRT stdio)
std::string computeFileSha256(const std::wstring& filePath);

} // namespace edvr::mfd
