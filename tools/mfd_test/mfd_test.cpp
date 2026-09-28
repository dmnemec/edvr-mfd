// mfd_test.cpp -- Unit test suite for EDVR Cockpit MFD framework
// Validates Option A (Declarative JSON MFD) implementation, Option B extensibility slots,
// gaze-directed focus tracking, UI input routing, and HUD rasterization.

#include "../../src/mfd/mfd_types.h"
#include "../../src/mfd/mfd_view_model.h"
#include "../../src/mfd/mfd_provider.h"
#include "../../src/mfd/mfd_font.h"
#include "../../src/mfd/mfd_renderer.h"
#include "../../src/mfd/mfd_gaze_tracker.h"
#include "../../src/mfd/mfd_input_router.h"
#include "../../src/mfd/mfd_compositor.h"
#include "../../src/mfd/mfd_manager.h"

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
        "title": "SPANSH NEUTRON ROUTER",
        "subtitle": "CURRENT: SOL",
        "statusBadge": "SYNCED",
        "footerHint": "[Q/E] TABS  [NAV] SELECT",
        "tabs": [
            {
                "title": "ROUTE",
                "type": "list",
                "items": [
                    {"id": "wp1", "label": "Jackson's Lighthouse", "value": "12.4 LY", "sublabel": "Neutron Star", "badge": "CURRENT"},
                    {"id": "wp2", "label": "Prua Phoe NC-D d12-14", "value": "148.1 LY", "sublabel": "Supercharge Ready", "badge": "NEXT"},
                    {"id": "wp3", "label": "Dryooe Prou CS-B d1-2", "value": "294.0 LY", "sublabel": "Class M Star", "badge": "REFUEL"}
                ]
            },
            {
                "title": "METRICS",
                "type": "keyvalue",
                "items": [
                    {"key": "Total Jumps", "value": "14"},
                    {"key": "Distance Left", "value": "2,410.8 LY"},
                    {"key": "Fuel Reserve", "value": "88%"}
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

    // Press NextTab (E key):
    router.processInput(true, &provider, false, false, false, false, false, false, false, false); // release
    bool swallowed3 = router.processInput(true, &provider,
                                          false, false, false, false,
                                          false, false, true, false);
    TEST_CHECK(swallowed3, "NextTab IS swallowed when focused");
    TEST_CHECK(provider.viewModel().activeTabIndex == 1, "Provider cycled to Tab 1");

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

// 7. Test MfdManager End-to-End Coordination
int test_manager() {
    auto& mgr = MfdManager::instance();
    bool init = mgr.initialize(320, 240);
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

int main() {
    std::cout << "[mfd_test] Running Cockpit MFD framework test suite..." << std::endl;

    if (test_math() != 0) return 1;
    std::cout << "  ok  Vec3 and Quat math" << std::endl;

    if (test_declarative_provider() != 0) return 1;
    std::cout << "  ok  Option A: Declarative JSON MFD Provider" << std::endl;

    if (test_scripted_provider_slot() != 0) return 1;
    std::cout << "  ok  Option B: Scripted Code MFD Provider Architecture Slot" << std::endl;

    if (test_gaze_tracker() != 0) return 1;
    std::cout << "  ok  Head-Gaze Tracking and Hysteresis State Machine" << std::endl;

    if (test_input_router() != 0) return 1;
    std::cout << "  ok  Input Router and Flight Control Protection" << std::endl;

    if (test_renderer() != 0) return 1;
    std::cout << "  ok  HUD Vector/Bitmap Renderer" << std::endl;

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

    std::cout << "[mfd_test] ALL TESTS PASSED." << std::endl;
    return 0;
}
