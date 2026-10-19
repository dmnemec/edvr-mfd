#include "mfd_content_watcher.h"
#include "log.h"

#include <algorithm>
#include <cwctype>

namespace edvr::mfd {

namespace {

std::wstring normalizePathW(std::wstring p) {
    for (auto& c : p) {
        if (c == L'/') c = L'\\';
        else c = static_cast<wchar_t>(towlower(c));
    }
    while (p.rfind(L".\\", 0) == 0) {
        p = p.substr(2);
    }
    while (!p.empty() && p.front() == L'\\') {
        p = p.substr(1);
    }
    return p;
}

std::string wideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return {};
    std::string result(sizeNeeded, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), sizeNeeded, nullptr, nullptr);
    return result;
}

std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (sizeNeeded <= 0) return {};
    std::wstring result(sizeNeeded, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded);
    return result;
}

} // anonymous namespace

MfdContentWatcher::MfdContentWatcher(std::wstring mfdsRootDir)
    : m_mfdsRootDir(std::move(mfdsRootDir)) {
    if (m_mfdsRootDir.empty()) {
        m_mfdsRootDir = getDefaultMfdsDirectory();
    }
    // Normalize root dir
    for (auto& c : m_mfdsRootDir) {
        if (c == L'/') c = L'\\';
    }
    while (m_mfdsRootDir.size() > 1 && m_mfdsRootDir.back() == L'\\') {
        m_mfdsRootDir.pop_back();
    }
}

MfdContentWatcher::~MfdContentWatcher() {
    stop();
}

void MfdContentWatcher::registerScreen(const std::string& packageId,
                                      const std::string& screenId,
                                      const std::wstring& fullFilePath,
                                      const std::string& relativePath) {
    std::lock_guard<std::mutex> lock(m_screensMutex);

    // If already registered, update it
    for (auto& s : m_screens) {
        if (s->packageId == packageId && s->screenId == screenId) {
            s->fullPath = fullFilePath;
            s->relativePath = relativePath.empty() ? wideToUtf8(fullFilePath) : relativePath;
            s->relativePathW = utf8ToWide(s->relativePath);
            return;
        }
    }

    auto s = std::make_unique<WatchedScreenState>();
    s->packageId = packageId;
    s->screenId = screenId;
    s->fullPath = fullFilePath;
    s->relativePath = relativePath.empty() ? wideToUtf8(fullFilePath) : relativePath;
    s->relativePathW = utf8ToWide(s->relativePath);

    // Query initial file attributes
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(s->fullPath.c_str(), GetFileExInfoStandard, &fad)) {
        s->lastWriteTime = fad.ftLastWriteTime;
        s->lastFileSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    }

    m_screens.push_back(std::move(s));
}

void MfdContentWatcher::registerPackage(const MfdPackageManifest& package) {
    for (const auto& screen : package.screens) {
        std::wstring screenFileWide = utf8ToWide(screen.file);
        std::wstring fullPath;
        if (!package.packageDir.empty()) {
            fullPath = package.packageDir + L"\\" + screenFileWide;
        } else {
            fullPath = m_mfdsRootDir + L"\\" + utf8ToWide(package.id) + L"\\" + screenFileWide;
        }
        registerScreen(package.id, screen.id, fullPath, screen.file);
    }
}

void MfdContentWatcher::registerPackages(const std::vector<MfdPackageManifest>& packages) {
    for (const auto& pkg : packages) {
        registerPackage(pkg);
    }
}

bool MfdContentWatcher::unregisterScreen(const std::string& packageId, const std::string& screenId) {
    std::lock_guard<std::mutex> lock(m_screensMutex);
    auto it = std::remove_if(m_screens.begin(), m_screens.end(), [&](const std::unique_ptr<WatchedScreenState>& s) {
        return s->packageId == packageId && s->screenId == screenId;
    });
    if (it != m_screens.end()) {
        m_screens.erase(it, m_screens.end());
        return true;
    }
    return false;
}

void MfdContentWatcher::clearScreens() {
    std::lock_guard<std::mutex> lock(m_screensMutex);
    m_screens.clear();
}

WatchedScreenState* MfdContentWatcher::findScreen(const std::string& packageId, const std::string& screenId) const {
    for (const auto& s : m_screens) {
        if (s->packageId == packageId && s->screenId == screenId) {
            return s.get();
        }
    }
    return nullptr;
}

WatchedScreenState* MfdContentWatcher::findScreenByPath(const std::wstring& path) const {
    std::wstring normInput = normalizePathW(path);
    for (const auto& s : m_screens) {
        std::wstring normFull = normalizePathW(s->fullPath);
        std::wstring normRel = normalizePathW(s->relativePathW);
        std::wstring normPkgRel = normalizePathW(utf8ToWide(s->packageId) + L"\\" + s->relativePathW);

        if (normInput == normFull || normInput == normRel || normInput == normPkgRel) {
            return s.get();
        }
        // Match if one ends with the relative path
        if (normInput.size() >= normPkgRel.size() &&
            normInput.compare(normInput.size() - normPkgRel.size(), normPkgRel.size(), normPkgRel) == 0) {
            return s.get();
        }
        if (normInput.size() >= normRel.size() &&
            normInput.compare(normInput.size() - normRel.size(), normRel.size(), normRel) == 0) {
            return s.get();
        }
    }
    return nullptr;
}

bool MfdContentWatcher::start() {
    if (m_running.exchange(true)) {
        return true; // Already running
    }

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent) {
        m_running = false;
        return false;
    }

    m_thread = std::thread(&MfdContentWatcher::watcherThreadFunc, this);
    return true;
}

void MfdContentWatcher::stop() {
    if (!m_running.exchange(false)) {
        return;
    }

    if (m_stopEvent) {
        SetEvent(m_stopEvent);
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
}

bool MfdContentWatcher::readAndParseFile(WatchedScreenState* screen, MfdViewModel& outVm, MfdParseError& outErr) {
    if (!screen) return false;

    // Retry up to 3 times at 10 ms on sharing violation (§5.1)
    HANDLE hFile = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 4; ++attempt) {
        hFile = CreateFileW(
            screen->fullPath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (hFile != INVALID_HANDLE_VALUE) {
            break;
        }
        DWORD err = GetLastError();
        if ((err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION) && attempt < 3) {
            Sleep(10);
            continue;
        }
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            break;
        }
        if (attempt < 3) {
            Sleep(10);
        }
    }

    if (hFile == INVALID_HANDLE_VALUE) {
        outErr.line = 0;
        outErr.column = 0;
        outErr.message = "Failed to open file (sharing violation or file not found)";
        return false;
    }

    LARGE_INTEGER liSize;
    if (!GetFileSizeEx(hFile, &liSize)) {
        CloseHandle(hFile);
        outErr.message = "Failed to get file size";
        return false;
    }

    if (liSize.QuadPart < 0 || liSize.QuadPart > 1024 * 1024) {
        CloseHandle(hFile);
        outErr.message = "File exceeds 1 MB limit";
        return false;
    }

    DWORD fileSize = static_cast<DWORD>(liSize.QuadPart);
    std::string buffer(fileSize, '\0');
    DWORD bytesRead = 0;
    if (fileSize > 0) {
        if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, nullptr) || bytesRead != fileSize) {
            CloseHandle(hFile);
            outErr.message = "ReadFile failed or returned partial buffer";
            return false;
        }
    }
    CloseHandle(hFile);

    return parseDisplayJson(buffer.data(), buffer.size(), outVm, &outErr, &m_palette);
}

void MfdContentWatcher::processFileChange(WatchedScreenState* screen) {
    if (!screen) return;

    MfdViewModel newVm;
    MfdParseError parseErr;
    bool success = readAndParseFile(screen, newVm, parseErr);

    if (success) {
        screen->parseFailCount = 0;
        screen->hasError = false;

        auto modelPtr = std::make_shared<const MfdViewModel>(std::move(newVm));
        screen->lastGoodViewModel = modelPtr;

        {
            std::lock_guard<std::mutex> lock(screen->modelMutex);
            screen->pendingViewModel = modelPtr;
        }
        screen->generation.fetch_add(1, std::memory_order_release);
        Log::get().note("mfd_watcher: [%s/%s] Reloaded view model (generation %llu)\n",
                        screen->packageId.c_str(), screen->screenId.c_str(),
                        screen->generation.load());
    } else {
        screen->parseFailCount++;

        // Non-atomic write tolerance (§5.1): retry once before raising load_error
        if (screen->parseFailCount < 2) {
            screen->pendingDebounce = true;
            screen->debounceDeadlineMs = GetTickCount64() + 15;
            return;
        }

        screen->hasError = true;
        MfdLoadError err;
        err.packageId = screen->packageId;
        err.screenId = screen->screenId;
        err.file = screen->relativePath;
        err.line = parseErr.line;
        err.column = parseErr.column;
        err.message = parseErr.message;
        screen->lastError = err;

        Log::get().note("mfd_watcher: [%s/%s] Load error in %s:%d:%d: %s\n",
                        screen->packageId.c_str(), screen->screenId.c_str(),
                        err.file.c_str(), err.line, err.column, err.message.c_str());

        if (m_loadErrorCallback) {
            try {
                m_loadErrorCallback(err);
            } catch (...) {}
        }

        // On failure: keep last good model, set errorBadge = "ERR" (§5.2)
        MfdViewModel errorVm;
        if (screen->lastGoodViewModel) {
            errorVm = *screen->lastGoodViewModel;
        } else {
            errorVm.title = screen->screenId;
        }
        errorVm.statusBadge = "ERR";
        errorVm.statusBadgeNamedColor = MfdNamedColor::kCrit;
        errorVm.statusBadgeColorName = "crit";
        errorVm.statusBadgeColor = palette::kAlertRed;

        auto errorPtr = std::make_shared<const MfdViewModel>(std::move(errorVm));
        {
            std::lock_guard<std::mutex> lock(screen->modelMutex);
            screen->pendingViewModel = errorPtr;
        }
        screen->generation.fetch_add(1, std::memory_order_release);
    }
}

bool MfdContentWatcher::reloadScreenSync(const std::string& packageId, const std::string& screenId) {
    std::lock_guard<std::mutex> lock(m_screensMutex);
    auto* s = findScreen(packageId, screenId);
    if (!s) return false;
    processFileChange(s);
    return !s->hasError;
}

void MfdContentWatcher::reloadAllSync() {
    std::lock_guard<std::mutex> lock(m_screensMutex);
    for (auto& s : m_screens) {
        processFileChange(s.get());
    }
}

void MfdContentWatcher::restoreCursor(const MfdViewModel& previousVm, MfdViewModel& newVm) {
    if (newVm.tabs.empty()) {
        newVm.activeTabIndex = 0;
        return;
    }

    newVm.isFocused = previousVm.isFocused;
    newVm.renderHeight = previousVm.renderHeight;

    if (previousVm.tabs.empty()) {
        newVm.activeTabIndex = 0;
        return;
    }

    int prevActiveTabIdx = previousVm.activeTabIndex;
    if (prevActiveTabIdx < 0 || prevActiveTabIdx >= static_cast<int>(previousVm.tabs.size())) {
        newVm.activeTabIndex = 0;
        return;
    }

    const auto& prevTab = previousVm.tabs[prevActiveTabIdx];
    std::string targetTabId = prevTab.id;

    // 1. Locate tab in new model by tab.id (§5.2)
    int targetTabIdx = -1;
    for (size_t i = 0; i < newVm.tabs.size(); ++i) {
        if (newVm.tabs[i].id == targetTabId) {
            targetTabIdx = static_cast<int>(i);
            break;
        }
    }

    if (targetTabIdx < 0) {
        // Tab is gone: select tab 0 (§5.2)
        newVm.activeTabIndex = 0;
        return;
    }

    newVm.activeTabIndex = targetTabIdx;
    auto& newTab = newVm.tabs[targetTabIdx];

    // 2. Restore item cursor within tab (§5.2)
    if (newTab.type == MfdTabType::kList) {
        if (newTab.items.empty()) {
            newTab.selectedIndex = 0;
            newTab.scrollOffset = 0;
        } else {
            std::string targetItemId;
            if (prevTab.selectedIndex >= 0 && prevTab.selectedIndex < static_cast<int>(prevTab.items.size())) {
                targetItemId = prevTab.items[prevTab.selectedIndex].id;
            }

            int foundItemIdx = -1;
            if (!targetItemId.empty()) {
                for (size_t j = 0; j < newTab.items.size(); ++j) {
                    if (newTab.items[j].id == targetItemId) {
                        foundItemIdx = static_cast<int>(j);
                        break;
                    }
                }
            }

            if (foundItemIdx >= 0) {
                newTab.selectedIndex = foundItemIdx;
            } else {
                // Item is gone, clamp index (§5.2)
                newTab.selectedIndex = (std::clamp)(prevTab.selectedIndex, 0, static_cast<int>(newTab.items.size()) - 1);
            }

            // Keep selection visible
            int visibleRows = newTab.visibleRows(newVm.renderHeight);
            int maxScroll = (std::max)(0, static_cast<int>(newTab.items.size()) - visibleRows);
            newTab.scrollOffset = (std::clamp)(prevTab.scrollOffset, 0, maxScroll);
            newTab.keepSelectionVisible(visibleRows);
        }
    } else if (newTab.type == MfdTabType::kKeyValue) {
        if (newTab.keyValues.empty()) {
            newTab.selectedIndex = 0;
            newTab.scrollOffset = 0;
        } else {
            std::string targetItemId;
            if (prevTab.selectedIndex >= 0 && prevTab.selectedIndex < static_cast<int>(prevTab.keyValues.size())) {
                targetItemId = prevTab.keyValues[prevTab.selectedIndex].id;
            }

            int foundItemIdx = -1;
            if (!targetItemId.empty()) {
                for (size_t j = 0; j < newTab.keyValues.size(); ++j) {
                    if (newTab.keyValues[j].id == targetItemId) {
                        foundItemIdx = static_cast<int>(j);
                        break;
                    }
                }
            }

            if (foundItemIdx >= 0) {
                newTab.selectedIndex = foundItemIdx;
            } else {
                newTab.selectedIndex = (std::clamp)(prevTab.selectedIndex, 0, static_cast<int>(newTab.keyValues.size()) - 1);
            }

            int visibleRows = newTab.visibleRows(newVm.renderHeight);
            int maxScroll = (std::max)(0, static_cast<int>(newTab.keyValues.size()) - visibleRows);
            newTab.scrollOffset = (std::clamp)(prevTab.scrollOffset, 0, maxScroll);
            newTab.keepSelectionVisible(visibleRows);
        }
    } else if (newTab.type == MfdTabType::kBars) {
        if (newTab.barItems.empty()) {
            newTab.selectedIndex = 0;
            newTab.scrollOffset = 0;
        } else {
            std::string targetItemId;
            if (prevTab.selectedIndex >= 0 && prevTab.selectedIndex < static_cast<int>(prevTab.barItems.size())) {
                targetItemId = prevTab.barItems[prevTab.selectedIndex].id;
            }

            int foundItemIdx = -1;
            if (!targetItemId.empty()) {
                for (size_t j = 0; j < newTab.barItems.size(); ++j) {
                    if (newTab.barItems[j].id == targetItemId) {
                        foundItemIdx = static_cast<int>(j);
                        break;
                    }
                }
            }

            if (foundItemIdx >= 0) {
                newTab.selectedIndex = foundItemIdx;
            } else {
                newTab.selectedIndex = (std::clamp)(prevTab.selectedIndex, 0, static_cast<int>(newTab.barItems.size()) - 1);
            }

            int visibleRows = newTab.visibleRows(newVm.renderHeight);
            int maxScroll = (std::max)(0, static_cast<int>(newTab.barItems.size()) - visibleRows);
            newTab.scrollOffset = (std::clamp)(prevTab.scrollOffset, 0, maxScroll);
            newTab.keepSelectionVisible(visibleRows);
        }
    } else if (newTab.type == MfdTabType::kText) {
        int maxScroll = (std::max)(0, static_cast<int>(newTab.lines.size()) - 1);
        newTab.textScrollLine = (std::clamp)(prevTab.textScrollLine, 0, maxScroll);
    }
}

bool MfdContentWatcher::swapIfUpdated(const std::string& packageId,
                                      const std::string& screenId,
                                      uint64_t& inOutGeneration,
                                      MfdViewModel& inOutVm) {
    WatchedScreenState* s = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_screensMutex);
        s = findScreen(packageId, screenId);
    }
    if (!s) return false;

    uint64_t curGen = s->generation.load(std::memory_order_acquire);
    if (curGen <= inOutGeneration) {
        return false;
    }

    std::shared_ptr<const MfdViewModel> pending;
    {
        std::lock_guard<std::mutex> lock(s->modelMutex);
        pending = s->pendingViewModel;
    }

    if (!pending) return false;

    MfdViewModel updated = *pending;
    restoreCursor(inOutVm, updated);
    inOutVm = std::move(updated);
    inOutGeneration = curGen;
    return true;
}

uint64_t MfdContentWatcher::getGeneration(const std::string& packageId, const std::string& screenId) const {
    std::lock_guard<std::mutex> lock(m_screensMutex);
    auto* s = findScreen(packageId, screenId);
    return s ? s->generation.load(std::memory_order_acquire) : 0;
}

std::shared_ptr<const MfdViewModel> MfdContentWatcher::getPendingViewModel(const std::string& packageId, const std::string& screenId) const {
    std::lock_guard<std::mutex> lock(m_screensMutex);
    auto* s = findScreen(packageId, screenId);
    if (!s) return nullptr;
    std::lock_guard<std::mutex> modelLock(s->modelMutex);
    return s->pendingViewModel;
}

bool MfdContentWatcher::hasError(const std::string& packageId, const std::string& screenId) const {
    std::lock_guard<std::mutex> lock(m_screensMutex);
    auto* s = findScreen(packageId, screenId);
    return s ? s->hasError : false;
}

bool MfdContentWatcher::getLastError(const std::string& packageId, const std::string& screenId, MfdLoadError* outError) const {
    std::lock_guard<std::mutex> lock(m_screensMutex);
    auto* s = findScreen(packageId, screenId);
    if (s && s->hasError) {
        if (outError) *outError = s->lastError;
        return true;
    }
    return false;
}

void MfdContentWatcher::watcherThreadFunc() {
    HANDLE hDir = INVALID_HANDLE_VALUE;
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
        return;
    }

    alignas(DWORD) BYTE buffer[64 * 1024];
    bool readPending = false;
    uint64_t lastFallbackPollMs = GetTickCount64();

    const DWORD notifyFilter = FILE_NOTIFY_CHANGE_LAST_WRITE |
                               FILE_NOTIFY_CHANGE_FILE_NAME |
                               FILE_NOTIFY_CHANGE_CREATION;

    auto tryOpenDirectory = [&]() {
        if (hDir != INVALID_HANDLE_VALUE) return true;
        hDir = CreateFileW(
            m_mfdsRootDir.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr
        );
        return (hDir != INVALID_HANDLE_VALUE);
    };

    tryOpenDirectory();

    while (m_running.load(std::memory_order_relaxed)) {
        uint64_t now = GetTickCount64();

        // 1. Issue directory watch if directory opened and not currently pending
        if (hDir == INVALID_HANDLE_VALUE) {
            tryOpenDirectory();
        }

        if (hDir != INVALID_HANDLE_VALUE && !readPending) {
            ResetEvent(ov.hEvent);
            DWORD bytesReturned = 0;
            BOOL ok = ReadDirectoryChangesW(
                hDir,
                buffer,
                sizeof(buffer),
                TRUE, // recursive (§5.1)
                notifyFilter,
                &bytesReturned,
                &ov,
                nullptr
            );

            if (ok || GetLastError() == ERROR_IO_PENDING) {
                readPending = true;
            } else {
                // Directory handle likely invalid; close and retry later
                CloseHandle(hDir);
                hDir = INVALID_HANDLE_VALUE;
            }
        }

        // 2. Compute wait timeout based on nearest debounce deadline or 500ms fallback poll
        DWORD waitTimeoutMs = 500;
        {
            std::lock_guard<std::mutex> lock(m_screensMutex);
            for (const auto& s : m_screens) {
                if (s->pendingDebounce) {
                    if (s->debounceDeadlineMs <= now) {
                        waitTimeoutMs = 0;
                        break;
                    } else {
                        DWORD diff = static_cast<DWORD>(s->debounceDeadlineMs - now);
                        waitTimeoutMs = (std::min)(waitTimeoutMs, diff);
                    }
                }
            }
        }

        // Clamp wait timeout to interval before next fallback poll
        if (now - lastFallbackPollMs < 500) {
            DWORD pollDiff = static_cast<DWORD>(500 - (now - lastFallbackPollMs));
            waitTimeoutMs = (std::min)(waitTimeoutMs, pollDiff);
        } else {
            waitTimeoutMs = 0;
        }

        HANDLE waitHandles[2] = { m_stopEvent, ov.hEvent };
        DWORD waitCount = (readPending && ov.hEvent) ? 2 : 1;

        DWORD waitRes = WaitForMultipleObjects(waitCount, waitHandles, FALSE, waitTimeoutMs);

        if (waitRes == WAIT_OBJECT_0) {
            // Stop requested
            break;
        }

        now = GetTickCount64();

        if (waitRes == WAIT_OBJECT_0 + 1) {
            // Directory change notification fired
            DWORD bytesTransferred = 0;
            if (GetOverlappedResult(hDir, &ov, &bytesTransferred, FALSE) && bytesTransferred > 0) {
                PFILE_NOTIFY_INFORMATION info = reinterpret_cast<PFILE_NOTIFY_INFORMATION>(buffer);
                while (info) {
                    std::wstring relName(info->FileName, info->FileNameLength / sizeof(wchar_t));

                    std::lock_guard<std::mutex> lock(m_screensMutex);
                    auto* s = findScreenByPath(relName);
                    if (s) {
                        // Debounce 30 ms per file (§5.1)
                        s->pendingDebounce = true;
                        s->debounceDeadlineMs = now + 30;
                    }

                    if (info->NextEntryOffset == 0) break;
                    info = reinterpret_cast<PFILE_NOTIFY_INFORMATION>(reinterpret_cast<BYTE*>(info) + info->NextEntryOffset);
                }
            }
            readPending = false;
        }

        // 3. Process expired debounce deadlines
        now = GetTickCount64();
        {
            std::lock_guard<std::mutex> lock(m_screensMutex);
            for (auto& s : m_screens) {
                if (s->pendingDebounce && now >= s->debounceDeadlineMs) {
                    s->pendingDebounce = false;

                    // Update tracked file attributes
                    WIN32_FILE_ATTRIBUTE_DATA fad{};
                    if (GetFileAttributesExW(s->fullPath.c_str(), GetFileExInfoStandard, &fad)) {
                        s->lastWriteTime = fad.ftLastWriteTime;
                        s->lastFileSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                    }

                    processFileChange(s.get());
                }
            }
        }

        // 4. Fallback poll every 500 ms checking (mtime, size) (§5.1)
        now = GetTickCount64();
        if (now - lastFallbackPollMs >= 500) {
            lastFallbackPollMs = now;
            std::lock_guard<std::mutex> lock(m_screensMutex);
            for (auto& s : m_screens) {
                WIN32_FILE_ATTRIBUTE_DATA fad{};
                if (GetFileAttributesExW(s->fullPath.c_str(), GetFileExInfoStandard, &fad)) {
                    uint64_t size = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                    if (CompareFileTime(&fad.ftLastWriteTime, &s->lastWriteTime) != 0 || size != s->lastFileSize) {
                        s->lastWriteTime = fad.ftLastWriteTime;
                        s->lastFileSize = size;
                        if (!s->pendingDebounce) {
                            s->pendingDebounce = true;
                            s->debounceDeadlineMs = now + 30;
                        }
                    }
                }
            }
        }
    }

    if (hDir != INVALID_HANDLE_VALUE) {
        if (readPending) {
            CancelIoEx(hDir, &ov);
        }
        CloseHandle(hDir);
        hDir = INVALID_HANDLE_VALUE;
    }
    if (ov.hEvent) {
        CloseHandle(ov.hEvent);
    }
}

} // namespace edvr::mfd
