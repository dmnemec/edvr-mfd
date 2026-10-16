#include "mfd_manager.h"
#include "mfd_companion.h"
#include "mfd_event_log.h"
#include "mfd_package.h"
#include "edvr_plugin_api.h"
#include "config.h"
#include "log.h"

#if __has_include(<openxr/openxr.h>)
#include <openxr/openxr.h>
#else
struct XrVector3f { float x, y, z; };
struct XrQuaternionf { float x, y, z, w; };
struct XrPosef { XrQuaternionf orientation; XrVector3f position; };
struct XrFovf { float angleLeft, angleRight, angleUp, angleDown; };
#endif

#include <windows.h>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>

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

// Global settings backing variables (§9.1)
static std::atomic<bool> s_cockpitMfdEnabled{true};
static std::atomic<bool> s_mfdShowAll{true};
static std::atomic<bool> s_mfdHeadLocked{false};
static std::atomic<bool> s_mfdHideAllToggle{false};
static std::atomic<bool> s_mfdShowSettingsTab{true};

struct PackageSettingDefHolder {
    std::string packageId;
    std::string packageName;
    std::string header;
    std::string showKey;
    std::string companionsKey;
    std::string showLabel;
    std::string showHint;
    std::string companionsLabel;
    std::string companionsHint;

    std::atomic<bool> show{true};
    std::atomic<bool> companions{true};

    EdvrPluginSettingDef defShow{};
    EdvrPluginSettingDef defCompanions{};
};

static std::vector<std::unique_ptr<PackageSettingDefHolder>> s_packageSettings;
static std::mutex s_packageSettingsMutex;

static std::wstring getPluginSettingsIniPath() {
    wchar_t localAppData[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) {
        std::wstring edvrDir = std::wstring(localAppData) + L"\\EDVR";
        CreateDirectoryW(edvrDir.c_str(), nullptr);
        return edvrDir + L"\\edvr_mfd.ini";
    }
    return edvr::executableDirectory() + L"\\edvr_mfd.ini";
}

static bool readIniBool(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool defaultVal) {
    int val = GetPrivateProfileIntW(section, key, defaultVal ? 1 : 0, path.c_str());
    return val != 0;
}

static void writeIniBool(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool val) {
    WritePrivateProfileStringW(section, key, val ? L"1" : L"0", path.c_str());
}

static int64_t EDVR_API getMfdEnabled(const char*, void*) {
    return s_cockpitMfdEnabled.load(std::memory_order_relaxed) ? 1 : 0;
}
static void EDVR_API setMfdEnabled(const char*, int64_t val, void*) {
    bool b = (val != 0);
    s_cockpitMfdEnabled.store(b, std::memory_order_relaxed);
    MfdManager::instance().setEnabled(b);
    writeIniBool(getPluginSettingsIniPath(), L"settings", L"cockpit_mfd_enabled", b);
}

static int64_t EDVR_API getShowAll(const char*, void*) {
    return s_mfdShowAll.load(std::memory_order_relaxed) ? 1 : 0;
}
static void EDVR_API setShowAll(const char*, int64_t val, void*) {
    bool b = (val != 0);
    s_mfdShowAll.store(b, std::memory_order_relaxed);
    MfdManager::instance().setShowAllWindows(b);
    writeIniBool(getPluginSettingsIniPath(), L"settings", L"mfd_show_all", b);
}

static int64_t EDVR_API getHeadLocked(const char*, void*) {
    return s_mfdHeadLocked.load(std::memory_order_relaxed) ? 1 : 0;
}
static void EDVR_API setHeadLocked(const char*, int64_t val, void*) {
    bool b = (val != 0);
    s_mfdHeadLocked.store(b, std::memory_order_relaxed);
    MfdManager::instance().setHeadLocked(b);
    writeIniBool(getPluginSettingsIniPath(), L"settings", L"mfd_head_locked", b);
}

static int64_t EDVR_API getHideAllToggle(const char*, void*) {
    return s_mfdHideAllToggle.load(std::memory_order_relaxed) ? 1 : 0;
}
static void EDVR_API setHideAllToggle(const char*, int64_t val, void*) {
    bool b = (val != 0);
    s_mfdHideAllToggle.store(b, std::memory_order_relaxed);
    writeIniBool(getPluginSettingsIniPath(), L"settings", L"mfd_hide_all_toggle", b);
}

static int64_t EDVR_API getShowSettingsTab(const char*, void*) {
    return s_mfdShowSettingsTab.load(std::memory_order_relaxed) ? 1 : 0;
}
static void EDVR_API setShowSettingsTab(const char*, int64_t val, void*) {
    bool b = (val != 0);
    s_mfdShowSettingsTab.store(b, std::memory_order_relaxed);
    MfdManager::instance().setShowSettingsTab(b);
    writeIniBool(getPluginSettingsIniPath(), L"settings", L"mfd_show_settings_tab", b);
}

static int64_t EDVR_API getPackageShow(const char*, void* userData) {
    auto* holder = static_cast<PackageSettingDefHolder*>(userData);
    if (!holder) return 1;
    return holder->show.load(std::memory_order_relaxed) ? 1 : 0;
}
static void EDVR_API setPackageShow(const char*, int64_t val, void* userData) {
    auto* holder = static_cast<PackageSettingDefHolder*>(userData);
    if (!holder) return;
    bool b = (val != 0);
    holder->show.store(b, std::memory_order_relaxed);
    writeIniBool(getPluginSettingsIniPath(), L"settings", utf8ToWide(holder->showKey).c_str(), b);
}

static int64_t EDVR_API getPackageCompanions(const char*, void* userData) {
    auto* holder = static_cast<PackageSettingDefHolder*>(userData);
    if (!holder) return 1;
    return holder->companions.load(std::memory_order_relaxed) ? 1 : 0;
}
static void EDVR_API setPackageCompanions(const char*, int64_t val, void* userData) {
    auto* holder = static_cast<PackageSettingDefHolder*>(userData);
    if (!holder) return;
    bool b = (val != 0);
    holder->companions.store(b, std::memory_order_relaxed);
    writeIniBool(getPluginSettingsIniPath(), L"settings", utf8ToWide(holder->companionsKey).c_str(), b);
    CompanionSupervisor::instance().setPackageCompanionsEnabled(holder->packageId, b);
}

// The host returns -1 from registerSetting when it rejects a row; log the key so the rejection is visible.
static void logRejectedSetting(const EdvrHostServices* host, const char* key) {
    if (!host->logNote) return;
    std::string msg = std::string("mfd_plugin: registerSetting rejected key '") + key + "'\n";
    host->logNote(msg.c_str());
}

static void registerMfdSettings(const EdvrHostServices* host,
                                const std::vector<MfdPackageManifest>& packages,
                                const std::wstring& iniPath) {
    if (!host || !host->registerSetting) return;

    // 1. Global Settings (§9.1)
    EdvrPluginSettingDef defEnabled{};
    defEnabled.structSize = sizeof(EdvrPluginSettingDef);
    defEnabled.header = "Cockpit MFD";
    defEnabled.key = "cockpit_mfd_enabled";
    defEnabled.label = "Cockpit MFD Displays";
    defEnabled.hint = "Master toggle for in-cockpit MFD virtual displays.";
    defEnabled.type = EDVR_PLUGIN_SETTING_BOOL;
    defEnabled.defaultValue = 1;
    defEnabled.getter = getMfdEnabled;
    defEnabled.setter = setMfdEnabled;
    if (host->registerSetting(&defEnabled) < 0) logRejectedSetting(host, defEnabled.key);

    EdvrPluginSettingDef defShowAll{};
    defShowAll.structSize = sizeof(EdvrPluginSettingDef);
    defShowAll.header = "Cockpit MFD";
    defShowAll.key = "mfd_show_all";
    defShowAll.label = "Show all windows";
    defShowAll.hint = "Disables gaze fade on all screens.";
    defShowAll.type = EDVR_PLUGIN_SETTING_BOOL;
    defShowAll.defaultValue = 1;
    defShowAll.getter = getShowAll;
    defShowAll.setter = setShowAll;
    if (host->registerSetting(&defShowAll) < 0) logRejectedSetting(host, defShowAll.key);

    EdvrPluginSettingDef defLocked{};
    defLocked.structSize = sizeof(EdvrPluginSettingDef);
    defLocked.header = "Cockpit MFD";
    defLocked.key = "mfd_head_locked";
    defLocked.label = "Head-locked HUD";
    defLocked.hint = "Attach displays to VR head orientation instead of cockpit 3D space.";
    defLocked.type = EDVR_PLUGIN_SETTING_BOOL;
    defLocked.defaultValue = 0;
    defLocked.getter = getHeadLocked;
    defLocked.setter = setHeadLocked;
    if (host->registerSetting(&defLocked) < 0) logRejectedSetting(host, defLocked.key);

    EdvrPluginSettingDef defHideAll{};
    defHideAll.structSize = sizeof(EdvrPluginSettingDef);
    defHideAll.header = "Cockpit MFD";
    defHideAll.key = "mfd_hide_all_toggle";
    defHideAll.label = "Hide all displays";
    defHideAll.hint = "Suppress all MFD display rendering (pause/ESC menu).";
    defHideAll.type = EDVR_PLUGIN_SETTING_BOOL;
    defHideAll.defaultValue = 0;
    defHideAll.getter = getHideAllToggle;
    defHideAll.setter = setHideAllToggle;
    if (host->registerSetting(&defHideAll) < 0) logRejectedSetting(host, defHideAll.key);

    EdvrPluginSettingDef defShowSettingsTab{};
    defShowSettingsTab.structSize = sizeof(EdvrPluginSettingDef);
    defShowSettingsTab.header = "Cockpit MFD";
    defShowSettingsTab.key = "mfd_show_settings_tab";
    defShowSettingsTab.label = "Show Settings Tab";
    defShowSettingsTab.hint = "Show or hide the interactive SETTINGS tab across all MFD displays.";
    defShowSettingsTab.type = EDVR_PLUGIN_SETTING_BOOL;
    defShowSettingsTab.defaultValue = 1;
    defShowSettingsTab.getter = getShowSettingsTab;
    defShowSettingsTab.setter = setShowSettingsTab;
    if (host->registerSetting(&defShowSettingsTab) < 0) logRejectedSetting(host, defShowSettingsTab.key);

    // 2. Dynamic Per-Package Settings (§9.2)
    std::lock_guard<std::mutex> lock(s_packageSettingsMutex);
    s_packageSettings.clear();

    for (const auto& pkg : packages) {
        auto holder = std::make_unique<PackageSettingDefHolder>();
        holder->packageId = pkg.id;
        holder->packageName = pkg.name.empty() ? pkg.id : pkg.name;
        holder->header = "MFD: " + holder->packageName;
        holder->showKey = "pkg." + pkg.id + ".show";
        holder->companionsKey = "pkg." + pkg.id + ".companions";
        holder->showLabel = "Show displays";
        holder->showHint = "Show this package's screens in cockpit.";
        holder->companionsLabel = "Run companions";
        holder->companionsHint = "Run background companion processes for this package.";

        // Read persistence
        holder->show.store(readIniBool(iniPath, L"settings", utf8ToWide(holder->showKey).c_str(), true));
        holder->companions.store(readIniBool(iniPath, L"settings", utf8ToWide(holder->companionsKey).c_str(), true));

        // Register pkg.<id>.show
        holder->defShow.structSize = sizeof(EdvrPluginSettingDef);
        holder->defShow.header = holder->header.c_str();
        holder->defShow.key = holder->showKey.c_str();
        holder->defShow.label = holder->showLabel.c_str();
        holder->defShow.hint = holder->showHint.c_str();
        holder->defShow.type = EDVR_PLUGIN_SETTING_BOOL;
        holder->defShow.defaultValue = 1;
        holder->defShow.getter = getPackageShow;
        holder->defShow.setter = setPackageShow;
        holder->defShow.userData = holder.get();
        if (host->registerSetting(&holder->defShow) < 0) logRejectedSetting(host, holder->defShow.key);

        // Register pkg.<id>.companions
        holder->defCompanions.structSize = sizeof(EdvrPluginSettingDef);
        holder->defCompanions.header = holder->header.c_str();
        holder->defCompanions.key = holder->companionsKey.c_str();
        holder->defCompanions.label = holder->companionsLabel.c_str();
        holder->defCompanions.hint = holder->companionsHint.c_str();
        holder->defCompanions.type = EDVR_PLUGIN_SETTING_BOOL;
        holder->defCompanions.defaultValue = 1;
        holder->defCompanions.getter = getPackageCompanions;
        holder->defCompanions.setter = setPackageCompanions;
        holder->defCompanions.userData = holder.get();
        if (host->registerSetting(&holder->defCompanions) < 0) logRejectedSetting(host, holder->defCompanions.key);

        s_packageSettings.push_back(std::move(holder));
    }
}

} // namespace

// Public test and diagnostic helpers
bool getPluginSettingBool(const std::string& key, bool defaultVal) {
    if (key == "cockpit_mfd_enabled") return s_cockpitMfdEnabled.load();
    if (key == "mfd_show_all") return s_mfdShowAll.load();
    if (key == "mfd_head_locked") return s_mfdHeadLocked.load();
    if (key == "mfd_hide_all_toggle") return s_mfdHideAllToggle.load();
    if (key == "mfd_show_settings_tab") return s_mfdShowSettingsTab.load();

    std::lock_guard<std::mutex> lock(s_packageSettingsMutex);
    for (const auto& pkg : s_packageSettings) {
        if (pkg->showKey == key) return pkg->show.load();
        if (pkg->companionsKey == key) return pkg->companions.load();
    }
    return defaultVal;
}

void setPluginSettingBool(const std::string& key, bool val) {
    if (key == "cockpit_mfd_enabled") {
        setMfdEnabled(key.c_str(), val ? 1 : 0, nullptr);
    } else if (key == "mfd_show_all") {
        setShowAll(key.c_str(), val ? 1 : 0, nullptr);
    } else if (key == "mfd_head_locked") {
        setHeadLocked(key.c_str(), val ? 1 : 0, nullptr);
    } else if (key == "mfd_hide_all_toggle") {
        setHideAllToggle(key.c_str(), val ? 1 : 0, nullptr);
    } else if (key == "mfd_show_settings_tab") {
        setShowSettingsTab(key.c_str(), val ? 1 : 0, nullptr);
    } else {
        std::lock_guard<std::mutex> lock(s_packageSettingsMutex);
        for (const auto& pkg : s_packageSettings) {
            if (pkg->showKey == key) {
                setPackageShow(key.c_str(), val ? 1 : 0, pkg.get());
                return;
            }
            if (pkg->companionsKey == key) {
                setPackageCompanions(key.c_str(), val ? 1 : 0, pkg.get());
                return;
            }
        }
    }
}

std::wstring getPluginSettingsPathForTest() {
    return getPluginSettingsIniPath();
}

int EDVR_API mfdPluginInit(const EdvrHostServices* host) {
    edvr::Log::get().init(edvr::executableDirectory() + L"\\edvr_logs");
    edvr::Config::get().init(edvr::executableDirectory());

    std::wstring iniPath = getPluginSettingsIniPath();
    s_cockpitMfdEnabled.store(readIniBool(iniPath, L"settings", L"cockpit_mfd_enabled", true));
    s_mfdShowAll.store(readIniBool(iniPath, L"settings", L"mfd_show_all", true));
    s_mfdHeadLocked.store(readIniBool(iniPath, L"settings", L"mfd_head_locked", false));
    s_mfdHideAllToggle.store(readIniBool(iniPath, L"settings", L"mfd_hide_all_toggle", false));
    s_mfdShowSettingsTab.store(readIniBool(iniPath, L"settings", L"mfd_show_settings_tab", true));

    MfdManager::instance().initialize(512, 384);
    MfdManager::instance().setEnabled(s_cockpitMfdEnabled.load());
    MfdManager::instance().setShowAllWindows(s_mfdShowAll.load());
    MfdManager::instance().setHeadLocked(s_mfdHeadLocked.load());
    MfdManager::instance().setShowSettingsTab(s_mfdShowSettingsTab.load());

    std::wstring mfdsDir = getDefaultMfdsDirectory();
    auto packages = discoverPackages(mfdsDir);

    EventWriter::instance().initialize(mfdsDir);
    CompanionSupervisor::instance().initialize(mfdsDir);
    CompanionSupervisor::instance().registerPackages(packages);

    registerMfdSettings(host, packages, iniPath);

    // Apply companion states
    {
        std::lock_guard<std::mutex> lock(s_packageSettingsMutex);
        for (const auto& holder : s_packageSettings) {
            CompanionSupervisor::instance().setPackageCompanionsEnabled(holder->packageId, holder->companions.load());
        }
    }

    Log::get().note("mfd_plugin: initialized Cockpit MFD Addon v1.0 (enabled=%d, packages=%zu)\n",
                    s_cockpitMfdEnabled.load() ? 1 : 0, packages.size());
    return 0;
}

void EDVR_API mfdPluginShutdown() {
    CompanionSupervisor::instance().shutdown();
    MfdManager::instance().shutdown();
    EventWriter::instance().shutdown();

    {
        std::lock_guard<std::mutex> lock(s_packageSettingsMutex);
        s_packageSettings.clear();
    }

    Log::get().note("mfd_plugin: shutdown Cockpit MFD Addon\n");
    Log::get().close();
}

void EDVR_API mfdPluginUpdate(const EdvrPosef* headPose, float dtSeconds) {
    if (!headPose) return;
    if (!s_cockpitMfdEnabled.load()) return;
    if (s_mfdHideAllToggle.load()) return; // MFD-006: suppress quad rendering

    Vec3 headPos(headPose->position.x, headPose->position.y, headPose->position.z);
    Quat headRot(headPose->orientation.x, headPose->orientation.y, headPose->orientation.z, headPose->orientation.w);
    Vec3 headFwd = headRot.rotate(Vec3(0.0f, 0.0f, -1.0f));

    MfdManager::instance().update(headPos, headFwd, dtSeconds);

    auto* focused = MfdManager::instance().focusedSlot();
    MfdManager::instance().inputRouter().pollAndRoute(
        focused != nullptr,
        focused ? focused->provider.get() : nullptr,
        [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
    );

    MfdManager::instance().render();
}

void EDVR_API mfdPluginRenderEye(const EdvrEyeRenderContext* eyeCtx) {
    if (!eyeCtx || !eyeCtx->device || !eyeCtx->context || !eyeCtx->rtv) return;
    if (!s_cockpitMfdEnabled.load()) return;
    if (s_mfdHideAllToggle.load()) return; // MFD-006: bypass eye blitting

    XrPosef xrPose{};
    xrPose.position.x = eyeCtx->eyePose.position.x;
    xrPose.position.y = eyeCtx->eyePose.position.y;
    xrPose.position.z = eyeCtx->eyePose.position.z;
    xrPose.orientation.x = eyeCtx->eyePose.orientation.x;
    xrPose.orientation.y = eyeCtx->eyePose.orientation.y;
    xrPose.orientation.z = eyeCtx->eyePose.orientation.z;
    xrPose.orientation.w = eyeCtx->eyePose.orientation.w;

    XrFovf xrFov{};
    xrFov.angleLeft = eyeCtx->eyeFov.angleLeft;
    xrFov.angleRight = eyeCtx->eyeFov.angleRight;
    xrFov.angleUp = eyeCtx->eyeFov.angleUp;
    xrFov.angleDown = eyeCtx->eyeFov.angleDown;

    MfdManager::instance().renderToEyeRtv(
        eyeCtx->device, eyeCtx->context, eyeCtx->rtv,
        xrPose, xrFov,
        eyeCtx->viewportWidth, eyeCtx->viewportHeight,
        nullptr, nullptr, nullptr, nullptr
    );
}

void EDVR_API mfdPluginFilterInput(EdvrInputContext* inputCtx) {
    if (!inputCtx || !s_cockpitMfdEnabled.load()) return;
    if (s_mfdHideAllToggle.load()) return; // MFD-006: do not swallow input when hidden

    auto* focused = MfdManager::instance().focusedSlot();
    bool isFocused = (focused != nullptr && focused->gazeTracker.isFocused());

    if (isFocused) {
        inputCtx->swallowInput = 1;
    }
}

} // namespace edvr::mfd

extern "C" __declspec(dllexport) int EDVR_API EdvrPluginRegister(uint32_t hostApiVersion, EdvrPluginCallbacks* outCallbacks) {
    if (hostApiVersion != EDVR_PLUGIN_API_VERSION || !outCallbacks) {
        return -1; // Version mismatch or invalid buffer
    }

    outCallbacks->pluginName = "Cockpit MFD";
    outCallbacks->pluginVersion = "1.0.0";
    outCallbacks->onInitialize = edvr::mfd::mfdPluginInit;
    outCallbacks->onShutdown = edvr::mfd::mfdPluginShutdown;
    outCallbacks->onUpdate = edvr::mfd::mfdPluginUpdate;
    outCallbacks->onRenderEye = edvr::mfd::mfdPluginRenderEye;
    outCallbacks->onFilterInput = edvr::mfd::mfdPluginFilterInput;

    return 0; // Success
}
