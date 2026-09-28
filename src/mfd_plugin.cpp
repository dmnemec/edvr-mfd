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

int mfdPluginInit(void* /*hostReserved*/) {
    edvr::Log::get().init(edvr::executableDirectory() + L"\\edvr_logs");
    edvr::Config::get().init(edvr::executableDirectory());
    bool enabled = edvr::Config::get().getBool("fix.cockpit_mfd", true);
    MfdManager::instance().initialize(512, 384);
    MfdManager::instance().setEnabled(enabled);
    Log::get().note("mfd_plugin: initialized Cockpit MFD Addon v1.0 (enabled=%d)\n", enabled ? 1 : 0);
    return 0;
}

void mfdPluginShutdown() {
    MfdManager::instance().shutdown();
    Log::get().note("mfd_plugin: shutdown Cockpit MFD Addon\n");
}

void mfdPluginUpdate(const EdvrPosef* headPose, float dtSeconds) {
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

void mfdPluginRenderEye(const EdvrEyeRenderContext* eyeCtx) {
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

void mfdPluginFilterInput(EdvrInputContext* inputCtx) {
    if (!inputCtx || !MfdManager::instance().isEnabled()) return;

    auto* focused = MfdManager::instance().focusedSlot();
    bool isFocused = (focused != nullptr && focused->gazeTracker.isFocused());

    if (isFocused) {
        inputCtx->swallowInput = 1;
    }
}

} // namespace

} // namespace edvr::mfd

extern "C" __declspec(dllexport) int EdvrPluginRegister(uint32_t hostApiVersion, EdvrPluginCallbacks* outCallbacks) {
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
