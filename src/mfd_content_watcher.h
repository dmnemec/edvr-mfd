#pragma once

#include "mfd_types.h"
#include "mfd_view_model.h"
#include "mfd_display_schema.h"
#include "mfd_package.h"

#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

namespace edvr::mfd {

// Load error report for companion/watcher consumers (§5.2, §6.2)
struct MfdLoadError {
    std::string packageId;
    std::string screenId;
    std::string file;
    int line = 0;
    int column = 0;
    std::string message;
};

// Represents the thread-safe runtime state of a single watched screen (§5)
struct WatchedScreenState {
    std::string packageId;
    std::string screenId;
    std::wstring fullPath;        // Absolute canonical path on disk
    std::string relativePath;     // Relative path within package (e.g. "screens/modules.json")
    std::wstring relativePathW;

    // Published state accessed by render thread:
    std::atomic<uint64_t> generation{0};
    mutable std::mutex modelMutex;
    std::shared_ptr<const MfdViewModel> pendingViewModel;
    std::shared_ptr<const MfdViewModel> lastGoodViewModel;

    // Error state
    bool hasError = false;
    MfdLoadError lastError;
    int parseFailCount = 0;

    // Debounce & polling tracking (watcher thread only):
    FILETIME lastWriteTime{};
    uint64_t lastFileSize = 0;
    uint64_t debounceDeadlineMs = 0;
    bool pendingDebounce = false;
};

class MfdContentWatcher {
public:
    using LoadErrorCallback = std::function<void(const MfdLoadError& error)>;

    explicit MfdContentWatcher(std::wstring mfdsRootDir = L"");
    ~MfdContentWatcher();

    // Non-copyable, non-movable
    MfdContentWatcher(const MfdContentWatcher&) = delete;
    MfdContentWatcher& operator=(const MfdContentWatcher&) = delete;

    // Screen registration (§5.1)
    void registerScreen(const std::string& packageId,
                        const std::string& screenId,
                        const std::wstring& fullFilePath,
                        const std::string& relativePath = "");

    void registerPackage(const MfdPackageManifest& package);
    void registerPackages(const std::vector<MfdPackageManifest>& packages);
    bool unregisterScreen(const std::string& packageId, const std::string& screenId);
    void clearScreens();

    // Lifecycle
    bool start();
    void stop();
    bool isRunning() const { return m_running; }

    // Render-thread consumption (§5.2):
    // Returns true if generation > inOutGeneration, swaps in new model with restored cursor.
    bool swapIfUpdated(const std::string& packageId,
                       const std::string& screenId,
                       uint64_t& inOutGeneration,
                       MfdViewModel& inOutVm);

    // Direct state accessors
    uint64_t getGeneration(const std::string& packageId, const std::string& screenId) const;
    std::shared_ptr<const MfdViewModel> getPendingViewModel(const std::string& packageId, const std::string& screenId) const;
    bool hasError(const std::string& packageId, const std::string& screenId) const;
    bool getLastError(const std::string& packageId, const std::string& screenId, MfdLoadError* outError = nullptr) const;

    // Synchronous reload (useful for initial load & tests)
    bool reloadScreenSync(const std::string& packageId, const std::string& screenId);
    void reloadAllSync();

    // Cursor restoration logic (§5.2):
    // Preserves cursor by (tab.id, item.id). Clamps item if gone; selects tab 0 if tab is gone.
    static void restoreCursor(const MfdViewModel& previousVm, MfdViewModel& newVm);

    // Palette & error callback configuration
    void setPalette(const MfdPaletteSet& palette) { m_palette = palette; }
    void setLoadErrorCallback(LoadErrorCallback cb) { m_loadErrorCallback = std::move(cb); }

    const std::wstring& rootDir() const { return m_mfdsRootDir; }

private:
    void watcherThreadFunc();
    void processFileChange(WatchedScreenState* screen);
    bool readAndParseFile(WatchedScreenState* screen, MfdViewModel& outVm, MfdParseError& outErr);
    WatchedScreenState* findScreen(const std::string& packageId, const std::string& screenId) const;
    WatchedScreenState* findScreenByPath(const std::wstring& path) const;

    std::wstring m_mfdsRootDir;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    HANDLE m_stopEvent = nullptr;

    mutable std::mutex m_screensMutex;
    std::vector<std::unique_ptr<WatchedScreenState>> m_screens;

    MfdPaletteSet m_palette = palette::getTheme(MfdColorTheme::kDefaultAmber);
    LoadErrorCallback m_loadErrorCallback;
};

} // namespace edvr::mfd
