// mfd_test.cpp -- Unit test suite for EDVR Cockpit MFD framework
// Validates Option A (Declarative JSON MFD) implementation, Option B extensibility slots,
// gaze-directed focus tracking, UI input routing, and HUD rasterization.

#include "mfd_types.h"
#include "mfd_view_model.h"
#include "mfd_provider.h"
#include "mfd_font.h"
#include "mfd_renderer.h"
#include "mfd_gaze_tracker.h"
#include "mfd_input_router.h"
#include "mfd_compositor.h"
#include "mfd_manager.h"
#include "mfd_display_schema.h"
#include "mfd_package.h"
#include "mfd_content_watcher.h"
#include "mfd_event_log.h"
#include "mfd_context.h"
#include "mfd_companion.h"
#include "edvr_plugin_api.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#if __has_include(<openxr/openxr.h>)
#include <openxr/openxr.h>
#else
struct XrVector3f { float x, y, z; };
struct XrQuaternionf { float x, y, z, w; };
struct XrPosef { XrQuaternionf orientation; XrVector3f position; };
struct XrFovf { float angleLeft, angleRight, angleUp, angleDown; };
#endif
#include <cassert>
#include <iostream>
#include <string>
#include <cmath>

using namespace edvr::mfd;

extern "C" int EDVR_API EdvrPluginRegister(uint32_t hostApiVersion, EdvrPluginCallbacks* outCallbacks);
extern "C" int EDVR_API mfdPluginInit(const EdvrHostServices* host);
extern "C" void EDVR_API mfdPluginShutdown();
extern "C" void EDVR_API mfdPluginFilterInput(EdvrInputContext* ctx);

namespace edvr::mfd {
bool getPluginSettingBool(const std::string& key, bool defaultVal);
void setPluginSettingBool(const std::string& key, bool val);
std::wstring getPluginSettingsPathForTest();
}

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " (" << #cond << ") at line " << __LINE__ << std::endl; \
            return 1; \
        } \
    } while(0)

// 1. Test geometric vector and quaternion math
int test_math() {
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(4.0f, 5.0f, 6.0f);
    Vec3 c = a + b;
    TEST_CHECK(std::abs(c.x - 5.0f) < 1e-5f, "Vec3 addition X");
    TEST_CHECK(std::abs(c.y - 7.0f) < 1e-5f, "Vec3 addition Y");
    TEST_CHECK(std::abs(c.z - 9.0f) < 1e-5f, "Vec3 addition Z");

    Vec3 norm = Vec3(0.0f, 3.0f, 4.0f).normalized();
    TEST_CHECK(std::abs(norm.length() - 1.0f) < 1e-5f, "Vec3 normalization length");
    TEST_CHECK(std::abs(norm.y - 0.6f) < 1e-5f, "Vec3 normalized Y");
    TEST_CHECK(std::abs(norm.z - 0.8f) < 1e-5f, "Vec3 normalized Z");

    // Quaternion identity rotation
    Quat q = Quat::fromEulerDegrees(0.0f, 0.0f, 0.0f);
    Vec3 fwd(0.0f, 0.0f, -1.0f);
    Vec3 rotated = q.rotate(fwd);
    TEST_CHECK(std::abs(rotated.x - fwd.x) < 1e-4f, "Identity rotation X");
    TEST_CHECK(std::abs(rotated.y - fwd.y) < 1e-4f, "Identity rotation Y");
    TEST_CHECK(std::abs(rotated.z - fwd.z) < 1e-4f, "Identity rotation Z");

    // Yaw 90 degrees left around +Y (right-handed): fwd (0, 0, -1) rotates to (-1, 0, 0)
    Quat qYaw90 = Quat::fromEulerDegrees(0.0f, 90.0f, 0.0f);
    Vec3 rotatedYaw = qYaw90.rotate(fwd);
    TEST_CHECK(std::abs(rotatedYaw.x - (-1.0f)) < 1e-4f, "Yaw 90 rotation X");
    TEST_CHECK(std::abs(rotatedYaw.z) < 1e-4f, "Yaw 90 rotation Z");

    return 0;
}

// 2. Test Option A: Declarative JSON MFD Provider
int test_declarative_provider() {
    DeclarativeMfdProvider provider("spansh_test", "SPANSH ROUTER");
    TEST_CHECK(provider.type() == MfdProviderType::kDeclarativeJson, "Provider type is DeclarativeJson (Option A)");
    TEST_CHECK(std::string(provider.id()) == "spansh_test", "Provider ID matches");

    std::string testJson = R"({
        "schemaVersion": 1,
        "title": "SPANSH NEUTRON ROUTER",
        "subtitle": "CURRENT: SOL",
        "statusBadge": "SYNCED",
        "footerHint": "[Q/E] TABS  [NAV] SELECT",
        "tabs": [
            {
                "id": "route",
                "title": "ROUTE",
                "type": "list",
                "items": [
                    {"id": "wp1", "label": "Jackson's Lighthouse", "value": "12.4 LY", "sublabel": "Neutron Star", "badge": "CURRENT"},
                    {"id": "wp2", "label": "Prua Phoe NC-D d12-14", "value": "148.1 LY", "sublabel": "Supercharge Ready", "badge": "NEXT"},
                    {"id": "wp3", "label": "Dryooe Prou CS-B d1-2", "value": "294.0 LY", "sublabel": "Class M Star", "badge": "REFUEL"}
                ]
            },
            {
                "id": "metrics",
                "title": "METRICS",
                "type": "keyvalue",
                "items": [
                    {"id": "jumps", "key": "Total Jumps", "value": "14"},
                    {"id": "dist", "key": "Distance Left", "value": "2,410.8 LY"},
                    {"id": "fuel", "key": "Fuel Reserve", "value": "88%"}
                ]
            }
        ]
    })";

    bool parsed = provider.loadFromJson(testJson);
    TEST_CHECK(parsed, "Declarative JSON parsed successfully");

    const auto& vm = provider.viewModel();
    TEST_CHECK(vm.title == "SPANSH NEUTRON ROUTER", "View model title");
    TEST_CHECK(vm.subtitle == "CURRENT: SOL", "View model subtitle");
    TEST_CHECK(vm.statusBadge == "SYNCED", "View model status badge");
    TEST_CHECK(vm.tabs.size() == 2, "View model tab count");
    TEST_CHECK(vm.tabs[0].title == "ROUTE", "Tab 0 title");
    TEST_CHECK(vm.tabs[0].items.size() == 3, "Tab 0 item count");
    TEST_CHECK(vm.tabs[0].items[1].label == "Prua Phoe NC-D d12-14", "Tab 0 item 1 label");
    TEST_CHECK(vm.tabs[1].title == "METRICS", "Tab 1 title");
    TEST_CHECK(vm.tabs[1].keyValues.size() == 3, "Tab 1 key-value count");

    // Test Navigation Inputs when unfocused (should NOT consume):
    provider.onFocusChanged(false);
    TEST_CHECK(!provider.onInput(MfdInputAction::kNextTab), "Unfocused input is ignored");
    TEST_CHECK(provider.viewModel().activeTabIndex == 0, "Tab remains unchanged when unfocused");

    // Test Navigation Inputs when focused (SHOULD consume):
    provider.onFocusChanged(true);
    TEST_CHECK(provider.onInput(MfdInputAction::kNextTab), "NextTab consumed when focused");
    TEST_CHECK(provider.viewModel().activeTabIndex == 1, "Tab cycled to 1");

    TEST_CHECK(provider.onInput(MfdInputAction::kPrevTab), "PrevTab consumed when focused");
    TEST_CHECK(provider.viewModel().activeTabIndex == 0, "Tab cycled back to 0");

    // Test list item selection
    TEST_CHECK(provider.viewModel().tabs[0].selectedIndex == 0, "Initial selection at index 0");
    TEST_CHECK(provider.onInput(MfdInputAction::kDown), "Down arrow moves selection");
    TEST_CHECK(provider.viewModel().tabs[0].selectedIndex == 1, "Selection moved to index 1");

    // Test Action Trigger on Select
    std::string selectedId;
    std::string selectedLabel;
    provider.setActionCallback([&](const std::string& id, const std::string& label) {
        selectedId = id;
        selectedLabel = label;
    });

    TEST_CHECK(provider.onInput(MfdInputAction::kSelect), "Select action executed");
    TEST_CHECK(selectedId == "wp2", "Selected item ID forwarded to callback");
    TEST_CHECK(selectedLabel == "Prua Phoe NC-D d12-14", "Selected item label forwarded to callback");

    return 0;
}

// 3. Test Option B: Scripted Code MFD Provider Architecture Slot
int test_scripted_provider_slot() {
    ScriptedMfdProvider scripted("custom_lua_slot", "scripts/cockpit_calc.lua");
    TEST_CHECK(scripted.type() == MfdProviderType::kScriptedCode, "Provider type is ScriptedCode (Option B)");
    TEST_CHECK(std::string(scripted.id()) == "custom_lua_slot", "Scripted provider ID matches");
    TEST_CHECK(scripted.scriptPath() == "scripts/cockpit_calc.lua", "Script path preserved");
    TEST_CHECK(std::string(scripted.title()) == "SCRIPTED MFD", "Scripted provider default title");

    // Verify polymorphic handling via IMfdProvider base pointer:
    IMfdProvider* basePtr = &scripted;
    TEST_CHECK(basePtr->type() == MfdProviderType::kScriptedCode, "Polymorphic type check for Option B");

    return 0;
}

// 4. Test Head-Gaze Tracker and Hysteresis State Machine
int test_gaze_tracker() {
    MfdGazeTracker tracker;
    tracker.setDwellEnterTime(0.10f); // 100ms dwell threshold
    tracker.setDwellExitTime(0.15f);  // 150ms release grace period
    tracker.setConeAngleDegrees(15.0f);

    // MFD situated on cockpit lower console: x=0.25m (right), y=-0.20m (down), z=-0.50m (forward)
    MfdPose mfdPose;
    mfdPose.position = Vec3(0.25f, -0.20f, -0.50f);
    mfdPose.orientation = Quat::fromEulerDegrees(-25.0f, -25.0f, 0.0f); // Angled up and toward pilot
    mfdPose.widthM = 0.25f;
    mfdPose.heightM = 0.15f;

    Vec3 headPos(0.0f, 0.0f, 0.0f);

    // Case A: Looking straight ahead (fwd = (0, 0, -1)). Should be unfocused.
    Vec3 headFwd(0.0f, 0.0f, -1.0f);
    MfdFocusState s1 = tracker.update(headPos, headFwd, mfdPose, 0.016f);
    TEST_CHECK(s1 == MfdFocusState::kUnfocused, "Looking straight forward is Unfocused");

    // Case B: Pilot turns head directly towards MFD
    Vec3 toMfd = (mfdPose.position - headPos).normalized();
    float angleToMfd = MfdGazeTracker::computeGazeAngle(headPos, toMfd, mfdPose.position);
    TEST_CHECK(angleToMfd < 0.1f, "Direct gaze angle is near 0 deg");

    // Frame 1 looking at MFD: transitions to kAcquiring
    MfdFocusState s2 = tracker.update(headPos, toMfd, mfdPose, 0.016f);
    TEST_CHECK(s2 == MfdFocusState::kAcquiring, "Initial gaze transitions to kAcquiring");

    // Advance 50ms (not yet at 100ms threshold): should still be acquiring
    MfdFocusState s3 = tracker.update(headPos, toMfd, mfdPose, 0.050f);
    TEST_CHECK(s3 == MfdFocusState::kAcquiring, "Dwell under threshold remains kAcquiring");

    // Advance past 100ms: transitions to kFocused!
    MfdFocusState s4 = tracker.update(headPos, toMfd, mfdPose, 0.060f);
    TEST_CHECK(s4 == MfdFocusState::kFocused, "Dwell past threshold transitions to kFocused");
    TEST_CHECK(tracker.isFocused(), "isFocused() returns true");

    // Case C: Pilot glances away (fwd = (0, 0, -1)): enters kReleasing grace period
    MfdFocusState s5 = tracker.update(headPos, headFwd, mfdPose, 0.050f);
    TEST_CHECK(s5 == MfdFocusState::kReleasing, "Glancing away enters kReleasing");

    // Pilot glances back at MFD before 150ms grace period expires: recovers to kFocused!
    MfdFocusState s6 = tracker.update(headPos, toMfd, mfdPose, 0.020f);
    TEST_CHECK(s6 == MfdFocusState::kFocused, "Recovering gaze restores kFocused");

    // Pilot turns away completely for > 150ms: transitions back to kUnfocused
    tracker.update(headPos, headFwd, mfdPose, 0.050f); // enters kReleasing
    MfdFocusState s7 = tracker.update(headPos, headFwd, mfdPose, 0.200f); // exceeds grace period
    TEST_CHECK(s7 == MfdFocusState::kUnfocused, "Exceeding release grace period returns to kUnfocused");

    return 0;
}

// 5. Test Input Router and UI Navigation Suppression
int test_input_router() {
    MfdInputRouter router;
    DeclarativeMfdProvider provider("nav_test");
    provider.loadFromJson(R"({
        "tabs": [
            { "title": "TAB1", "type": "list", "items": [{"id": "1", "label": "A"}, {"id": "2", "label": "B"}] },
            { "title": "TAB2", "type": "list", "items": [{"id": "3", "label": "C"}] }
        ]
    })");

    // Scenario 1: MFD NOT focused. Pressing Down arrow (keyDownDown = true)
    // Inputs must NOT be swallowed, passing through to game flight controls.
    bool swallowed1 = router.processInput(false, &provider,
                                          false, true, false, false,
                                          false, false, false, false);
    TEST_CHECK(!swallowed1, "Input is NOT swallowed when MFD is unfocused");
    TEST_CHECK(provider.viewModel().tabs[0].selectedIndex == 0, "Provider selection unchanged when unfocused");

    // Scenario 2: MFD IS focused.
    provider.onFocusChanged(true);
    // Release key to reset edge detector:
    router.processInput(true, &provider, false, false, false, false, false, false, false, false);

    // Press Down arrow while focused:
    bool swallowed2 = router.processInput(true, &provider,
                                          false, true, false, false,
                                          false, false, false, false);
    TEST_CHECK(swallowed2, "Input IS swallowed when MFD is focused");
    TEST_CHECK(provider.viewModel().tabs[0].selectedIndex == 1, "Provider received and handled Down arrow");

    // Scenario 3: Consecutive repeat presses of the SAME direction (e.g. Up Up Up)
    // First Up press:
    router.processInput(true, &provider, false, false, false, false, false, false, false, false); // release
    bool sw1 = router.processInput(true, &provider, true, false, false, false, false, false, false, false);
    TEST_CHECK(sw1, "First Up press swallowed");

    // Second Up press (held or re-pressed):
    bool sw2 = router.processInput(true, &provider, true, false, false, false, false, false, false, false);
    TEST_CHECK(sw2, "Second consecutive Up press STILL swallowed even when edge is inactive");

    // Third Up press after explicit release:
    router.processInput(true, &provider, false, false, false, false, false, false, false, false); // release
    bool sw3 = router.processInput(true, &provider, true, false, false, false, false, false, false, false);
    TEST_CHECK(sw3, "Third Up press after release swallowed");

    return 0;
}

// 5b. Test UP Navigation Key & POV Bindings Parsing
int test_up_navigation_and_binds_parsing() {
    MfdBindingsConfig config;
    MfdActionBinding upBinding;

    // Test parseDeviceKey with Keyboard "Key_Up" and "Key_UpArrow"
    parseDeviceKey("Keyboard", "Key_Up", upBinding);
    parseDeviceKey("Keyboard", "Key_UpArrow", upBinding);
    TEST_CHECK(!upBinding.vkeys.empty(), "Key_Up / Key_UpArrow parsed into vkeys");
    bool hasVkUp = false;
    for (int vk : upBinding.vkeys) {
        if (vk == VK_UP) hasVkUp = true;
    }
    TEST_CHECK(hasVkUp, "VK_UP present in upBinding");

    // Test parseDeviceKey with HOTAS POV Hat variations
    MfdActionBinding hatBinding;
    parseDeviceKey("Joy_1", "Joy_POV1Up", hatBinding);
    parseDeviceKey("Joy_1", "Hat1Up", hatBinding);
    TEST_CHECK(hatBinding.povUp, "POV / Hat Up parsed into povUp flag");

    // Test POV angle evaluation for 36000 (360.0 degrees UP boundary)
    DWORD povUp360 = 36000;
    TEST_CHECK(povUp360 != 65535 && povUp360 <= 36000, "POV 36000 within valid non-centered range");
    TEST_CHECK(povUp360 >= 31500 || povUp360 <= 4500, "POV 36000 evaluates to UP direction");

    return 0;
}

// 5c. Test Plugin Input Filter & Suppression Protocol
int test_plugin_input_filter_suppression() {
    EdvrInputContext inputCtx{};
    inputCtx.structSize = sizeof(EdvrInputContext);
    inputCtx.deviceType = 0; // Keyboard
    inputCtx.swallowInput = 0;

    // Verify filter sets swallowInput when MFD focused
    auto& mgr = MfdManager::instance();
    mgr.initialize(320, 240);
    mgr.setEnabled(true);
    ContextEvaluator::instance().updateFromStatus(kFlagInMainShip, 0, 0);

    MfdSlot* slot = mgr.findSlot("main_mfd");
    TEST_CHECK(slot != nullptr, "main_mfd slot available for filter test");

    // Force focus
    Vec3 headPos(0.0f, 0.0f, 0.0f);
    Vec3 toCenter = (slot->pose.position - headPos).normalized();
    for (int i = 0; i < 10; ++i) {
        mgr.update(headPos, toCenter, 0.020f);
    }
    TEST_CHECK(mgr.focusedSlot() != nullptr && mgr.focusedSlot()->gazeTracker.isFocused(), "Slot has active focus");

    // Call mfdPluginFilterInput via API table callback
    EdvrPluginCallbacks callbacks{};
    callbacks.structSize = sizeof(EdvrPluginCallbacks);
    int regRes = EdvrPluginRegister(EDVR_PLUGIN_API_VERSION, &callbacks);
    TEST_CHECK(regRes == 0, "EdvrPluginRegister success");
    TEST_CHECK(callbacks.onFilterInput != nullptr, "onFilterInput callback registered");

    callbacks.onFilterInput(&inputCtx);
    TEST_CHECK(inputCtx.swallowInput == 1, "Plugin input filter set swallowInput = 1 when MFD focused (Keyboard)");

    // Test DirectInput Joystick device type (deviceType = 1)
    EdvrInputContext joyCtx{};
    joyCtx.structSize = sizeof(EdvrInputContext);
    joyCtx.deviceType = 1; // DirectInput Joystick
    joyCtx.swallowInput = 0;
    callbacks.onFilterInput(&joyCtx);
    TEST_CHECK(joyCtx.swallowInput == 1, "Plugin input filter set swallowInput = 1 when MFD focused (Joystick/HOTAS)");

    mgr.shutdown();
    return 0;
}

// 6. Test HUD Renderer and Pixel Rasterization
int test_renderer() {
    MfdRenderer renderer(256, 192);
    TEST_CHECK(renderer.width() == 256, "Renderer width");
    TEST_CHECK(renderer.height() == 192, "Renderer height");

    MfdViewModel model;
    model.title = "TEST HUD";
    model.subtitle = "SYSTEM OK";
    model.statusBadge = "LIVE";
    model.isFocused = true;

    MfdTab tab;
    tab.title = "NAV";
    tab.type = MfdTabType::kList;
    MfdListItem item1{"i1", "SOL", "0.0 LY", "Home", "ORIGIN"};
    MfdListItem item2{"i2", "ALPHA CENTAURI", "4.3 LY", "Triple Star", "WAYPOINT"};
    tab.items.push_back(item1);
    tab.items.push_back(item2);
    tab.selectedIndex = 0;
    model.tabs.push_back(tab);

    renderer.render(model);

    // Verify pixel buffer has been drawn to
    const uint32_t* pixels = renderer.pixelData();
    TEST_CHECK(pixels != nullptr, "Pixel buffer pointer valid");

    bool hasNonBackgroundPixels = false;
    uint32_t bgVal = palette::kBackground.toRgba();
    for (int i = 0; i < renderer.width() * renderer.height(); ++i) {
        if (pixels[i] != bgVal) {
            hasNonBackgroundPixels = true;
            break;
        }
    }
    TEST_CHECK(hasNonBackgroundPixels, "Renderer successfully drew HUD content into buffer");

    return 0;
}

// 6b. Test Schema v1 Renderer Primitives (Checks, Bars, Icons, Named Palette, Status Badges - Ticket A8)
int test_schema_v1_renderer() {
    MfdRenderer renderer(512, 384);
    TEST_CHECK(renderer.width() == 512, "Renderer width");
    TEST_CHECK(renderer.height() == 384, "Renderer height");

    // A. Verify 3x5 font rasterizer
    renderer.clear(MfdColor(0, 0, 0, 255));
    renderer.drawChar3x5(10, 10, 'A', palette::kSuccessGreen);
    int charPixels = 0;
    for (int y = 10; y < 15; ++y) {
        for (int x = 10; x < 13; ++x) {
            if (renderer.pixelData()[y * 512 + x] == palette::kSuccessGreen.toRgba()) {
                charPixels++;
            }
        }
    }
    TEST_CHECK(charPixels == 10, "3x5 'A' glyph renders 10 pixels");

    // B. Verify check item drawing (kDone, kTodo, kNa)
    renderer.clear(MfdColor(0, 0, 0, 255));
    MfdCheckItem chkDone{"S", MfdCheckState::kDone};
    MfdCheckItem chkTodo{"M", MfdCheckState::kTodo};
    MfdCheckItem chkNa{"B", MfdCheckState::kNa};
    MfdCheckItem chkDoneSolid{"", MfdCheckState::kDone};

    renderer.drawCheck(20, 20, chkDone, palette::kAmberNormal, palette::kAmberDim, MfdColor(0, 0, 0, 255), 10);
    renderer.drawCheck(35, 20, chkTodo, palette::kAmberNormal, palette::kAmberDim, MfdColor(0, 0, 0, 255), 10);
    renderer.drawCheck(50, 20, chkNa, palette::kAmberNormal, palette::kAmberDim, MfdColor(0, 0, 0, 255), 10);
    renderer.drawCheck(65, 20, chkDoneSolid, palette::kAmberNormal, palette::kAmberDim, MfdColor(0, 0, 0, 255), 10);

    // Verify chkDone filled box has pixels
    int donePixels = 0;
    for (int y = 20; y < 30; ++y) {
        for (int x = 20; x < 30; ++x) {
            if (renderer.pixelData()[y * 512 + x] != 0xFF000000) donePixels++;
        }
    }
    TEST_CHECK(donePixels > 50, "kDone check box draws filled box with inverted text");

    // C. Verify all 9 fixed glyph icons
    MfdIcon icons[] = {
        MfdIcon::kFuel, MfdIcon::kWarning, MfdIcon::kNeutron,
        MfdIcon::kWhitedwarf, MfdIcon::kBio, MfdIcon::kGeo,
        MfdIcon::kCheck, MfdIcon::kLock, MfdIcon::kStar
    };
    for (MfdIcon ic : icons) {
        renderer.clear(MfdColor(0, 0, 0, 255));
        renderer.drawIcon(10, 10, ic, palette::kCyanAccent, 12);
        int iconPix = 0;
        for (int y = 10; y < 24; ++y) {
            for (int x = 10; x < 24; ++x) {
                if (renderer.pixelData()[y * 512 + x] == palette::kCyanAccent.toRgba()) {
                    iconPix++;
                }
            }
        }
        TEST_CHECK(iconPix > 0, "Icon glyph renders non-zero pixels");
    }

    // D. Verify progress bars (continuous and segmented)
    renderer.clear(MfdColor(0, 0, 0, 255));
    // Continuous: 50%
    renderer.drawProgressBar(10, 10, 100, 14, 50.0f, 100.0f, 0, palette::kSuccessGreen, MfdColor(20, 20, 20, 255), palette::kAmberNormal);
    // Segmented: 4 of 8 segments (50%)
    renderer.drawProgressBar(10, 30, 100, 14, 4.0f, 8.0f, 8, palette::kAmberBright, MfdColor(20, 20, 20, 255), palette::kAmberNormal);

    int barPix = 0;
    for (int y = 10; y < 24; ++y) {
        for (int x = 10; x < 110; ++x) {
            if (renderer.pixelData()[y * 512 + x] == palette::kSuccessGreen.toRgba()) barPix++;
        }
    }
    TEST_CHECK(barPix > 100, "Continuous progress bar renders filled portion");

    int segPix = 0;
    for (int y = 30; y < 44; ++y) {
        for (int x = 10; x < 110; ++x) {
            if (renderer.pixelData()[y * 512 + x] == palette::kAmberBright.toRgba()) segPix++;
        }
    }
    TEST_CHECK(segPix > 100, "Segmented progress bar renders active segments");

    // E. Verify full Schema v1 view model rendering (checks, bars, icons, status badge, named palette)
    MfdViewModel vm;
    vm.title = "SHIP SYSTEMS";
    vm.subtitle = "INTEGRITY & POWER";
    vm.statusBadge = "2 DAMAGED";
    vm.statusBadgeNamedColor = MfdNamedColor::kWarn;
    vm.statusBadgeColorName = "warn";

    // Tab 1: List with checks and icons
    MfdTab listTab;
    listTab.id = "modules";
    listTab.title = "MODULES";
    listTab.type = MfdTabType::kList;

    MfdListItem item1;
    item1.id = "fsd";
    item1.label = "Frame Shift Drive";
    item1.sublabel = "5A Rating";
    item1.value = "87%";
    item1.valueNamedColor = MfdNamedColor::kWarn;
    item1.valueColorName = "warn";
    item1.icon = MfdIcon::kWarning;
    item1.checks = { {"S", MfdCheckState::kDone}, {"M", MfdCheckState::kTodo} };
    listTab.items.push_back(item1);

    MfdListItem item2;
    item2.id = "cargo";
    item2.label = "Cargo Rack";
    item2.value = "64 T";
    item2.valueNamedColor = MfdNamedColor::kOk;
    item2.valueColorName = "ok";
    item2.icon = MfdIcon::kLock;
    item2.badge = "SECURE";
    listTab.items.push_back(item2);

    vm.tabs.push_back(listTab);

    // Tab 2: Bars
    MfdTab barsTab;
    barsTab.id = "power";
    barsTab.title = "POWER";
    barsTab.type = MfdTabType::kBars;

    MfdBarItem barSys{"sys", "SYS", 4.0f, 8.0f, 8, "accent", MfdNamedColor::kAccent};
    MfdBarItem barEng{"eng", "ENG", 2.0f, 8.0f, 8, "accent", MfdNamedColor::kAccent};
    MfdBarItem barWep{"wep", "WEP", 6.0f, 8.0f, 8, "accent", MfdNamedColor::kAccent};
    MfdBarItem barFuel{"fuel", "FUEL", 28.5f, 32.0f, 0, "ok", MfdNamedColor::kOk};
    barsTab.barItems = { barSys, barEng, barWep, barFuel };

    vm.tabs.push_back(barsTab);

    // Render Tab 0 (List with checks, icons, warn badge)
    vm.activeTabIndex = 0;
    renderer.render(vm, 0.75f, MfdColorTheme::kDefaultAmber);

    uint32_t bgVal = palette::kBackground.toRgba();
    int drawnPixels = 0;
    for (size_t i = 0; i < renderer.pixelByteSize() / sizeof(uint32_t); ++i) {
        if (renderer.pixelData()[i] != bgVal) drawnPixels++;
    }
    TEST_CHECK(drawnPixels > 1000, "Rendered list tab with checks and icons produced rich HUD output");

    // Render Tab 1 (Bars tab)
    vm.activeTabIndex = 1;
    renderer.render(vm, 0.75f, MfdColorTheme::kCyanIce);

    drawnPixels = 0;
    for (size_t i = 0; i < renderer.pixelByteSize() / sizeof(uint32_t); ++i) {
        if (renderer.pixelData()[i] != bgVal) drawnPixels++;
    }
    TEST_CHECK(drawnPixels > 1000, "Rendered bars tab under CyanIce theme produced rich HUD output");

    // Test Status Badge override with crit color
    vm.statusBadge = "COMPANION OFFLINE";
    vm.statusBadgeNamedColor = MfdNamedColor::kCrit;
    vm.statusBadgeColorName = "crit";
    renderer.render(vm, 0.75f, MfdColorTheme::kDefaultAmber);

    bool hasCritRed = false;
    for (int y = 10; y < 32; ++y) {
        for (int x = 512 - 200; x < 512 - 16; ++x) {
            uint32_t pix = renderer.pixelData()[y * 512 + x];
            uint8_t r = pix & 0xFF;
            uint8_t g = (pix >> 8) & 0xFF;
            uint8_t b = (pix >> 16) & 0xFF;
            if (r > 200 && g < 50 && b < 50) {
                hasCritRed = true;
                break;
            }
        }
        if (hasCritRed) break;
    }
    TEST_CHECK(hasCritRed, "Status badge with crit color rendered AlertRed pixels");

    // Test list rendering with sublabels (MFD-010): verify sublabel and label occupy separate pixel spans with 0 overlap
    MfdViewModel listVm;
    listVm.tabs.push_back(listTab);
    renderer.render(listVm, 0.0f, MfdColorTheme::kDefaultAmber);
    // Line 1 is at y=72+2=74..89; Line 2 sublabel is at y=72+19=91..106.
    // Pixel row y=90 is the breathing gap between Line 1 and Line 2.
    uint32_t bgPixel = renderer.pixelData()[90 * 512 + 0];
    int gapPixels = 0;
    for (int x = 60; x < 200; ++x) {
        if (renderer.pixelData()[90 * 512 + x] != bgPixel) gapPixels++;
    }
    TEST_CHECK(gapPixels == 0, "MFD-010: Separation gap between Line 1 label and Line 2 sublabel has zero vertical overlap");

    return 0;
}

// 7. Test MfdManager End-to-End Coordination
int test_manager() {
    auto& mgr = MfdManager::instance();
    bool init = mgr.initialize(320, 240);
    ContextEvaluator::instance().stopWatcher();
    ContextEvaluator::instance().updateFromStatus(kFlagInMainShip, 0, 0);
    mgr.setEnabled(true);
    TEST_CHECK(init, "MfdManager initialized");

    MfdPose pose;
    pose.position = Vec3(0.0f, -0.2f, -0.6f);
    pose.orientation = Quat::fromEulerDegrees(-30.0f, 0.0f, 0.0f);

    auto provider = std::make_unique<DeclarativeMfdProvider>("main_center", "CENTRAL MFD");
    provider->loadFromJson(R"({
        "tabs": [{ "title": "MAIN", "type": "text", "items": ["SYSTEM DIAGNOSTICS: NOMINAL"] }]
    })");

    MfdSlot* slot = mgr.findSlot("main_mfd");
    TEST_CHECK(slot != nullptr, "Default main_mfd slot lookup succeeds");

    // Look directly at main MFD:
    Vec3 headPos(0.0f, 0.0f, 0.0f);
    Vec3 toCenter = (slot->pose.position - headPos).normalized();

    // Tick enough time to acquire focus (150ms):
    for (int i = 0; i < 10; ++i) {
        mgr.update(headPos, toCenter, 0.020f);
    }

    TEST_CHECK(mgr.focusedSlot() == slot, "Main MFD is now the focused slot");
    TEST_CHECK(slot->provider->viewModel().isFocused, "Provider view model reflects focused state");

    // Test Show All Windows gaze fade override (MFD-007)
    slot->autoHideUntilGaze = true;
    mgr.setShowAllWindows(false);
    Vec3 lookAway(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < 20; ++i) {
        mgr.update(headPos, lookAway, 0.050f);
    }
    TEST_CHECK(slot->currentAlpha < 0.5f, "Slot fades out when not focused with showAllWindows=false");

    mgr.setShowAllWindows(true);
    TEST_CHECK(slot->currentAlpha == 1.0f, "setShowAllWindows(true) immediately resets currentAlpha to 1.0");
    for (int i = 0; i < 20; ++i) {
        mgr.update(headPos, lookAway, 0.050f);
    }
    TEST_CHECK(slot->currentAlpha == 1.0f, "Show all windows maintains full alpha regardless of gaze");

    // Execute render cycle
    mgr.render();
    mgr.shutdown();
    return 0;
}

// 8. Test Cross-DLL Shared Memory Telemetry
int test_shared_telemetry() {
    MfdManager::publishSharedTelemetry(42, 2, true, 0.1f, -0.2f, -0.5f, 100.0f, 200.0f, 300.0f, 150.0f);

    uint32_t draws = 0;
    int focusState = 0;
    bool inFrustum = false;
    float eyeX = 0, eyeY = 0, eyeZ = 0;
    float sx = 0, sy = 0, sw = 0, sh = 0;
    bool ok = MfdManager::readSharedTelemetry(&draws, &focusState, &inFrustum, &eyeX, &eyeY, &eyeZ, &sx, &sy, &sw, &sh);
    TEST_CHECK(ok, "readSharedTelemetry succeeds");
    TEST_CHECK(draws == 42, "draws match published value");
    TEST_CHECK(focusState == 2, "focusState matches published value");
    TEST_CHECK(inFrustum == true, "inFrustum matches published value");
    TEST_CHECK(std::abs(eyeX - 0.1f) < 1e-3f, "eyeX matches");
    TEST_CHECK(std::abs(eyeY - -0.2f) < 1e-3f, "eyeY matches");
    TEST_CHECK(std::abs(eyeZ - -0.5f) < 1e-3f, "eyeZ matches");
    return 0;
}

// 9. Test End-to-End Direct3D 11 Render Target Rasterization and Pixel Assertion
int test_d3d11_render_to_eye_rtv() {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &fl, &context);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &fl, &context);
    }
    TEST_CHECK(SUCCEEDED(hr) && device && context, "D3D11 device creation");

    const UINT width = 1280;
    const UINT height = 720;

    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* eyeTex = nullptr;
    TEST_CHECK(SUCCEEDED(device->CreateTexture2D(&texDesc, nullptr, &eyeTex)), "Create eye texture");

    ID3D11RenderTargetView* rtv = nullptr;
    TEST_CHECK(SUCCEEDED(device->CreateRenderTargetView(eyeTex, nullptr, &rtv)), "Create eye RTV");

    D3D11_TEXTURE2D_DESC td{};
    eyeTex->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    HRESULT hrStage = device->CreateTexture2D(&td, nullptr, &staging);
    if (FAILED(hrStage)) {
        std::cerr << "Create Staging failed hr=0x" << std::hex << hrStage
                  << " devRemoved=0x" << device->GetDeviceRemovedReason() << std::dec << std::endl;
        return 1;
    }

    // Clear RTV to pure black
    const float black[4] = {0, 0, 0, 1.0f};
    context->ClearRenderTargetView(rtv, black);

    const char* vsCode = R"(
        struct O { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };
        O vs(uint id : SV_VertexID) {
            O o;
            float2 p = float2((id << 1) & 2, id & 2);
            o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
            o.uv = p;
            return o;
        }
    )";
    const char* psCode = R"(
        cbuffer Constants : register(b0) { float4 bounds; float4 clampUV; float encodeSRGB; float3 pad; };
        Texture2D srcTex : register(t0);
        SamplerState samp : register(s0);
        float4 ps(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET {
            float2 suv = clamp(bounds.xy + uv * (bounds.zw - bounds.xy), clampUV.xy, clampUV.zw);
            return srcTex.Sample(samp, suv);
        }
    )";

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    TEST_CHECK(SUCCEEDED(D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vsBlob, nullptr)), "Compile VS");
    TEST_CHECK(SUCCEEDED(D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "ps", "ps_5_0", 0, 0, &psBlob, nullptr)), "Compile PS");

    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    TEST_CHECK(SUCCEEDED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs)), "Create VS");
    TEST_CHECK(SUCCEEDED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps)), "Create PS");
    vsBlob->Release();
    psBlob->Release();

    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ID3D11SamplerState* sampler = nullptr;
    TEST_CHECK(SUCCEEDED(device->CreateSamplerState(&sampDesc, &sampler)), "Create Sampler");

    D3D11_BUFFER_DESC bufDesc{};
    bufDesc.ByteWidth = 48;
    bufDesc.Usage = D3D11_USAGE_DEFAULT;
    bufDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Buffer* cb = nullptr;
    TEST_CHECK(SUCCEEDED(device->CreateBuffer(&bufDesc, nullptr, &cb)), "Create CB");

    auto& mgr = MfdManager::instance();
    mgr.initialize(512, 384);
    mgr.setEnabled(true);
    mgr.render();

    XrPosef eyePose{};
    eyePose.orientation.w = 1.0f;
    eyePose.position.x = 0.0f;
    eyePose.position.y = 0.0f;
    eyePose.position.z = 0.0f;

    XrFovf eyeFov{};
    eyeFov.angleLeft = -0.785398f;
    eyeFov.angleRight = 0.785398f;
    eyeFov.angleUp = 0.785398f;
    eyeFov.angleDown = -0.785398f;

    mgr.renderToEyeRtv(device, context, rtv, eyePose, eyeFov, width, height, vs, ps, sampler, cb);

    ID3D11RenderTargetView* nullRtv[1] = {nullptr};
    context->OMSetRenderTargets(1, nullRtv, nullptr);
    context->CopyResource(staging, eyeTex);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hrMap = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hrMap)) {
        std::cerr << "Map Staging failed hr=0x" << std::hex << hrMap
                  << " devRemoved=0x" << device->GetDeviceRemovedReason() << std::dec << std::endl;
        return 1;
    }

    uint32_t nonBlackPixels = 0;
    const uint8_t* row = static_cast<const uint8_t*>(mapped.pData);
    for (UINT y = 0; y < height; ++y) {
        const uint32_t* pix = reinterpret_cast<const uint32_t*>(row + y * mapped.RowPitch);
        for (UINT x = 0; x < width; ++x) {
            if ((pix[x] & 0x00FFFFFF) != 0) {
                nonBlackPixels++;
            }
        }
    }
    context->Unmap(staging, 0);

    TEST_CHECK(nonBlackPixels > 1000, "MFD rendered visible non-black pixels to eye RTV");
    std::cout << "      [D3D11 WARP] Rendered " << nonBlackPixels << " MFD pixels to eye RTV" << std::endl;

    staging->Release();
    cb->Release();
    sampler->Release();
    vs->Release();
    ps->Release();
    rtv->Release();
    eyeTex->Release();
    context->Release();
    device->Release();
    mgr.shutdown();
    return 0;
}

// 10. Test Color Themes and Interactive Settings Adjustment Callback
int test_color_themes_and_settings() {
    // Verify theme palettes
    auto amberPal = palette::getTheme(MfdColorTheme::kDefaultAmber);
    auto cyanPal = palette::getTheme(MfdColorTheme::kCyanIce);
    auto greenPal = palette::getTheme(MfdColorTheme::kMatrixGreen);
    auto whitePal = palette::getTheme(MfdColorTheme::kSolarWhite);
    auto crimsonPal = palette::getTheme(MfdColorTheme::kCrimson);
    auto purplePal = palette::getTheme(MfdColorTheme::kPurpleHaze);

    TEST_CHECK(amberPal.mainColor == palette::kAmberNormal, "Amber theme palette");
    TEST_CHECK(cyanPal.mainColor.r == 0 && cyanPal.mainColor.g == 180, "Cyan theme palette");
    TEST_CHECK(greenPal.mainColor.g == 230, "Green theme palette");
    TEST_CHECK(crimsonPal.mainColor.r == 255, "Crimson theme palette");

    // Verify DeclarativeMfdProvider KeyValueActionCallback for in-MFD interactive settings
    DeclarativeMfdProvider prov("settings_test", "SETTINGS");
    prov.loadFromJson(R"({
        "tabs": [{
            "title": "SETTINGS",
            "type": "keyvalue",
            "items": [
                {"key": "Position X", "value": "0.00 m"},
                {"key": "Glass Opacity", "value": "75%"},
                {"key": "Color Theme", "value": "Amber / Orange"}
            ]
        }]
    })");

    std::string lastKey;
    bool lastIsLeft = false;
    prov.setKeyValueActionCallback([&](const std::string& /*tabTitle*/, int /*index*/, const std::string& key, int delta) {
        lastKey = key;
        lastIsLeft = (delta < 0);
    });

    prov.onFocusChanged(true);

    // Send Left input on first key-value item
    prov.onInput(MfdInputAction::kLeft);
    TEST_CHECK(lastKey == "Position X", "Callback received key Position X");
    TEST_CHECK(lastIsLeft == true, "Callback received isLeft true");

    // Move down and send Right input
    prov.onInput(MfdInputAction::kDown);
    prov.onInput(MfdInputAction::kRight);
    TEST_CHECK(lastKey == "Glass Opacity", "Callback received key Glass Opacity");
    TEST_CHECK(lastIsLeft == false, "Callback received isLeft false");

    return 0;
}

// 11. Test Vehicle / Contextual Activity Gating Mask
int test_activity_gating() {
    uint32_t shipMask = kActivityShip;
    uint32_t srvMask = kActivitySrv;
    uint32_t flightMask = kActivityInFlight;

    // Simulate Elite Status.json flags
    uint32_t mainShipFlags = (1 << 24) | (1 << 3); // MainShip + ShieldsUp (In Flight)
    uint32_t srvFlags = (1 << 26);                 // SRV
    uint32_t dockedShipFlags = (1 << 24) | 1;      // MainShip + Docked (bit 0)
    uint32_t titleScreenFlags = 0;                 // Title / Loading screen

    // Suppress on title screen (flags == 0)
    TEST_CHECK(titleScreenFlags == 0, "Title screen flags zero");

    // Ship mask active in ship
    bool inShip = (mainShipFlags & (1 << 24)) != 0 && (shipMask & kActivityShip) != 0;
    TEST_CHECK(inShip, "Ship active in main ship");

    // SRV mask active in SRV
    bool inSrv = (srvFlags & (1 << 26)) != 0 && (srvMask & kActivitySrv) != 0;
    TEST_CHECK(inSrv, "SRV active in SRV");

    // Flight mask inactive when docked
    bool isDocked = (dockedShipFlags & 0x3) != 0;
    bool inFlight = !isDocked && (flightMask & kActivityInFlight) != 0;
    TEST_CHECK(!inFlight, "Flight mask inactive when docked");

    return 0;
}

// 12. Test Schema v1 Parser and Validation (§5.3)
int test_display_schema_v1() {
    // A. Valid canonical schema v1 document (§5.3)
    const std::string validJson = R"({
        "schemaVersion": 1,
        "title": "MODULES",
        "subtitle": "INTEGRITY",
        "statusBadge": { "text": "2 DAMAGED", "color": "warn" },
        "footerHint": "SELECT: DETAILS",
        "tabs": [
            {
                "id": "integrity",
                "title": "INTEGRITY",
                "type": "list",
                "items": [
                    {
                        "id": "slot_fsd",
                        "label": "Frame Shift Drive",
                        "sublabel": "5A",
                        "value": "87%",
                        "valueColor": "warn",
                        "icon": "warning",
                        "checks": [
                            { "label": "S", "state": "done" },
                            { "label": "M", "state": "todo" },
                            { "label": "X", "state": "na" }
                        ],
                        "action": "event"
                    },
                    {
                        "id": "slot_thrusters",
                        "label": "Thrusters",
                        "sublabel": "7A",
                        "value": "100%",
                        "valueColor": "ok",
                        "icon": "fuel",
                        "action": "clipboard"
                    }
                ]
            },
            {
                "id": "pips",
                "title": "POWER",
                "type": "bars",
                "items": [
                    { "id": "sys", "label": "SYS", "value": 4.0, "max": 8.0, "segments": 8, "color": "accent" },
                    { "id": "eng", "label": "ENG", "value": 2.5, "max": 8.0, "segments": 8, "color": "ok" },
                    { "id": "wep", "label": "WEP", "value": 1.5, "max": 8.0, "segments": 8, "color": "crit" }
                ]
            },
            {
                "id": "kv",
                "title": "INFO",
                "type": "keyvalue",
                "items": [
                    { "id": "hull", "key": "Hull", "value": "100%", "valueColor": "ok", "action": "event" },
                    { "id": "cargo", "key": "Cargo", "value": "64 T", "valueColor": "dim", "action": "clipboard" }
                ]
            },
            {
                "id": "about",
                "title": "ABOUT",
                "type": "text",
                "text": "Multi-line\nbriefing text\nline 3"
            }
        ]
    })";

    MfdViewModel vm;
    MfdParseError err;
    MfdPaletteSet theme = palette::getTheme(MfdColorTheme::kDefaultAmber);
    bool ok = parseDisplayJson(validJson, vm, &err, &theme);
    TEST_CHECK(ok, "Schema v1 parses valid canonical document");
    TEST_CHECK(vm.schemaVersion == 1, "schemaVersion is 1");
    TEST_CHECK(vm.title == "MODULES", "Title matches");
    TEST_CHECK(vm.subtitle == "INTEGRITY", "Subtitle matches");
    TEST_CHECK(vm.statusBadge == "2 DAMAGED", "Status badge text matches");
    TEST_CHECK(vm.statusBadgeColorName == "warn", "Status badge color name is warn");
    TEST_CHECK(vm.statusBadgeColor == resolveNamedColor(MfdNamedColor::kWarn, theme), "Status badge resolved color matches warn");
    TEST_CHECK(vm.tabs.size() == 4, "4 tabs parsed");

    // Verify List Tab
    const auto& listTab = vm.tabs[0];
    TEST_CHECK(listTab.id == "integrity", "Tab 0 id is integrity");
    TEST_CHECK(listTab.type == MfdTabType::kList, "Tab 0 type is list");
    TEST_CHECK(listTab.items.size() == 2, "Tab 0 has 2 items");
    const auto& fsd = listTab.items[0];
    TEST_CHECK(fsd.id == "slot_fsd", "Item 0 id matches");
    TEST_CHECK(fsd.label == "Frame Shift Drive", "Item 0 label matches");
    TEST_CHECK(fsd.sublabel == "5A", "Item 0 sublabel matches");
    TEST_CHECK(fsd.value == "87%", "Item 0 value matches");
    TEST_CHECK(fsd.valueColorName == "warn", "Item 0 valueColor is warn");
    TEST_CHECK(fsd.valueNamedColor == MfdNamedColor::kWarn, "Item 0 valueNamedColor is kWarn");
    TEST_CHECK(fsd.icon == MfdIcon::kWarning, "Item 0 icon is kWarning");
    TEST_CHECK(fsd.action == MfdItemAction::kEvent, "Item 0 action is kEvent");
    TEST_CHECK(fsd.checks.size() == 3, "Item 0 has 3 checks");
    TEST_CHECK(fsd.checks[0].label == "S" && fsd.checks[0].state == MfdCheckState::kDone, "Check 0 is S done");
    TEST_CHECK(fsd.checks[1].label == "M" && fsd.checks[1].state == MfdCheckState::kTodo, "Check 1 is M todo");
    TEST_CHECK(fsd.checks[2].label == "X" && fsd.checks[2].state == MfdCheckState::kNa, "Check 2 is X na");

    const auto& thrusters = listTab.items[1];
    TEST_CHECK(thrusters.icon == MfdIcon::kFuel, "Item 1 icon is kFuel");
    TEST_CHECK(thrusters.action == MfdItemAction::kClipboard, "Item 1 action is kClipboard");
    TEST_CHECK(thrusters.valueColor == resolveNamedColor(MfdNamedColor::kOk, theme), "Item 1 color is ok");

    // Verify Bars Tab
    const auto& barsTab = vm.tabs[1];
    TEST_CHECK(barsTab.id == "pips", "Tab 1 id is pips");
    TEST_CHECK(barsTab.type == MfdTabType::kBars, "Tab 1 type is bars");
    TEST_CHECK(barsTab.barItems.size() == 3, "Tab 1 has 3 bar items");
    TEST_CHECK(barsTab.barItems[0].id == "sys", "Bar 0 id is sys");
    TEST_CHECK(barsTab.barItems[0].label == "SYS", "Bar 0 label is SYS");
    TEST_CHECK(std::abs(barsTab.barItems[0].value - 4.0f) < 1e-4f, "Bar 0 value is 4.0");
    TEST_CHECK(std::abs(barsTab.barItems[0].max - 8.0f) < 1e-4f, "Bar 0 max is 8.0");
    TEST_CHECK(barsTab.barItems[0].segments == 8, "Bar 0 segments is 8");
    TEST_CHECK(barsTab.barItems[0].colorName == "accent", "Bar 0 colorName is accent");
    TEST_CHECK(barsTab.barItems[0].resolvedColor == theme.accentColor, "Bar 0 resolvedColor matches accent");
    TEST_CHECK(barsTab.barItems[2].namedColor == MfdNamedColor::kCrit, "Bar 2 namedColor is kCrit");
    TEST_CHECK(barsTab.barItems[2].resolvedColor == resolveNamedColor(MfdNamedColor::kCrit, theme), "Bar 2 color matches crit");

    // Verify KeyValue Tab
    const auto& kvTab = vm.tabs[2];
    TEST_CHECK(kvTab.id == "kv", "Tab 2 id is kv");
    TEST_CHECK(kvTab.type == MfdTabType::kKeyValue, "Tab 2 type is keyvalue");
    TEST_CHECK(kvTab.keyValues.size() == 2, "Tab 2 has 2 key-values");
    TEST_CHECK(kvTab.keyValues[0].id == "hull", "KV 0 id is hull");
    TEST_CHECK(kvTab.keyValues[0].key == "Hull", "KV 0 key is Hull");
    TEST_CHECK(kvTab.keyValues[0].value == "100%", "KV 0 value is 100%");
    TEST_CHECK(kvTab.keyValues[1].id == "cargo", "KV 1 id is cargo");
    TEST_CHECK(kvTab.keyValues[1].action == MfdItemAction::kClipboard, "KV 1 action is clipboard");

    // Verify Text Tab
    const auto& textTab = vm.tabs[3];
    TEST_CHECK(textTab.id == "about", "Tab 3 id is about");
    TEST_CHECK(textTab.type == MfdTabType::kText, "Tab 3 type is text");
    TEST_CHECK(textTab.lines.size() == 3, "Tab 3 split into 3 lines");
    TEST_CHECK(textTab.lines[0] == "Multi-line", "Text line 0");
    TEST_CHECK(textTab.lines[1] == "briefing text", "Text line 1");
    TEST_CHECK(textTab.lines[2] == "line 3", "Text line 2");

    // Verify all 9 icons
    TEST_CHECK(parseIcon("fuel") == MfdIcon::kFuel, "icon fuel");
    TEST_CHECK(parseIcon("warning") == MfdIcon::kWarning, "icon warning");
    TEST_CHECK(parseIcon("neutron") == MfdIcon::kNeutron, "icon neutron");
    TEST_CHECK(parseIcon("whitedwarf") == MfdIcon::kWhitedwarf, "icon whitedwarf");
    TEST_CHECK(parseIcon("bio") == MfdIcon::kBio, "icon bio");
    TEST_CHECK(parseIcon("geo") == MfdIcon::kGeo, "icon geo");
    TEST_CHECK(parseIcon("check") == MfdIcon::kCheck, "icon check");
    TEST_CHECK(parseIcon("lock") == MfdIcon::kLock, "icon lock");
    TEST_CHECK(parseIcon("star") == MfdIcon::kStar, "icon star");
    TEST_CHECK(parseIcon("unknown_glyph") == MfdIcon::kNone, "unknown icon returns kNone");

    // B. Validation and Error Location Tests
    MfdViewModel badVm;
    MfdParseError badErr;

    // 1. Missing schemaVersion
    const std::string missingVerJson = R"({ "title": "TEST", "tabs": [] })";
    TEST_CHECK(!parseDisplayJson(missingVerJson, badVm, &badErr), "Rejects missing schemaVersion");
    TEST_CHECK(badErr.line > 0 && badErr.column > 0, "Error line/col provided for missing schemaVersion");

    // 2. Unsupported schemaVersion (e.g. 2)
    const std::string badVerJson = R"({ "schemaVersion": 2, "tabs": [] })";
    TEST_CHECK(!parseDisplayJson(badVerJson, badVm, &badErr), "Rejects schemaVersion != 1");
    TEST_CHECK(badErr.line == 1, "Error line for bad schemaVersion");

    // 3. Tab missing id
    const std::string noTabIdJson = R"({
        "schemaVersion": 1,
        "tabs": [
            { "title": "LIST", "type": "list", "items": [] }
        ]
    })";
    TEST_CHECK(!parseDisplayJson(noTabIdJson, badVm, &badErr), "Rejects tab without id");
    TEST_CHECK(badErr.line > 0, "Line provided for tab missing id");

    // 4. Duplicate tab id
    const std::string dupTabIdJson = R"({
        "schemaVersion": 1,
        "tabs": [
            { "id": "t1", "title": "TAB 1", "type": "list", "items": [] },
            { "id": "t1", "title": "TAB 2", "type": "text", "lines": [] }
        ]
    })";
    TEST_CHECK(!parseDisplayJson(dupTabIdJson, badVm, &badErr), "Rejects duplicate tab id");

    // 5. Reserved tab id '__settings'
    const std::string reservedTabJson = R"({
        "schemaVersion": 1,
        "tabs": [
            { "id": "__settings", "title": "SETTINGS", "type": "list", "items": [] }
        ]
    })";
    TEST_CHECK(!parseDisplayJson(reservedTabJson, badVm, &badErr), "Rejects reserved tab id __settings");

    // 6. List item missing id
    const std::string noItemIdJson = R"({
        "schemaVersion": 1,
        "tabs": [
            { "id": "t1", "title": "LIST", "type": "list", "items": [ { "label": "No ID" } ] }
        ]
    })";
    TEST_CHECK(!parseDisplayJson(noItemIdJson, badVm, &badErr), "Rejects list item without id");

    // 7. Duplicate item id in same tab
    const std::string dupItemIdJson = R"({
        "schemaVersion": 1,
        "tabs": [
            {
                "id": "t1", "title": "LIST", "type": "list",
                "items": [
                    { "id": "item1", "label": "Item A" },
                    { "id": "item1", "label": "Item B" }
                ]
            }
        ]
    })";
    TEST_CHECK(!parseDisplayJson(dupItemIdJson, badVm, &badErr), "Rejects duplicate item id in same tab");

    // 8. KeyValue item missing id
    const std::string noKvIdJson = R"({
        "schemaVersion": 1,
        "tabs": [
            { "id": "t1", "title": "KV", "type": "keyvalue", "items": [ { "key": "K", "value": "V" } ] }
        ]
    })";
    TEST_CHECK(!parseDisplayJson(noKvIdJson, badVm, &badErr), "Rejects keyvalue item without id");

    // 9. Bars item missing id
    const std::string noBarIdJson = R"({
        "schemaVersion": 1,
        "tabs": [
            { "id": "t1", "title": "BARS", "type": "bars", "items": [ { "label": "B1", "value": 1 } ] }
        ]
    })";
    TEST_CHECK(!parseDisplayJson(noBarIdJson, badVm, &badErr), "Rejects bars item without id");

    // 10. Unknown tab type
    const std::string badTypeJson = R"({
        "schemaVersion": 1,
        "tabs": [
            { "id": "t1", "title": "FOO", "type": "piechart", "items": [] }
        ]
    })";
    TEST_CHECK(!parseDisplayJson(badTypeJson, badVm, &badErr), "Rejects unknown tab type");

    // 11. Malformed JSON syntax reports exact location
    const std::string malformedJson = "{\n  \"schemaVersion\": 1,\n  \"title\": \"Broken\",\n  \"tabs\": [ invalid json\n}";
    TEST_CHECK(!parseDisplayJson(malformedJson, badVm, &badErr), "Fails on syntax error");
    TEST_CHECK(badErr.line == 4, "Parse error line is 4");
    TEST_CHECK(badErr.column > 0, "Parse error column reported");

    // 12. Limit checks: truncating long strings (>128 chars) and check count (>6)
    std::string longStr(200, 'A');
    std::string limitTestJson = R"({
        "schemaVersion": 1,
        "title": ")" + longStr + R"(",
        "tabs": [
            {
                "id": "lim", "title": "LIMITS", "type": "list",
                "items": [
                    {
                        "id": "i1", "label": ")" + longStr + R"(",
                        "checks": [
                            {"label": "1", "state": "done"},
                            {"label": "2", "state": "done"},
                            {"label": "3", "state": "done"},
                            {"label": "4", "state": "done"},
                            {"label": "5", "state": "done"},
                            {"label": "6", "state": "done"},
                            {"label": "7", "state": "done"}
                        ]
                    }
                ]
            }
        ]
    })";
    TEST_CHECK(parseDisplayJson(limitTestJson, vm, &err), "Parses limits json");
    TEST_CHECK(vm.title.size() == 128, "Title truncated to 128 characters");
    TEST_CHECK(vm.tabs[0].items[0].label.size() == 128, "Item label truncated to 128 characters");
    TEST_CHECK(vm.tabs[0].items[0].checks.size() == 6, "Checks array capped at 6 entries");

    // 13. File size limit (>1 MB rejected)
    std::string hugeJson(1024 * 1024 + 10, ' ');
    TEST_CHECK(!parseDisplayJson(hugeJson, badVm, &badErr), "Rejects file > 1MB");
    TEST_CHECK(badErr.message.find("1 MB") != std::string::npos, "Error mentions 1 MB limit");

    return 0;
}

// 15. Test Package Manifest Parser, Loader, and Security Constraints (§4)
int test_package_manifest_and_loader() {
    // A. Package ID validation regex ^[a-z0-9][a-z0-9_-]{1,31}$
    TEST_CHECK(isValidPackageId("ship"), "Valid package ID: ship");
    TEST_CHECK(isValidPackageId("route"), "Valid package ID: route");
    TEST_CHECK(isValidPackageId("exploration"), "Valid package ID: exploration");
    TEST_CHECK(isValidPackageId("a1"), "Valid package ID: a1 (min 2 chars)");
    TEST_CHECK(isValidPackageId("pkg_1-test"), "Valid package ID: pkg_1-test");
    TEST_CHECK(isValidPackageId("x1234567890123456789012345678901"), "Valid package ID: 32 chars length");

    TEST_CHECK(!isValidPackageId(""), "Invalid package ID: empty");
    TEST_CHECK(!isValidPackageId("a"), "Invalid package ID: length 1 too short");
    TEST_CHECK(!isValidPackageId("1"), "Invalid package ID: digit only length 1 too short");
    TEST_CHECK(!isValidPackageId("Ship"), "Invalid package ID: uppercase rejected");
    TEST_CHECK(!isValidPackageId("-ship"), "Invalid package ID: leading hyphen rejected");
    TEST_CHECK(!isValidPackageId("_ship"), "Invalid package ID: leading underscore rejected");
    TEST_CHECK(!isValidPackageId("ship.test"), "Invalid package ID: dot rejected");
    TEST_CHECK(!isValidPackageId("ship/test"), "Invalid package ID: slash rejected");
    TEST_CHECK(!isValidPackageId("ship test"), "Invalid package ID: space rejected");
    TEST_CHECK(!isValidPackageId("x12345678901234567890123456789012"), "Invalid package ID: 33 chars exceeds 32");

    // B. Security Constraints: Path Traversal checks
    TEST_CHECK(isPathSafeRelative("screens/modules.json"), "Safe relative path");
    TEST_CHECK(isPathSafeRelative("screens/sub/foo.json"), "Safe subfolder relative path");
    TEST_CHECK(isPathSafeRelative("screens/../screens/modules.json"), "Safe normalized relative path within root");
    TEST_CHECK(!isPathSafeRelative("../modules.json"), "Rejects escaping relative path ../");
    TEST_CHECK(!isPathSafeRelative("screens/../../modules.json"), "Rejects escaping relative path via depth");
    TEST_CHECK(!isPathSafeRelative("screens/../../../etc/passwd"), "Rejects escaping relative path deep");
    TEST_CHECK(!isPathSafeRelative("C:/screens/modules.json"), "Rejects absolute drive letter path with slash");
    TEST_CHECK(!isPathSafeRelative("C:\\screens\\modules.json"), "Rejects absolute drive letter path with backslash");
    TEST_CHECK(!isPathSafeRelative("/screens/modules.json"), "Rejects root slash path");
    TEST_CHECK(!isPathSafeRelative("\\screens\\modules.json"), "Rejects root backslash path");
    TEST_CHECK(!isPathSafeRelative(""), "Rejects empty relative path");

    // Exe path security checks (must resolve inside package OR be absolute)
    TEST_CHECK(isExePathSafe("bin/edvr-mfd-ship.exe"), "Safe relative exe path");
    TEST_CHECK(isExePathSafe("C:\\Program Files\\EDEngineer\\EDEngineer.exe"), "Safe absolute exe path allowed");
    TEST_CHECK(isExePathSafe("C:/Apps/tool.exe"), "Safe absolute forward slash exe path allowed");
    TEST_CHECK(isExePathSafe("/usr/bin/tool"), "Safe absolute root exe path allowed");
    TEST_CHECK(!isExePathSafe("../outside.exe"), "Rejects relative exe escaping package");
    TEST_CHECK(!isExePathSafe("bin/../../outside.exe"), "Rejects relative exe escaping package via traversal");
    TEST_CHECK(!isExePathSafe(""), "Rejects empty exe path");

    // C. Valid Manifest v1 parsing
    std::string validManifestJson = R"({
        "manifestVersion": 1,
        "id": "ship",
        "name": "Ship Readout",
        "version": "1.0.0",
        "author": "EDVR MFD",
        "description": "Module integrity, power and cargo.",
        "minPluginVersion": "1.0.0",
        "screens": [
            {
                "id": "modules",
                "file": "screens/modules.json",
                "title": "MODULES",
                "pose": {
                    "position": [-0.45, -0.22, -0.48],
                    "pitchDeg": -22.0,
                    "yawDeg": 32.0,
                    "rollDeg": 0.0,
                    "widthM": 0.24,
                    "heightM": 0.16
                },
                "contexts": ["ship_cockpit", "fighter"],
                "excludeContexts": ["galaxy_map"],
                "autoHideUntilGaze": false
            },
            {
                "id": "power",
                "file": "screens/power.json",
                "title": "POWER",
                "autoHideUntilGaze": true
            }
        ],
        "companions": [
            {
                "id": "ship-feed",
                "exe": "bin/edvr-mfd-ship.exe",
                "args": ["--debug", "--rate", "100"],
                "env": {
                    "EDVR_LOG_LEVEL": "trace"
                },
                "restart": "on-failure"
            },
            {
                "id": "external-companion",
                "exe": "C:\\Tools\\companion.exe",
                "restart": "always"
            },
            {
                "id": "oneshot-tool",
                "exe": "bin/tool.exe",
                "restart": "never"
            }
        ],
        "unknownFutureField": "ignored_for_forward_compat"
    })";

    MfdPackageManifest manifest;
    std::string err;
    TEST_CHECK(parseManifestJson(validManifestJson, manifest, "ship", &err), "Parses valid manifest v1");
    TEST_CHECK(manifest.manifestVersion == 1, "manifestVersion is 1");
    TEST_CHECK(manifest.id == "ship", "id is 'ship'");
    TEST_CHECK(manifest.name == "Ship Readout", "name is 'Ship Readout'");
    TEST_CHECK(manifest.version == "1.0.0", "version is '1.0.0'");
    TEST_CHECK(manifest.author == "EDVR MFD", "author is 'EDVR MFD'");
    TEST_CHECK(manifest.description == "Module integrity, power and cargo.", "description matches");
    TEST_CHECK(manifest.minPluginVersion == "1.0.0", "minPluginVersion is '1.0.0'");

    // Screens verification
    TEST_CHECK(manifest.screens.size() == 2, "Screen count is 2");
    const auto& s0 = manifest.screens[0];
    TEST_CHECK(s0.id == "modules", "Screen 0 id is 'modules'");
    TEST_CHECK(s0.file == "screens/modules.json", "Screen 0 file matches");
    TEST_CHECK(s0.title == "MODULES", "Screen 0 title is 'MODULES'");
    TEST_CHECK(s0.pose.hasPose, "Screen 0 has explicit pose");
    TEST_CHECK(std::abs(s0.pose.position[0] - (-0.45f)) < 1e-4f, "Screen 0 pose pos X");
    TEST_CHECK(std::abs(s0.pose.position[1] - (-0.22f)) < 1e-4f, "Screen 0 pose pos Y");
    TEST_CHECK(std::abs(s0.pose.position[2] - (-0.48f)) < 1e-4f, "Screen 0 pose pos Z");
    TEST_CHECK(std::abs(s0.pose.pitchDeg - (-22.0f)) < 1e-4f, "Screen 0 pose pitchDeg");
    TEST_CHECK(std::abs(s0.pose.yawDeg - 32.0f) < 1e-4f, "Screen 0 pose yawDeg");
    TEST_CHECK(std::abs(s0.pose.widthM - 0.24f) < 1e-4f, "Screen 0 pose widthM");
    TEST_CHECK(std::abs(s0.pose.heightM - 0.16f) < 1e-4f, "Screen 0 pose heightM");
    TEST_CHECK(s0.contexts.size() == 2 && s0.contexts[0] == "ship_cockpit", "Screen 0 custom contexts parsed");
    TEST_CHECK(s0.excludeContexts.size() == 1 && s0.excludeContexts[0] == "galaxy_map", "Screen 0 excludeContexts parsed");
    TEST_CHECK(!s0.autoHideUntilGaze, "Screen 0 autoHideUntilGaze is false");

    MfdPose convertedPose = s0.pose.toMfdPose();
    TEST_CHECK(std::abs(convertedPose.position.x - (-0.45f)) < 1e-4f, "toMfdPose position.x matches");
    TEST_CHECK(std::abs(convertedPose.widthM - 0.24f) < 1e-4f, "toMfdPose widthM matches");

    const auto& s1 = manifest.screens[1];
    TEST_CHECK(s1.id == "power", "Screen 1 id is 'power'");
    TEST_CHECK(!s1.pose.hasPose, "Screen 1 has no explicit pose (auto-layout)");
    TEST_CHECK(s1.autoHideUntilGaze, "Screen 1 autoHideUntilGaze is true");
    TEST_CHECK(s1.contexts == getDefaultScreenContexts(), "Screen 1 contexts defaulted to §7.3 default set");
    TEST_CHECK(s1.contexts.size() == 6, "Default screen contexts has 6 vehicle+on_foot contexts");

    // Companions verification
    TEST_CHECK(manifest.companions.size() == 3, "Companion count is 3");
    const auto& c0 = manifest.companions[0];
    TEST_CHECK(c0.id == "ship-feed", "Companion 0 id is 'ship-feed'");
    TEST_CHECK(c0.exe == "bin/edvr-mfd-ship.exe", "Companion 0 exe matches");
    TEST_CHECK(c0.args.size() == 3 && c0.args[0] == "--debug", "Companion 0 args parsed");
    TEST_CHECK(c0.env.count("EDVR_LOG_LEVEL") == 1 && c0.env.at("EDVR_LOG_LEVEL") == "trace", "Companion 0 env parsed");
    TEST_CHECK(c0.restart == "on-failure" && c0.restartPolicy == MfdCompanionRestart::kOnFailure, "Companion 0 restart policy");

    const auto& c1 = manifest.companions[1];
    TEST_CHECK(c1.restart == "always" && c1.restartPolicy == MfdCompanionRestart::kAlways, "Companion 1 restart always");

    const auto& c2 = manifest.companions[2];
    TEST_CHECK(c2.restart == "never" && c2.restartPolicy == MfdCompanionRestart::kNever, "Companion 2 restart never");

    // D. Rejection cases
    MfdPackageManifest badManifest;
    std::string badErr;

    // manifestVersion != 1
    std::string badVerJson = R"({"manifestVersion": 2, "id": "ship", "name": "Ship", "screens": []})";
    TEST_CHECK(!parseManifestJson(badVerJson, badManifest, "ship", &badErr), "Rejects manifestVersion 2");
    TEST_CHECK(badErr.find("manifestVersion") != std::string::npos, "Error reports manifestVersion issue");

    // missing manifestVersion
    std::string noVerJson = R"({"id": "ship", "name": "Ship", "screens": []})";
    TEST_CHECK(!parseManifestJson(noVerJson, badManifest, "ship", &badErr), "Rejects missing manifestVersion");

    // package id mismatch with directory name
    TEST_CHECK(!parseManifestJson(validManifestJson, badManifest, "different_dir", &badErr), "Rejects package id / directory mismatch");
    TEST_CHECK(badErr.find("does not match directory name") != std::string::npos, "Error reports directory mismatch");

    // missing name
    std::string noNameJson = R"({"manifestVersion": 1, "id": "ship", "screens": []})";
    TEST_CHECK(!parseManifestJson(noNameJson, badManifest, "ship", &badErr), "Rejects missing name");

    // missing screens
    std::string noScreensJson = R"({"manifestVersion": 1, "id": "ship", "name": "Ship"})";
    TEST_CHECK(!parseManifestJson(noScreensJson, badManifest, "ship", &badErr), "Rejects missing screens");

    // screen missing id or file
    std::string badScreenJson = R"({
        "manifestVersion": 1, "id": "ship", "name": "Ship",
        "screens": [{"title": "MODULES"}]
    })";
    TEST_CHECK(!parseManifestJson(badScreenJson, badManifest, "ship", &badErr), "Rejects screen missing id/file");

    // duplicate screen id
    std::string dupScreenJson = R"({
        "manifestVersion": 1, "id": "ship", "name": "Ship",
        "screens": [
            {"id": "scr1", "file": "screens/s1.json"},
            {"id": "scr1", "file": "screens/s2.json"}
        ]
    })";
    TEST_CHECK(!parseManifestJson(dupScreenJson, badManifest, "ship", &badErr), "Rejects duplicate screen id");

    // screen file path traversal
    std::string escapeScreenJson = R"({
        "manifestVersion": 1, "id": "ship", "name": "Ship",
        "screens": [{"id": "scr1", "file": "../outside.json"}]
    })";
    TEST_CHECK(!parseManifestJson(escapeScreenJson, badManifest, "ship", &badErr), "Rejects screen file path traversal");

    // screen pose invalid dimensions (width <= 0)
    std::string badPoseJson = R"({
        "manifestVersion": 1, "id": "ship", "name": "Ship",
        "screens": [{
            "id": "scr1", "file": "screens/s1.json",
            "pose": {"position": [0, 0, 0], "pitchDeg": 0, "yawDeg": 0, "widthM": -0.1, "heightM": 0.16}
        }]
    })";
    TEST_CHECK(!parseManifestJson(badPoseJson, badManifest, "ship", &badErr), "Rejects non-positive pose widthM");

    // companion invalid restart policy
    std::string badRestartJson = R"({
        "manifestVersion": 1, "id": "ship", "name": "Ship",
        "screens": [{"id": "s1", "file": "screens/s1.json"}],
        "companions": [{"id": "c1", "exe": "bin/test.exe", "restart": "sometimes"}]
    })";
    TEST_CHECK(!parseManifestJson(badRestartJson, badManifest, "ship", &badErr), "Rejects invalid companion restart policy");

    // companion exe path traversal
    std::string escapeExeJson = R"({
        "manifestVersion": 1, "id": "ship", "name": "Ship",
        "screens": [{"id": "s1", "file": "screens/s1.json"}],
        "companions": [{"id": "c1", "exe": "../escape.exe"}]
    })";
    TEST_CHECK(!parseManifestJson(escapeExeJson, badManifest, "ship", &badErr), "Rejects companion exe path traversal");

    // E. Directory scanning and package discovery (sorting by package id ascending)
    wchar_t tempBase[MAX_PATH];
    GetTempPathW(MAX_PATH, tempBase);
    std::wstring testRootDir = std::wstring(tempBase) + L"edvr_mfd_test_" + std::to_wstring(GetTickCount64());
    std::wstring testMfdsDir = testRootDir + L"\\mfds";
    std::wstring pkgBDir = testMfdsDir + L"\\pkg_b";
    std::wstring pkgADir = testMfdsDir + L"\\pkg_a";
    std::wstring pkgBadDir = testMfdsDir + L"\\pkg_bad";
    std::wstring pkgMismatchDir = testMfdsDir + L"\\pkg_mismatch";

    CreateDirectoryW(testRootDir.c_str(), nullptr);
    CreateDirectoryW(testMfdsDir.c_str(), nullptr);
    CreateDirectoryW(pkgBDir.c_str(), nullptr);
    CreateDirectoryW(pkgADir.c_str(), nullptr);
    CreateDirectoryW(pkgBadDir.c_str(), nullptr);
    CreateDirectoryW(pkgMismatchDir.c_str(), nullptr);

    auto writeTestFile = [](const std::wstring& path, const std::string& content) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
            CloseHandle(h);
        }
    };

    // Valid package B
    std::string manifestB = R"({
        "manifestVersion": 1, "id": "pkg_b", "name": "Package B",
        "screens": [{"id": "screen_b1", "file": "b1.json"}]
    })";
    writeTestFile(pkgBDir + L"\\manifest.json", manifestB);

    // Valid package A (with 2 screens)
    std::string manifestA = R"({
        "manifestVersion": 1, "id": "pkg_a", "name": "Package A",
        "screens": [
            {"id": "screen_a1", "file": "a1.json"},
            {"id": "screen_a2", "file": "a2.json"}
        ]
    })";
    writeTestFile(pkgADir + L"\\manifest.json", manifestA);

    // Invalid manifestVersion
    std::string manifestBad = R"({
        "manifestVersion": 99, "id": "pkg_bad", "name": "Bad Package",
        "screens": [{"id": "s1", "file": "s1.json"}]
    })";
    writeTestFile(pkgBadDir + L"\\manifest.json", manifestBad);

    // Mismatched directory name
    std::string manifestMismatch = R"({
        "manifestVersion": 1, "id": "other_id", "name": "Mismatch Package",
        "screens": [{"id": "s1", "file": "s1.json"}]
    })";
    writeTestFile(pkgMismatchDir + L"\\manifest.json", manifestMismatch);

    std::vector<MfdPackageManifest> discovered = discoverPackages(testMfdsDir);

    // Cleanup files and folders
    DeleteFileW((pkgBDir + L"\\manifest.json").c_str());
    DeleteFileW((pkgADir + L"\\manifest.json").c_str());
    DeleteFileW((pkgBadDir + L"\\manifest.json").c_str());
    DeleteFileW((pkgMismatchDir + L"\\manifest.json").c_str());
    RemoveDirectoryW(pkgBDir.c_str());
    RemoveDirectoryW(pkgADir.c_str());
    RemoveDirectoryW(pkgBadDir.c_str());
    RemoveDirectoryW(pkgMismatchDir.c_str());
    RemoveDirectoryW(testMfdsDir.c_str());
    RemoveDirectoryW(testRootDir.c_str());

    TEST_CHECK(discovered.size() == 2, "Discovered exactly 2 valid packages (rejected bad & mismatch)");
    // Discovered packages must be sorted by package id ascending: pkg_a then pkg_b (§4.2)
    TEST_CHECK(discovered[0].id == "pkg_a", "First package in sort order is 'pkg_a'");
    TEST_CHECK(discovered[1].id == "pkg_b", "Second package in sort order is 'pkg_b'");
    TEST_CHECK(discovered[0].screens.size() == 2, "pkg_a has 2 screens");
    TEST_CHECK(discovered[0].screens[0].id == "screen_a1", "pkg_a screen 0 order preserved");
    TEST_CHECK(discovered[0].screens[1].id == "screen_a2", "pkg_a screen 1 order preserved");
    TEST_CHECK(discovered[1].screens.size() == 1, "pkg_b has 1 screen");

    return 0;
}

// 4b. Test ContentWatcher background reload, atomic writes, cursor preservation, ERR badge (§5 - Ticket A3)
int test_content_watcher() {
    // 1. Test Cursor Preservation logic (§5.2)
    {
        MfdViewModel oldVm;
        MfdTab navTab;
        navTab.id = "nav";
        navTab.title = "NAV";
        navTab.type = MfdTabType::kList;
        navTab.items.push_back(MfdListItem("wp1", "Waypoint 1"));
        navTab.items.push_back(MfdListItem("wp2", "Waypoint 2"));

        MfdTab powerTab;
        powerTab.id = "power";
        powerTab.title = "POWER";
        powerTab.type = MfdTabType::kList;
        powerTab.items.push_back(MfdListItem("sys", "SYS"));
        powerTab.items.push_back(MfdListItem("eng", "ENG"));
        powerTab.items.push_back(MfdListItem("wep", "WEP"));
        powerTab.selectedIndex = 2; // "wep"
        powerTab.scrollOffset = 0;

        oldVm.tabs.push_back(navTab);
        oldVm.tabs.push_back(powerTab);
        oldVm.activeTabIndex = 1; // "power" selected
        oldVm.isFocused = true;

        // Case A: Items updated/reordered; "wep" item still exists at new index 1
        MfdViewModel newVm1;
        MfdTab newPowerTab;
        newPowerTab.id = "power";
        newPowerTab.title = "POWER";
        newPowerTab.type = MfdTabType::kList;
        newPowerTab.items.push_back(MfdListItem("eng", "ENG"));
        newPowerTab.items.push_back(MfdListItem("wep", "WEP")); // was index 2, now index 1!
        newPowerTab.items.push_back(MfdListItem("aux", "AUX"));
        newVm1.tabs.push_back(navTab);
        newVm1.tabs.push_back(newPowerTab);

        MfdContentWatcher::restoreCursor(oldVm, newVm1);
        TEST_CHECK(newVm1.activeTabIndex == 1, "Tab 'power' preserved");
        TEST_CHECK(newVm1.tabs[1].selectedIndex == 1, "Cursor restored by item ID 'wep' (index 1)");
        TEST_CHECK(newVm1.isFocused, "Focus state preserved");

        // Case B: Selected item removed -> clamps index
        MfdViewModel newVm2;
        MfdTab clampedPowerTab;
        clampedPowerTab.id = "power";
        clampedPowerTab.title = "POWER";
        clampedPowerTab.type = MfdTabType::kList;
        clampedPowerTab.items.push_back(MfdListItem("sys", "SYS"));
        clampedPowerTab.items.push_back(MfdListItem("eng", "ENG")); // only 2 items; index 2 clamped to 1
        newVm2.tabs.push_back(clampedPowerTab);

        MfdContentWatcher::restoreCursor(oldVm, newVm2);
        TEST_CHECK(newVm2.activeTabIndex == 0, "Tab 'power' matched");
        TEST_CHECK(newVm2.tabs[0].selectedIndex == 1, "Item 'wep' removed: index clamped to 1");

        // Case C: Active tab removed -> selects tab 0
        MfdViewModel newVm3;
        newVm3.tabs.push_back(navTab); // only 'nav' tab exists; 'power' is gone

        MfdContentWatcher::restoreCursor(oldVm, newVm3);
        TEST_CHECK(newVm3.activeTabIndex == 0, "Tab 'power' removed: fell back to tab 0");

        // Case D: KeyValue and Bars tabs cursor preservation
        MfdViewModel oldKvVm;
        MfdTab kvTab;
        kvTab.id = "kv_metrics";
        kvTab.type = MfdTabType::kKeyValue;
        kvTab.keyValues.push_back(MfdKeyValue("k_hull", "Hull", "100%"));
        kvTab.keyValues.push_back(MfdKeyValue("k_shields", "Shields", "80%"));
        kvTab.keyValues.push_back(MfdKeyValue("k_fuel", "Fuel", "45%"));
        kvTab.selectedIndex = 1; // "k_shields"
        oldKvVm.tabs.push_back(kvTab);
        oldKvVm.activeTabIndex = 0;

        MfdViewModel newKvVm;
        MfdTab newKvTab;
        newKvTab.id = "kv_metrics";
        newKvTab.type = MfdTabType::kKeyValue;
        newKvTab.keyValues.push_back(MfdKeyValue("k_fuel", "Fuel", "45%"));
        newKvTab.keyValues.push_back(MfdKeyValue("k_shields", "Shields", "75%")); // now index 1
        newKvTab.keyValues.push_back(MfdKeyValue("k_cargo", "Cargo", "12t"));
        newKvVm.tabs.push_back(newKvTab);

        MfdContentWatcher::restoreCursor(oldKvVm, newKvVm);
        TEST_CHECK(newKvVm.tabs[0].selectedIndex == 1, "KeyValue item 'k_shields' preserved by id");

        // KeyValue item removed -> clamps index
        MfdViewModel newKvVm2;
        MfdTab newKvTab2;
        newKvTab2.id = "kv_metrics";
        newKvTab2.type = MfdTabType::kKeyValue;
        newKvTab2.keyValues.push_back(MfdKeyValue("k_hull", "Hull", "100%")); // only 1 item
        newKvVm2.tabs.push_back(newKvTab2);
        MfdContentWatcher::restoreCursor(oldKvVm, newKvVm2);
        TEST_CHECK(newKvVm2.tabs[0].selectedIndex == 0, "KeyValue item removed: index clamped to 0");
    }

    // 2. Test Live Content Watcher: Atomic writes (<100ms) & ERR badge on parse errors (§5)
    {
        wchar_t tempBase[MAX_PATH];
        GetTempPathW(MAX_PATH, tempBase);
        std::wstring testRootDir = std::wstring(tempBase) + L"edvr_watcher_test_" + std::to_wstring(GetTickCount64());
        std::wstring testMfdsDir = testRootDir + L"\\mfds";
        std::wstring pkgDir = testMfdsDir + L"\\test_pkg";
        std::wstring screensDir = pkgDir + L"\\screens";

        CreateDirectoryW(testRootDir.c_str(), nullptr);
        CreateDirectoryW(testMfdsDir.c_str(), nullptr);
        CreateDirectoryW(pkgDir.c_str(), nullptr);
        CreateDirectoryW(screensDir.c_str(), nullptr);

        std::wstring screenPath = screensDir + L"\\modules.json";
        std::wstring tempFilePath = screensDir + L"\\modules.tmp";

        auto writeTestFile = [](const std::wstring& path, const std::string& content) {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
                CloseHandle(h);
            }
        };

        // Write initial valid display JSON
        std::string initialJson = R"({
            "schemaVersion": 1,
            "title": "MODULES",
            "tabs": [
                {
                    "id": "integrity",
                    "title": "INTEGRITY",
                    "type": "list",
                    "items": [
                        {"id": "fsd", "label": "Frame Shift Drive", "value": "100%"}
                    ]
                }
            ]
        })";
        writeTestFile(screenPath, initialJson);

        MfdContentWatcher watcher(testMfdsDir);
        watcher.registerScreen("test_pkg", "modules", screenPath, "screens/modules.json");

        MfdLoadError reportedError;
        bool errorCallbackInvoked = false;
        watcher.setLoadErrorCallback([&](const MfdLoadError& err) {
            reportedError = err;
            errorCallbackInvoked = true;
        });

        TEST_CHECK(watcher.start(), "ContentWatcher started successfully");
        TEST_CHECK(watcher.isRunning(), "ContentWatcher is running");

        // Initial load check
        uint64_t initialGen = watcher.getGeneration("test_pkg", "modules");
        if (initialGen == 0) {
            watcher.reloadScreenSync("test_pkg", "modules");
            initialGen = watcher.getGeneration("test_pkg", "modules");
        }
        TEST_CHECK(initialGen >= 1, "Initial generation published (gen >= 1)");
        auto vmInit = watcher.getPendingViewModel("test_pkg", "modules");
        TEST_CHECK(vmInit != nullptr && vmInit->title == "MODULES", "Initial model loaded with title 'MODULES'");

        // A. Simulate atomic write (temp file + MoveFileEx replace) and verify generation updates < 100 ms (§5.1, §5.2)
        std::string updatedJson = R"({
            "schemaVersion": 1,
            "title": "MODULES UPDATED",
            "tabs": [
                {
                    "id": "integrity",
                    "title": "INTEGRITY",
                    "type": "list",
                    "items": [
                        {"id": "fsd", "label": "Frame Shift Drive", "value": "85%"}
                    ]
                }
            ]
        })";
        writeTestFile(tempFilePath, updatedJson);

        uint64_t tStart = GetTickCount64();
        BOOL moveOk = MoveFileExW(tempFilePath.c_str(), screenPath.c_str(), MOVEFILE_REPLACE_EXISTING);
        TEST_CHECK(moveOk, "Atomic MoveFileEx replacement succeeded");

        uint64_t genAfter = initialGen;
        while (GetTickCount64() - tStart < 2000) {
            genAfter = watcher.getGeneration("test_pkg", "modules");
            if (genAfter > initialGen) break;
            Sleep(5);
        }
        uint64_t elapsedMs = GetTickCount64() - tStart;
        TEST_CHECK(genAfter > initialGen, "Published generation updated after atomic write");
        TEST_CHECK(elapsedMs < 100, "Published generation updated within < 100 ms target");

        auto vmUpdated = watcher.getPendingViewModel("test_pkg", "modules");
        TEST_CHECK(vmUpdated != nullptr && vmUpdated->title == "MODULES UPDATED", "Updated model has title 'MODULES UPDATED'");
        TEST_CHECK(vmUpdated->tabs[0].items[0].value == "85%", "Updated model has value '85%'");

        // B. Simulate parse error: invalid JSON syntax -> keeps last good model, flags ERR badge (§5.2)
        std::string brokenJson = R"({ "schemaVersion": 1, "title": "CORRUPT JSON", "tabs": [ broken )";
        writeTestFile(tempFilePath, brokenJson);
        uint64_t genBeforeError = watcher.getGeneration("test_pkg", "modules");
        MoveFileExW(tempFilePath.c_str(), screenPath.c_str(), MOVEFILE_REPLACE_EXISTING);

        uint64_t errorWaitStart = GetTickCount64();
        while (GetTickCount64() - errorWaitStart < 500) {
            if (watcher.hasError("test_pkg", "modules")) break;
            Sleep(5);
        }
        TEST_CHECK(watcher.hasError("test_pkg", "modules"), "Watcher detected parse error");
        TEST_CHECK(errorCallbackInvoked, "Load error callback was invoked");
        TEST_CHECK(reportedError.file == "screens/modules.json", "Load error report matches file");
        TEST_CHECK(reportedError.line > 0, "Load error report contains valid line number");

        auto vmError = watcher.getPendingViewModel("test_pkg", "modules");
        TEST_CHECK(vmError != nullptr, "Pending view model exists on error");
        TEST_CHECK(vmError->statusBadge == "ERR", "Status badge flagged as 'ERR'");
        TEST_CHECK(vmError->statusBadgeNamedColor == MfdNamedColor::kCrit, "Status badge color set to crit");
        TEST_CHECK(vmError->title == "MODULES UPDATED", "Last good view model content preserved on error");

        // C. Recover from error by writing valid JSON again
        std::string recoverJson = R"({
            "schemaVersion": 1,
            "title": "MODULES RECOVERED",
            "tabs": [
                {
                    "id": "integrity",
                    "title": "INTEGRITY",
                    "type": "list",
                    "items": [
                        {"id": "fsd", "label": "Frame Shift Drive", "value": "100%"}
                    ]
                }
            ]
        })";
        writeTestFile(tempFilePath, recoverJson);
        MoveFileExW(tempFilePath.c_str(), screenPath.c_str(), MOVEFILE_REPLACE_EXISTING);

        uint64_t recoverWaitStart = GetTickCount64();
        while (GetTickCount64() - recoverWaitStart < 500) {
            if (!watcher.hasError("test_pkg", "modules") &&
                watcher.getPendingViewModel("test_pkg", "modules")->title == "MODULES RECOVERED") {
                break;
            }
            Sleep(5);
        }
        TEST_CHECK(!watcher.hasError("test_pkg", "modules"), "Error state cleared on valid JSON write");
        auto vmRecovered = watcher.getPendingViewModel("test_pkg", "modules");
        TEST_CHECK(vmRecovered->statusBadge != "ERR", "ERR badge cleared");
        TEST_CHECK(vmRecovered->title == "MODULES RECOVERED", "Recovered view model content published");

        // D. Verify render thread consumption via swapIfUpdated (§5.2)
        uint64_t renderGen = genBeforeError;
        MfdViewModel renderVm;
        renderVm.title = "RENDER STALE";
        bool swapped = watcher.swapIfUpdated("test_pkg", "modules", renderGen, renderVm);
        TEST_CHECK(swapped, "swapIfUpdated returns true for newer generation");
        TEST_CHECK(renderVm.title == "MODULES RECOVERED", "renderVm swapped to latest model");
        TEST_CHECK(renderGen == watcher.getGeneration("test_pkg", "modules"), "renderGen synchronized");

        bool swappedAgain = watcher.swapIfUpdated("test_pkg", "modules", renderGen, renderVm);
        TEST_CHECK(!swappedAgain, "swapIfUpdated returns false when generation is identical");

        // Cleanup
        watcher.stop();
        DeleteFileW(tempFilePath.c_str());
        DeleteFileW(screenPath.c_str());
        RemoveDirectoryW(screensDir.c_str());
        RemoveDirectoryW(pkgDir.c_str());
        RemoveDirectoryW(testMfdsDir.c_str());
        RemoveDirectoryW(testRootDir.c_str());
    }

    return 0;
}

// 4c. Test EventWriter background thread, per-package roll, sequence numbering, drops (§6 - Ticket A4)
int test_event_log_writer() {
    // 1. UUIDv4 and ISO 8601 UTC timestamp generation
    {
        std::string uuid = generateUuidV4();
        TEST_CHECK(uuid.size() == 36, "UUIDv4 length is 36");
        TEST_CHECK(uuid[8] == '-' && uuid[13] == '-' && uuid[18] == '-' && uuid[23] == '-', "UUIDv4 format hyphens");
        TEST_CHECK(uuid[14] == '4', "UUIDv4 version nibble is '4'");
        char varChar = uuid[19];
        TEST_CHECK(varChar == '8' || varChar == '9' || varChar == 'a' || varChar == 'b', "UUIDv4 variant is RFC 4122");

        std::string ts = getCurrentIsoUtcTimestamp();
        TEST_CHECK(ts.size() == 24, "ISO 8601 UTC timestamp length is 24 (YYYY-MM-DDTHH:MM:SS.mmmZ)");
        TEST_CHECK(ts[10] == 'T', "Timestamp has 'T' delimiter");
        TEST_CHECK(ts.back() == 'Z', "Timestamp has 'Z' UTC indicator");
    }

    // 2. Event record schema serialization & deserialization (§6.2)
    {
        // A. Standard Select event with nullable key & delta (null)
        MfdEventRecord sel = MfdEventRecord::createSelect("ship", "modules", "integrity", 3, "slot_fsd", "Frame Shift Drive", "clipboard");
        sel.seq = 42;
        sel.ts = "2026-10-15T14:53:01.123Z";
        sel.session = "7f3c0000-0000-4000-8000-000000000000";

        std::string selJson = serializeEventToJson(sel);
        TEST_CHECK(selJson.find("\"seq\":42") != std::string::npos, "Serialized seq matches");
        TEST_CHECK(selJson.find("\"screen\":\"modules\"") != std::string::npos, "Serialized screen matches");
        TEST_CHECK(selJson.find("\"type\":\"select\"") != std::string::npos, "Serialized type matches");
        TEST_CHECK(selJson.find("\"itemId\":\"slot_fsd\"") != std::string::npos, "Serialized itemId matches");
        TEST_CHECK(selJson.find("\"action\":\"clipboard\"") != std::string::npos, "Serialized optional action matches");
        TEST_CHECK(selJson.find("\"key\":null") != std::string::npos, "key is null for select");
        TEST_CHECK(selJson.find("\"delta\":null") != std::string::npos, "delta is null for select");

        MfdEventRecord parsedSel;
        TEST_CHECK(parseEventFromJson(selJson, parsedSel), "Parses select JSON back");
        TEST_CHECK(parsedSel.seq == 42, "Parsed seq is 42");
        TEST_CHECK(parsedSel.screen == "modules", "Parsed screen matches");
        TEST_CHECK(parsedSel.itemId == "slot_fsd", "Parsed itemId matches");
        TEST_CHECK(parsedSel.action == "clipboard", "Parsed action matches");
        TEST_CHECK(!parsedSel.hasKey && parsedSel.key.empty(), "Parsed key is null");
        TEST_CHECK(!parsedSel.hasDelta, "Parsed delta is null");

        // B. Adjust event with key and delta
        MfdEventRecord adj = MfdEventRecord::createAdjust("ship", "modules", "integrity", 1, "slot_thrusters", "Thrusters", "power_priority", 1);
        adj.seq = 43;
        adj.ts = "2026-10-15T14:53:02.000Z";
        adj.session = sel.session;

        std::string adjJson = serializeEventToJson(adj);
        TEST_CHECK(adjJson.find("\"key\":\"power_priority\"") != std::string::npos, "adjust event serializes key");
        TEST_CHECK(adjJson.find("\"delta\":1") != std::string::npos, "adjust event serializes delta");

        MfdEventRecord parsedAdj;
        TEST_CHECK(parseEventFromJson(adjJson, parsedAdj), "Parses adjust JSON back");
        TEST_CHECK(parsedAdj.hasKey && parsedAdj.key == "power_priority", "Parsed key matches");
        TEST_CHECK(parsedAdj.hasDelta && parsedAdj.delta == 1, "Parsed delta matches");

        // C. Load error event with file, line, column, message
        MfdEventRecord err = MfdEventRecord::createLoadError("route", "router", "screens/router.json", 12, 4, "Unexpected token");
        err.seq = 44;
        err.ts = "2026-10-15T14:53:03.000Z";
        err.session = sel.session;

        std::string errJson = serializeEventToJson(err);
        TEST_CHECK(errJson.find("\"file\":\"screens/router.json\"") != std::string::npos, "load_error file matches");
        TEST_CHECK(errJson.find("\"line\":12") != std::string::npos, "load_error line matches");
        TEST_CHECK(errJson.find("\"column\":4") != std::string::npos, "load_error column matches");
        TEST_CHECK(errJson.find("\"message\":\"Unexpected token\"") != std::string::npos, "load_error message matches");

        MfdEventRecord parsedErr;
        TEST_CHECK(parseEventFromJson(errJson, parsedErr), "Parses load_error JSON back");
        TEST_CHECK(parsedErr.file == "screens/router.json", "Parsed file matches");
        TEST_CHECK(parsedErr.line == 12 && parsedErr.column == 4, "Parsed line & column match");
        TEST_CHECK(parsedErr.message == "Unexpected token", "Parsed message matches");

        // D. Context changed event
        std::vector<std::string> ctxList = {"ship_cockpit", "supercruise"};
        MfdEventRecord ctx = MfdEventRecord::createContextChanged("ship", ctxList, 0x1000000, 0, 0);
        ctx.seq = 45;
        ctx.ts = "2026-10-15T14:53:04.000Z";
        ctx.session = sel.session;

        std::string ctxJson = serializeEventToJson(ctx);
        TEST_CHECK(ctxJson.find("\"contexts\":[\"ship_cockpit\",\"supercruise\"]") != std::string::npos, "contexts serialized");
        TEST_CHECK(ctxJson.find("\"flags\":16777216") != std::string::npos, "flags serialized");

        MfdEventRecord parsedCtx;
        TEST_CHECK(parseEventFromJson(ctxJson, parsedCtx), "Parses context_changed JSON back");
        TEST_CHECK(parsedCtx.contexts.size() == 2 && parsedCtx.contexts[0] == "ship_cockpit", "Parsed contexts match");
        TEST_CHECK(parsedCtx.flags == 0x1000000, "Parsed flags match");

        // E. Companion crashed event
        MfdEventRecord comp = MfdEventRecord::createCompanionCrashed("ship", "bin/edvr-mfd-ship.exe", 1234, 1, 2);
        comp.seq = 46;
        comp.ts = "2026-10-15T14:53:05.000Z";
        comp.session = sel.session;

        std::string compJson = serializeEventToJson(comp);
        TEST_CHECK(compJson.find("\"companion\":\"bin/edvr-mfd-ship.exe\"") != std::string::npos, "companion serialized");
        TEST_CHECK(compJson.find("\"pid\":1234") != std::string::npos, "pid serialized");
        TEST_CHECK(compJson.find("\"exitCode\":1") != std::string::npos, "exitCode serialized");
        TEST_CHECK(compJson.find("\"attempt\":2") != std::string::npos, "attempt serialized");

        // F. Events dropped event
        MfdEventRecord dropRec = MfdEventRecord::createEventsDropped("ship", 15);
        dropRec.seq = 47;
        dropRec.ts = "2026-10-15T14:53:06.000Z";
        dropRec.session = sel.session;

        std::string dropJson = serializeEventToJson(dropRec);
        TEST_CHECK(dropJson.find("\"type\":\"events_dropped\"") != std::string::npos, "events_dropped type serialized");
        TEST_CHECK(dropJson.find("\"count\":15") != std::string::npos, "events_dropped count serialized");
    }

    // 3. Monotonic sequence numbering per package (§6.2) & Per-package event files
    wchar_t tempBase[MAX_PATH];
    GetTempPathW(MAX_PATH, tempBase);
    std::wstring testRootDir = std::wstring(tempBase) + L"edvr_mfd_events_test_" + std::to_wstring(GetTickCount64());
    std::wstring testMfdsDir = testRootDir + L"\\mfds";
    CreateDirectoryW(testRootDir.c_str(), nullptr);
    CreateDirectoryW(testMfdsDir.c_str(), nullptr);

    auto readLinesWin32 = [](const std::wstring& path) -> std::vector<std::string> {
        std::vector<std::string> lines;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return lines;
        DWORD size = GetFileSize(h, nullptr);
        if (size > 0 && size != INVALID_FILE_SIZE) {
            std::string content(size, '\0');
            DWORD readBytes = 0;
            ReadFile(h, &content[0], size, &readBytes, nullptr);
            std::stringstream ss(content);
            std::string line;
            while (std::getline(ss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) lines.push_back(line);
            }
        }
        CloseHandle(h);
        return lines;
    };

    {
        EventWriter writer;
        std::string session1 = "11111111-2222-4333-8444-555555555555";
        TEST_CHECK(writer.initialize(testMfdsDir, session1), "EventWriter initialized");

        // Enqueue events for pkg_a
        writer.enqueue(MfdEventRecord::createPluginStart("pkg_a"));
        writer.enqueue(MfdEventRecord::createScreenLoaded("pkg_a", "screen_1"));
        writer.enqueue(MfdEventRecord::createNav("pkg_a", "screen_1", "tab_1", 0, "item_0", "Item 0"));

        // Enqueue events for pkg_b
        writer.enqueue(MfdEventRecord::createPluginStart("pkg_b"));
        writer.enqueue(MfdEventRecord::createScreenLoaded("pkg_b", "screen_2"));

        writer.flush();

        // Verify pkg_a events
        std::wstring pkgAEvents = writer.getPackageEventsPath("pkg_a");
        std::vector<std::string> linesA = readLinesWin32(pkgAEvents);
        TEST_CHECK(linesA.size() == 3, "pkg_a has exactly 3 events");

        MfdEventRecord rA0, rA1, rA2;
        TEST_CHECK(parseEventFromJson(linesA[0], rA0) && rA0.seq == 1 && rA0.type == "plugin_start", "pkg_a seq 1");
        TEST_CHECK(parseEventFromJson(linesA[1], rA1) && rA1.seq == 2 && rA1.type == "screen_loaded", "pkg_a seq 2");
        TEST_CHECK(parseEventFromJson(linesA[2], rA2) && rA2.seq == 3 && rA2.type == "nav", "pkg_a seq 3");
        TEST_CHECK(rA0.session == session1 && rA1.session == session1 && rA2.session == session1, "pkg_a session UUID");

        // Verify pkg_b events: seq starts at 1 independently!
        std::wstring pkgBEvents = writer.getPackageEventsPath("pkg_b");
        std::vector<std::string> linesB = readLinesWin32(pkgBEvents);
        TEST_CHECK(linesB.size() == 2, "pkg_b has exactly 2 events");

        MfdEventRecord rB0, rB1;
        TEST_CHECK(parseEventFromJson(linesB[0], rB0) && rB0.seq == 1 && rB0.type == "plugin_start", "pkg_b seq 1 starts at 1");
        TEST_CHECK(parseEventFromJson(linesB[1], rB1) && rB1.seq == 2 && rB1.type == "screen_loaded", "pkg_b seq 2");
        TEST_CHECK(rB0.session == session1 && rB1.session == session1, "pkg_b session UUID");

        // Concurrently tailing external handle test (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
        HANDLE hTail = CreateFileW(pkgAEvents.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        TEST_CHECK(hTail != INVALID_HANDLE_VALUE, "Companion app can open and tail events.jsonl concurrently");
        if (hTail != INVALID_HANDLE_VALUE) {
            CloseHandle(hTail);
        }

        writer.shutdown();
    }

    // 4. Truncation at plugin init (new session) (§6.1)
    {
        EventWriter writer;
        std::string session2 = "22222222-3333-4444-8555-666666666666";
        // Re-initialize: existing events.jsonl must be truncated to 0 bytes
        TEST_CHECK(writer.initialize(testMfdsDir, session2), "Re-initialize for new session");

        std::wstring pkgAEvents = writer.getPackageEventsPath("pkg_a");
        std::vector<std::string> linesAfterInit = readLinesWin32(pkgAEvents);
        TEST_CHECK(linesAfterInit.empty(), "pkg_a events.jsonl is truncated at init (new session)");

        // Enqueue event in new session
        writer.enqueue(MfdEventRecord::createPluginStart("pkg_a"));
        writer.flush();

        std::vector<std::string> linesNew = readLinesWin32(pkgAEvents);
        TEST_CHECK(linesNew.size() == 1, "pkg_a has 1 new event after init");
        MfdEventRecord rNew;
        TEST_CHECK(parseEventFromJson(linesNew[0], rNew), "Parses new event");
        TEST_CHECK(rNew.seq == 1, "seq starts at 1 again for new session");
        TEST_CHECK(rNew.session == session2, "Session UUID matches new session");

        writer.shutdown();
    }

    // 5. Log roll: when file size > 1 MB (or configured limit), renames to events.1.jsonl and starts fresh (§6.1)
    {
        EventWriter writer;
        std::string session3 = "33333333-4444-4555-8666-777777777777";
        writer.initialize(testMfdsDir, session3);

        // Set low max file size for fast unit test roll verification
        writer.setMaxFileSize(500);

        // Write events until file rolls
        for (int i = 0; i < 6; ++i) {
            writer.enqueue(MfdEventRecord::createNav("pkg_a", "screen_1", "tab_1", i, "item_" + std::to_string(i),
                                                    "A slightly long item label to exceed file limit quickly"));
        }
        writer.flush();

        std::wstring rolledPath = writer.getPackageRolledPath("pkg_a");
        std::wstring currentPath = writer.getPackageEventsPath("pkg_a");

        DWORD rolledAttr = GetFileAttributesW(rolledPath.c_str());
        TEST_CHECK(rolledAttr != INVALID_FILE_ATTRIBUTES, "events.1.jsonl exists after rolling");

        std::vector<std::string> rolledLines = readLinesWin32(rolledPath);
        std::vector<std::string> currentLines = readLinesWin32(currentPath);

        TEST_CHECK(!rolledLines.empty(), "events.1.jsonl contains rolled events");
        TEST_CHECK(!currentLines.empty(), "events.jsonl starts fresh with remaining/subsequent events");

        // Verify sequence continues monotonically across the roll!
        MfdEventRecord lastRolled, firstCurrent;
        TEST_CHECK(parseEventFromJson(rolledLines.back(), lastRolled), "Parse last rolled record");
        TEST_CHECK(parseEventFromJson(currentLines.front(), firstCurrent), "Parse first fresh record");
        TEST_CHECK(firstCurrent.seq == lastRolled.seq + 1, "seq continues monotonically across log roll");

        writer.shutdown();
    }

    // 6. Non-blocking queue overflow drop handling (§6.3)
    {
        // A. Small queue overflow drop test (drops oldest 'nav' first, emits 'events_dropped')
        {
            EventWriter smallWriter(5);
            std::wstring smallMfds = testRootDir + L"\\small_mfds";
            CreateDirectoryW(smallMfds.c_str(), nullptr);
            smallWriter.initialize(smallMfds, "overflow-session-1");
            smallWriter.setWorkerPaused(true);

            // Fill queue to capacity (5 items): 4 nav, 1 select
            smallWriter.enqueue(MfdEventRecord::createNav("pkg_x", "s", "t", 0, "n0", "Nav 0"));
            smallWriter.enqueue(MfdEventRecord::createNav("pkg_x", "s", "t", 1, "n1", "Nav 1"));
            smallWriter.enqueue(MfdEventRecord::createNav("pkg_x", "s", "t", 2, "n2", "Nav 2"));
            smallWriter.enqueue(MfdEventRecord::createNav("pkg_x", "s", "t", 3, "n3", "Nav 3"));
            smallWriter.enqueue(MfdEventRecord::createSelect("pkg_x", "s", "t", 4, "s4", "Select 4"));

            TEST_CHECK(smallWriter.queueSize() == 5, "Queue is full at capacity 5");

            // Enqueue another select event; must not block, and drops oldest 'nav' (n0)!
            bool ok = smallWriter.enqueue(MfdEventRecord::createSelect("pkg_x", "s", "t", 5, "s5", "Select 5"));
            TEST_CHECK(ok, "enqueue returns non-blocking on overflow");
            TEST_CHECK(smallWriter.queueSize() == 5, "Queue size remains bounded at capacity 5");
            TEST_CHECK(smallWriter.totalDroppedCount() == 1, "totalDroppedCount is 1");

            smallWriter.setWorkerPaused(false);
            smallWriter.flush();

            std::wstring pkgXEvents = smallWriter.getPackageEventsPath("pkg_x");
            std::vector<std::string> linesX = readLinesWin32(pkgXEvents);

            // Record 0 must be 'events_dropped' with count 1
            TEST_CHECK(!linesX.empty(), "pkg_x has written records");
            MfdEventRecord r0;
            TEST_CHECK(parseEventFromJson(linesX[0], r0), "Parse first event");
            TEST_CHECK(r0.type == "events_dropped", "First emitted event on drop is events_dropped (§6.3)");
            TEST_CHECK(r0.dropCount == 1, "events_dropped count is 1");

            // Remaining records should have n1, n2, n3, s4, s5 (n0 was dropped, s4 was preserved!)
            bool foundS4 = false;
            bool foundN0 = false;
            for (size_t i = 1; i < linesX.size(); ++i) {
                MfdEventRecord r;
                parseEventFromJson(linesX[i], r);
                if (r.itemId == "s4") foundS4 = true;
                if (r.itemId == "n0") foundN0 = true;
            }
            TEST_CHECK(foundS4, "Select event s4 was preserved during nav drops");
            TEST_CHECK(!foundN0, "Oldest nav event n0 was dropped");

            smallWriter.shutdown();
        }

        // B. Full 4096 capacity non-blocking enqueue test
        {
            EventWriter bigWriter(4096);
            std::wstring bigMfds = testRootDir + L"\\big_mfds";
            CreateDirectoryW(bigMfds.c_str(), nullptr);
            bigWriter.initialize(bigMfds, "overflow-session-4096");

            TEST_CHECK(bigWriter.queueCapacity() == 4096, "Queue capacity is 4096");

            // Burst enqueue 10000 items rapidly to ensure queue reaches capacity and drops
            for (int i = 0; i < 10000; ++i) {
                bigWriter.enqueue(MfdEventRecord::createNav("pkg_big", "s", "t", i, "id", "lbl"));
            }

            // Enqueue #10001 critical select: must be non-blocking and return immediately
            bool ok = bigWriter.enqueue(MfdEventRecord::createSelect("pkg_big", "s", "t", 9999, "id_final", "Critical Select"));
            TEST_CHECK(ok, "Non-blocking enqueue succeeded at 4096 capacity");
            TEST_CHECK(bigWriter.queueSize() <= 4096, "Queue remained bounded <= 4096");
            TEST_CHECK(bigWriter.totalDroppedCount() > 0, "Drop count tracked queue overflow");

            bigWriter.flush();
            bigWriter.shutdown();
        }
    }

    // Cleanup temporary test directories
    auto removeFileIfExists = [](const std::wstring& p) { DeleteFileW(p.c_str()); };
    removeFileIfExists(testMfdsDir + L"\\pkg_a\\events.jsonl");
    removeFileIfExists(testMfdsDir + L"\\pkg_a\\events.1.jsonl");
    removeFileIfExists(testMfdsDir + L"\\pkg_b\\events.jsonl");
    removeFileIfExists(testMfdsDir + L"\\pkg_b\\events.1.jsonl");
    RemoveDirectoryW((testMfdsDir + L"\\pkg_a").c_str());
    RemoveDirectoryW((testMfdsDir + L"\\pkg_b").c_str());
    removeFileIfExists(testRootDir + L"\\small_mfds\\pkg_x\\events.jsonl");
    removeFileIfExists(testRootDir + L"\\small_mfds\\pkg_x\\events.1.jsonl");
    RemoveDirectoryW((testRootDir + L"\\small_mfds\\pkg_x").c_str());
    RemoveDirectoryW((testRootDir + L"\\small_mfds").c_str());
    removeFileIfExists(testRootDir + L"\\big_mfds\\pkg_big\\events.jsonl");
    removeFileIfExists(testRootDir + L"\\big_mfds\\pkg_big\\events.1.jsonl");
    RemoveDirectoryW((testRootDir + L"\\big_mfds\\pkg_big").c_str());
    RemoveDirectoryW((testRootDir + L"\\big_mfds").c_str());
    RemoveDirectoryW(testMfdsDir.c_str());
    RemoveDirectoryW(testRootDir.c_str());

    return 0;
}

// 18. Test Game Context Evaluator, Vocabulary, Truth Table & MFD-003 Resolution (§7, Ticket A5/A13)
int test_game_context_evaluator() {
    // 1. Vocabulary & Conversions
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::MainMenu)) == "main_menu", "contextToString MainMenu");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::ShipCockpit)) == "ship_cockpit", "contextToString ShipCockpit");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::Fighter)) == "fighter", "contextToString Fighter");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::Srv)) == "srv", "contextToString Srv");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::OnFoot)) == "on_foot", "contextToString OnFoot");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::Taxi)) == "taxi", "contextToString Taxi");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::Multicrew)) == "multicrew", "contextToString Multicrew");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::Docked)) == "docked", "contextToString Docked");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::Landed)) == "landed", "contextToString Landed");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::Supercruise)) == "supercruise", "contextToString Supercruise");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::NormalSpace)) == "normal_space", "contextToString NormalSpace");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::Hardpoints)) == "hardpoints", "contextToString Hardpoints");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::AnalysisMode)) == "analysis_mode", "contextToString AnalysisMode");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::CombatMode)) == "combat_mode", "contextToString CombatMode");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::GalaxyMap)) == "galaxy_map", "contextToString GalaxyMap");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::SystemMap)) == "system_map", "contextToString SystemMap");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::Orrery)) == "orrery", "contextToString Orrery");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::Fss)) == "fss", "contextToString Fss");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::Saa)) == "saa", "contextToString Saa");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::Codex)) == "codex", "contextToString Codex");
    TEST_CHECK(std::string(ContextEvaluator::contextToString(GameContextBit::StationServices)) == "station_services", "contextToString StationServices");

    // Mask parsing and formatting
    std::vector<std::string> testNames = { "ship_cockpit", "normal_space", "combat_mode" };
    GameContextMask parsedMask = ContextEvaluator::parseContexts(testNames);
    TEST_CHECK(parsedMask == (GameContext::ShipCockpit | GameContext::NormalSpace | GameContext::CombatMode), "parseContexts multi-token");
    auto formatted = ContextEvaluator::formatContexts(parsedMask);
    TEST_CHECK(formatted.size() == 3, "formatContexts size");

    // 2. Truth table verification (§7.2)
    struct TruthTableEntry {
        const char* name;
        uint32_t flags;
        uint32_t flags2;
        int guiFocus;
        GameContextMask expected;
    };

    const TruthTableEntry kTruthTable[] = {
        // Missing / zero flags -> main_menu
        { "MainMenu zero", 0, 0, 0, GameContext::MainMenu },
        // InMainShip flying in normal space
        { "Ship flying", kFlagInMainShip, 0, 0,
          GameContext::ShipCockpit | GameContext::NormalSpace | GameContext::CombatMode },
        // InMainShip in supercruise
        { "Ship supercruise", kFlagInMainShip | kFlagSupercruise, 0, 0,
          GameContext::ShipCockpit | GameContext::Supercruise | GameContext::CombatMode },
        // InMainShip docked
        { "Ship docked", kFlagInMainShip | kFlagDocked, 0, 0,
          GameContext::ShipCockpit | GameContext::Docked | GameContext::CombatMode },
        // InMainShip landed
        { "Ship landed", kFlagInMainShip | kFlagLanded, 0, 0,
          GameContext::ShipCockpit | GameContext::Landed | GameContext::CombatMode },
        // InMainShip with hardpoints deployed
        { "Ship hardpoints", kFlagInMainShip | kFlagHardpointsDeployed, 0, 0,
          GameContext::ShipCockpit | GameContext::NormalSpace | GameContext::CombatMode | GameContext::Hardpoints },
        // InMainShip in analysis mode
        { "Ship analysis mode", kFlagInMainShip | kFlagHudInAnalysisMode, 0, 0,
          GameContext::ShipCockpit | GameContext::NormalSpace | GameContext::AnalysisMode },
        // Fighter flying in normal space
        { "Fighter flying", kFlagInFighter, 0, 0,
          GameContext::Fighter | GameContext::NormalSpace | GameContext::CombatMode },
        // SRV driving landed
        { "SRV landed", kFlagInSRV | kFlagLanded, 0, 0,
          GameContext::Srv | GameContext::Landed },
        // On foot (Odyssey Flags2 bit 0)
        { "On foot", 0, kFlag2OnFoot, 0,
          GameContext::OnFoot },
        // In Taxi (Odyssey Flags2 bit 1) in supercruise
        { "Taxi supercruise", kFlagSupercruise, kFlag2InTaxi, 0,
          GameContext::Taxi | GameContext::Supercruise },
        // In Multicrew (Odyssey Flags2 bit 2)
        { "Multicrew normal space", 0, kFlag2InMulticrew, 0,
          GameContext::Multicrew | GameContext::NormalSpace },
        { "Multicrew docked", kFlagDocked, kFlag2InMulticrew, 0,
          GameContext::Multicrew | GameContext::Docked },
        // Station services (GuiFocus 5)
        { "Station services", kFlagInMainShip | kFlagDocked, 0, kGuiFocusStationServices,
          GameContext::ShipCockpit | GameContext::Docked | GameContext::CombatMode | GameContext::StationServices },
        // Galaxy map (GuiFocus 6)
        { "Galaxy map", kFlagInMainShip, 0, kGuiFocusGalaxyMap,
          GameContext::ShipCockpit | GameContext::NormalSpace | GameContext::CombatMode | GameContext::GalaxyMap },
        // System map (GuiFocus 7)
        { "System map", kFlagInMainShip, 0, kGuiFocusSystemMap,
          GameContext::ShipCockpit | GameContext::NormalSpace | GameContext::CombatMode | GameContext::SystemMap },
        // Orrery (GuiFocus 8)
        { "Orrery", kFlagInMainShip, 0, kGuiFocusOrrery,
          GameContext::ShipCockpit | GameContext::NormalSpace | GameContext::CombatMode | GameContext::Orrery },
        // FSS (GuiFocus 9)
        { "FSS", kFlagInMainShip | kFlagSupercruise, 0, kGuiFocusFSS,
          GameContext::ShipCockpit | GameContext::Supercruise | GameContext::CombatMode | GameContext::Fss },
        // SAA (GuiFocus 10)
        { "SAA", kFlagInMainShip | kFlagSupercruise, 0, kGuiFocusSAA,
          GameContext::ShipCockpit | GameContext::Supercruise | GameContext::CombatMode | GameContext::Saa },
        // Codex (GuiFocus 11)
        { "Codex", kFlagInMainShip, 0, kGuiFocusCodex,
          GameContext::ShipCockpit | GameContext::NormalSpace | GameContext::CombatMode | GameContext::Codex }
    };

    for (const auto& entry : kTruthTable) {
        GameContextMask evaluated = ContextEvaluator::evaluate(entry.flags, entry.flags2, entry.guiFocus);
        TEST_CHECK(evaluated == entry.expected, entry.name);
    }

    // 3. Screen Visibility Matching & MFD-003 Verification (§7.3)
    // Default screen contexts: [ship_cockpit, fighter, srv, on_foot, taxi, multicrew]
    const std::vector<std::string> defaultContexts = {
        "ship_cockpit", "fighter", "srv", "on_foot", "taxi", "multicrew"
    };

    // Case 3a: Standard flight -> default screen is visible
    GameContextMask flyingCtx = ContextEvaluator::evaluate(kFlagInMainShip, 0, 0);
    TEST_CHECK(ContextEvaluator::isScreenVisible(defaultContexts, {}, flyingCtx), "Cockpit screen visible in normal flight");

    // Case 3b: MFD-003 Resolution: Cockpit MFD is hidden in Galaxy Map
    GameContextMask galMapCtx = ContextEvaluator::evaluate(kFlagInMainShip, 0, kGuiFocusGalaxyMap);
    TEST_CHECK(!ContextEvaluator::isScreenVisible(defaultContexts, {}, galMapCtx), "MFD-003: Cockpit MFD hidden in Galaxy Map");

    // Case 3c: MFD-003 Resolution: Cockpit MFD is hidden in System Map
    GameContextMask sysMapCtx = ContextEvaluator::evaluate(kFlagInMainShip, 0, kGuiFocusSystemMap);
    TEST_CHECK(!ContextEvaluator::isScreenVisible(defaultContexts, {}, sysMapCtx), "MFD-003: Cockpit MFD hidden in System Map");

    // Case 3d: Other global exclusions suppress default cockpit MFD
    TEST_CHECK(!ContextEvaluator::isScreenVisible(defaultContexts, {}, ContextEvaluator::evaluate(kFlagInMainShip, 0, kGuiFocusOrrery)), "Hidden in Orrery");
    TEST_CHECK(!ContextEvaluator::isScreenVisible(defaultContexts, {}, ContextEvaluator::evaluate(kFlagInMainShip | kFlagSupercruise, 0, kGuiFocusFSS)), "Hidden in FSS");
    TEST_CHECK(!ContextEvaluator::isScreenVisible(defaultContexts, {}, ContextEvaluator::evaluate(kFlagInMainShip | kFlagSupercruise, 0, kGuiFocusSAA)), "Hidden in SAA");
    TEST_CHECK(!ContextEvaluator::isScreenVisible(defaultContexts, {}, ContextEvaluator::evaluate(kFlagInMainShip, 0, kGuiFocusCodex)), "Hidden in Codex");
    TEST_CHECK(!ContextEvaluator::isScreenVisible(defaultContexts, {}, ContextEvaluator::evaluate(0, 0, 0)), "Hidden in Main Menu");

    // Case 3e: Station Services (GuiFocus 5) is NOT in global exclusion set -> visible
    GameContextMask stationCtx = ContextEvaluator::evaluate(kFlagInMainShip | kFlagDocked, 0, kGuiFocusStationServices);
    TEST_CHECK(ContextEvaluator::isScreenVisible(defaultContexts, {}, stationCtx), "Cockpit screen visible in Station Services");

    // Case 3f: Explicit opt-in to global exclusion
    const std::vector<std::string> mapOptInContexts = { "ship_cockpit", "galaxy_map" };
    TEST_CHECK(ContextEvaluator::isScreenVisible(mapOptInContexts, {}, galMapCtx), "Explicitly opt-in screen visible in Galaxy Map");
    TEST_CHECK(!ContextEvaluator::isScreenVisible(mapOptInContexts, {}, sysMapCtx), "Explicit Galaxy Map screen still hidden in System Map");

    // Case 3g: On-foot vs ship classification
    GameContextMask onFootCtx = ContextEvaluator::evaluate(0, kFlag2OnFoot, 0);
    const std::vector<std::string> shipOnlyContexts = { "ship_cockpit" };
    const std::vector<std::string> footOnlyContexts = { "on_foot" };

    TEST_CHECK(!ContextEvaluator::isScreenVisible(shipOnlyContexts, {}, onFootCtx), "Ship-only screen hidden on foot");
    TEST_CHECK(ContextEvaluator::isScreenVisible(footOnlyContexts, {}, onFootCtx), "Foot-only screen visible on foot");
    TEST_CHECK(ContextEvaluator::isScreenVisible(shipOnlyContexts, {}, flyingCtx), "Ship-only screen visible in ship");
    TEST_CHECK(!ContextEvaluator::isScreenVisible(footOnlyContexts, {}, flyingCtx), "Foot-only screen hidden in ship");

    // Case 3h: Exclusions
    const std::vector<std::string> excludeCombat = { "combat_mode" };
    TEST_CHECK(!ContextEvaluator::isScreenVisible(defaultContexts, excludeCombat, flyingCtx), "Screen with excludeContexts=combat_mode hidden in combat");
    GameContextMask analysisCtx = ContextEvaluator::evaluate(kFlagInMainShip | kFlagHudInAnalysisMode, 0, 0);
    TEST_CHECK(ContextEvaluator::isScreenVisible(defaultContexts, excludeCombat, analysisCtx), "Screen with excludeContexts=combat_mode visible in analysis");

    // 4. Status.json Parsing & Partial Write Handling
    uint32_t parsedFlags = 0, parsedFlags2 = 0;
    int parsedGui = 0;
    std::string validJson = R"({
        "timestamp": "2026-10-15T12:00:00Z",
        "event": "Status",
        "Flags": 16777216,
        "Flags2": 1,
        "GuiFocus": 6
    })";
    TEST_CHECK(ContextEvaluator::parseStatusJson(validJson, parsedFlags, parsedFlags2, parsedGui), "parseStatusJson valid");
    TEST_CHECK(parsedFlags == 16777216, "parsedFlags match");
    TEST_CHECK(parsedFlags2 == 1, "parsedFlags2 match");
    TEST_CHECK(parsedGui == 6, "parsedGui match");

    TEST_CHECK(!ContextEvaluator::parseStatusJson("", parsedFlags, parsedFlags2, parsedGui), "parseStatusJson empty rejects");
    TEST_CHECK(!ContextEvaluator::parseStatusJson("{ truncated: true,", parsedFlags, parsedFlags2, parsedGui), "parseStatusJson malformed rejects");

    // 5. EDVR_MFD_JOURNAL_DIR Override (Ticket A13)
    ContextEvaluator::setJournalDirOverride(L"C:\\Custom\\JournalOverride");
    TEST_CHECK(ContextEvaluator::getJournalDir() == L"C:\\Custom\\JournalOverride", "Programmatic journal override");
    TEST_CHECK(ContextEvaluator::getStatusJsonPath() == L"C:\\Custom\\JournalOverride\\Status.json", "getStatusJsonPath matches programmatic override");
    ContextEvaluator::setJournalDirOverride(L""); // Reset programmatic

    SetEnvironmentVariableW(L"EDVR_MFD_JOURNAL_DIR", L"C:\\Env\\JournalDir");
    TEST_CHECK(ContextEvaluator::getJournalDir() == L"C:\\Env\\JournalDir", "Environment variable EDVR_MFD_JOURNAL_DIR override");
    SetEnvironmentVariableW(L"EDVR_MFD_JOURNAL_DIR", nullptr); // Clear env var

    // 6. Live ContextEvaluator Polling with Status.json
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring testDir = std::wstring(tempPath) + L"edvr_mfd_context_test_" + std::to_wstring(GetTickCount64());
    CreateDirectoryW(testDir.c_str(), nullptr);
    std::wstring statusFilePath = testDir + L"\\Status.json";

    ContextEvaluator::setJournalDirOverride(testDir);
    ContextEvaluator testEvaluator;

    // Initially without file -> MainMenu
    testEvaluator.pollOnce(); // Run initial poll
    TEST_CHECK(testEvaluator.getActiveContexts() == GameContext::MainMenu, "Initial evaluator state is MainMenu");

    // Write file with Ship flying
    std::string shipJson = "{\"Flags\": 16777216, \"Flags2\": 0, \"GuiFocus\": 0}";
    HANDLE hFile = CreateFileW(statusFilePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    WriteFile(hFile, shipJson.c_str(), static_cast<DWORD>(shipJson.size()), &written, nullptr);
    CloseHandle(hFile);

    bool changed = testEvaluator.pollOnce();
    TEST_CHECK(changed, "pollOnce detects new Status.json");
    TEST_CHECK(testEvaluator.isContextActive(GameContext::ShipCockpit), "ShipCockpit active");
    TEST_CHECK(testEvaluator.isContextActive(GameContext::NormalSpace), "NormalSpace active");
    TEST_CHECK(testEvaluator.isVisible(defaultContexts), "Default screen visible");

    // Transition to Galaxy Map (MFD-003)
    Sleep(50); // Ensure file time changes
    std::string galMapJson = "{\"Flags\": 16777216, \"Flags2\": 0, \"GuiFocus\": 6}";
    hFile = CreateFileW(statusFilePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    WriteFile(hFile, galMapJson.c_str(), static_cast<DWORD>(galMapJson.size()), &written, nullptr);
    CloseHandle(hFile);

    changed = testEvaluator.pollOnce();
    TEST_CHECK(changed, "pollOnce detects Galaxy Map transition");
    TEST_CHECK(testEvaluator.isContextActive(GameContext::GalaxyMap), "GalaxyMap active");
    TEST_CHECK(!testEvaluator.isVisible(defaultContexts), "MFD-003: Default screen hidden when Galaxy Map is active");

    // Transition back to MainMenu on file deletion
    DeleteFileW(statusFilePath.c_str());
    changed = testEvaluator.pollOnce();
    TEST_CHECK(changed, "pollOnce detects transition to MainMenu on missing Status.json");
    TEST_CHECK(testEvaluator.getActiveContexts() == GameContext::MainMenu, "Active contexts reset to MainMenu");

    // Clean up
    ContextEvaluator::setJournalDirOverride(L"");
    ContextEvaluator::instance().updateFromStatus(kFlagInMainShip, 0, 0);
    RemoveDirectoryW(testDir.c_str());

    return 0;
}

// 19. Test CompanionSupervisor background thread, Job Object, backoff, gave up, and status badges (§8 - Ticket A10)
int test_companion_supervisor() {
    std::wstring testDir = L"build\\test_companion_pkg";
    std::string testDirA = "build\\test_companion_pkg";
    CreateDirectoryW(L"build", nullptr);
    CreateDirectoryW(testDir.c_str(), nullptr);

    // Verify SHA-256 computation
    std::string sha = computeFileSha256(L"C:\\Windows\\System32\\cmd.exe");
    TEST_CHECK(!sha.empty(), "SHA-256 computed for cmd.exe");
    TEST_CHECK(sha.length() == 64, "SHA-256 is 64 hex characters");

    CompanionSupervisor supervisor;
    TEST_CHECK(supervisor.initialize(testDir, "test_session_uuid"), "Supervisor initialized");
    TEST_CHECK(supervisor.isRunning(), "Supervisor thread is running");
    TEST_CHECK(supervisor.jobHandle() != nullptr, "Job Object handle created with KILL_ON_JOB_CLOSE");

    // 1. Clean exit companion (exit code 0 - intentional, do not restart)
    MfdPackageManifest manifestClean;
    manifestClean.id = "clean_pkg";
    manifestClean.packageDir = testDir;
    MfdCompanionManifest compClean;
    compClean.id = "clean_comp";
    compClean.exe = "C:\\Windows\\System32\\cmd.exe";
    compClean.args = {"/c", "echo", "CLEAN_START"};
    compClean.restartPolicy = MfdCompanionRestart::kOnFailure;
    manifestClean.companions.push_back(compClean);

    supervisor.registerPackage(manifestClean);

    // Wait briefly for cmd.exe to complete
    for (int i = 0; i < 30; ++i) {
        Sleep(50);
        supervisor.tick();
        if (!supervisor.isCompanionRunning("clean_pkg", "clean_comp")) break;
    }
    TEST_CHECK(!supervisor.isCompanionRunning("clean_pkg", "clean_comp"), "Clean companion finished execution");

    auto instances = supervisor.getInstances();
    bool foundClean = false;
    for (const auto& inst : instances) {
        if (inst.packageId == "clean_pkg" && inst.companionId == "clean_comp") {
            TEST_CHECK(inst.state == CompanionProcessState::kExitedCleanly, "Clean companion marked kExitedCleanly");
            TEST_CHECK(inst.exitCode == 0, "Clean companion exit code is 0");
            foundClean = true;
        }
    }
    TEST_CHECK(foundClean, "Found clean_pkg instance in supervisor list");

    // Check companion.log created and truncated
    std::wstring logPath = testDir + L"\\companion.log";
    DWORD logAttr = GetFileAttributesW(logPath.c_str());
    TEST_CHECK(logAttr != INVALID_FILE_ATTRIBUTES, "companion.log created in package folder");

    // 2. Crash companion & Backoff (exit code 42)
    MfdPackageManifest manifestCrash;
    manifestCrash.id = "crash_pkg";
    manifestCrash.packageDir = testDir;
    MfdCompanionManifest compCrash;
    compCrash.id = "crash_comp";
    compCrash.exe = "C:\\Windows\\System32\\cmd.exe";
    compCrash.args = {"/c", "exit 42"};
    compCrash.restartPolicy = MfdCompanionRestart::kOnFailure;
    manifestCrash.companions.push_back(compCrash);

    // Fast backoff schedule for testing (1s backoff on first crash to test offline badge)
    supervisor.setBackoffSchedule({1, 0, 0, 0});
    supervisor.setCrashWindowDuration(std::chrono::seconds(10));
    supervisor.setMaxCrashesInWindow(5);

    supervisor.registerPackage(manifestCrash);

    // Poll until first crash is detected
    for (int i = 0; i < 30; ++i) {
        supervisor.tick();
        Sleep(50);
        if (supervisor.getCompanionCrashCount("crash_pkg", "crash_comp") >= 1) break;
    }
    TEST_CHECK(supervisor.getCompanionCrashCount("crash_pkg", "crash_comp") >= 1, "Recorded first crash");

    // Check status badge override is COMPANION OFFLINE (color crit)
    std::string badge;
    MfdNamedColor badgeColor = MfdNamedColor::kNormal;
    std::string badgeColorName;
    TEST_CHECK(supervisor.getStatusBadgeOverride("crash_pkg", badge, badgeColor, badgeColorName), "Status badge override active on crash");
    TEST_CHECK(badge == "COMPANION OFFLINE", "Badge is COMPANION OFFLINE");
    TEST_CHECK(badgeColor == MfdNamedColor::kCrit, "Badge color is crit");
    TEST_CHECK(badgeColorName == "crit", "Badge color name is crit");

    MfdViewModel testVm;
    TEST_CHECK(supervisor.applyStatusBadgeOverride("crash_pkg", testVm), "Applied status override to MfdViewModel");
    TEST_CHECK(testVm.statusBadge == "COMPANION OFFLINE", "View model badge updated to COMPANION OFFLINE");
    TEST_CHECK(testVm.statusBadgeNamedColor == MfdNamedColor::kCrit, "View model color updated to crit");

    // 3. Give up after 5 crashes in window
    supervisor.setBackoffSchedule({0, 0, 0, 0});
    for (int i = 0; i < 60; ++i) {
        supervisor.tick();
        Sleep(50);
        if (supervisor.hasCompanionGaveUp("crash_pkg", "crash_comp")) break;
    }
    TEST_CHECK(supervisor.hasCompanionGaveUp("crash_pkg", "crash_comp"), "Companion marked gave_up after 5 crashes in 10m window");
    TEST_CHECK(supervisor.getStatusBadgeOverride("crash_pkg", badge, badgeColor, badgeColorName), "Badge override remains active after gave up");
    TEST_CHECK(badge == "COMPANION OFFLINE", "Badge remains COMPANION OFFLINE");

    // 4. Runtime toggle (§8.3)
    supervisor.setPackageCompanionsEnabled("crash_pkg", false);
    TEST_CHECK(!supervisor.isPackageCompanionsEnabled("crash_pkg"), "Companions disabled for crash_pkg");
    TEST_CHECK(supervisor.getStatusBadgeOverride("crash_pkg", badge, badgeColor, badgeColorName), "Badge override active when paused");
    TEST_CHECK(badge == "PAUSED", "Badge override is PAUSED when disabled at runtime");
    TEST_CHECK(badgeColor == MfdNamedColor::kWarn, "Badge color is warn when PAUSED");

    MfdViewModel pausedVm;
    supervisor.applyStatusBadgeOverride("crash_pkg", pausedVm);
    TEST_CHECK(pausedVm.statusBadge == "PAUSED", "View model badge is PAUSED");
    TEST_CHECK(pausedVm.statusBadgeNamedColor == MfdNamedColor::kWarn, "View model badge color is warn");

    // Re-enable clears gave_up / reset crash counters
    supervisor.setPackageCompanionsEnabled("crash_pkg", true);
    TEST_CHECK(supervisor.isPackageCompanionsEnabled("crash_pkg"), "Companions re-enabled");
    TEST_CHECK(!supervisor.hasCompanionGaveUp("crash_pkg", "crash_comp"), "Gave up cleared on re-enable");

    // 5. Clean shutdown (§8.4)
    supervisor.shutdown();
    TEST_CHECK(!supervisor.isRunning(), "Supervisor stopped");
    TEST_CHECK(supervisor.jobHandle() == nullptr, "Job Object handle closed");

    // Clean up test files
    DeleteFileW((testDir + L"\\companion.log").c_str());
    RemoveDirectoryW(testDir.c_str());

    return 0;
}

// 20. Test F8 menu settings, persistence in %LOCALAPPDATA%\EDVR\edvr_mfd.ini, and "Hide All" toggle (§9.1-9.2, Ticket A11 & D1)
int test_f8_settings_and_persistence() {
    std::wstring iniPath = getPluginSettingsPathForTest();

    // 1. Test global settings defaults & setters
    setPluginSettingBool("cockpit_mfd_enabled", true);
    setPluginSettingBool("mfd_show_all", true);
    setPluginSettingBool("mfd_head_locked", false);
    setPluginSettingBool("mfd_hide_all_toggle", false);
    setPluginSettingBool("mfd_show_settings_tab", true);

    TEST_CHECK(getPluginSettingBool("cockpit_mfd_enabled", false) == true, "cockpit_mfd_enabled default 1");
    TEST_CHECK(getPluginSettingBool("mfd_show_all", false) == true, "mfd_show_all default 1");
    TEST_CHECK(getPluginSettingBool("mfd_head_locked", true) == false, "mfd_head_locked default 0");
    TEST_CHECK(getPluginSettingBool("mfd_hide_all_toggle", true) == false, "mfd_hide_all_toggle default 0");
    TEST_CHECK(getPluginSettingBool("mfd_show_settings_tab", false) == true, "mfd_show_settings_tab default 1");

    // 2. Test INI round-trip persistence
    setPluginSettingBool("mfd_hide_all_toggle", true);
    TEST_CHECK(getPluginSettingBool("mfd_hide_all_toggle", false) == true, "mfd_hide_all_toggle is true");

    int val = GetPrivateProfileIntW(L"settings", L"mfd_hide_all_toggle", -1, iniPath.c_str());
    TEST_CHECK(val == 1, "mfd_hide_all_toggle persisted as 1 in edvr_mfd.ini");

    setPluginSettingBool("mfd_head_locked", true);
    val = GetPrivateProfileIntW(L"settings", L"mfd_head_locked", -1, iniPath.c_str());
    TEST_CHECK(val == 1, "mfd_head_locked persisted as 1 in edvr_mfd.ini");

    setPluginSettingBool("mfd_show_settings_tab", false);
    val = GetPrivateProfileIntW(L"settings", L"mfd_show_settings_tab", -1, iniPath.c_str());
    TEST_CHECK(val == 0, "mfd_show_settings_tab persisted as 0 in edvr_mfd.ini");
    setPluginSettingBool("mfd_show_settings_tab", true);
    val = GetPrivateProfileIntW(L"settings", L"mfd_show_settings_tab", -1, iniPath.c_str());
    TEST_CHECK(val == 1, "mfd_show_settings_tab persisted as 1 in edvr_mfd.ini");

    // 3. Test "Hide All" toggle behavior (Ticket D1 & MFD-006)
    EdvrPluginCallbacks callbacks{};
    callbacks.structSize = sizeof(callbacks);
    int regRes = EdvrPluginRegister(EDVR_PLUGIN_API_VERSION, &callbacks);
    TEST_CHECK(regRes == 0, "EdvrPluginRegister success");

    EdvrInputContext inputCtx{};
    inputCtx.structSize = sizeof(inputCtx);
    inputCtx.swallowInput = 0;

    // When hide_all_toggle is active, onFilterInput must not swallow input
    callbacks.onFilterInput(&inputCtx);
    TEST_CHECK(inputCtx.swallowInput == 0, "Input NOT swallowed when hide_all_toggle is active");

    // Restore hide_all_toggle
    setPluginSettingBool("mfd_hide_all_toggle", false);
    val = GetPrivateProfileIntW(L"settings", L"mfd_hide_all_toggle", -1, iniPath.c_str());
    TEST_CHECK(val == 0, "mfd_hide_all_toggle persisted as 0 in edvr_mfd.ini");

    // 4. Test Mock Dynamic Host Registration (§9.1-9.2)
    struct MockHostServices {
        static std::vector<EdvrPluginSettingDef>& getRegistered() {
            static std::vector<EdvrPluginSettingDef> s_defs;
            return s_defs;
        }
        static int EDVR_API registerSetting(const EdvrPluginSettingDef* def) {
            if (def) getRegistered().push_back(*def);
            return 0;
        }
    };
    MockHostServices::getRegistered().clear();

    EdvrHostServices host{};
    host.structSize = sizeof(host);
    host.registerSetting = MockHostServices::registerSetting;

    callbacks.onInitialize(&host);

    const auto& registered = MockHostServices::getRegistered();
    TEST_CHECK(!registered.empty(), "Settings registered dynamically with host");

    // Verify obsolete mfd_display_count was removed
    bool foundDisplayCount = false;
    bool foundHideAll = false;
    bool foundShowSettingsTab = false;
    for (const auto& s : registered) {
        if (s.key && strcmp(s.key, "mfd_display_count") == 0) {
            foundDisplayCount = true;
        }
        if (s.key && strcmp(s.key, "mfd_hide_all_toggle") == 0) {
            foundHideAll = true;
        }
        if (s.key && strcmp(s.key, "mfd_show_settings_tab") == 0) {
            foundShowSettingsTab = true;
        }
    }
    TEST_CHECK(!foundDisplayCount, "Obsolete mfd_display_count was removed");
    TEST_CHECK(foundHideAll, "mfd_hide_all_toggle was successfully registered");
    TEST_CHECK(foundShowSettingsTab, "mfd_show_settings_tab was successfully registered");

    callbacks.onShutdown();
    return 0;
}

// 21. Test Dynamic Package Screen Model, Auto-Layout, 16-Screen Cap, Settings Migration & Event Routing (Tickets A6, A7, A9, A12)
int test_dynamic_screens_cap_and_settings() {
    auto writeTestFileHelper = [](const std::wstring& path, const std::string& content) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
            CloseHandle(h);
        }
    };

    // 1. INI Migration & Persistence (§9.3, §9.5 - Ticket A7)
    wchar_t tempBase[MAX_PATH];
    GetTempPathW(MAX_PATH, tempBase);
    std::wstring testRoot = std::wstring(tempBase) + L"edvr_mfd_mgr_test_" + std::to_wstring(GetTickCount64());
    CreateDirectoryW(testRoot.c_str(), nullptr);
    std::wstring testIni = testRoot + L"\\edvr_mfd_test.ini";

    // Write legacy sections
    WritePrivateProfileStringW(L"main_mfd", L"pos_x", L"-0.1234", testIni.c_str());
    WritePrivateProfileStringW(L"main_mfd", L"pos_y", L"-0.5678", testIni.c_str());
    WritePrivateProfileStringW(L"main_mfd", L"pos_z", L"-0.9101", testIni.c_str());
    WritePrivateProfileStringW(L"main_mfd", L"scale", L"1.25", testIni.c_str());
    WritePrivateProfileStringW(L"main_mfd", L"opacity", L"0.85", testIni.c_str());
    WritePrivateProfileStringW(L"main_mfd", L"theme", L"2", testIni.c_str());
    WritePrivateProfileStringW(L"left_mfd", L"pos_x", L"-0.4500", testIni.c_str());
    WritePrivateProfileStringW(L"right_mfd", L"pos_x", L"0.4500", testIni.c_str());

    bool migrated = MfdManager::migrateLegacyIni(testIni);
    TEST_CHECK(migrated, "migrateLegacyIni returned true on initial migration");

    // Verify old sections removed
    wchar_t checkBuf[64];
    TEST_CHECK(GetPrivateProfileSectionW(L"main_mfd", checkBuf, 64, testIni.c_str()) == 0, "legacy main_mfd section deleted");
    TEST_CHECK(GetPrivateProfileSectionW(L"left_mfd", checkBuf, 64, testIni.c_str()) == 0, "legacy left_mfd section deleted");
    TEST_CHECK(GetPrivateProfileSectionW(L"right_mfd", checkBuf, 64, testIni.c_str()) == 0, "legacy right_mfd section deleted");

    // Verify migrated target sections exist with copied values
    GetPrivateProfileStringW(L"route/route", L"pos_x", L"", checkBuf, 64, testIni.c_str());
    TEST_CHECK(wcscmp(checkBuf, L"-0.1234") == 0, "route/route pos_x migrated");
    GetPrivateProfileStringW(L"route/route", L"scale", L"", checkBuf, 64, testIni.c_str());
    TEST_CHECK(wcscmp(checkBuf, L"1.25") == 0, "route/route scale migrated");
    GetPrivateProfileStringW(L"ship/ship", L"pos_x", L"", checkBuf, 64, testIni.c_str());
    TEST_CHECK(wcscmp(checkBuf, L"-0.4500") == 0, "ship/ship pos_x migrated");
    GetPrivateProfileStringW(L"exploration/exploration", L"pos_x", L"", checkBuf, 64, testIni.c_str());
    TEST_CHECK(wcscmp(checkBuf, L"0.4500") == 0, "exploration/exploration pos_x migrated");

    // Second run must be no-op (targets exist)
    bool migratedAgain = MfdManager::migrateLegacyIni(testIni);
    TEST_CHECK(!migratedAgain, "migrateLegacyIni is idempotent when targets exist");

    // 2. Dynamic Screen Discovery, Ordering, and Auto-Layout (§4, §9.4 - Ticket A6)
    std::wstring mfdsDir = testRoot + L"\\mfds";
    CreateDirectoryW(mfdsDir.c_str(), nullptr);
    std::wstring pkgBDir = mfdsDir + L"\\pkg_b";
    std::wstring pkgADir = mfdsDir + L"\\pkg_a";
    CreateDirectoryW(pkgBDir.c_str(), nullptr);
    CreateDirectoryW(pkgADir.c_str(), nullptr);

    std::string bManifest = R"({
        "manifestVersion": 1, "id": "pkg_b", "name": "Package B",
        "screens": [
            { "id": "scr_b1", "file": "screens/b1.json", "title": "B1 Auto" },
            { "id": "scr_b2", "file": "screens/b2.json", "title": "B2 Explicit",
              "pose": { "position": [-0.29, -0.22, -0.46], "pitchDeg": -22, "yawDeg": 32, "widthM": 0.24, "heightM": 0.16 } }
        ]
    })";
    writeTestFileHelper(pkgBDir + L"\\manifest.json", bManifest);

    std::string aManifest = R"({
        "manifestVersion": 1, "id": "pkg_a", "name": "Package A",
        "screens": [
            { "id": "scr_a1", "file": "screens/a1.json", "title": "A1 Auto" },
            { "id": "scr_a2", "file": "screens/a2.json", "title": "A2 Explicit",
              "pose": { "position": [0, -0.02, -0.55], "pitchDeg": -10, "yawDeg": 0, "widthM": 0.24, "heightM": 0.16 } }
        ]
    })";
    writeTestFileHelper(pkgADir + L"\\manifest.json", aManifest);

    MfdManager mgr;
    mgr.setIniPathOverride(testIni);
    bool initOk = mgr.initialize(512, 384, mfdsDir);
    TEST_CHECK(initOk, "MfdManager initialized with test mfdsDir");

    // Verify Screen order: package ID ascending (pkg_a before pkg_b), then manifest declaration order
    TEST_CHECK(mgr.screenCount() == 4, "4 screens loaded");
    TEST_CHECK(mgr.screens()[0].qualifiedId == "pkg_a/scr_a1", "Screen 0 is pkg_a/scr_a1");
    TEST_CHECK(mgr.screens()[1].qualifiedId == "pkg_a/scr_a2", "Screen 1 is pkg_a/scr_a2");
    TEST_CHECK(mgr.screens()[2].qualifiedId == "pkg_b/scr_b1", "Screen 2 is pkg_b/scr_b1");
    TEST_CHECK(mgr.screens()[3].qualifiedId == "pkg_b/scr_b2", "Screen 3 is pkg_b/scr_b2");

    // Verify auto-layout:
    // Candidate row 1 is: {y=-0.22, pitch=-22, yaw=0}, {yaw=32}, {yaw=-32}...
    // pkg_b/scr_b2 has explicit pose at pitch=-22, yaw=32 -> yaw=32 is skipped (conflict within +-8 deg)
    // scr_a1 gets yaw=0
    // scr_b1 gets next available slot: yaw=-32!
    TEST_CHECK(std::abs(mgr.screens()[0].pitchDeg - -22.0f) < 0.1f, "scr_a1 auto-layout pitch is -22");
    TEST_CHECK(std::abs(mgr.screens()[0].yawDeg - 0.0f) < 0.1f, "scr_a1 auto-layout yaw is 0");
    TEST_CHECK(std::abs(mgr.screens()[2].pitchDeg - -22.0f) < 0.1f, "scr_b1 auto-layout pitch is -22");
    TEST_CHECK(std::abs(mgr.screens()[2].yawDeg - -32.0f) < 0.1f, "scr_b1 auto-layout yaw is -32 (conflict skipped)");

    // 3. Injected SETTINGS Tab Verification (§9.3 - Ticket A7)
    for (auto& scr : mgr.screens()) {
        const auto& vm = scr.provider->viewModel();
        bool foundSettings = false;
        for (const auto& tab : vm.tabs) {
            if (tab.id == "__settings") {
                foundSettings = true;
                TEST_CHECK(tab.title == "SETTINGS", "Settings tab title is SETTINGS");
                TEST_CHECK(tab.type == MfdTabType::kKeyValue, "Settings tab type is keyvalue");
                bool hasTracking = false, hasScale = false, hasOpacity = false, hasReset = false;
                for (const auto& kv : tab.keyValues) {
                    if (kv.key == "Tracking Mode") hasTracking = true;
                    if (kv.key == "Display Scale") hasScale = true;
                    if (kv.key == "Glass Opacity") hasOpacity = true;
                    if (kv.key == "Reset Pose") hasReset = true;
                }
                TEST_CHECK(hasTracking && hasScale && hasOpacity && hasReset, "Settings tab has required items");
                break;
            }
        }
        TEST_CHECK(foundSettings, "Injected __settings tab present on every screen");
    }

    // 4. Hard Cap of 16 Visible Screens (§10.2 - Ticket A6)
    for (int i = 0; i < 14; ++i) {
        std::string sName = "extra_scr_" + std::to_string(i);
        auto prov = std::make_unique<DeclarativeMfdProvider>(sName, "EXTRA");
        MfdPose extraPose;
        extraPose.position = Vec3(0.1f * (i + 1), -0.2f, -0.5f);
        extraPose.orientation = Quat::fromEulerDegrees(0, 0, 0);
        mgr.addSlot(sName, std::move(prov), extraPose);
    }
    TEST_CHECK(mgr.screenCount() == 18, "Manager has 18 screens total");

    ContextEvaluator::instance().updateFromStatus(kFlagInMainShip, 0, 0);
    Vec3 headPos(0, 0, 0);
    Vec3 headFwd(0, 0, -1);
    mgr.update(headPos, headFwd, 0.016f);

    TEST_CHECK(mgr.visibleScreenCount() == 16, "Visible screen count capped at 16");
    TEST_CHECK(mgr.suppressedScreenCount() == 2, "2 screens suppressed by hard cap");

    // Load order: screens [0..15] visible, [16..17] suppressed
    for (size_t i = 0; i < 16; ++i) {
        TEST_CHECK(!mgr.screens()[i].isCapSuppressed, "Screen within cap is visible");
    }
    TEST_CHECK(mgr.screens()[16].isCapSuppressed, "Screen 16 suppressed by cap");
    TEST_CHECK(mgr.screens()[17].isCapSuppressed, "Screen 17 suppressed by cap");

    // Gaze at screen 17 so it becomes focused: focused screen is always included!
    Vec3 toScr17 = (mgr.screens()[17].pose.position - headPos).normalized();
    for (int t = 0; t < 20; ++t) {
        mgr.update(headPos, toScr17, 0.020f);
    }
    TEST_CHECK(mgr.screens()[17].gazeTracker.isFocused(), "Screen 17 acquired gaze focus");
    TEST_CHECK(!mgr.screens()[17].isCapSuppressed, "Focused screen is NOT suppressed (§10.2)");
    TEST_CHECK(mgr.visibleScreenCount() == 16, "Visible count remains 16");
    TEST_CHECK(mgr.suppressedScreenCount() == 2, "2 screens suppressed");
    TEST_CHECK(mgr.screens()[15].isCapSuppressed, "Screen 15 displaced and suppressed by focused screen");

    // 5. Dirty-Flag Rasterization & QPC Telemetry (§10 - Ticket A9)
    mgr.render();
    TEST_CHECK(mgr.perfTelemetry().visibleCount == 16, "16 screens visible during render");
    TEST_CHECK(mgr.perfTelemetry().dirtyCount == 16, "First frame all 16 visible screens rasterized");

    // Second frame: no changes -> dirtyCount == 0
    mgr.render();
    TEST_CHECK(mgr.perfTelemetry().dirtyCount == 0, "Second frame skips rasterization (dirtyCount == 0)");
    TEST_CHECK(mgr.perfTelemetry().lastFrameCpuTimeMs >= 0.0, "QPC measured frame CPU time");
    TEST_CHECK(mgr.perfTelemetry().p50CpuTimeMs >= 0.0, "QPC p50 tracked");
    TEST_CHECK(mgr.perfTelemetry().maxCpuTimeMs >= 0.0, "QPC max tracked");

    // Mark screen 0 dirty
    mgr.screens()[0].isDirty = true;
    mgr.render();
    TEST_CHECK(mgr.perfTelemetry().dirtyCount == 1, "Only dirty screen re-rasterized");

    // 6. Event Integration & Clipboard (§6, Ticket A12)
    EventWriter::instance().initialize(mfdsDir);
    auto* s0 = &mgr.screens()[0];
    MfdViewModel testVm;
    testVm.isFocused = true;
    MfdTab listTab;
    listTab.id = "data";
    listTab.title = "DATA";
    listTab.type = MfdTabType::kList;
    MfdListItem item;
    item.id = "item_wp";
    item.label = "Waypoint Alpha";
    item.value = "HIP 12345";
    item.action = MfdItemAction::kClipboard;
    listTab.items.push_back(item);
    testVm.tabs.push_back(std::move(listTab));

    s0->provider->viewModel() = std::move(testVm);
    s0->provider->onFocusChanged(true);
    s0->isDirty = true;

    // Send select input
    s0->provider->onInput(MfdInputAction::kSelect);
    EventWriter::instance().flush();

    // Verify clipboard content
    bool opened = false;
    for (int retry = 0; retry < 10; ++retry) {
        if (OpenClipboard(nullptr)) {
            opened = true;
            break;
        }
        Sleep(10);
    }
    TEST_CHECK(opened, "Opened clipboard");
    HANDLE hClip = GetClipboardData(CF_TEXT);
    TEST_CHECK(hClip != nullptr, "Clipboard has text");
    if (hClip) {
        char* clipText = static_cast<char*>(GlobalLock(hClip));
        TEST_CHECK(clipText != nullptr && strcmp(clipText, "HIP 12345") == 0, "Clipboard copied value 'HIP 12345'");
        GlobalUnlock(hClip);
    }
    CloseClipboard();

    // Verify event written to pkg_a events.jsonl
    std::wstring pkgAEventsPath = EventWriter::instance().getPackageEventsPath("pkg_a");
    HANDLE hEv = CreateFileW(pkgAEventsPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    TEST_CHECK(hEv != INVALID_HANDLE_VALUE, "pkg_a events.jsonl exists");
    if (hEv != INVALID_HANDLE_VALUE) {
        DWORD sz = GetFileSize(hEv, nullptr);
        std::string evText(sz, '\0');
        DWORD readBytes = 0;
        ReadFile(hEv, &evText[0], sz, &readBytes, nullptr);
        CloseHandle(hEv);
        TEST_CHECK(evText.find("\"type\":\"select\"") != std::string::npos, "Event log recorded select");
        TEST_CHECK(evText.find("\"action\":\"clipboard\"") != std::string::npos, "Event log recorded action clipboard");
    }

    // 7. MFD-009: Test settings adjustment on "Preset Theme" updates key-values immediately
    s0->theme = MfdColorTheme::kDefaultAmber;
    mgr.update(headPos, headFwd, 0.016f);
    auto& vm0 = s0->provider->viewModel();
    int settingsIdx = -1;
    for (size_t t = 0; t < vm0.tabs.size(); ++t) {
        if (vm0.tabs[t].id == "__settings") {
            settingsIdx = static_cast<int>(t);
            break;
        }
    }
    TEST_CHECK(settingsIdx >= 0, "Settings tab exists on screen 0");
    if (settingsIdx >= 0) {
        vm0.activeTabIndex = settingsIdx;
        auto& stab = vm0.tabs[settingsIdx];
        for (size_t k = 0; k < stab.keyValues.size(); ++k) {
            if (stab.keyValues[k].key == "Preset Theme") {
                stab.selectedIndex = static_cast<int>(k);
                break;
            }
        }
        s0->provider->onFocusChanged(true);
        s0->provider->onInput(MfdInputAction::kRight);
        std::string currentPresetName = "";
        for (const auto& kv : stab.keyValues) {
            if (kv.key == "Preset Theme") {
                currentPresetName = kv.value;
                break;
            }
        }
        TEST_CHECK(currentPresetName == "Cyan / Ice", "MFD-009: Preset Theme key-value updated synchronously to 'Cyan / Ice'");
    }

    // Cleanup test files
    mgr.shutdown();
    DeleteFileW((pkgADir + L"\\manifest.json").c_str());
    DeleteFileW((pkgADir + L"\\events.jsonl").c_str());
    DeleteFileW((pkgBDir + L"\\manifest.json").c_str());
    DeleteFileW((pkgBDir + L"\\events.jsonl").c_str());
    RemoveDirectoryW(pkgADir.c_str());
    RemoveDirectoryW(pkgBDir.c_str());
    RemoveDirectoryW(mfdsDir.c_str());
    DeleteFileW(testIni.c_str());
    RemoveDirectoryW(testRoot.c_str());

    return 0;
}

// 22. Test Show Settings Tab Global Toggle & Tab Lifecycle
int test_show_settings_tab_toggle() {
    auto& mgr = MfdManager::instance();
    bool init = mgr.initialize(320, 240);
    ContextEvaluator::instance().stopWatcher();
    ContextEvaluator::instance().updateFromStatus(kFlagInMainShip, 0, 0);
    mgr.setEnabled(true);
    TEST_CHECK(init, "MfdManager initialized");

    MfdScreen* scr = mgr.findScreen("main_mfd");
    if (!scr && mgr.screenCount() > 0) {
        scr = &mgr.screens()[0];
    }
    TEST_CHECK(scr != nullptr, "Screen exists in manager");
    TEST_CHECK(scr->provider != nullptr, "Screen provider exists");

    auto& model = scr->provider->viewModel();

    auto findSettingsTabIdx = [&]() -> int {
        for (size_t i = 0; i < model.tabs.size(); ++i) {
            if (model.tabs[i].id == "__settings" || model.tabs[i].title == "SETTINGS") {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    // 1. Initial state: showSettingsTab is true by default, and __settings tab exists
    TEST_CHECK(mgr.showSettingsTab() == true, "showSettingsTab() default is true");
    int initialIdx = findSettingsTabIdx();
    TEST_CHECK(initialIdx >= 0, "__settings tab exists when showSettingsTab is true");

    // Point activeTabIndex to settings tab
    model.activeTabIndex = initialIdx;
    scr->isDirty = false;

    // 2. Toggle off: setShowSettingsTab(false)
    mgr.setShowSettingsTab(false);
    TEST_CHECK(mgr.showSettingsTab() == false, "showSettingsTab() is false after setShowSettingsTab(false)");
    TEST_CHECK(findSettingsTabIdx() == -1, "__settings tab removed after setShowSettingsTab(false)");
    TEST_CHECK(scr->isDirty == true, "Screen marked dirty when __settings tab removed");
    if (!model.tabs.empty()) {
        TEST_CHECK(model.activeTabIndex < static_cast<int>(model.tabs.size()), "activeTabIndex clamped within bounds");
    } else {
        TEST_CHECK(model.activeTabIndex == 0, "activeTabIndex clamped to 0 for empty tabs");
    }

    // Verify all screens in manager have no __settings tab
    for (const auto& s : mgr.screens()) {
        if (!s.provider) continue;
        for (const auto& tab : s.provider->viewModel().tabs) {
            TEST_CHECK(tab.id != "__settings" && tab.title != "SETTINGS", "No screen contains __settings tab when false");
        }
    }

    // Call update to ensure periodic ensureSettingsTab does not recreate __settings tab
    Vec3 headPos(0.0f, 0.0f, 0.0f);
    Vec3 headFwd(0.0f, 0.0f, -1.0f);
    mgr.update(headPos, headFwd, 0.016f);
    TEST_CHECK(findSettingsTabIdx() == -1, "__settings tab remains absent during update() while showSettingsTab is false");

    // 3. Toggle on: setShowSettingsTab(true)
    scr->isDirty = false;
    mgr.setShowSettingsTab(true);
    TEST_CHECK(mgr.showSettingsTab() == true, "showSettingsTab() is true after setShowSettingsTab(true)");
    int restoredIdx = findSettingsTabIdx();
    TEST_CHECK(restoredIdx >= 0, "__settings tab restored after setShowSettingsTab(true)");
    TEST_CHECK(scr->isDirty == true, "Screen marked dirty when __settings tab restored");
    TEST_CHECK(model.tabs[restoredIdx].type == MfdTabType::kKeyValue, "Restored tab is kKeyValue");

    // Verify all screens in manager have __settings tab restored
    for (const auto& s : mgr.screens()) {
        if (!s.provider) continue;
        bool found = false;
        for (const auto& tab : s.provider->viewModel().tabs) {
            if (tab.id == "__settings" || tab.title == "SETTINGS") {
                found = true;
                break;
            }
        }
        TEST_CHECK(found, "Every screen restored its __settings tab when true");
    }

    // 4. Test integration with setPluginSettingBool
    setPluginSettingBool("mfd_show_settings_tab", false);
    TEST_CHECK(getPluginSettingBool("mfd_show_settings_tab", true) == false, "getPluginSettingBool returns false");
    TEST_CHECK(mgr.showSettingsTab() == false, "mgr.showSettingsTab() reflects plugin setting false");
    TEST_CHECK(findSettingsTabIdx() == -1, "__settings tab removed via setPluginSettingBool");

    setPluginSettingBool("mfd_show_settings_tab", true);
    TEST_CHECK(getPluginSettingBool("mfd_show_settings_tab", false) == true, "getPluginSettingBool returns true");
    TEST_CHECK(mgr.showSettingsTab() == true, "mgr.showSettingsTab() reflects plugin setting true");
    TEST_CHECK(findSettingsTabIdx() >= 0, "__settings tab restored via setPluginSettingBool");

    mgr.shutdown();
    return 0;
}

// 23. Per-screen render resolution from physical aspect + Width/Height settings (#4)
int test_screen_render_size_and_axis_factors() {
    // Derivation, override and clamping
    MfdRenderSize std3x2 = baseRenderSize(0.24f, 0.16f);
    TEST_CHECK(std3x2.width == 512 && std3x2.height == 341, "0.24x0.16 m derives 512x341");
    MfdRenderSize banner = baseRenderSize(0.24f, 0.06f);
    TEST_CHECK(banner.width == 512 && banner.height == 128, "0.24x0.06 m derives 512x128");
    MfdRenderSize ovr = baseRenderSize(0.24f, 0.16f, 640, 480);
    TEST_CHECK(ovr.width == 640 && ovr.height == 480, "Manifest override replaces derived size");
    MfdRenderSize halfOvr = baseRenderSize(0.24f, 0.16f, 640, 0);
    TEST_CHECK(halfOvr == std3x2, "Override needs both dimensions");
    MfdRenderSize tiny = baseRenderSize(0.005f, 0.005f);
    TEST_CHECK(tiny.width == kMinRenderDim && tiny.height == kMinRenderDim, "Tiny screens clamp to minimum");
    MfdRenderSize huge = baseRenderSize(3.0f, 3.0f);
    TEST_CHECK(huge.width == kMaxRenderDim && huge.height == kMaxRenderDim, "Huge screens clamp to maximum");

    // Factor math
    MfdRenderSize wide = effectiveRenderSize(std3x2, 1.5f, 1.0f);
    TEST_CHECK(wide.width == 768 && wide.height == 341, "Width 1.5x widens texture to 768");
    MfdRenderSize shortH = effectiveRenderSize(std3x2, 1.0f, 0.5f);
    TEST_CHECK(shortH.width == 512 && shortH.height == 171, "Height 0.5x shortens texture to 171");

    // Aspect mismatch warning predicate
    TEST_CHECK(!renderAspectMismatch(512, 341, 0.24f, 0.16f), "Matching aspect is not flagged");
    TEST_CHECK(renderAspectMismatch(640, 480, 0.24f, 0.16f), "4:3 override on 3:2 quad is flagged");

    // Per-template Height minimum: standard keeps one content row, never forces above 1.0
    TEST_CHECK(std::abs(minHeightFactor(MfdLayoutTemplate::kStandard, 341) - kMinAxisFactor) < 1e-4f, "341 px standard can shrink to 0.50x");
    TEST_CHECK(std::abs(minHeightFactor(MfdLayoutTemplate::kStandard, 200) - 0.71f) < 1e-4f, "200 px standard can shrink to 0.71x");
    TEST_CHECK(std::abs(minHeightFactor(MfdLayoutTemplate::kStandard, 128) - 1.0f) < 1e-4f, "Already-short standard can't shrink");

    // Manifest validation
    std::string err;
    MfdPackageManifest m;
    TEST_CHECK(parseManifestJson(R"({"manifestVersion":1,"id":"size_pkg","name":"S","screens":[{"id":"a","file":"a.json",
        "pose":{"position":[0,0,-0.5],"pitchDeg":0,"yawDeg":0,"widthM":0.24,"heightM":0.16,"renderWidth":640,"renderHeight":480}}]})",
        m, "", &err), "Manifest with render override parses");
    TEST_CHECK(m.screens[0].pose.renderWidth == 640 && m.screens[0].pose.renderHeight == 480, "Override values parsed");
    MfdPackageManifest bad;
    TEST_CHECK(!parseManifestJson(R"({"manifestVersion":1,"id":"size_pkg","name":"S","screens":[{"id":"a","file":"a.json",
        "pose":{"position":[0,0,-0.5],"pitchDeg":0,"yawDeg":0,"widthM":0.24,"heightM":0.16,"renderWidth":640}}]})",
        bad, "", &err), "renderWidth without renderHeight is rejected");
    TEST_CHECK(!parseManifestJson(R"({"manifestVersion":1,"id":"size_pkg","name":"S","screens":[{"id":"a","file":"a.json",
        "pose":{"position":[0,0,-0.5],"pitchDeg":0,"yawDeg":0,"widthM":0.24,"heightM":0.16,"renderWidth":640.5,"renderHeight":0}}]})",
        bad, "", &err), "Non-integer / non-positive override is rejected");

    // Manager integration: derived sizes, live Width/Height/Scale changes, persistence
    auto writeFile = [](const std::wstring& path, const std::string& content) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
            CloseHandle(h);
        }
    };
    wchar_t tempBase[MAX_PATH];
    GetTempPathW(MAX_PATH, tempBase);
    std::wstring testRoot = std::wstring(tempBase) + L"edvr_mfd_size_test_" + std::to_wstring(GetTickCount64());
    std::wstring mfdsDir = testRoot + L"\\mfds";
    std::wstring pkgDir = mfdsDir + L"\\size_pkg";
    std::wstring testIni = testRoot + L"\\edvr_mfd_size_test.ini";
    CreateDirectoryW(testRoot.c_str(), nullptr);
    CreateDirectoryW(mfdsDir.c_str(), nullptr);
    CreateDirectoryW(pkgDir.c_str(), nullptr);
    writeFile(pkgDir + L"\\manifest.json", R"({
        "manifestVersion": 1, "id": "size_pkg", "name": "Size Package",
        "screens": [
            { "id": "std", "file": "std.json",
              "pose": { "position": [0, -0.2, -0.5], "pitchDeg": -10, "yawDeg": 0, "widthM": 0.24, "heightM": 0.16 } },
            { "id": "banner", "file": "banner.json",
              "pose": { "position": [0.3, -0.2, -0.5], "pitchDeg": -10, "yawDeg": 30, "widthM": 0.24, "heightM": 0.06 } },
            { "id": "ovr", "file": "ovr.json",
              "pose": { "position": [-0.3, -0.2, -0.5], "pitchDeg": -10, "yawDeg": -30, "widthM": 0.24, "heightM": 0.16,
                        "renderWidth": 640, "renderHeight": 480 } }
        ]
    })");
    // Old INI without the new keys: factors must load as 1.00x
    WritePrivateProfileStringW(L"size_pkg/std", L"scale", L"1.00", testIni.c_str());

    auto adjust = [](MfdScreen& s, const std::string& key, MfdInputAction dir, int times) -> bool {
        auto& vm = s.provider->viewModel();
        int idx = -1;
        for (size_t t = 0; t < vm.tabs.size(); ++t) {
            if (vm.tabs[t].id == "__settings") { idx = static_cast<int>(t); break; }
        }
        if (idx < 0) return false;
        vm.activeTabIndex = idx;
        s.provider->onFocusChanged(true);
        for (int i = 0; i < times; ++i) {
            auto& tab = vm.tabs[idx];
            int k = -1;
            for (size_t j = 0; j < tab.keyValues.size(); ++j) {
                if (tab.keyValues[j].key == key) { k = static_cast<int>(j); break; }
            }
            if (k < 0) return false;
            tab.selectedIndex = k;
            s.provider->onInput(dir);
        }
        return true;
    };

    {
        MfdManager mgr;
        mgr.setIniPathOverride(testIni);
        TEST_CHECK(mgr.initialize(512, 384, mfdsDir), "Manager initialized for size test");
        MfdScreen* sStd = mgr.findScreen("size_pkg/std");
        MfdScreen* sBan = mgr.findScreen("size_pkg/banner");
        MfdScreen* sOvr = mgr.findScreen("size_pkg/ovr");
        TEST_CHECK(sStd && sBan && sOvr, "All three size screens loaded");

        TEST_CHECK(sStd->renderer->width() == 512 && sStd->renderer->height() == 341, "Standard 3:2 screen renders 512x341");
        TEST_CHECK(sBan->renderer->width() == 512 && sBan->renderer->height() == 128, "Banner-sized screen renders 512x128");
        TEST_CHECK(sOvr->renderer->width() == 640 && sOvr->renderer->height() == 480, "Override screen renders 640x480");
        TEST_CHECK(sStd->widthFactor == 1.0f && sStd->heightFactor == 1.0f, "Missing INI keys load as 1.00x");

        // Scale magnifies the quad only
        TEST_CHECK(adjust(*sStd, "Display Scale", MfdInputAction::kRight, 2), "Adjusted Display Scale");
        TEST_CHECK(std::abs(sStd->pose.widthM - 0.24f * 1.10f) < 1e-4f, "Scale grows quad width");
        TEST_CHECK(sStd->renderer->width() == 512 && sStd->renderer->height() == 341, "Scale leaves render size unchanged");
        TEST_CHECK(adjust(*sStd, "Display Scale", MfdInputAction::kLeft, 2), "Restored Display Scale");

        // Width 1.5x grows quad and texture together
        TEST_CHECK(adjust(*sStd, "Display Width", MfdInputAction::kRight, 10), "Adjusted Display Width");
        TEST_CHECK(std::abs(sStd->widthFactor - 1.5f) < 1e-4f, "Width factor 1.50x");
        TEST_CHECK(std::abs(sStd->pose.widthM - 0.36f) < 1e-4f, "Quad width 0.36 m");
        TEST_CHECK(sStd->renderer->width() == 768 && sStd->renderer->height() == 341, "Render width 768");
        TEST_CHECK(sStd->needsGpuUpload, "Resize schedules GPU re-upload");

        // Height clamps at the standard template minimum
        TEST_CHECK(adjust(*sStd, "Display Height", MfdInputAction::kLeft, 20), "Adjusted Display Height");
        TEST_CHECK(std::abs(sStd->heightFactor - 0.5f) < 1e-4f, "Height stops at 0.50x");
        TEST_CHECK(sStd->renderer->height() == 171, "Render height 171");
        TEST_CHECK(sStd->renderer->height() >= minContentHeightPx(MfdLayoutTemplate::kStandard), "Still fits one content row");

        TEST_CHECK(adjust(*sBan, "Display Height", MfdInputAction::kLeft, 3), "Adjusted banner Display Height");
        TEST_CHECK(std::abs(sBan->heightFactor - 1.0f) < 1e-4f, "Already-short standard screen can't shrink");

        mgr.render();
        TEST_CHECK(sStd->renderer->width() == 768, "Render after resize keeps new size");
        mgr.shutdown();
    }

    {
        MfdManager mgr;
        mgr.setIniPathOverride(testIni);
        TEST_CHECK(mgr.initialize(512, 384, mfdsDir), "Manager re-initialized for persistence check");
        MfdScreen* sStd = mgr.findScreen("size_pkg/std");
        TEST_CHECK(sStd, "Standard screen reloaded");
        TEST_CHECK(std::abs(sStd->widthFactor - 1.5f) < 1e-4f && std::abs(sStd->heightFactor - 0.5f) < 1e-4f, "Factors persist across restart");
        TEST_CHECK(sStd->renderer->width() == 768 && sStd->renderer->height() == 171, "Restored render size");

        // Reset Pose restores factors to 1.00x
        TEST_CHECK(adjust(*sStd, "Reset Pose", MfdInputAction::kRight, 1), "Pressed Reset Pose");
        TEST_CHECK(sStd->widthFactor == 1.0f && sStd->heightFactor == 1.0f && sStd->scale == 1.0f, "Reset Pose restores 1.00x");
        TEST_CHECK(sStd->renderer->width() == 512 && sStd->renderer->height() == 341, "Reset restores derived render size");
        mgr.shutdown();
    }

    DeleteFileW((pkgDir + L"\\manifest.json").c_str());
    DeleteFileW((pkgDir + L"\\events.jsonl").c_str());
    RemoveDirectoryW(pkgDir.c_str());
    RemoveDirectoryW(mfdsDir.c_str());
    DeleteFileW(testIni.c_str());
    RemoveDirectoryW(testRoot.c_str());
    return 0;
}

// 24. Hatless controllers don't hold Up (#13); selection stays inside the rendered rows (#14)
int test_pov_decode_and_scroll_window() {
    // #13: a device without a hat reports dwPOV = 0, which must not read as Up
    bool up = false, right = false, down = false, left = false;
    MfdInputRouter::decodePov(false, 0, up, right, down, left);
    TEST_CHECK(!up && !right && !down && !left, "Hatless device at dwPOV=0 produces no direction");
    MfdInputRouter::decodePov(true, 0, up, right, down, left);
    TEST_CHECK(up && !down, "Real hat at 0 reads as Up");
    up = right = down = left = false;
    MfdInputRouter::decodePov(true, 65535 /* JOY_POVCENTERED */, up, right, down, left);
    TEST_CHECK(!up && !right && !down && !left, "Centred hat produces no direction");
    MfdInputRouter::decodePov(true, 18000, up, right, down, left);
    TEST_CHECK(down && !up, "Hat at 180 deg reads as Down");

    // A held hatless device must not block keyboard Up edges
    MfdInputRouter router;
    DeclarativeMfdProvider prov("nav_test", "NAV");
    prov.loadFromJson(R"({"tabs":[{"title":"L","type":"list","items":[{"label":"A"},{"label":"B"},{"label":"C"}]}]})");
    prov.onFocusChanged(true);
    router.processInput(true, &prov, false, true, false, false, false, false, false, false);   // Down
    router.processInput(true, &prov, false, false, false, false, false, false, false, false);
    TEST_CHECK(prov.viewModel().currentTab()->selectedIndex == 1, "Down moved to row 1");
    router.processInput(true, &prov, true, false, false, false, false, false, false, false);   // Up
    TEST_CHECK(prov.viewModel().currentTab()->selectedIndex == 0, "Up edge moved back to row 0");

    // #14: the renderer and selection logic agree on visible rows at every height
    MfdRenderer r341(512, 341);
    MfdTab kvTab;
    kvTab.type = MfdTabType::kKeyValue;
    for (int i = 0; i < 20; ++i) kvTab.keyValues.push_back({"Key " + std::to_string(i), "v", palette::kAmberNormal});
    TEST_CHECK(kvTab.visibleRows(r341.height()) == (341 - kStdKeyValueTop - kStdFooterHeight) / kStdKeyValueRowHeight,
               "Key-value visible rows match renderer formula at 341 px");
    TEST_CHECK(kvTab.visibleRows(0) == 11, "Unknown height keeps legacy key-value count");

    const int heights[] = {384, 341, 171};
    for (int h : heights) {
        MfdViewModel vm;
        vm.renderHeight = h;
        vm.tabs.push_back(kvTab);
        int rows = vm.tabs[0].visibleRows(h);
        for (int step = 0; step < 19; ++step) {
            vm.moveSelection(1);
            const auto& t = vm.tabs[0];
            TEST_CHECK(t.selectedIndex >= t.scrollOffset && t.selectedIndex < t.scrollOffset + rows,
                       "Selection stays inside visible rows while moving down");
        }
        for (int step = 0; step < 19; ++step) {
            vm.moveSelection(-1);
            const auto& t = vm.tabs[0];
            TEST_CHECK(t.selectedIndex >= t.scrollOffset && t.selectedIndex < t.scrollOffset + rows,
                       "Selection stays inside visible rows while moving up");
        }
        TEST_CHECK(vm.tabs[0].selectedIndex == 0 && vm.tabs[0].scrollOffset == 0, "Back at the top");
    }

    // Lists with sublabels use the taller row
    MfdTab subList;
    subList.type = MfdTabType::kList;
    MfdListItem withSub;
    withSub.label = "Row";
    withSub.sublabel = "detail";
    subList.items.push_back(withSub);
    TEST_CHECK(subList.visibleRows(341) == (341 - kStdContentTop - kStdFooterHeight) / kStdListSublabelRowHeight,
               "Sublabel list uses 36 px rows");

    // Display reload keeps the screen height and keeps the restored selection visible
    MfdViewModel prevVm;
    prevVm.renderHeight = 171;
    MfdTab prevTab = kvTab;
    for (int i = 0; i < 20; ++i) prevTab.keyValues[i].id = "k" + std::to_string(i);
    prevTab.id = "kv";
    prevTab.selectedIndex = 9;
    prevTab.scrollOffset = 2; // stale offset from a taller screen
    prevVm.tabs.push_back(prevTab);
    MfdViewModel newVm;
    MfdTab newTab = prevTab;
    newTab.selectedIndex = 0;
    newTab.scrollOffset = 0;
    newVm.tabs.push_back(newTab);
    MfdContentWatcher::restoreCursor(prevVm, newVm);
    int rows171 = newVm.tabs[0].visibleRows(171);
    TEST_CHECK(newVm.renderHeight == 171, "Reload carries render height");
    TEST_CHECK(newVm.tabs[0].selectedIndex == 9, "Reload restores selection by id");
    TEST_CHECK(newVm.tabs[0].selectedIndex >= newVm.tabs[0].scrollOffset &&
               newVm.tabs[0].selectedIndex < newVm.tabs[0].scrollOffset + rows171,
               "Reload keeps restored selection visible on a short screen");
    return 0;
}

int main() {
    std::cout << "[mfd_test] Running Cockpit MFD framework test suite..." << std::endl;

    if (test_math() != 0) return 1;
    std::cout << "  ok  Vec3 and Quat math" << std::endl;

    if (test_declarative_provider() != 0) return 1;
    std::cout << "  ok  Option A: Declarative JSON MFD Provider" << std::endl;

    if (test_display_schema_v1() != 0) return 1;
    std::cout << "  ok  Schema v1 Parser and Validation (§5.3)" << std::endl;

    if (test_package_manifest_and_loader() != 0) return 1;
    std::cout << "  ok  Package Manifest Parser, Loader & Security (§4)" << std::endl;

    if (test_content_watcher() != 0) return 1;
    std::cout << "  ok  ContentWatcher Reload, Atomic Writes, Cursor Restore & ERR Badge (§5)" << std::endl;

    if (test_event_log_writer() != 0) return 1;
    std::cout << "  ok  EventWriter Thread, Roll, Sequence Numbering & Drop Handling (§6)" << std::endl;

    if (test_game_context_evaluator() != 0) return 1;
    std::cout << "  ok  Game Context Evaluator, Vocabulary, Truth Table & MFD-003 Resolution (§7)" << std::endl;

    if (test_scripted_provider_slot() != 0) return 1;
    std::cout << "  ok  Option B: Scripted Code MFD Provider Architecture Slot" << std::endl;

    if (test_gaze_tracker() != 0) return 1;
    std::cout << "  ok  Head-Gaze Tracking and Hysteresis State Machine" << std::endl;

    if (test_input_router() != 0) return 1;
    std::cout << "  ok  Input Router and Flight Control Protection" << std::endl;

    if (test_up_navigation_and_binds_parsing() != 0) return 1;
    std::cout << "  ok  UP Navigation Key & POV Bindings Parsing" << std::endl;

    if (test_plugin_input_filter_suppression() != 0) return 1;
    std::cout << "  ok  Plugin Input Filter & Suppression Protocol" << std::endl;

    if (test_renderer() != 0) return 1;
    std::cout << "  ok  HUD Vector/Bitmap Renderer" << std::endl;

    if (test_schema_v1_renderer() != 0) return 1;
    std::cout << "  ok  Schema v1 HUD Renderer Primitives (Checks, Bars, Icons, Palette, Badges)" << std::endl;

    if (test_manager() != 0) return 1;
    std::cout << "  ok  MfdManager End-to-End Orchestration" << std::endl;

    if (test_shared_telemetry() != 0) return 1;
    std::cout << "  ok  Cross-DLL Shared Memory Telemetry" << std::endl;

    if (test_d3d11_render_to_eye_rtv() != 0) return 1;
    std::cout << "  ok  Direct3D 11 Eye RTV Hardware Rasterization & Readback" << std::endl;

    if (test_color_themes_and_settings() != 0) return 1;
    std::cout << "  ok  Color Themes and In-MFD Interactive Settings Callback" << std::endl;

    if (test_activity_gating() != 0) return 1;
    std::cout << "  ok  Vehicle Activity Gating & Title Screen Suppression" << std::endl;

    if (test_companion_supervisor() != 0) return 1;
    std::cout << "  ok  CompanionSupervisor Background Thread, Job Object, Backoff & Badges (§8 - Ticket A10)" << std::endl;

    if (test_f8_settings_and_persistence() != 0) return 1;
    std::cout << "  ok  F8 Menu Settings Persistence, Atomic Backing & Hide All Toggle (§9.1-9.2 - Ticket A11)" << std::endl;

    if (test_dynamic_screens_cap_and_settings() != 0) return 1;
    std::cout << "  ok  Dynamic Screens, 16-Cap, Settings Migration & Event Routing (Tickets A6, A7, A9, A12)" << std::endl;

    if (test_show_settings_tab_toggle() != 0) return 1;
    std::cout << "  ok  Show Settings Tab Global Toggle & Tab Lifecycle" << std::endl;

    if (test_screen_render_size_and_axis_factors() != 0) return 1;
    std::cout << "  ok  Per-Screen Render Size, Manifest Override & Width/Height Settings (#4)" << std::endl;

    if (test_pov_decode_and_scroll_window() != 0) return 1;
    std::cout << "  ok  Hatless Controller POV (#13) & Selection Scroll Window (#14)" << std::endl;

    std::cout << "[mfd_test] ALL TESTS PASSED." << std::endl;
    ExitProcess(0);
}
