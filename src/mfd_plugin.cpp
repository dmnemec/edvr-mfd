#include "mfd_manager.h"
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
#include <cstring>

namespace edvr::mfd {

namespace {

static const char* kDisplayChoices[] = {
    "1 (Center)",
    "2 (Center + Left)",
    "3 (All Three)"
};

static int64_t EDVR_API getMfdEnabled(const char*, void*) {
    return MfdManager::instance().isEnabled() ? 1 : 0;
}
static void EDVR_API setMfdEnabled(const char*, int64_t val, void*) {
    MfdManager::instance().setEnabled(val != 0);
}

static int64_t EDVR_API getShowAll(const char*, void*) {
    return MfdManager::instance().showAllWindows() ? 1 : 0;
}
static void EDVR_API setShowAll(const char*, int64_t val, void*) {
    MfdManager::instance().setShowAllWindows(val != 0);
}

static int64_t EDVR_API getDisplayCount(const char*, void*) {
    int c = MfdManager::instance().activeDisplayCount();
    return (c >= 1 && c <= 3) ? (c - 1) : 0;
}
static void EDVR_API setDisplayCount(const char*, int64_t val, void*) {
    MfdManager::instance().setActiveDisplayCount(static_cast<int>(val) + 1);
}

static int64_t EDVR_API getHeadLocked(const char*, void*) {
    return MfdManager::instance().isHeadLocked() ? 1 : 0;
}
static void EDVR_API setHeadLocked(const char*, int64_t val, void*) {
    MfdManager::instance().setHeadLocked(val != 0);
}

static void registerMfdSettings(const EdvrHostServices* host) {
    if (!host || !host->registerSetting) return;

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
    host->registerSetting(&defEnabled);

    EdvrPluginSettingDef defShowAll{};
    defShowAll.structSize = sizeof(EdvrPluginSettingDef);
    defShowAll.header = "Cockpit MFD";
    defShowAll.key = "mfd_show_all";
    defShowAll.label = "Show all windows";
    defShowAll.hint = "Force all configured cockpit MFD displays to be visible.";
    defShowAll.type = EDVR_PLUGIN_SETTING_BOOL;
    defShowAll.defaultValue = 1;
    defShowAll.getter = getShowAll;
    defShowAll.setter = setShowAll;
    host->registerSetting(&defShowAll);

    EdvrPluginSettingDef defCount{};
    defCount.structSize = sizeof(EdvrPluginSettingDef);
    defCount.header = "Cockpit MFD";
    defCount.key = "mfd_display_count";
    defCount.label = "Active displays";
    defCount.hint = "Select number of active cockpit MFD displays.";
    defCount.type = EDVR_PLUGIN_SETTING_CHOICE;
    defCount.choiceOptions = kDisplayChoices;
    defCount.choiceCount = 3;
    defCount.defaultValue = 2;
    defCount.getter = getDisplayCount;
    defCount.setter = setDisplayCount;
    host->registerSetting(&defCount);

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
    host->registerSetting(&defLocked);
}

int EDVR_API mfdPluginInit(const EdvrHostServices* host) {
    edvr::Log::get().init(edvr::executableDirectory() + L"\\edvr_logs");
    edvr::Config::get().init(edvr::executableDirectory());
    bool enabled = edvr::Config::get().getBool("fix.cockpit_mfd", true);
    MfdManager::instance().initialize(512, 384);
    MfdManager::instance().setEnabled(enabled);
    registerMfdSettings(host);
    Log::get().note("mfd_plugin: initialized Cockpit MFD Addon v1.0 (enabled=%d)\n", enabled ? 1 : 0);
    return 0;
}

void EDVR_API mfdPluginShutdown() {
    MfdManager::instance().shutdown();
    Log::get().note("mfd_plugin: shutdown Cockpit MFD Addon\n");
    // Explicitly close the log before the DLL unloads so Win32 HANDLE cleanup
    // happens here, not in the static destructor during CRT teardown.
    Log::get().close();
}

void EDVR_API mfdPluginUpdate(const EdvrPosef* headPose, float dtSeconds) {
    if (!headPose) return;
    bool enabled = edvr::Config::get().getBool("fix.cockpit_mfd", true);
    MfdManager::instance().setEnabled(enabled);
    if (!enabled) return;

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
    if (!MfdManager::instance().isEnabled()) return;

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
    if (!inputCtx || !MfdManager::instance().isEnabled()) return;

    auto* focused = MfdManager::instance().focusedSlot();
    bool isFocused = (focused != nullptr && focused->gazeTracker.isFocused());

    if (isFocused) {
        inputCtx->swallowInput = 1;
    }
}

} // namespace

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
