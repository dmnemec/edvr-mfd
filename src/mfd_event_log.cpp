#include "mfd_event_log.h"
#include "log.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <objbase.h>

namespace edvr::mfd {

namespace {

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

} // namespace

std::string generateUuidV4() {
    GUID guid{};
    HRESULT hr = CoCreateGuid(&guid);
    if (FAILED(hr)) {
        return "00000000-0000-4000-8000-000000000000";
    }

    // Set version 4: bits 12-15 of time_hi_and_version set to 0100 (0x4000)
    guid.Data3 = (guid.Data3 & 0x0fff) | 0x4000;
    // Set variant: bits 6-7 of clock_seq_hi_and_reserved set to 10 (0x80)
    guid.Data4[0] = (guid.Data4[0] & 0x3f) | 0x80;

    char buf[37];
    snprintf(buf, sizeof(buf),
        "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return std::string(buf);
}

std::string getCurrentIsoUtcTimestamp() {
    SYSTEMTIME st{};
    GetSystemTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return std::string(buf);
}

std::string serializeEventToJson(const MfdEventRecord& record) {
    nlohmann::ordered_json j;
    j["seq"] = record.seq;
    j["ts"] = record.ts;
    j["session"] = record.session;
    j["screen"] = record.screen;
    j["type"] = record.type;
    j["tab"] = record.tab;
    j["index"] = record.index;
    j["itemId"] = record.itemId;
    j["label"] = record.label;

    if (record.hasKey) {
        j["key"] = record.key;
    } else {
        j["key"] = nullptr;
    }

    if (record.hasDelta) {
        j["delta"] = record.delta;
    } else {
        j["delta"] = nullptr;
    }

    // Optional / type-specific extra fields
    if (record.type == "load_error" || !record.file.empty() || record.line > 0 || record.column > 0 || !record.message.empty()) {
        j["file"] = record.file;
        j["line"] = record.line;
        j["column"] = record.column;
        j["message"] = record.message;
    }

    if (record.type == "context_changed" || record.hasContextInfo) {
        j["contexts"] = record.contexts;
        j["flags"] = record.flags;
        j["flags2"] = record.flags2;
        j["guiFocus"] = record.guiFocus;
    }

    if (record.type == "select" || !record.action.empty()) {
        if (!record.action.empty()) {
            j["action"] = record.action;
        }
    }

    if (record.type == "events_dropped" || record.dropCount > 0) {
        j["count"] = record.dropCount;
    }

    if (record.type.rfind("companion_", 0) == 0 || !record.companion.empty()) {
        j["companion"] = record.companion;
        j["pid"] = record.pid;
        j["exitCode"] = record.exitCode;
        j["attempt"] = record.attempt;
    }

    return j.dump();
}

bool parseEventFromJson(const std::string& jsonLine, MfdEventRecord& outRecord, std::string* outError) {
    try {
        auto j = nlohmann::json::parse(jsonLine);
        if (!j.is_object()) {
            if (outError) *outError = "Root JSON element must be an object";
            return false;
        }

        outRecord = MfdEventRecord{};
        outRecord.seq = j.value("seq", uint64_t(0));
        outRecord.ts = j.value("ts", "");
        outRecord.session = j.value("session", "");
        outRecord.screen = j.value("screen", "");
        outRecord.type = j.value("type", "");
        outRecord.tab = j.value("tab", "");
        outRecord.index = j.value("index", 0);
        outRecord.itemId = j.value("itemId", "");
        outRecord.label = j.value("label", "");

        if (j.contains("key") && !j["key"].is_null()) {
            outRecord.hasKey = true;
            outRecord.key = j["key"].get<std::string>();
        } else {
            outRecord.hasKey = false;
            outRecord.key.clear();
        }

        if (j.contains("delta") && !j["delta"].is_null()) {
            outRecord.hasDelta = true;
            outRecord.delta = j["delta"].get<int>();
        } else {
            outRecord.hasDelta = false;
            outRecord.delta = 0;
        }

        if (j.contains("action") && !j["action"].is_null()) {
            outRecord.action = j["action"].get<std::string>();
        }
        if (j.contains("file")) outRecord.file = j["file"].get<std::string>();
        if (j.contains("line")) outRecord.line = j["line"].get<int>();
        if (j.contains("column")) outRecord.column = j["column"].get<int>();
        if (j.contains("message")) outRecord.message = j["message"].get<std::string>();

        if (j.contains("contexts")) {
            outRecord.hasContextInfo = true;
            outRecord.contexts = j["contexts"].get<std::vector<std::string>>();
        }
        if (j.contains("flags")) {
            outRecord.hasContextInfo = true;
            outRecord.flags = j["flags"].get<uint32_t>();
        }
        if (j.contains("flags2")) {
            outRecord.hasContextInfo = true;
            outRecord.flags2 = j["flags2"].get<uint32_t>();
        }
        if (j.contains("guiFocus")) {
            outRecord.hasContextInfo = true;
            outRecord.guiFocus = j["guiFocus"].get<int>();
        }

        if (j.contains("companion")) outRecord.companion = j["companion"].get<std::string>();
        if (j.contains("pid")) outRecord.pid = j["pid"].get<uint32_t>();
        if (j.contains("exitCode")) outRecord.exitCode = j["exitCode"].get<int>();
        if (j.contains("attempt")) outRecord.attempt = j["attempt"].get<int>();

        if (j.contains("count")) outRecord.dropCount = j["count"].get<uint32_t>();

        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

// Factory methods
MfdEventRecord MfdEventRecord::createPluginStart(const std::string& packageId, const std::string& screen) {
    MfdEventRecord r;
    r.packageId = packageId;
    r.screen = screen;
    r.type = "plugin_start";
    return r;
}

MfdEventRecord MfdEventRecord::createScreenLoaded(const std::string& packageId, const std::string& screen) {
    MfdEventRecord r;
    r.packageId = packageId;
    r.screen = screen;
    r.type = "screen_loaded";
    return r;
}

MfdEventRecord MfdEventRecord::createLoadError(const std::string& packageId, const std::string& screen,
                                              const std::string& file, int line, int column, const std::string& message) {
    MfdEventRecord r;
    r.packageId = packageId;
    r.screen = screen;
    r.type = "load_error";
    r.file = file;
    r.line = line;
    r.column = column;
    r.message = message;
    return r;
}

MfdEventRecord MfdEventRecord::createContextChanged(const std::string& packageId,
                                                   const std::vector<std::string>& contexts,
                                                   uint32_t flags, uint32_t flags2, int guiFocus) {
    MfdEventRecord r;
    r.packageId = packageId;
    r.type = "context_changed";
    r.hasContextInfo = true;
    r.contexts = contexts;
    r.flags = flags;
    r.flags2 = flags2;
    r.guiFocus = guiFocus;
    return r;
}

MfdEventRecord MfdEventRecord::createFocusGained(const std::string& packageId, const std::string& screen) {
    MfdEventRecord r;
    r.packageId = packageId;
    r.screen = screen;
    r.type = "focus_gained";
    return r;
}

MfdEventRecord MfdEventRecord::createFocusLost(const std::string& packageId, const std::string& screen) {
    MfdEventRecord r;
    r.packageId = packageId;
    r.screen = screen;
    r.type = "focus_lost";
    return r;
}

MfdEventRecord MfdEventRecord::createTabChange(const std::string& packageId, const std::string& screen,
                                              const std::string& tab, int index) {
    MfdEventRecord r;
    r.packageId = packageId;
    r.screen = screen;
    r.type = "tab_change";
    r.tab = tab;
    r.index = index;
    return r;
}

MfdEventRecord MfdEventRecord::createNav(const std::string& packageId, const std::string& screen,
                                        const std::string& tab, int index,
                                        const std::string& itemId, const std::string& label) {
    MfdEventRecord r;
    r.packageId = packageId;
    r.screen = screen;
    r.type = "nav";
    r.tab = tab;
    r.index = index;
    r.itemId = itemId;
    r.label = label;
    return r;
}

MfdEventRecord MfdEventRecord::createSelect(const std::string& packageId, const std::string& screen,
                                           const std::string& tab, int index,
                                           const std::string& itemId, const std::string& label,
                                           const std::string& action) {
    MfdEventRecord r;
    r.packageId = packageId;
    r.screen = screen;
    r.type = "select";
    r.tab = tab;
    r.index = index;
    r.itemId = itemId;
    r.label = label;
    r.action = action;
    return r;
}

MfdEventRecord MfdEventRecord::createAdjust(const std::string& packageId, const std::string& screen,
                                           const std::string& tab, int index,
                                           const std::string& itemId, const std::string& label,
                                           const std::string& key, int delta) {
    MfdEventRecord r;
    r.packageId = packageId;
    r.screen = screen;
    r.type = "adjust";
    r.tab = tab;
    r.index = index;
    r.itemId = itemId;
    r.label = label;
    r.hasKey = true;
    r.key = key;
    r.hasDelta = true;
    r.delta = delta;
    return r;
}

MfdEventRecord MfdEventRecord::createCompanionStarted(const std::string& packageId, const std::string& companion,
                                                     uint32_t pid, int attempt) {
    MfdEventRecord r;
    r.packageId = packageId;
    r.type = "companion_started";
    r.companion = companion;
    r.pid = pid;
    r.attempt = attempt;
    return r;
}

MfdEventRecord MfdEventRecord::createCompanionCrashed(const std::string& packageId, const std::string& companion,
                                                     uint32_t pid, int exitCode, int attempt) {
    MfdEventRecord r;
    r.packageId = packageId;
    r.type = "companion_crashed";
    r.companion = companion;
    r.pid = pid;
    r.exitCode = exitCode;
    r.attempt = attempt;
    return r;
}

MfdEventRecord MfdEventRecord::createCompanionGaveUp(const std::string& packageId, const std::string& companion,
                                                    uint32_t pid, int exitCode, int attempt) {
    MfdEventRecord r;
    r.packageId = packageId;
    r.type = "companion_gave_up";
    r.companion = companion;
    r.pid = pid;
    r.exitCode = exitCode;
    r.attempt = attempt;
    return r;
}

MfdEventRecord MfdEventRecord::createEventsDropped(const std::string& packageId, uint32_t dropCount) {
    MfdEventRecord r;
    r.packageId = packageId;
    r.type = "events_dropped";
    r.dropCount = dropCount;
    return r;
}

// EventWriter implementation
EventWriter& EventWriter::instance() {
    static EventWriter s_instance;
    return s_instance;
}

EventWriter::EventWriter(size_t queueCapacity, size_t maxFileSize)
    : m_maxQueueCapacity(queueCapacity), m_maxFileSize(maxFileSize) {}

EventWriter::~EventWriter() {
    shutdown();
}

void EventWriter::setQueueCapacity(size_t capacity) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_maxQueueCapacity = capacity > 0 ? capacity : 1;
}

void EventWriter::setMaxFileSize(size_t maxBytes) {
    std::lock_guard<std::mutex> lock(m_fileMutex);
    m_maxFileSize = maxBytes > 0 ? maxBytes : 1;
}

size_t EventWriter::queueSize() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_queue.size();
}

uint64_t EventWriter::getNextSeq(const std::string& packageId) const {
    std::lock_guard<std::mutex> lock(m_fileMutex);
    auto it = m_packageStates.find(packageId);
    if (it != m_packageStates.end()) {
        return it->second.nextSeq;
    }
    return 1;
}

std::wstring EventWriter::getPackageEventsPath(const std::string& packageId) const {
    return m_mfdsDir + L"\\" + utf8ToWide(packageId) + L"\\events.jsonl";
}

std::wstring EventWriter::getPackageRolledPath(const std::string& packageId) const {
    return m_mfdsDir + L"\\" + utf8ToWide(packageId) + L"\\events.1.jsonl";
}

bool EventWriter::initialize(const std::wstring& mfdsDir, const std::string& sessionUuid) {
    shutdown();

    m_mfdsDir = mfdsDir;
    m_sessionUuid = sessionUuid.empty() ? generateUuidV4() : sessionUuid;
    m_totalDroppedCount = 0;

    CreateDirectoryW(m_mfdsDir.c_str(), nullptr);

    // Truncate any existing events.jsonl files in package subdirectories for this new session
    WIN32_FIND_DATAW findData{};
    std::wstring searchPattern = m_mfdsDir + L"\\*";
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                wcscmp(findData.cFileName, L".") != 0 &&
                wcscmp(findData.cFileName, L"..") != 0) {
                std::string pkg = wideToUtf8(findData.cFileName);
                std::wstring eventsPath = getPackageEventsPath(pkg);

                // Open with CREATE_ALWAYS to truncate to 0 bytes
                HANDLE hFile = CreateFileW(
                    eventsPath.c_str(),
                    GENERIC_WRITE | GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr,
                    CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr
                );
                if (hFile != INVALID_HANDLE_VALUE) {
                    PackageState state;
                    state.hFile = hFile;
                    state.nextSeq = 1;
                    state.initialized = true;
                    m_packageStates[pkg] = state;
                }
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }

    m_running = true;
    m_workerThread = std::thread(&EventWriter::workerThreadFunc, this);

    Log::get().note("mfd_event_log: initialized EventWriter session=%s in '%ls'\n",
                    m_sessionUuid.c_str(), m_mfdsDir.c_str());
    return true;
}

void EventWriter::shutdown() {
    if (!m_running.load() && !m_workerThread.joinable()) {
        closeAllFilesLocked();
        return;
    }

    m_running = false;
    m_cv.notify_all();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    // Flush any remaining items in queue
    flush();

    std::lock_guard<std::mutex> lock(m_fileMutex);
    closeAllFilesLocked();

    Log::get().note("mfd_event_log: shutdown EventWriter session=%s\n", m_sessionUuid.c_str());
}

bool EventWriter::enqueue(MfdEventRecord record) {
    if (!m_running.load()) {
        return false;
    }

    if (record.ts.empty()) {
        record.ts = getCurrentIsoUtcTimestamp();
    }
    if (record.session.empty()) {
        record.session = m_sessionUuid;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);

        if (m_queue.size() >= m_maxQueueCapacity) {
            // Drop oldest 'nav' event first (§6.3)
            auto it = std::find_if(m_queue.begin(), m_queue.end(), [](const MfdEventRecord& r) {
                return r.type == "nav";
            });

            if (it != m_queue.end()) {
                m_packageDropCounts[it->packageId]++;
                m_totalDroppedCount++;
                m_hasPendingDrops = true;
                m_queue.erase(it);
                m_queue.push_back(std::move(record));
            } else {
                // If incoming is nav, drop incoming
                if (record.type == "nav") {
                    m_packageDropCounts[record.packageId]++;
                    m_totalDroppedCount++;
                    m_hasPendingDrops = true;
                    return false;
                } else {
                    // Queue is full of non-nav events; drop oldest from head
                    m_packageDropCounts[m_queue.front().packageId]++;
                    m_totalDroppedCount++;
                    m_hasPendingDrops = true;
                    m_queue.pop_front();
                    m_queue.push_back(std::move(record));
                }
            }
        } else {
            m_queue.push_back(std::move(record));
        }
    }

    m_cv.notify_one();
    return true;
}

void EventWriter::workerThreadFunc() {
    while (m_running.load()) {
        std::vector<MfdEventRecord> batch;
        std::unordered_map<std::string, uint32_t> pendingDrops;

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_cv.wait_for(lock, std::chrono::milliseconds(50), [this] {
                return !m_running.load() || (!m_paused.load() && (!m_queue.empty() || m_hasPendingDrops));
            });

            if (m_paused.load() && m_running.load()) {
                continue;
            }

            while (!m_queue.empty()) {
                batch.push_back(std::move(m_queue.front()));
                m_queue.pop_front();
            }

            if (m_hasPendingDrops) {
                pendingDrops = m_packageDropCounts;
                m_packageDropCounts.clear();
                m_hasPendingDrops = false;
            }
        }

        if (!batch.empty() || !pendingDrops.empty()) {
            std::lock_guard<std::mutex> fileLock(m_fileMutex);

            for (const auto& kv : pendingDrops) {
                if (kv.second > 0) {
                    emitDroppedRecordLocked(kv.first, kv.second);
                }
            }

            for (auto& rec : batch) {
                writeRecordLocked(rec);
            }

            flushOpenFilesLocked();
        }
    }
}

void EventWriter::flush() {
    std::vector<MfdEventRecord> batch;
    std::unordered_map<std::string, uint32_t> pendingDrops;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_queue.empty()) {
            batch.push_back(std::move(m_queue.front()));
            m_queue.pop_front();
        }
        if (m_hasPendingDrops) {
            pendingDrops = m_packageDropCounts;
            m_packageDropCounts.clear();
            m_hasPendingDrops = false;
        }
    }

    std::lock_guard<std::mutex> fileLock(m_fileMutex);
    for (const auto& kv : pendingDrops) {
        if (kv.second > 0) {
            emitDroppedRecordLocked(kv.first, kv.second);
        }
    }
    for (auto& rec : batch) {
        writeRecordLocked(rec);
    }
    flushOpenFilesLocked();
}

bool EventWriter::ensurePackageFileOpenLocked(const std::string& packageId, PackageState& state) {
    if (state.hFile != INVALID_HANDLE_VALUE) {
        return true;
    }

    std::wstring pkgDir = m_mfdsDir + L"\\" + utf8ToWide(packageId);
    CreateDirectoryW(pkgDir.c_str(), nullptr);

    std::wstring eventsPath = getPackageEventsPath(packageId);
    DWORD disposition = state.initialized ? OPEN_ALWAYS : CREATE_ALWAYS;

    state.hFile = CreateFileW(
        eventsPath.c_str(),
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        disposition,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (state.hFile == INVALID_HANDLE_VALUE) {
        Log::get().note("mfd_event_log: ERROR failed to open events file '%ls' (err=%lu)\n",
                        eventsPath.c_str(), GetLastError());
        return false;
    }

    if (state.initialized) {
        SetFilePointer(state.hFile, 0, nullptr, FILE_END);
    } else {
        state.nextSeq = 1;
        state.initialized = true;
    }

    return true;
}

void EventWriter::checkAndRollLocked(const std::string& packageId, PackageState& state, size_t upcomingBytes) {
    if (state.hFile == INVALID_HANDLE_VALUE) {
        return;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(state.hFile, &fileSize)) {
        return;
    }

    if (static_cast<size_t>(fileSize.QuadPart) + upcomingBytes > m_maxFileSize) {
        CloseHandle(state.hFile);
        state.hFile = INVALID_HANDLE_VALUE;

        std::wstring eventsPath = getPackageEventsPath(packageId);
        std::wstring rolledPath = getPackageRolledPath(packageId);

        // Rename events.jsonl to events.1.jsonl (replacing any existing) (§6.1)
        BOOL ok = MoveFileExW(eventsPath.c_str(), rolledPath.c_str(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
        if (!ok) {
            Log::get().note("mfd_event_log: WARNING MoveFileExW failed for roll '%ls' -> '%ls' (err=%lu)\n",
                            eventsPath.c_str(), rolledPath.c_str(), GetLastError());
        } else {
            Log::get().note("mfd_event_log: rolled events file for '%s' to events.1.jsonl\n", packageId.c_str());
        }

        // Start fresh events.jsonl
        state.hFile = CreateFileW(
            eventsPath.c_str(),
            GENERIC_WRITE | GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
    }
}

void EventWriter::writeRecordLocked(MfdEventRecord& record) {
    if (record.packageId.empty()) {
        return;
    }

    auto& state = m_packageStates[record.packageId];
    if (!ensurePackageFileOpenLocked(record.packageId, state)) {
        return;
    }

    if (record.seq == 0) {
        record.seq = state.nextSeq++;
    }

    if (record.ts.empty()) {
        record.ts = getCurrentIsoUtcTimestamp();
    }
    if (record.session.empty()) {
        record.session = m_sessionUuid;
    }

    std::string line = serializeEventToJson(record) + "\n";
    checkAndRollLocked(record.packageId, state, line.size());

    if (state.hFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(state.hFile, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    }
}

void EventWriter::emitDroppedRecordLocked(const std::string& packageId, uint32_t dropCount) {
    Log::get().note("mfd_event_log: dropped %u events due to queue overflow for package '%s'\n",
                    dropCount, packageId.c_str());

    MfdEventRecord dropRec = MfdEventRecord::createEventsDropped(packageId, dropCount);
    dropRec.session = m_sessionUuid;
    dropRec.ts = getCurrentIsoUtcTimestamp();
    writeRecordLocked(dropRec);
}

void EventWriter::flushOpenFilesLocked() {
    for (auto& pair : m_packageStates) {
        if (pair.second.hFile != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(pair.second.hFile);
        }
    }
}

void EventWriter::closeAllFilesLocked() {
    for (auto& pair : m_packageStates) {
        if (pair.second.hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(pair.second.hFile);
            pair.second.hFile = INVALID_HANDLE_VALUE;
        }
    }
}

} // namespace edvr::mfd
