#include "mfd_manager.h"
#include "mfd_context.h"
#include "mfd_event_log.h"
#include "config.h"
#include "log.h"
#include "mfd_telemetry.h"

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <windows.h>
#include <shlobj.h>
#include <d3dcompiler.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d3dcompiler.lib")

#if __has_include(<openxr/openxr.h>)
#include <openxr/openxr.h>
#else
struct XrVector3f { float x, y, z; };
struct XrQuaternionf { float x, y, z, w; };
struct XrPosef { XrQuaternionf orientation; XrVector3f position; };
struct XrFovf { float angleLeft, angleRight, angleUp, angleDown; };
#endif

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace edvr::mfd {

namespace {

void copyToClipboard(const std::string& text) {
    if (text.empty()) return;
    bool opened = false;
    for (int retry = 0; retry < 5; ++retry) {
        if (OpenClipboard(nullptr)) {
            opened = true;
            break;
        }
        Sleep(5);
    }
    if (!opened) return;
    EmptyClipboard();
    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (hGlob) {
        char* p = static_cast<char*>(GlobalLock(hGlob));
        if (p) {
            memcpy(p, text.c_str(), text.size() + 1);
            GlobalUnlock(hGlob);
            SetClipboardData(CF_TEXT, hGlob);
        }
    }
    CloseClipboard();
}

struct MfdQuadCbData {
    float clipPos[4][4]; // 0: TL, 1: TR, 2: BL, 3: BR
    float uv[4][4];      // 0: TL, 1: TR, 2: BL, 3: BR
    float opacity;
    float pad[3];
};
static_assert(sizeof(MfdQuadCbData) == 144, "MfdQuadCbData size check");

const char* s_mfdQuadShader = R"(
cbuffer MfdQuadConstants : register(b0) {
    float4 clipPos[4];
    float4 uv[4];
    float opacity;
    float3 pad;
};

Texture2D sourceTexture : register(t0);
SamplerState linearSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

static const uint kQuadIdx[6] = { 0, 1, 2, 2, 1, 3 };

PSInput vs(uint vertexId : SV_VertexID) {
    PSInput output;
    uint idx = kQuadIdx[vertexId % 6];
    output.position = clipPos[idx];
    output.uv = uv[idx].xy;
    return output;
}

float4 ps(PSInput input) : SV_TARGET {
    float4 col = sourceTexture.Sample(linearSampler, input.uv);
    col.a *= opacity;
    return col;
}
)";

const char* themeName(MfdColorTheme theme) {
    switch (theme) {
        case MfdColorTheme::kDefaultAmber: return "Amber / Orange";
        case MfdColorTheme::kCyanIce:      return "Cyan / Ice";
        case MfdColorTheme::kMatrixGreen:  return "Matrix Green";
        case MfdColorTheme::kSolarWhite:   return "Solar White";
        case MfdColorTheme::kCrimson:      return "Crimson Red";
        case MfdColorTheme::kPurpleHaze:   return "Purple Haze";
        default:                           return "Amber / Orange";
    }
}

std::wstring getMfdPluginDir() {
    HMODULE hMod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&getMfdPluginDir), &hMod) && hMod) {
        wchar_t path[MAX_PATH]{};
        if (GetModuleFileNameW(hMod, path, MAX_PATH) > 0) {
            wchar_t* lastSlash = wcsrchr(path, L'\\');
            if (lastSlash) {
                *lastSlash = L'\0';
                return std::wstring(path);
            }
        }
    }
    return edvr::executableDirectory() + L"\\plugins\\edvr_mfd";
}

} // namespace

MfdManager& MfdManager::instance() {
    static MfdManager s_instance;
    return s_instance;
}

MfdManager::MfdManager() {
    m_compositor = std::make_unique<OpenXrQuadCompositor>();
    QueryPerformanceFrequency(&m_qpcFreq);
}

MfdManager::~MfdManager() {
    shutdown();
}

void MfdManager::setMfdsDirectory(const std::wstring& dir) {
    m_mfdsDir = dir;
    m_packagesLoaded = false;
}

std::wstring MfdManager::getIniPath() const {
    if (!m_iniPathOverride.empty()) {
        return m_iniPathOverride;
    }

    wchar_t localAppData[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) {
        std::wstring edvrDir = std::wstring(localAppData) + L"\\EDVR";
        CreateDirectoryW(edvrDir.c_str(), nullptr);
        return edvrDir + L"\\edvr_mfd.ini";
    }

    std::wstring pluginDir = getMfdPluginDir();
    CreateDirectoryW(pluginDir.c_str(), nullptr);
    return pluginDir + L"\\edvr_mfd.ini";
}

bool MfdManager::migrateLegacyIni(const std::wstring& iniPath) {
    if (iniPath.empty()) return false;
    if (GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    struct MigrationPair {
        const wchar_t* oldSection;
        const wchar_t* targetSection;
    };

    static const MigrationPair s_pairs[] = {
        { L"main_mfd", L"route/route" },
        { L"slot.main_mfd", L"route/route" },
        { L"left_mfd", L"ship/ship" },
        { L"slot.left_mfd", L"ship/ship" },
        { L"ship/power", L"ship/ship" },
        { L"right_mfd", L"exploration/exploration" },
        { L"slot.right_mfd", L"exploration/exploration" },
        { L"exploration/bio", L"exploration/exploration" }
    };

    bool anyMigrated = false;
    for (const auto& pair : s_pairs) {
        wchar_t testBuf[64]{};
        // Check if target section already exists (has any keys)
        if (GetPrivateProfileSectionW(pair.targetSection, testBuf, 64, iniPath.c_str()) > 0) {
            continue; // Target already configured, do not overwrite
        }

        // Check if legacy section exists
        if (GetPrivateProfileSectionW(pair.oldSection, testBuf, 64, iniPath.c_str()) > 0 ||
            GetPrivateProfileStringW(pair.oldSection, L"pos_x", L"", testBuf, 64, iniPath.c_str()) > 0 ||
            GetPrivateProfileStringW(pair.oldSection, L"scale", L"", testBuf, 64, iniPath.c_str()) > 0) {

            static const wchar_t* s_keys[] = {
                L"pos_x", L"pos_y", L"pos_z", L"pitch", L"yaw", L"scale",
                L"opacity", L"locked", L"auto_hide", L"theme", L"use_custom_color",
                L"custom_r", L"custom_g", L"custom_b", L"custom_a",
                L"dwell_delay", L"release_delay", L"cone_margin", L"anchor_offset_y"
            };

            for (const wchar_t* k : s_keys) {
                wchar_t val[128]{};
                if (GetPrivateProfileStringW(pair.oldSection, k, L"", val, 128, iniPath.c_str()) > 0) {
                    WritePrivateProfileStringW(pair.targetSection, k, val, iniPath.c_str());
                }
            }

            // Remove old section from INI
            WritePrivateProfileStringW(pair.oldSection, nullptr, nullptr, iniPath.c_str());
            Log::get().note("mfd_ini: migrated legacy INI section '%ls' to '%ls'\n", pair.oldSection, pair.targetSection);
            anyMigrated = true;
        }
    }

    return anyMigrated;
}

void MfdManager::applyAutoLayout(std::vector<MfdScreen>& screens) {
    constexpr float kRadius = 0.55f;
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

    struct AutoSlot {
        float y;
        float pitch;
        float yaw;
    };

    // Candidate slots per §9.4:
    // Row 1: y = -0.22m, pitch = -22 deg, yaw slots at 0, +-32, +-64 deg
    // Row 2: y = -0.02m, pitch = -10 deg, yaw slots at 0, +-32, +-64 deg
    // Row 3: y = +0.18m, pitch = +2 deg, yaw slots at 0, +-32, +-64 deg
    // Row 4: y = -0.42m, pitch = -34 deg, yaw slots at 0, +-32, +-64 deg
    static const AutoSlot s_candidateSlots[] = {
        // Row 1
        { -0.22f, -22.0f,   0.0f },
        { -0.22f, -22.0f,  32.0f },
        { -0.22f, -22.0f, -32.0f },
        { -0.22f, -22.0f,  64.0f },
        { -0.22f, -22.0f, -64.0f },
        // Row 2
        { -0.02f, -10.0f,   0.0f },
        { -0.02f, -10.0f,  32.0f },
        { -0.02f, -10.0f, -32.0f },
        { -0.02f, -10.0f,  64.0f },
        { -0.02f, -10.0f, -64.0f },
        // Row 3 (overflow row)
        {  0.18f,   2.0f,   0.0f },
        {  0.18f,   2.0f,  32.0f },
        {  0.18f,   2.0f, -32.0f },
        {  0.18f,   2.0f,  64.0f },
        {  0.18f,   2.0f, -64.0f },
        // Row 4 (overflow row)
        { -0.42f, -34.0f,   0.0f },
        { -0.42f, -34.0f,  32.0f },
        { -0.42f, -34.0f, -32.0f },
        { -0.42f, -34.0f,  64.0f },
        { -0.42f, -34.0f, -64.0f },
    };
    constexpr size_t kNumCandidates = sizeof(s_candidateSlots) / sizeof(s_candidateSlots[0]);

    bool slotUsed[kNumCandidates]{};

    // Mark candidate slots that conflict with any explicit manifest pose (±8° tolerance)
    for (size_t c = 0; c < kNumCandidates; ++c) {
        for (const auto& scr : screens) {
            if (scr.hasExplicitPose) {
                if (std::abs(scr.pitchDeg - s_candidateSlots[c].pitch) <= 8.0f &&
                    std::abs(scr.yawDeg - s_candidateSlots[c].yaw) <= 8.0f) {
                    slotUsed[c] = true;
                    break;
                }
            }
        }
    }

    // Allocate remaining candidate slots in load order to screens lacking an explicit pose
    for (auto& scr : screens) {
        if (!scr.hasExplicitPose) {
            for (size_t c = 0; c < kNumCandidates; ++c) {
                if (!slotUsed[c]) {
                    slotUsed[c] = true;
                    float yawRad = s_candidateSlots[c].yaw * kDegToRad;
                    float x = -kRadius * std::sin(yawRad);
                    float y = s_candidateSlots[c].y;
                    float z = -kRadius * std::cos(yawRad);

                    scr.pitchDeg = s_candidateSlots[c].pitch;
                    scr.yawDeg = s_candidateSlots[c].yaw;
                    scr.baseWidthM = 0.24f;
                    scr.baseHeightM = 0.16f;

                    scr.pose.position = Vec3(x, y, z);
                    scr.pose.orientation = Quat::fromEulerDegrees(scr.pitchDeg, scr.yawDeg, 0.0f);
                    scr.pose.widthM = scr.baseWidthM * scr.scale;
                    scr.pose.heightM = scr.baseHeightM * scr.scale;
                    scr.baselinePose = scr.pose;
                    break;
                }
            }
        }
    }
}

void MfdManager::setupScreenCallbacks(MfdScreen& screen) {
    if (!screen.provider) return;
    if (screen.provider->type() != MfdProviderType::kDeclarativeJson) return;

    auto* decl = static_cast<DeclarativeMfdProvider*>(screen.provider.get());
    std::string pkgId = screen.packageId;
    std::string scrId = screen.screenId;
    std::string qId = screen.qualifiedId.empty() ? screen.name : screen.qualifiedId;

    decl->setTabChangeCallback([this, pkgId, scrId, qId](const std::string& tabId, int tabIndex) {
        if (tabId != "__settings" && !pkgId.empty()) {
            EventWriter::instance().enqueue(
                MfdEventRecord::createTabChange(pkgId, scrId, tabId, tabIndex)
            );
        }
        auto* scr = findScreen(qId);
        if (scr) scr->isDirty = true;
    });

    decl->setNavCallback([this, pkgId, scrId, qId](const std::string& tabId, int itemIndex, const std::string& itemId, const std::string& itemLabel) {
        if (tabId != "__settings" && !pkgId.empty()) {
            EventWriter::instance().enqueue(
                MfdEventRecord::createNav(pkgId, scrId, tabId, itemIndex, itemId, itemLabel)
            );
        }
        auto* scr = findScreen(qId);
        if (scr) scr->isDirty = true;
    });

    decl->setSelectCallback([this, pkgId, scrId, qId](const std::string& tabId, int itemIndex, const std::string& itemId, const std::string& itemLabel, const std::string& action, const std::string& value) {
        auto* scr = findScreen(qId);
        if (tabId == "__settings") {
            if (scr) handleSettingsAdjustment(*scr, itemId.empty() ? itemLabel : itemId, 1);
        } else {
            if (action == "clipboard") {
                copyToClipboard(value.empty() ? itemLabel : value);
            }
            if (!pkgId.empty()) {
                EventWriter::instance().enqueue(
                    MfdEventRecord::createSelect(pkgId, scrId, tabId, itemIndex, itemId, itemLabel, action)
                );
            }
        }
        if (scr) scr->isDirty = true;
    });

    decl->setAdjustCallback([this, pkgId, scrId, qId](const std::string& tabId, int itemIndex, const std::string& itemId, const std::string& itemLabel, const std::string& key, int delta) {
        (void)itemIndex; (void)itemId; (void)itemLabel;
        auto* scr = findScreen(qId);
        if (tabId == "__settings") {
            if (scr) handleSettingsAdjustment(*scr, key, delta);
        } else {
            if (!pkgId.empty()) {
                EventWriter::instance().enqueue(
                    MfdEventRecord::createAdjust(pkgId, scrId, tabId, itemIndex, itemId, itemLabel, key, delta)
                );
            }
        }
        if (scr) scr->isDirty = true;
    });
}

void MfdManager::ensureSettingsTab(MfdScreen& screen) {
    if (!screen.provider) return;
    auto& model = screen.provider->viewModel();

    MfdTab* settingsTab = nullptr;
    for (auto& tab : model.tabs) {
        if (tab.id == "__settings" || tab.title == "SETTINGS") {
            tab.id = "__settings";
            settingsTab = &tab;
            break;
        }
    }

    if (!settingsTab) {
        MfdTab newTab;
        newTab.id = "__settings";
        newTab.title = "SETTINGS";
        newTab.type = MfdTabType::kKeyValue;
        model.tabs.push_back(std::move(newTab));
        settingsTab = &model.tabs.back();
    } else if (model.currentTab() != settingsTab) {
        return;
    }

    settingsTab->keyValues.clear();
    char buf[64];

    settingsTab->keyValues.push_back({
        "Tracking Mode",
        screen.trackingLocked ? "Locked (Cockpit)" : "Freemove (Head HUD)",
        screen.trackingLocked ? palette::kSuccessGreen : palette::kCyanAccent
    });

    snprintf(buf, sizeof(buf), "%+.2f m", screen.pose.position.x);
    settingsTab->keyValues.push_back({"Position X", buf, palette::kAmberBright});

    snprintf(buf, sizeof(buf), "%+.2f m", screen.pose.position.y);
    settingsTab->keyValues.push_back({"Position Y", buf, palette::kAmberBright});

    snprintf(buf, sizeof(buf), "%+.2f m", screen.pose.position.z);
    settingsTab->keyValues.push_back({"Position Z", buf, palette::kAmberBright});

    snprintf(buf, sizeof(buf), "%.0f deg", screen.pitchDeg);
    settingsTab->keyValues.push_back({"Pitch", buf, palette::kAmberBright});

    snprintf(buf, sizeof(buf), "%.0f deg", screen.yawDeg);
    settingsTab->keyValues.push_back({"Yaw", buf, palette::kAmberBright});

    snprintf(buf, sizeof(buf), "%.2fx", screen.scale);
    settingsTab->keyValues.push_back({"Display Scale", buf, palette::kAmberNormal});

    snprintf(buf, sizeof(buf), "%.0f%%", screen.opacity * 100.0f);
    settingsTab->keyValues.push_back({"Glass Opacity", buf, palette::kCyanAccent});

    const char* colorModeLabel = "Default (Amber)";
    MfdColor colorModeColor = palette::kAmberBright;
    if (screen.useCustomColor) {
        colorModeLabel = "Custom (RGBA)";
        colorModeColor = palette::kSuccessGreen;
    } else if (screen.theme != MfdColorTheme::kDefaultAmber) {
        colorModeLabel = "Preset Theme";
        colorModeColor = palette::kCyanAccent;
    }
    settingsTab->keyValues.push_back({"Color Mode", colorModeLabel, colorModeColor});

    if (screen.useCustomColor) {
        snprintf(buf, sizeof(buf), "#%02X%02X%02X (%d)", screen.customColor.r, screen.customColor.g, screen.customColor.b, screen.customColor.r);
        settingsTab->keyValues.push_back({"Custom Red (R)", buf, MfdColor(255, 60, 60)});

        snprintf(buf, sizeof(buf), "#%02X%02X%02X (%d)", screen.customColor.r, screen.customColor.g, screen.customColor.b, screen.customColor.g);
        settingsTab->keyValues.push_back({"Custom Green (G)", buf, MfdColor(60, 255, 60)});

        snprintf(buf, sizeof(buf), "#%02X%02X%02X (%d)", screen.customColor.r, screen.customColor.g, screen.customColor.b, screen.customColor.b);
        settingsTab->keyValues.push_back({"Custom Blue (B)", buf, MfdColor(60, 180, 255)});

        snprintf(buf, sizeof(buf), "%.0f%% (%d)", (screen.customColor.a / 255.0f) * 100.0f, screen.customColor.a);
        settingsTab->keyValues.push_back({"Custom Brightness (A)", buf, palette::kCyanAccent});
        settingsTab->keyValues.push_back({"Reset Colors", "[RESTORE DEFAULTS]", palette::kAmberDim});
    } else {
        settingsTab->keyValues.push_back({"Preset Theme", themeName(screen.theme), palette::kCyanAccent});
        if (screen.theme != MfdColorTheme::kDefaultAmber) {
            settingsTab->keyValues.push_back({"Reset Colors", "[RESTORE DEFAULTS]", palette::kAmberDim});
        }
    }

    settingsTab->keyValues.push_back({"Reset Pose", "[RESTORE DEFAULTS]", palette::kAmberDim});

    settingsTab->keyValues.push_back({
        "Auto-Hide Gaze",
        screen.autoHideUntilGaze ? "Enabled (Fade)" : "Disabled (Always On)",
        screen.autoHideUntilGaze ? palette::kSuccessGreen : palette::kAmberDim
    });

    snprintf(buf, sizeof(buf), "%.2f s", screen.gazeTracker.dwellTimeThreshold());
    settingsTab->keyValues.push_back({"Gaze Dwell Delay", buf, palette::kAmberNormal});

    snprintf(buf, sizeof(buf), "%.2f s", screen.gazeTracker.dwellExitTime());
    settingsTab->keyValues.push_back({"Gaze Release Delay", buf, palette::kAmberNormal});

    snprintf(buf, sizeof(buf), "%.2fx", screen.gazeTracker.hitMargin());
    settingsTab->keyValues.push_back({"Focus Cone Margin", buf, palette::kAmberNormal});

    snprintf(buf, sizeof(buf), "%+.2f", screen.gazeTracker.anchorOffsetY());
    settingsTab->keyValues.push_back({"Off-Center Y", buf, palette::kAmberNormal});
}

void MfdManager::handleSettingsAdjustment(MfdScreen& screen, const std::string& key, int delta) {
    bool isLeft = (delta < 0);
    float step = isLeft ? -1.0f : 1.0f;
    int stepInt = isLeft ? -5 : 5;

    if (key == "Tracking Mode") {
        screen.trackingLocked = !screen.trackingLocked;
        if (!screen.trackingLocked) {
            screen.pose.position = screen.baselinePose.position;
            screen.pose.orientation = Quat::fromEulerDegrees(screen.pitchDeg, screen.yawDeg, 0.0f);
        } else {
            screen.pose.position = screen.baselinePose.position;
            screen.pose.orientation = Quat::fromEulerDegrees(screen.pitchDeg, screen.yawDeg, 0.0f);
        }
    } else if (key == "Position X") {
        screen.pose.position.x += step * 0.02f;
        screen.baselinePose.position.x = screen.pose.position.x;
    } else if (key == "Position Y") {
        screen.pose.position.y += step * 0.02f;
        screen.baselinePose.position.y = screen.pose.position.y;
    } else if (key == "Position Z") {
        screen.pose.position.z += step * 0.02f;
        screen.baselinePose.position.z = screen.pose.position.z;
    } else if (key == "Pitch") {
        screen.pitchDeg += step * 2.0f;
        screen.pose.orientation = Quat::fromEulerDegrees(screen.pitchDeg, screen.yawDeg, 0.0f);
        screen.baselinePose.orientation = screen.pose.orientation;
    } else if (key == "Yaw") {
        screen.yawDeg += step * 2.0f;
        screen.pose.orientation = Quat::fromEulerDegrees(screen.pitchDeg, screen.yawDeg, 0.0f);
        screen.baselinePose.orientation = screen.pose.orientation;
    } else if (key == "Glass Opacity") {
        screen.opacity = (std::clamp)(screen.opacity + step * 0.05f, 0.0f, 1.0f);
    } else if (key == "Display Scale") {
        screen.scale = (std::clamp)(screen.scale + step * 0.05f, 0.50f, 2.0f);
        screen.pose.widthM = screen.baseWidthM * screen.scale;
        screen.pose.heightM = screen.baseHeightM * screen.scale;
        screen.baselinePose.widthM = screen.pose.widthM;
        screen.baselinePose.heightM = screen.pose.heightM;
    } else if (key == "Reset Pose") {
        screen.pose = screen.baselinePose;
        screen.scale = 1.0f;
        screen.pose.widthM = screen.baseWidthM;
        screen.pose.heightM = screen.baseHeightM;
        screen.baselinePose = screen.pose;
    } else if (key == "Color Mode") {
        int currentMode = screen.useCustomColor ? 2 : (screen.theme == MfdColorTheme::kDefaultAmber ? 0 : 1);
        int nextMode = (currentMode + (isLeft ? 2 : 1)) % 3;
        if (nextMode == 0) {
            screen.useCustomColor = false;
            screen.theme = MfdColorTheme::kDefaultAmber;
            screen.customColor = MfdColor(255, 110, 0, 255);
        } else if (nextMode == 1) {
            screen.useCustomColor = false;
            if (screen.theme == MfdColorTheme::kDefaultAmber) {
                screen.theme = MfdColorTheme::kCyanIce;
            }
            switch (screen.theme) {
                case MfdColorTheme::kDefaultAmber: screen.customColor.r = 255; screen.customColor.g = 110; screen.customColor.b = 0; break;
                case MfdColorTheme::kCyanIce:      screen.customColor.r = 60;  screen.customColor.g = 200; screen.customColor.b = 255; break;
                case MfdColorTheme::kMatrixGreen:  screen.customColor.r = 0;   screen.customColor.g = 255; screen.customColor.b = 128; break;
                case MfdColorTheme::kSolarWhite:   screen.customColor.r = 220; screen.customColor.g = 230; screen.customColor.b = 240; break;
                case MfdColorTheme::kCrimson:      screen.customColor.r = 255; screen.customColor.g = 40;  screen.customColor.b = 40; break;
                case MfdColorTheme::kPurpleHaze:   screen.customColor.r = 200; screen.customColor.g = 100; screen.customColor.b = 255; break;
            }
        } else {
            screen.useCustomColor = true;
        }
    } else if (key == "Preset Theme") {
        int themeIdx = static_cast<int>(screen.theme);
        themeIdx = (themeIdx + (isLeft ? 5 : 1)) % 6;
        screen.theme = static_cast<MfdColorTheme>(themeIdx);
        switch (screen.theme) {
            case MfdColorTheme::kDefaultAmber: screen.customColor.r = 255; screen.customColor.g = 110; screen.customColor.b = 0; break;
            case MfdColorTheme::kCyanIce:      screen.customColor.r = 60;  screen.customColor.g = 200; screen.customColor.b = 255; break;
            case MfdColorTheme::kMatrixGreen:  screen.customColor.r = 0;   screen.customColor.g = 255; screen.customColor.b = 128; break;
            case MfdColorTheme::kSolarWhite:   screen.customColor.r = 220; screen.customColor.g = 230; screen.customColor.b = 240; break;
            case MfdColorTheme::kCrimson:      screen.customColor.r = 255; screen.customColor.g = 40;  screen.customColor.b = 40; break;
            case MfdColorTheme::kPurpleHaze:   screen.customColor.r = 200; screen.customColor.g = 100; screen.customColor.b = 255; break;
        }
    } else if (key == "Reset Colors") {
        screen.useCustomColor = false;
        screen.theme = MfdColorTheme::kDefaultAmber;
        screen.customColor = MfdColor(255, 110, 0, 255);
    } else if (key == "Custom Red (R)") {
        int r = static_cast<int>(screen.customColor.r) + stepInt;
        screen.customColor.r = static_cast<uint8_t>(std::clamp(r, 0, 255));
    } else if (key == "Custom Green (G)") {
        int g = static_cast<int>(screen.customColor.g) + stepInt;
        screen.customColor.g = static_cast<uint8_t>(std::clamp(g, 0, 255));
    } else if (key == "Custom Blue (B)") {
        int b = static_cast<int>(screen.customColor.b) + stepInt;
        screen.customColor.b = static_cast<uint8_t>(std::clamp(b, 0, 255));
    } else if (key == "Custom Brightness (A)") {
        int a = static_cast<int>(screen.customColor.a) + stepInt;
        screen.customColor.a = static_cast<uint8_t>(std::clamp(a, 10, 255));
    } else if (key == "Auto-Hide Gaze") {
        screen.autoHideUntilGaze = !screen.autoHideUntilGaze;
    } else if (key == "Gaze Dwell Delay") {
        float dt = screen.gazeTracker.dwellTimeThreshold() + step * 0.10f;
        screen.gazeTracker.setDwellTimeThreshold((std::clamp)(dt, 0.05f, 4.0f));
    } else if (key == "Gaze Release Delay") {
        float dt = screen.gazeTracker.dwellExitTime() + step * 0.10f;
        screen.gazeTracker.setDwellExitTime((std::clamp)(dt, 0.05f, 4.0f));
    } else if (key == "Focus Cone Margin") {
        float m = screen.gazeTracker.hitMargin() + step * 0.05f;
        screen.gazeTracker.setHitMargin((std::clamp)(m, 0.80f, 2.50f));
    } else if (key == "Off-Center Y") {
        float offY = screen.gazeTracker.anchorOffsetY() + step * 0.05f;
        screen.gazeTracker.setAnchorOffset(0.0f, (std::clamp)(offY, -0.5f, 0.5f));
    }

    screen.isDirty = true;
    saveSettings();
}

void MfdManager::discoverAndLoadPackages() {
    if (m_packagesLoaded) return;
    m_packagesLoaded = true;

    if (m_mfdsDir.empty()) {
        m_mfdsDir = getDefaultMfdsDirectory();
    }

    auto packages = discoverPackages(m_mfdsDir);
    m_screens.clear();

    if (m_contentWatcher) {
        m_contentWatcher->stop();
        m_contentWatcher.reset();
    }

    for (const auto& pkg : packages) {
        for (const auto& scrManifest : pkg.screens) {
            MfdScreen screen;
            screen.packageId = pkg.id;
            screen.screenId = scrManifest.id;
            screen.qualifiedId = pkg.id + "/" + scrManifest.id;
            screen.name = screen.qualifiedId;
            screen.title = scrManifest.title;
            screen.contexts = scrManifest.contexts;
            screen.excludeContexts = scrManifest.excludeContexts;
            screen.autoHideUntilGaze = scrManifest.autoHideUntilGaze;

            if (scrManifest.pose.hasPose) {
                screen.hasExplicitPose = true;
                screen.pose = scrManifest.pose.toMfdPose();
                screen.baselinePose = screen.pose;
                screen.pitchDeg = scrManifest.pose.pitchDeg;
                screen.yawDeg = scrManifest.pose.yawDeg;
                screen.baseWidthM = scrManifest.pose.widthM;
                screen.baseHeightM = scrManifest.pose.heightM;
            } else {
                screen.hasExplicitPose = false;
            }

            screen.provider = std::make_unique<DeclarativeMfdProvider>(screen.qualifiedId, screen.title);
            screen.renderer = std::make_unique<MfdRenderer>(m_renderWidth, m_renderHeight);
            setupScreenCallbacks(screen);

            m_screens.push_back(std::move(screen));
        }
    }

    loadSettings();
    applyAutoLayout(m_screens);

    m_contentWatcher = std::make_unique<MfdContentWatcher>(m_mfdsDir);
    m_contentWatcher->registerPackages(packages);
    m_contentWatcher->start();
    m_contentWatcher->reloadAllSync();

    for (auto& scr : m_screens) {
        if (!scr.packageId.empty()) {
            m_contentWatcher->swapIfUpdated(scr.packageId, scr.screenId, scr.contentGeneration, scr.provider->viewModel());
        }
        ensureSettingsTab(scr);
    }
}

bool MfdManager::loadSettings() {
    std::wstring path = getIniPath();
    migrateLegacyIni(path);

    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    for (auto& scr : m_screens) {
        std::string secName = scr.qualifiedId.empty() ? scr.name : scr.qualifiedId;
        std::wstring secW(secName.begin(), secName.end());
        wchar_t buf[128];

        wchar_t secCheck[32];
        if (GetPrivateProfileSectionW(secW.c_str(), secCheck, 32, path.c_str()) == 0) {
            std::wstring legacySec = L"slot." + std::wstring(scr.name.begin(), scr.name.end());
            if (GetPrivateProfileSectionW(legacySec.c_str(), secCheck, 32, path.c_str()) > 0) {
                secW = legacySec;
            }
        }

        if (GetPrivateProfileStringW(secW.c_str(), L"pos_x", L"", buf, 128, path.c_str()) > 0) {
            scr.pose.position.x = static_cast<float>(_wtof(buf));
            scr.hasExplicitPose = true;
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"pos_y", L"", buf, 128, path.c_str()) > 0) {
            scr.pose.position.y = static_cast<float>(_wtof(buf));
            scr.hasExplicitPose = true;
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"pos_z", L"", buf, 128, path.c_str()) > 0) {
            scr.pose.position.z = static_cast<float>(_wtof(buf));
            scr.hasExplicitPose = true;
        }
        scr.baselinePose.position = scr.pose.position;

        if (GetPrivateProfileStringW(secW.c_str(), L"pitch", L"", buf, 128, path.c_str()) > 0) {
            scr.pitchDeg = static_cast<float>(_wtof(buf));
            scr.hasExplicitPose = true;
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"yaw", L"", buf, 128, path.c_str()) > 0) {
            scr.yawDeg = static_cast<float>(_wtof(buf));
            scr.hasExplicitPose = true;
        }
        scr.pose.orientation = Quat::fromEulerDegrees(scr.pitchDeg, scr.yawDeg, 0.0f);
        scr.baselinePose.orientation = scr.pose.orientation;

        if (GetPrivateProfileStringW(secW.c_str(), L"scale", L"", buf, 128, path.c_str()) > 0) {
            scr.scale = (std::clamp)(static_cast<float>(_wtof(buf)), 0.50f, 2.0f);
            scr.pose.widthM = scr.baseWidthM * scr.scale;
            scr.pose.heightM = scr.baseHeightM * scr.scale;
            scr.baselinePose.widthM = scr.pose.widthM;
            scr.baselinePose.heightM = scr.pose.heightM;
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"opacity", L"", buf, 128, path.c_str()) > 0) {
            scr.opacity = (std::clamp)(static_cast<float>(_wtof(buf)), 0.0f, 1.0f);
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"locked", L"", buf, 128, path.c_str()) > 0) {
            scr.trackingLocked = (_wtoi(buf) != 0);
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"auto_hide", L"", buf, 128, path.c_str()) > 0) {
            scr.autoHideUntilGaze = (_wtoi(buf) != 0);
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"use_custom_color", L"", buf, 128, path.c_str()) > 0) {
            scr.useCustomColor = (_wtoi(buf) != 0);
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"theme", L"", buf, 128, path.c_str()) > 0) {
            int t = _wtoi(buf);
            if (t >= 0 && t <= 5) scr.theme = static_cast<MfdColorTheme>(t);
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"custom_r", L"", buf, 128, path.c_str()) > 0) {
            scr.customColor.r = static_cast<uint8_t>(std::clamp(_wtoi(buf), 0, 255));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"custom_g", L"", buf, 128, path.c_str()) > 0) {
            scr.customColor.g = static_cast<uint8_t>(std::clamp(_wtoi(buf), 0, 255));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"custom_b", L"", buf, 128, path.c_str()) > 0) {
            scr.customColor.b = static_cast<uint8_t>(std::clamp(_wtoi(buf), 0, 255));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"custom_a", L"", buf, 128, path.c_str()) > 0) {
            scr.customColor.a = static_cast<uint8_t>(std::clamp(_wtoi(buf), 10, 255));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"dwell_delay", L"", buf, 128, path.c_str()) > 0) {
            scr.gazeTracker.setDwellTimeThreshold((std::clamp)(static_cast<float>(_wtof(buf)), 0.05f, 4.0f));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"release_delay", L"", buf, 128, path.c_str()) > 0) {
            scr.gazeTracker.setDwellExitTime((std::clamp)(static_cast<float>(_wtof(buf)), 0.05f, 4.0f));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"cone_margin", L"", buf, 128, path.c_str()) > 0) {
            scr.gazeTracker.setHitMargin((std::clamp)(static_cast<float>(_wtof(buf)), 0.80f, 2.50f));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"anchor_offset_y", L"", buf, 128, path.c_str()) > 0) {
            scr.gazeTracker.setAnchorOffset(0.0f, (std::clamp)(static_cast<float>(_wtof(buf)), -0.5f, 0.5f));
        }
        scr.isDirty = true;
    }
    return true;
}

bool MfdManager::saveSettings() {
    std::wstring path = getIniPath();
    if (path.empty()) return false;

    for (const auto& scr : m_screens) {
        std::string secName = scr.qualifiedId.empty() ? scr.name : scr.qualifiedId;
        std::wstring secW(secName.begin(), secName.end());
        wchar_t buf[128];

        swprintf_s(buf, L"%.4f", scr.pose.position.x);
        WritePrivateProfileStringW(secW.c_str(), L"pos_x", buf, path.c_str());

        swprintf_s(buf, L"%.4f", scr.pose.position.y);
        WritePrivateProfileStringW(secW.c_str(), L"pos_y", buf, path.c_str());

        swprintf_s(buf, L"%.4f", scr.pose.position.z);
        WritePrivateProfileStringW(secW.c_str(), L"pos_z", buf, path.c_str());

        swprintf_s(buf, L"%.2f", scr.pitchDeg);
        WritePrivateProfileStringW(secW.c_str(), L"pitch", buf, path.c_str());

        swprintf_s(buf, L"%.2f", scr.yawDeg);
        WritePrivateProfileStringW(secW.c_str(), L"yaw", buf, path.c_str());

        swprintf_s(buf, L"%.2f", scr.scale);
        WritePrivateProfileStringW(secW.c_str(), L"scale", buf, path.c_str());

        swprintf_s(buf, L"%.2f", scr.opacity);
        WritePrivateProfileStringW(secW.c_str(), L"opacity", buf, path.c_str());

        swprintf_s(buf, L"%d", scr.trackingLocked ? 1 : 0);
        WritePrivateProfileStringW(secW.c_str(), L"locked", buf, path.c_str());

        swprintf_s(buf, L"%d", scr.autoHideUntilGaze ? 1 : 0);
        WritePrivateProfileStringW(secW.c_str(), L"auto_hide", buf, path.c_str());

        swprintf_s(buf, L"%d", scr.useCustomColor ? 1 : 0);
        WritePrivateProfileStringW(secW.c_str(), L"use_custom_color", buf, path.c_str());

        swprintf_s(buf, L"%d", static_cast<int>(scr.theme));
        WritePrivateProfileStringW(secW.c_str(), L"theme", buf, path.c_str());

        swprintf_s(buf, L"%u", static_cast<unsigned int>(scr.customColor.r));
        WritePrivateProfileStringW(secW.c_str(), L"custom_r", buf, path.c_str());

        swprintf_s(buf, L"%u", static_cast<unsigned int>(scr.customColor.g));
        WritePrivateProfileStringW(secW.c_str(), L"custom_g", buf, path.c_str());

        swprintf_s(buf, L"%u", static_cast<unsigned int>(scr.customColor.b));
        WritePrivateProfileStringW(secW.c_str(), L"custom_b", buf, path.c_str());

        swprintf_s(buf, L"%u", static_cast<unsigned int>(scr.customColor.a));
        WritePrivateProfileStringW(secW.c_str(), L"custom_a", buf, path.c_str());

        swprintf_s(buf, L"%.2f", scr.gazeTracker.dwellTimeThreshold());
        WritePrivateProfileStringW(secW.c_str(), L"dwell_delay", buf, path.c_str());

        swprintf_s(buf, L"%.2f", scr.gazeTracker.dwellExitTime());
        WritePrivateProfileStringW(secW.c_str(), L"release_delay", buf, path.c_str());

        swprintf_s(buf, L"%.2f", scr.gazeTracker.hitMargin());
        WritePrivateProfileStringW(secW.c_str(), L"cone_margin", buf, path.c_str());

        swprintf_s(buf, L"%+.2f", scr.gazeTracker.anchorOffsetY());
        WritePrivateProfileStringW(secW.c_str(), L"anchor_offset_y", buf, path.c_str());
    }
    return true;
}

bool MfdManager::initialize(int renderWidth, int renderHeight, const std::wstring& mfdsDir) {
    shutdown();
    m_renderWidth = renderWidth;
    m_renderHeight = renderHeight;
    if (!mfdsDir.empty()) {
        m_mfdsDir = mfdsDir;
    } else if (m_mfdsDir.empty()) {
        m_mfdsDir = getDefaultMfdsDirectory();
    }

    edvr::Config::get().init(edvr::executableDirectory());

    EventWriter::instance().initialize(m_mfdsDir);

    if (GetModuleHandleW(L"EliteDangerous64.exe") == nullptr) {
        std::wstring statusPath = ContextEvaluator::getStatusJsonPath();
        if (GetFileAttributesW(statusPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            ContextEvaluator::instance().updateFromStatus(kFlagInMainShip | kFlagShieldsUp, 0, 0);
        } else {
            ContextEvaluator::instance().startWatcher(100);
        }
    } else {
        ContextEvaluator::instance().startWatcher(100);
    }

    discoverAndLoadPackages();

    m_enabled = true;
    Log::get().note("mfd: initialized renderSize=(%dx%d), enabled=%d, screens=%zu, mfdsDir='%ls'\n",
                    m_renderWidth, m_renderHeight, isEnabled() ? 1 : 0, m_screens.size(), m_mfdsDir.c_str());

    if (m_compositor && m_compositor->backend() == MfdCompositorBackend::kOpenXrQuadLayer) {
        auto* quad = static_cast<OpenXrQuadCompositor*>(m_compositor.get());
        quad->initialize(renderWidth, renderHeight);
    }
    return true;
}

void MfdManager::shutdown() {
    if (m_contentWatcher) {
        m_contentWatcher->stop();
        m_contentWatcher.reset();
    }
    ContextEvaluator::instance().stopWatcher();
    EventWriter::instance().shutdown();

    m_screens.clear();
    m_packagesLoaded = false;
    m_capLogged = false;
    m_enabled = false;

    if (m_compositor) {
        m_compositor->shutdown();
    }
    m_quadVs.Reset();
    m_quadPs.Reset();
    m_quadCb.Reset();
    m_quadSampler.Reset();
    m_blendState.Reset();
}

bool MfdManager::isEnabled() const {
    return m_enabled;
}

bool MfdManager::showAllWindows() const {
    return m_showAllWindows;
}

void MfdManager::setShowAllWindows(bool show) {
    m_showAllWindows = show;
    for (auto& scr : m_screens) {
        if (show) {
            scr.currentAlpha = 1.0f;
        }
        scr.isDirty = true;
    }
}

int MfdManager::activeDisplayCount() const {
    return m_activeDisplayCount;
}

void MfdManager::setActiveDisplayCount(int count) {
    if (count < 1) count = 1;
    if (count > 16) count = 16;
    m_activeDisplayCount = count;
}

bool MfdManager::isHeadLocked() const {
    return m_headLocked;
}

void MfdManager::setHeadLocked(bool headLocked) {
    m_headLocked = headLocked;
    for (auto& scr : m_screens) {
        scr.trackingLocked = !headLocked;
        scr.isDirty = true;
    }
}

bool MfdManager::addSlot(std::string name, std::unique_ptr<IMfdProvider> provider, const MfdPose& pose) {
    if (!provider) return false;

    MfdScreen scr;
    scr.name = name;
    scr.screenId = name;
    scr.qualifiedId = name;
    scr.title = provider->title();
    scr.contexts = getDefaultScreenContexts();
    scr.pose = pose;
    scr.baselinePose = pose;
    scr.hasExplicitPose = true;
    scr.trackingLocked = true;
    scr.baseWidthM = pose.widthM > 0.01f ? pose.widthM : 0.24f;
    scr.baseHeightM = pose.heightM > 0.01f ? pose.heightM : 0.16f;
    scr.provider = std::move(provider);
    scr.renderer = std::make_unique<MfdRenderer>(m_renderWidth, m_renderHeight);
    scr.isVisible = true;

    setupScreenCallbacks(scr);
    ensureSettingsTab(scr);

    m_screens.push_back(std::move(scr));
    return true;
}

MfdScreen* MfdManager::findScreen(const std::string& qualifiedOrScreenId) {
    for (auto& scr : m_screens) {
        if (scr.qualifiedId == qualifiedOrScreenId ||
            scr.screenId == qualifiedOrScreenId ||
            scr.name == qualifiedOrScreenId) {
            return &scr;
        }
    }
    return nullptr;
}

MfdScreen* MfdManager::findScreen(const std::string& packageId, const std::string& screenId) {
    for (auto& scr : m_screens) {
        if (scr.packageId == packageId && scr.screenId == screenId) {
            return &scr;
        }
    }
    return nullptr;
}

MfdSlot* MfdManager::findSlot(const std::string& name) {
    MfdScreen* s = findScreen(name);
    if (s) return s;

    // Legacy slot alias resolution
    if (name == "main_mfd") {
        s = findScreen("route/route");
        if (!s) s = findScreen("route");
    } else if (name == "left_mfd") {
        s = findScreen("ship/power");
        if (!s) s = findScreen("ship/modules");
        if (!s) s = findScreen("power");
    } else if (name == "right_mfd") {
        s = findScreen("exploration/bio");
        if (!s) s = findScreen("exploration/system");
        if (!s) s = findScreen("bio");
    }

    if (!s && !m_screens.empty()) {
        if (name == "main_mfd") return &m_screens[0];
        if (name == "left_mfd" && m_screens.size() > 1) return &m_screens[1];
        if (name == "right_mfd" && m_screens.size() > 2) return &m_screens[2];
    }
    return s;
}

MfdScreen* MfdManager::focusedScreen() {
    MfdScreen* bestScr = nullptr;
    float minAngle = 1e9f;
    for (auto& scr : m_screens) {
        if (scr.isVisible && scr.isContextVisible && !scr.isCapSuppressed && scr.gazeTracker.isFocused()) {
            float angle = scr.gazeTracker.gazeAngleDegrees();
            if (angle < minAngle) {
                minAngle = angle;
                bestScr = &scr;
            }
        }
    }
    return bestScr;
}

void MfdManager::update(const Vec3& headPos, const Vec3& headForward, float dtSeconds) {
    if (!m_enabled) return;
    if (!m_packagesLoaded) {
        discoverAndLoadPackages();
    }

    LARGE_INTEGER qpcStart;
    QueryPerformanceCounter(&qpcStart);

    m_lastHeadPos = headPos;

    // 1. Evaluate game context
    auto& evaluator = ContextEvaluator::instance();
    GameContextMask activeContexts = evaluator.getActiveContexts();

    // Check context transition and emit context_changed event if visibility changed (§7.3)
    if (activeContexts != m_lastActiveContexts) {
        std::vector<std::string> activeNames = evaluator.getActiveContextNames();
        auto snapshot = evaluator.getSnapshot();

        for (auto& scr : m_screens) {
            bool oldVisible = scr.isContextVisible;
            scr.isContextVisible = ContextEvaluator::isScreenVisible(scr.contexts, scr.excludeContexts, activeContexts);
            if (oldVisible != scr.isContextVisible) {
                scr.isDirty = true;
                if (!scr.packageId.empty()) {
                    EventWriter::instance().enqueue(MfdEventRecord::createContextChanged(
                        scr.packageId, activeNames, snapshot.flags, snapshot.flags2, snapshot.guiFocus
                    ));
                }
            }
        }
        m_lastActiveContexts = activeContexts;
    } else {
        for (auto& scr : m_screens) {
            scr.isContextVisible = ContextEvaluator::isScreenVisible(scr.contexts, scr.excludeContexts, activeContexts);
        }
    }

    // 2. Live content updates via ContentWatcher (Ticket A3)
    for (auto& scr : m_screens) {
        if (m_contentWatcher && !scr.packageId.empty() && scr.provider) {
            if (m_contentWatcher->swapIfUpdated(scr.packageId, scr.screenId, scr.contentGeneration, scr.provider->viewModel())) {
                scr.isDirty = true;
            }
        }
        ensureSettingsTab(scr);
    }

    // 3. Screen updates, gaze tracking, and focus transitions (Ticket A12)
    for (auto& scr : m_screens) {
        if (!scr.isVisible || !scr.provider) continue;

        if (scr.isContextVisible) {
            scr.provider->update(dtSeconds);
        } else {
            scr.currentAlpha = 0.0f;
            scr.provider->onFocusChanged(false);
            if (scr.lastFocused) {
                scr.lastFocused = false;
                scr.isDirty = true;
                if (!scr.packageId.empty()) {
                    EventWriter::instance().enqueue(MfdEventRecord::createFocusLost(scr.packageId, scr.screenId));
                }
            }
            continue;
        }

        MfdPose effectivePose = scr.pose;
        if (!scr.trackingLocked) {
            effectivePose.position = headPos + scr.pose.position;
            effectivePose.orientation = scr.pose.orientation;
        }

        scr.gazeTracker.update(headPos, headForward, effectivePose, dtSeconds);
    }

    // Find winning screen with minimum gaze angle
    MfdScreen* focused = nullptr;
    float bestGazeAngle = 1e9f;
    for (auto& scr : m_screens) {
        if (scr.isVisible && scr.isContextVisible && scr.gazeTracker.isFocused()) {
            float angle = scr.gazeTracker.gazeAngleDegrees();
            if (angle < bestGazeAngle) {
                bestGazeAngle = angle;
                focused = &scr;
            }
        }
    }

    for (auto& scr : m_screens) {
        if (!scr.isVisible || !scr.provider || !scr.isContextVisible) continue;

        bool isFocused = (&scr == focused);
        bool wasFocused = scr.lastFocused;

        if (!wasFocused && isFocused) {
            scr.lastFocused = true;
            scr.isDirty = true;
            if (!scr.packageId.empty()) {
                EventWriter::instance().enqueue(MfdEventRecord::createFocusGained(scr.packageId, scr.screenId));
            }
        } else if (wasFocused && !isFocused) {
            scr.lastFocused = false;
            scr.isDirty = true;
            if (!scr.packageId.empty()) {
                EventWriter::instance().enqueue(MfdEventRecord::createFocusLost(scr.packageId, scr.screenId));
            }
        }

        // Smooth fade transition for auto-hide
        if (scr.autoHideUntilGaze && !m_showAllWindows) {
            float targetAlpha = (isFocused || scr.gazeTracker.dwellProgress() > 0.05f) ? 1.0f : 0.0f;
            scr.currentAlpha += (targetAlpha - scr.currentAlpha) * std::clamp(dtSeconds * 10.0f, 0.0f, 1.0f);
        } else {
            scr.currentAlpha = 1.0f;
        }

        scr.provider->onFocusChanged(isFocused);
    }

    // 4. Hard cap of 16 visible screens (§10.2)
    size_t activeCount = 0;
    for (auto& scr : m_screens) {
        if (scr.isVisible && scr.isContextVisible) {
            activeCount++;
        }
    }

    if (activeCount <= kMaxVisibleScreens) {
        for (auto& scr : m_screens) {
            scr.isCapSuppressed = false;
        }
        m_visibleScreenCount = activeCount;
        m_suppressedScreenCount = 0;
    } else {
        std::vector<std::string> suppressedIds;
        size_t includedCount = 0;

        if (focused && focused->isVisible && focused->isContextVisible) {
            focused->isCapSuppressed = false;
            includedCount++;
        }

        for (auto& scr : m_screens) {
            if (&scr == focused) continue;
            if (!scr.isVisible || !scr.isContextVisible) {
                scr.isCapSuppressed = false;
                continue;
            }
            if (includedCount < kMaxVisibleScreens) {
                scr.isCapSuppressed = false;
                includedCount++;
            } else {
                scr.isCapSuppressed = true;
                suppressedIds.push_back(scr.qualifiedId.empty() ? scr.name : scr.qualifiedId);
            }
        }

        m_visibleScreenCount = includedCount;
        m_suppressedScreenCount = suppressedIds.size();

        if (!suppressedIds.empty() && !m_capLogged) {
            m_capLogged = true;
            std::string idList;
            for (size_t i = 0; i < suppressedIds.size(); ++i) {
                if (i > 0) idList += ", ";
                idList += suppressedIds[i];
            }
            Log::get().note("mfd: visible cap 16 reached, %zu screens suppressed (%s)\n",
                            suppressedIds.size(), idList.c_str());
        }
    }

    LARGE_INTEGER qpcEnd;
    QueryPerformanceCounter(&qpcEnd);
    if (m_qpcFreq.QuadPart > 0) {
        m_perfTelemetry.lastFrameCpuTimeMs = (qpcEnd.QuadPart - qpcStart.QuadPart) * 1000.0 / m_qpcFreq.QuadPart;
    }
}

void MfdManager::render() {
    if (!isEnabled()) return;
    if (!m_packagesLoaded) {
        discoverAndLoadPackages();
    }

    LARGE_INTEGER qpcStart;
    QueryPerformanceCounter(&qpcStart);

    size_t dirtyCount = 0;
    size_t visibleCount = 0;

    for (auto& scr : m_screens) {
        if (!scr.isVisible || !scr.isContextVisible || scr.isCapSuppressed || !scr.provider || !scr.renderer) {
            continue;
        }
        if (scr.currentAlpha < 0.01f) {
            continue;
        }

        visibleCount++;

        // Dirty-flag rasterization check (§10.1, Ticket A9)
        bool dirty = scr.isDirty;
        const auto& vm = scr.provider->viewModel();
        if (scr.contentGeneration != scr.lastRenderedGeneration) dirty = true;
        if (scr.gazeTracker.isFocused() != scr.lastFocused) dirty = true;
        if (vm.activeTabIndex != scr.lastSelectedTab) dirty = true;
        const auto* curTab = vm.currentTab();
        int curIdx = curTab ? curTab->selectedIndex : -1;
        if (curIdx != scr.lastSelectedIndex) dirty = true;
        if (scr.theme != scr.lastTheme) dirty = true;
        if (vm.statusBadge != scr.lastBadgeText) dirty = true;
        if ((scr.currentAlpha <= 0.01f) != (scr.lastRenderedAlpha <= 0.01f)) dirty = true;

        if (dirty) {
            scr.renderer->render(vm, scr.opacity, scr.theme, scr.useCustomColor, scr.customColor);
            scr.lastRenderedGeneration = scr.contentGeneration;
            scr.lastFocused = scr.gazeTracker.isFocused();
            scr.lastSelectedTab = vm.activeTabIndex;
            scr.lastSelectedIndex = curIdx;
            scr.lastTheme = scr.theme;
            scr.lastBadgeText = vm.statusBadge;
            scr.lastRenderedAlpha = scr.currentAlpha;
            scr.isDirty = false;
            scr.needsGpuUpload = true;
            dirtyCount++;
        }

        if (m_compositor && m_compositor->isReady()) {
            m_compositor->updatePose(scr.pose);
            m_compositor->submitTexture(scr.renderer->pixelData(), scr.renderer->width(), scr.renderer->height());
        }
    }

    m_perfTelemetry.visibleCount = visibleCount;
    m_perfTelemetry.dirtyCount = dirtyCount;

    LARGE_INTEGER qpcEnd;
    QueryPerformanceCounter(&qpcEnd);
    if (m_qpcFreq.QuadPart > 0) {
        double renderMs = (qpcEnd.QuadPart - qpcStart.QuadPart) * 1000.0 / m_qpcFreq.QuadPart;
        m_perfTelemetry.lastFrameCpuTimeMs += renderMs;

        m_cpuSamples.push_back(m_perfTelemetry.lastFrameCpuTimeMs);
        if (m_cpuSamples.size() > 300) {
            m_cpuSamples.erase(m_cpuSamples.begin(), m_cpuSamples.begin() + 100);
        }

        std::vector<double> sorted = m_cpuSamples;
        std::sort(sorted.begin(), sorted.end());
        m_perfTelemetry.p50CpuTimeMs = sorted[sorted.size() * 50 / 100];
        m_perfTelemetry.p95CpuTimeMs = sorted[sorted.size() * 95 / 100];
        m_perfTelemetry.maxCpuTimeMs = sorted.back();

        uint64_t nowMs = GetTickCount64();
        if (nowMs - m_lastPerfLogMs >= 60000 && !m_cpuSamples.empty()) {
            m_lastPerfLogMs = nowMs;
            Log::get().note("mfd_perf: CPU render time p50=%.2f ms, p95=%.2f ms, max=%.2f ms, visible=%zu, dirty=%zu\n",
                            m_perfTelemetry.p50CpuTimeMs, m_perfTelemetry.p95CpuTimeMs,
                            m_perfTelemetry.maxCpuTimeMs, m_perfTelemetry.visibleCount, m_perfTelemetry.dirtyCount);
        }
    }
}

void MfdManager::renderToEyeRtv(ID3D11Device* device, ID3D11DeviceContext* context,
                                ID3D11RenderTargetView* rtv,
                                const XrPosef& eyePose, const XrFovf& eyeFov,
                                uint32_t viewportWidth, uint32_t viewportHeight,
                                ID3D11VertexShader* vs, ID3D11PixelShader* ps,
                                ID3D11SamplerState* sampler, ID3D11Buffer* constantsBuffer) {
    (void)vs; (void)ps; (void)sampler; (void)constantsBuffer;
    if (!isEnabled() || m_screens.empty() || !device || !context || !rtv) return;
    if (viewportWidth == 0 || viewportHeight == 0) return;

    LARGE_INTEGER qpcStart;
    QueryPerformanceCounter(&qpcStart);

    Vec3 eyePos(eyePose.position.x, eyePose.position.y, eyePose.position.z);
    Quat eyeRot(eyePose.orientation.x, eyePose.orientation.y, eyePose.orientation.z, eyePose.orientation.w);

    if (!m_blendState) {
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device->CreateBlendState(&bd, &m_blendState);
    }

    if (!m_quadVs || !m_quadPs || !m_quadCb || !m_quadSampler) {
        Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob, errors;
        HRESULT hrVs = D3DCompile(s_mfdQuadShader, std::strlen(s_mfdQuadShader), "EDVR MFD Quad VS",
                                  nullptr, nullptr, "vs", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vsBlob, &errors);
        if (SUCCEEDED(hrVs)) {
            device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_quadVs);
        } else {
            Log::get().note("mfd_shader: VS compile error: %s\n", errors ? (char*)errors->GetBufferPointer() : "unknown");
        }
        errors.Reset();
        HRESULT hrPs = D3DCompile(s_mfdQuadShader, std::strlen(s_mfdQuadShader), "EDVR MFD Quad PS",
                                  nullptr, nullptr, "ps", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &psBlob, &errors);
        if (SUCCEEDED(hrPs)) {
            device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_quadPs);
        } else {
            Log::get().note("mfd_shader: PS compile error: %s\n", errors ? (char*)errors->GetBufferPointer() : "unknown");
        }

        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(MfdQuadCbData);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        device->CreateBuffer(&bd, nullptr, &m_quadCb);

        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        device->CreateSamplerState(&sd, &m_quadSampler);
    }

    if (!m_quadVs || !m_quadPs || !m_quadCb || !m_quadSampler) return;

    ComPtr<ID3D11BlendState> prevBlendState;
    float prevBlendFactor[4] = {0};
    UINT prevSampleMask = 0;
    context->OMGetBlendState(&prevBlendState, prevBlendFactor, &prevSampleMask);

    ComPtr<ID3D11DepthStencilState> prevDepthStencilState;
    UINT prevStencilRef = 0;
    context->OMGetDepthStencilState(&prevDepthStencilState, &prevStencilRef);

    ComPtr<ID3D11RasterizerState> prevRasterizerState;
    context->RSGetState(&prevRasterizerState);

    ComPtr<ID3D11InputLayout> prevInputLayout;
    context->IAGetInputLayout(&prevInputLayout);

    D3D11_PRIMITIVE_TOPOLOGY prevTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    context->IAGetPrimitiveTopology(&prevTopology);

    ComPtr<ID3D11VertexShader> prevVs;
    context->VSGetShader(&prevVs, nullptr, nullptr);

    ComPtr<ID3D11PixelShader> prevPs;
    context->PSGetShader(&prevPs, nullptr, nullptr);

    ComPtr<ID3D11Buffer> prevVsCb;
    context->VSGetConstantBuffers(0, 1, &prevVsCb);

    ComPtr<ID3D11Buffer> prevPsCb;
    context->PSGetConstantBuffers(0, 1, &prevPsCb);

    ComPtr<ID3D11SamplerState> prevSampler;
    context->PSGetSamplers(0, 1, &prevSampler);

    ComPtr<ID3D11ShaderResourceView> prevSrv;
    context->PSGetShaderResources(0, 1, &prevSrv);

    UINT numVp = 1;
    D3D11_VIEWPORT prevVp{};
    context->RSGetViewports(&numVp, &prevVp);

    ComPtr<ID3D11RenderTargetView> prevRtv;
    ComPtr<ID3D11DepthStencilView> prevDsv;
    context->OMGetRenderTargets(1, &prevRtv, &prevDsv);

    if (m_blendState) {
        context->OMSetBlendState(m_blendState.Get(), nullptr, 0xFFFFFFFF);
    }

    float tanLeft = std::tan(eyeFov.angleLeft);
    float tanRight = std::tan(eyeFov.angleRight);
    float tanUp = std::tan(eyeFov.angleUp);
    float tanDown = std::tan(eyeFov.angleDown);
    float tanW = tanRight - tanLeft;
    float tanH = tanUp - tanDown;
    if (tanW <= 1e-4f || tanH <= 1e-4f) return;

    Quat invEyeRot(-eyeRot.x, -eyeRot.y, -eyeRot.z, eyeRot.w);

    for (auto& scr : m_screens) {
        if (!scr.isVisible || !scr.isContextVisible || scr.isCapSuppressed || !scr.renderer) continue;
        if (scr.currentAlpha < 0.01f) continue;

        Vec3 halfRight = scr.pose.orientation.rotate(Vec3(scr.pose.widthM * 0.5f, 0.0f, 0.0f));
        Vec3 halfUp    = scr.pose.orientation.rotate(Vec3(0.0f, scr.pose.heightM * 0.5f, 0.0f));

        Vec3 corners[4] = {
            scr.pose.position - halfRight + halfUp,
            scr.pose.position + halfRight + halfUp,
            scr.pose.position - halfRight - halfUp,
            scr.pose.position + halfRight - halfUp
        };

        MfdQuadCbData cbData{};
        cbData.uv[0][0] = 0.0f; cbData.uv[0][1] = 0.0f;
        cbData.uv[1][0] = 1.0f; cbData.uv[1][1] = 0.0f;
        cbData.uv[2][0] = 0.0f; cbData.uv[2][1] = 1.0f;
        cbData.uv[3][0] = 1.0f; cbData.uv[3][1] = 1.0f;
        cbData.opacity = scr.currentAlpha;

        bool allBehind = true;
        for (int c = 0; c < 4; ++c) {
            Vec3 cornerLocal;
            if (!scr.trackingLocked) {
                cornerLocal = corners[c];
            } else {
                Vec3 relPos = corners[c] - eyePos;
                cornerLocal = invEyeRot.rotate(relPos);
            }

            if (cornerLocal.z < -0.01f) {
                allBehind = false;
            }

            float Wc = -cornerLocal.z;
            if (Wc < 0.01f) Wc = 0.01f;

            float xClip = (2.0f * cornerLocal.x - (tanRight + tanLeft) * Wc) / tanW;
            float yClip = (2.0f * cornerLocal.y - (tanUp + tanDown) * Wc) / tanH;
            float zClip = 0.5f * Wc;

            cbData.clipPos[c][0] = xClip;
            cbData.clipPos[c][1] = yClip;
            cbData.clipPos[c][2] = zClip;
            cbData.clipPos[c][3] = Wc;
        }

        if (allBehind) {
            m_debugStats.inFrustum = false;
            continue;
        }

        m_debugStats.inFrustum = true;
        m_debugStats.renderDraws++;

        int focusStateInt = 0;
        auto* foc = focusedScreen();
        if (foc) {
            focusStateInt = (foc->gazeTracker.currentState() == MfdFocusState::kFocused) ? 2 : 1;
        }
        publishSharedTelemetry(m_debugStats.renderDraws, focusStateInt, m_debugStats.inFrustum,
                               m_debugStats.eyeLocalX, m_debugStats.eyeLocalY, m_debugStats.eyeLocalZ,
                               m_debugStats.screenX, m_debugStats.screenY, m_debugStats.screenW, m_debugStats.screenH);

        ID3D11ShaderResourceView* mfdSrv = nullptr;
        scr.renderer->createOrUpdateD3D11Srv(device, context, &mfdSrv, scr.needsGpuUpload);
        scr.needsGpuUpload = false;
        if (!mfdSrv) continue;

        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(viewportWidth);
        vp.Height = static_cast<float>(viewportHeight);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;

        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->RSSetViewports(1, &vp);
        context->OMSetRenderTargets(1, &rtv, nullptr);
        context->VSSetShader(m_quadVs.Get(), nullptr, 0);
        context->PSSetShader(m_quadPs.Get(), nullptr, 0);
        context->PSSetSamplers(0, 1, m_quadSampler.GetAddressOf());
        context->PSSetShaderResources(0, 1, &mfdSrv);
        context->UpdateSubresource(m_quadCb.Get(), 0, nullptr, &cbData, 0, 0);
        context->VSSetConstantBuffers(0, 1, m_quadCb.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, m_quadCb.GetAddressOf());

        context->Draw(6, 0);
    }

    // State restoration
    context->OMSetBlendState(prevBlendState.Get(), prevBlendFactor, prevSampleMask);
    context->OMSetDepthStencilState(prevDepthStencilState.Get(), prevStencilRef);
    context->RSSetState(prevRasterizerState.Get());
    context->IASetInputLayout(prevInputLayout.Get());
    context->IASetPrimitiveTopology(prevTopology);
    context->VSSetShader(prevVs.Get(), nullptr, 0);
    context->PSSetShader(prevPs.Get(), nullptr, 0);
    ID3D11Buffer* vsCbPtr = prevVsCb.Get();
    context->VSSetConstantBuffers(0, 1, &vsCbPtr);
    ID3D11Buffer* psCbPtr = prevPsCb.Get();
    context->PSSetConstantBuffers(0, 1, &psCbPtr);
    ID3D11SamplerState* sampPtr = prevSampler.Get();
    context->PSSetSamplers(0, 1, &sampPtr);
    ID3D11ShaderResourceView* srvPtr = prevSrv.Get();
    context->PSSetShaderResources(0, 1, &srvPtr);
    if (numVp > 0) {
        context->RSSetViewports(numVp, &prevVp);
    }
    ID3D11RenderTargetView* rtvPtr = prevRtv.Get();
    context->OMSetRenderTargets(1, &rtvPtr, prevDsv.Get());

    // QPC Telemetry update (§10.3)
    LARGE_INTEGER qpcEnd;
    QueryPerformanceCounter(&qpcEnd);
    if (m_qpcFreq.QuadPart > 0) {
        double frameMs = (qpcEnd.QuadPart - qpcStart.QuadPart) * 1000.0 / m_qpcFreq.QuadPart;
        m_perfTelemetry.lastFrameCpuTimeMs = frameMs;

        m_cpuSamples.push_back(frameMs);
        if (m_cpuSamples.size() > 300) {
            m_cpuSamples.erase(m_cpuSamples.begin(), m_cpuSamples.begin() + 100);
        }

        std::vector<double> sorted = m_cpuSamples;
        std::sort(sorted.begin(), sorted.end());
        m_perfTelemetry.p50CpuTimeMs = sorted[sorted.size() * 50 / 100];
        m_perfTelemetry.p95CpuTimeMs = sorted[sorted.size() * 95 / 100];
        m_perfTelemetry.maxCpuTimeMs = sorted.back();

        uint64_t nowMs = GetTickCount64();
        if (nowMs - m_lastPerfLogMs >= 60000 && !m_cpuSamples.empty()) {
            m_lastPerfLogMs = nowMs;
            Log::get().note("mfd_perf: CPU render time p50=%.2f ms, p95=%.2f ms, max=%.2f ms, visible=%zu, dirty=%zu\n",
                            m_perfTelemetry.p50CpuTimeMs, m_perfTelemetry.p95CpuTimeMs,
                            m_perfTelemetry.maxCpuTimeMs, m_perfTelemetry.visibleCount, m_perfTelemetry.dirtyCount);
        }
    }
}

const XrCompositionLayerBaseHeader* MfdManager::getOpenXrLayerHeader() {
    return nullptr;
}

void MfdManager::publishSharedTelemetry(uint32_t draws, int focusState, bool inFrustum,
                                       float eyeX, float eyeY, float eyeZ,
                                       float screenX, float screenY, float screenW, float screenH) {
    MfdSharedState* s = getMfdSharedState();
    if (!s) return;
    InterlockedExchange(&s->initialized, 1);
    InterlockedExchange(&s->draws, static_cast<LONG>(draws));
    InterlockedExchange(&s->focusState, static_cast<LONG>(focusState));
    InterlockedExchange(&s->inFrustum, inFrustum ? 1 : 0);
    InterlockedExchange(&s->eyeX_mm, static_cast<LONG>(std::round(eyeX * 1000.0f)));
    InterlockedExchange(&s->eyeY_mm, static_cast<LONG>(std::round(eyeY * 1000.0f)));
    InterlockedExchange(&s->eyeZ_mm, static_cast<LONG>(std::round(eyeZ * 1000.0f)));
    InterlockedExchange(&s->screenX, static_cast<LONG>(std::round(screenX)));
    InterlockedExchange(&s->screenY, static_cast<LONG>(std::round(screenY)));
    InterlockedExchange(&s->screenW, static_cast<LONG>(std::round(screenW)));
    InterlockedExchange(&s->screenH, static_cast<LONG>(std::round(screenH)));
}

bool MfdManager::readSharedTelemetry(uint32_t* draws, int* focusState, bool* inFrustum,
                                    float* eyeX, float* eyeY, float* eyeZ,
                                    float* screenX, float* screenY, float* screenW, float* screenH) {
    MfdSharedState* s = getMfdSharedState();
    if (!s || InterlockedCompareExchange(&s->initialized, 0, 0) == 0) return false;
    if (draws) *draws = static_cast<uint32_t>(InterlockedCompareExchange(&s->draws, 0, 0));
    if (focusState) *focusState = static_cast<int>(InterlockedCompareExchange(&s->focusState, 0, 0));
    if (inFrustum) *inFrustum = (InterlockedCompareExchange(&s->inFrustum, 0, 0) != 0);
    if (eyeX) *eyeX = static_cast<float>(InterlockedCompareExchange(&s->eyeX_mm, 0, 0)) / 1000.0f;
    if (eyeY) *eyeY = static_cast<float>(InterlockedCompareExchange(&s->eyeY_mm, 0, 0)) / 1000.0f;
    if (eyeZ) *eyeZ = static_cast<float>(InterlockedCompareExchange(&s->eyeZ_mm, 0, 0)) / 1000.0f;
    if (screenX) *screenX = static_cast<float>(InterlockedCompareExchange(&s->screenX, 0, 0));
    if (screenY) *screenY = static_cast<float>(InterlockedCompareExchange(&s->screenY, 0, 0));
    if (screenW) *screenW = static_cast<float>(InterlockedCompareExchange(&s->screenW, 0, 0));
    if (screenH) *screenH = static_cast<float>(InterlockedCompareExchange(&s->screenH, 0, 0));
    return true;
}

} // namespace edvr::mfd
