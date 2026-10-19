#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <windows.h>

namespace edvr::mfd {

// Event record schema per §6.2
struct MfdEventRecord {
    std::string packageId;                   // Package identifier (determines destination file)
    uint64_t seq = 0;                        // Monotonic uint64 per package per session (starts at 1)
    std::string ts;                          // ISO 8601 UTC timestamp string with milliseconds
    std::string session;                     // UUIDv4 session string
    std::string screen;                      // Screen ID string
    std::string type;                        // Event type string
    std::string tab;                         // Tab ID string
    int index = 0;                           // Index
    std::string itemId;                      // Item ID string
    std::string label;                       // Label string
    bool hasKey = false;                     // Whether key is present
    std::string key;                         // Key string (or null in JSON)
    bool hasDelta = false;                   // Whether delta is present
    int delta = 0;                           // Delta int (or null in JSON)

    // Optional / type-specific extra fields
    std::string action;                      // Optional action for select (e.g. "clipboard")

    // load_error fields
    std::string file;
    int line = 0;
    int column = 0;
    std::string message;

    // context_changed fields
    bool hasContextInfo = false;
    std::vector<std::string> contexts;
    uint32_t flags = 0;
    uint32_t flags2 = 0;
    int guiFocus = 0;

    // companion_* fields
    std::string companion;
    uint32_t pid = 0;
    int exitCode = 0;
    int attempt = 0;

    // events_dropped fields
    uint32_t dropCount = 0;

    // Convenience factory methods
    static MfdEventRecord createPluginStart(const std::string& packageId, const std::string& screen = "");
    static MfdEventRecord createScreenLoaded(const std::string& packageId, const std::string& screen);
    static MfdEventRecord createLoadError(const std::string& packageId, const std::string& screen,
                                          const std::string& file, int line, int column, const std::string& message);
    static MfdEventRecord createContextChanged(const std::string& packageId,
                                               const std::vector<std::string>& contexts,
                                               uint32_t flags, uint32_t flags2, int guiFocus);
    static MfdEventRecord createFocusGained(const std::string& packageId, const std::string& screen);
    static MfdEventRecord createFocusLost(const std::string& packageId, const std::string& screen);
    static MfdEventRecord createTabChange(const std::string& packageId, const std::string& screen,
                                          const std::string& tab, int index);
    static MfdEventRecord createNav(const std::string& packageId, const std::string& screen,
                                    const std::string& tab, int index,
                                    const std::string& itemId, const std::string& label);
    static MfdEventRecord createSelect(const std::string& packageId, const std::string& screen,
                                       const std::string& tab, int index,
                                       const std::string& itemId, const std::string& label,
                                       const std::string& action = "");
    // Back press. index is -1 and itemId/label empty when no item is focused.
    static MfdEventRecord createBack(const std::string& packageId, const std::string& screen,
                                     const std::string& tab, int index,
                                     const std::string& itemId, const std::string& label,
                                     const std::string& action);
    static MfdEventRecord createAdjust(const std::string& packageId, const std::string& screen,
                                       const std::string& tab, int index,
                                       const std::string& itemId, const std::string& label,
                                       const std::string& key, int delta);
    static MfdEventRecord createCompanionStarted(const std::string& packageId, const std::string& companion,
                                                 uint32_t pid, int attempt);
    static MfdEventRecord createCompanionCrashed(const std::string& packageId, const std::string& companion,
                                                 uint32_t pid, int exitCode, int attempt);
    static MfdEventRecord createCompanionGaveUp(const std::string& packageId, const std::string& companion,
                                                uint32_t pid, int exitCode, int attempt);
    static MfdEventRecord createEventsDropped(const std::string& packageId, uint32_t dropCount);
};

// Utilities
std::string generateUuidV4();
std::string getCurrentIsoUtcTimestamp();
std::string serializeEventToJson(const MfdEventRecord& record);
bool parseEventFromJson(const std::string& jsonLine, MfdEventRecord& outRecord, std::string* outError = nullptr);

// EventWriter background thread managing per-package event logs (§6)
class EventWriter {
public:
    static constexpr size_t kDefaultQueueCapacity = 4096;
    static constexpr size_t kDefaultMaxFileSize = 1024 * 1024; // 1 MB

    static EventWriter& instance();

    explicit EventWriter(size_t queueCapacity = kDefaultQueueCapacity, size_t maxFileSize = kDefaultMaxFileSize);
    ~EventWriter();

    // Prevent copies
    EventWriter(const EventWriter&) = delete;
    EventWriter& operator=(const EventWriter&) = delete;

    // Lifecycle
    bool initialize(const std::wstring& mfdsDir, const std::string& sessionUuid = "");
    void shutdown();
    bool isRunning() const { return m_running.load(); }

    // Enqueue event from render thread (non-blocking, bounded queue)
    bool enqueue(MfdEventRecord record);

    // Synchronously flush all queued events to disk (useful for tests and shutdown)
    void flush();

    // Configuration & stats (useful for tests)
    void setQueueCapacity(size_t capacity);
    size_t queueCapacity() const { return m_maxQueueCapacity; }
    void setMaxFileSize(size_t maxBytes);
    size_t maxFileSize() const { return m_maxFileSize; }
    void setWorkerPaused(bool paused) {
        m_paused.store(paused);
        if (!paused) m_cv.notify_all();
    }
    bool isWorkerPaused() const { return m_paused.load(); }

    const std::string& sessionUuid() const { return m_sessionUuid; }
    const std::wstring& mfdsDir() const { return m_mfdsDir; }

    size_t queueSize() const;
    uint64_t totalDroppedCount() const { return m_totalDroppedCount.load(); }
    uint64_t getNextSeq(const std::string& packageId) const;

    std::wstring getPackageEventsPath(const std::string& packageId) const;
    std::wstring getPackageRolledPath(const std::string& packageId) const;

private:
    struct PackageState {
        uint64_t nextSeq = 1;
        HANDLE hFile = INVALID_HANDLE_VALUE;
        bool initialized = false;
    };

    void workerThreadFunc();
    void writeRecordLocked(MfdEventRecord& record);
    void emitDroppedRecordLocked(const std::string& packageId, uint32_t dropCount);
    void checkAndRollLocked(const std::string& packageId, PackageState& state, size_t upcomingBytes);
    bool ensurePackageFileOpenLocked(const std::string& packageId, PackageState& state);
    void flushOpenFilesLocked();
    void closeAllFilesLocked();

    std::wstring m_mfdsDir;
    std::string m_sessionUuid;
    size_t m_maxQueueCapacity = kDefaultQueueCapacity;
    size_t m_maxFileSize = kDefaultMaxFileSize;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
    std::thread m_workerThread;

    mutable std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::deque<MfdEventRecord> m_queue;
    std::unordered_map<std::string, uint32_t> m_packageDropCounts;
    bool m_hasPendingDrops = false;
    std::atomic<uint64_t> m_totalDroppedCount{0};

    mutable std::mutex m_fileMutex;
    std::unordered_map<std::string, PackageState> m_packageStates;
};

} // namespace edvr::mfd
