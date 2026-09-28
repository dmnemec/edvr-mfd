#include "mfd_manager.h"
#include "config.h"
#include "log.h"
#include "mfd_telemetry.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
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
    if (!OpenClipboard(nullptr)) return;
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

std::wstring getSavedGamesEliteDir() {
    wchar_t userProfile[MAX_PATH];
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
        std::wstring p = userProfile;
        p += L"\\Saved Games\\Frontier Developments\\Elite Dangerous";
        return p;
    }
    return L"";
}

struct EliteStatusData {
    int pipsSysHalf = 8;
    int pipsEngHalf = 0;
    int pipsWepHalf = 4;
    float fuelMain = -1.0f;
    float fuelReservoir = -1.0f;
    int cargoCount = 0;
    int fireGroup = 0;
    uint32_t flags = 0;
    bool hasPips = false;
    bool hasFuel = false;
};

EliteStatusData readEliteStatus() {
    EliteStatusData data;
    // When running in standalone test rigs outside the game, simulate an active cockpit
    if (GetModuleHandleW(L"EliteDangerous64.exe") == nullptr) {
        data.flags = (1 << 24) | (1 << 3); // MainShip + ShieldsUp
        return data;
    }

    std::wstring statusPath = getSavedGamesEliteDir() + L"\\Status.json";
    std::ifstream f(statusPath);
    if (!f.is_open()) return data;

    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string s = buffer.str();

    // Parse Pips: [x, y, z]
    size_t pipsPos = s.find("\"Pips\":");
    if (pipsPos != std::string::npos) {
        size_t openBracket = s.find('[', pipsPos);
        size_t closeBracket = s.find(']', openBracket);
        if (openBracket != std::string::npos && closeBracket != std::string::npos) {
            std::string pipsStr = s.substr(openBracket + 1, closeBracket - openBracket - 1);
            int p0 = 0, p1 = 0, p2 = 0;
            if (sscanf_s(pipsStr.c_str(), "%d,%d,%d", &p0, &p1, &p2) == 3) {
                data.pipsSysHalf = p0;
                data.pipsEngHalf = p1;
                data.pipsWepHalf = p2;
                data.hasPips = true;
            }
        }
    }

    // Parse Fuel: {"FuelMain": x.xx, "FuelReservoir": y.yy}
    size_t fuelPos = s.find("\"Fuel\":");
    if (fuelPos != std::string::npos) {
        size_t fm = s.find("\"FuelMain\":", fuelPos);
        if (fm != std::string::npos) {
            float fval = 0.0f;
            if (sscanf_s(s.c_str() + fm + 11, "%f", &fval) == 1) {
                data.fuelMain = fval;
                data.hasFuel = true;
            }
        }
    }

    // Parse Cargo: x
    size_t cargoPos = s.find("\"Cargo\":");
    if (cargoPos != std::string::npos) {
        int cval = 0;
        if (sscanf_s(s.c_str() + cargoPos + 8, "%d", &cval) == 1) {
            data.cargoCount = cval;
        }
    }

    // Parse FireGroup: x
    size_t fgPos = s.find("\"FireGroup\":");
    if (fgPos != std::string::npos) {
        int fgVal = 0;
        if (sscanf_s(s.c_str() + fgPos + 12, "%d", &fgVal) == 1) {
            data.fireGroup = fgVal;
        }
    }

    // Parse Flags: x
    size_t flagsPos = s.find("\"Flags\":");
    if (flagsPos != std::string::npos) {
        uint32_t fl = 0;
        if (sscanf_s(s.c_str() + flagsPos + 8, "%u", &fl) == 1) {
            data.flags = fl;
        }
    }

    return data;
}

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

bool isActivityActive(uint32_t mask, uint32_t flags) {
    if (flags == 0) return false; // Suppress during title, loading, main menu
    if (mask == kActivityAlways) return true;

    bool inShip = (flags & (1 << 24)) != 0 || ((flags & ((1 << 25) | (1 << 26))) == 0);
    bool inSrv = (flags & (1 << 26)) != 0;
    bool inFighter = (flags & (1 << 25)) != 0;
    bool isDocked = (flags & 0x3) != 0; // Docked or Landed
    bool inFlight = !isDocked;
    bool hardpoints = (flags & (1 << 6)) != 0;
    bool inAnalysis = (flags & (1 << 27)) != 0;
    bool inCombat = !inAnalysis;

    // Vehicle Category:
    uint32_t vehicleMask = mask & (kActivityShip | kActivitySrv | kActivityFighter);
    if (vehicleMask != 0) {
        bool matchVehicle = ((mask & kActivityShip) && inShip) ||
                            ((mask & kActivitySrv) && inSrv) ||
                            ((mask & kActivityFighter) && inFighter);
        if (!matchVehicle) return false;
    }

    // Flight State Category:
    uint32_t flightStateMask = mask & (kActivityInFlight | kActivityDocked);
    if (flightStateMask != 0) {
        bool matchFlightState = ((mask & kActivityInFlight) && inFlight) ||
                                ((mask & kActivityDocked) && isDocked);
        if (!matchFlightState) return false;
    }

    // HUD Mode Category (Combat vs Analysis):
    uint32_t hudModeMask = mask & (kActivityCombat | kActivityAnalysis);
    if (hudModeMask != 0) {
        bool matchHudMode = ((mask & kActivityCombat) && inCombat) ||
                            ((mask & kActivityAnalysis) && inAnalysis);
        if (!matchHudMode) return false;
    }

    // Deployments Category:
    if (mask & kActivityHardpoints) {
        if (!hardpoints) return false;
    }

    return true;
}

void handleSettingsAdjustment(MfdSlot& slot, const std::string& key, int delta) {
    bool isLeft = (delta < 0);
    float step = isLeft ? -1.0f : 1.0f;
    int stepInt = isLeft ? -5 : 5;

    if (key == "Tracking Mode") {
        slot.trackingLocked = !slot.trackingLocked;
        if (!slot.trackingLocked) {
            // Freemove Head HUD: relative to head origin
            slot.pose.position = slot.baselinePose.position;
            slot.pose.orientation = Quat::fromEulerDegrees(slot.pitchDeg, slot.yawDeg, 0.0f);
        } else {
            // Locked Cockpit Space: restore baseline or locked cockpit coordinates
            slot.pose.position = slot.baselinePose.position;
            slot.pose.orientation = Quat::fromEulerDegrees(slot.pitchDeg, slot.yawDeg, 0.0f);
        }
    } else if (key == "Position X") {
        slot.pose.position.x += step * 0.02f;
        slot.baselinePose.position.x = slot.pose.position.x;
    } else if (key == "Position Y") {
        slot.pose.position.y += step * 0.02f;
        slot.baselinePose.position.y = slot.pose.position.y;
    } else if (key == "Position Z") {
        slot.pose.position.z += step * 0.02f;
        slot.baselinePose.position.z = slot.pose.position.z;
    } else if (key == "Pitch") {
        slot.pitchDeg += step * 2.0f;
        slot.pose.orientation = Quat::fromEulerDegrees(slot.pitchDeg, slot.yawDeg, 0.0f);
        slot.baselinePose.orientation = slot.pose.orientation;
    } else if (key == "Yaw") {
        slot.yawDeg += step * 2.0f;
        slot.pose.orientation = Quat::fromEulerDegrees(slot.pitchDeg, slot.yawDeg, 0.0f);
        slot.baselinePose.orientation = slot.pose.orientation;
    } else if (key == "Glass Opacity") {
        slot.opacity = (std::clamp)(slot.opacity + step * 0.05f, 0.0f, 1.0f);
    } else if (key == "Display Scale") {
        slot.scale = (std::clamp)(slot.scale + step * 0.05f, 0.50f, 2.0f);
        float baseW = (slot.name == "main_mfd") ? 0.28f : 0.24f;
        float baseH = (slot.name == "main_mfd") ? 0.18f : 0.16f;
        slot.pose.widthM = baseW * slot.scale;
        slot.pose.heightM = baseH * slot.scale;
        slot.baselinePose.widthM = slot.pose.widthM;
        slot.baselinePose.heightM = slot.pose.heightM;
    } else if (key == "Color Mode") {
        int currentMode = slot.useCustomColor ? 2 : (slot.theme == MfdColorTheme::kDefaultAmber ? 0 : 1);
        int nextMode = (currentMode + (isLeft ? 2 : 1)) % 3;
        if (nextMode == 0) {
            slot.useCustomColor = false;
            slot.theme = MfdColorTheme::kDefaultAmber;
            slot.customColor = MfdColor(255, 110, 0, 255);
        } else if (nextMode == 1) {
            slot.useCustomColor = false;
            if (slot.theme == MfdColorTheme::kDefaultAmber) {
                slot.theme = MfdColorTheme::kCyanIce;
            }
            switch (slot.theme) {
                case MfdColorTheme::kDefaultAmber:
                    slot.customColor.r = 255; slot.customColor.g = 110; slot.customColor.b = 0; break;
                case MfdColorTheme::kCyanIce:
                    slot.customColor.r = 60; slot.customColor.g = 200; slot.customColor.b = 255; break;
                case MfdColorTheme::kMatrixGreen:
                    slot.customColor.r = 0; slot.customColor.g = 255; slot.customColor.b = 128; break;
                case MfdColorTheme::kSolarWhite:
                    slot.customColor.r = 220; slot.customColor.g = 230; slot.customColor.b = 240; break;
                case MfdColorTheme::kCrimson:
                    slot.customColor.r = 255; slot.customColor.g = 40; slot.customColor.b = 40; break;
                case MfdColorTheme::kPurpleHaze:
                    slot.customColor.r = 200; slot.customColor.g = 100; slot.customColor.b = 255; break;
            }
        } else { // nextMode == 2 (Custom RGBA)
            slot.useCustomColor = true;
            switch (slot.theme) {
                case MfdColorTheme::kDefaultAmber:
                    slot.customColor.r = 255; slot.customColor.g = 110; slot.customColor.b = 0; break;
                case MfdColorTheme::kCyanIce:
                    slot.customColor.r = 60; slot.customColor.g = 200; slot.customColor.b = 255; break;
                case MfdColorTheme::kMatrixGreen:
                    slot.customColor.r = 0; slot.customColor.g = 255; slot.customColor.b = 128; break;
                case MfdColorTheme::kSolarWhite:
                    slot.customColor.r = 220; slot.customColor.g = 230; slot.customColor.b = 240; break;
                case MfdColorTheme::kCrimson:
                    slot.customColor.r = 255; slot.customColor.g = 40; slot.customColor.b = 40; break;
                case MfdColorTheme::kPurpleHaze:
                    slot.customColor.r = 200; slot.customColor.g = 100; slot.customColor.b = 255; break;
            }
        }
    } else if (key == "Preset Theme") {
        int themeIdx = static_cast<int>(slot.theme);
        themeIdx = (themeIdx + (isLeft ? 5 : 1)) % 6;
        slot.theme = static_cast<MfdColorTheme>(themeIdx);
        switch (slot.theme) {
            case MfdColorTheme::kDefaultAmber:
                slot.customColor.r = 255; slot.customColor.g = 110; slot.customColor.b = 0; break;
            case MfdColorTheme::kCyanIce:
                slot.customColor.r = 60; slot.customColor.g = 200; slot.customColor.b = 255; break;
            case MfdColorTheme::kMatrixGreen:
                slot.customColor.r = 0; slot.customColor.g = 255; slot.customColor.b = 128; break;
            case MfdColorTheme::kSolarWhite:
                slot.customColor.r = 220; slot.customColor.g = 230; slot.customColor.b = 240; break;
            case MfdColorTheme::kCrimson:
                slot.customColor.r = 255; slot.customColor.g = 40; slot.customColor.b = 40; break;
            case MfdColorTheme::kPurpleHaze:
                slot.customColor.r = 200; slot.customColor.g = 100; slot.customColor.b = 255; break;
        }
    } else if (key == "Reset Colors") {
        slot.useCustomColor = false;
        slot.theme = MfdColorTheme::kDefaultAmber;
        slot.customColor = MfdColor(255, 110, 0, 255);
    } else if (key == "Custom Red (R)") {
        int r = static_cast<int>(slot.customColor.r) + stepInt;
        slot.customColor.r = static_cast<uint8_t>(std::clamp(r, 0, 255));
    } else if (key == "Custom Green (G)") {
        int g = static_cast<int>(slot.customColor.g) + stepInt;
        slot.customColor.g = static_cast<uint8_t>(std::clamp(g, 0, 255));
    } else if (key == "Custom Blue (B)") {
        int b = static_cast<int>(slot.customColor.b) + stepInt;
        slot.customColor.b = static_cast<uint8_t>(std::clamp(b, 0, 255));
    } else if (key == "Custom Brightness (A)") {
        int a = static_cast<int>(slot.customColor.a) + stepInt;
        slot.customColor.a = static_cast<uint8_t>(std::clamp(a, 10, 255));
    } else if (key == "Auto-Hide Gaze") {
        slot.autoHideUntilGaze = !slot.autoHideUntilGaze;
    } else if (key == "Gaze Dwell Delay") {
        float dt = slot.gazeTracker.dwellTimeThreshold() + step * 0.10f;
        slot.gazeTracker.setDwellTimeThreshold((std::clamp)(dt, 0.05f, 4.0f));
    } else if (key == "Gaze Release Delay") {
        float dt = slot.gazeTracker.dwellExitTime() + step * 0.10f;
        slot.gazeTracker.setDwellExitTime((std::clamp)(dt, 0.05f, 4.0f));
    } else if (key == "Focus Cone Margin") {
        float m = slot.gazeTracker.hitMargin() + step * 0.05f;
        slot.gazeTracker.setHitMargin((std::clamp)(m, 0.80f, 2.50f));
    } else if (key == "Off-Center Y") {
        float offY = slot.gazeTracker.anchorOffsetY() + step * 0.05f;
        slot.gazeTracker.setAnchorOffset(0.0f, (std::clamp)(offY, -0.5f, 0.5f));
    }

    MfdManager::instance().saveSettings();
}

void ensureSettingsTab(MfdSlot& slot) {
    if (!slot.provider) return;
    auto& model = slot.provider->viewModel();
    MfdTab* settingsTab = nullptr;
    for (auto& tab : model.tabs) {
        if (tab.title == "SETTINGS") {
            settingsTab = &tab;
            break;
        }
    }
    if (!settingsTab) {
        MfdTab newTab;
        newTab.title = "SETTINGS";
        newTab.type = MfdTabType::kKeyValue;
        model.tabs.push_back(std::move(newTab));
        settingsTab = &model.tabs.back();
    } else if (model.currentTab() != settingsTab) {
        return;
    }

    settingsTab->keyValues.clear();
    char buf[64];

    settingsTab->keyValues.push_back({"Tracking Mode", slot.trackingLocked ? "Locked (Cockpit)" : "Freemove (Head HUD)",
                                      slot.trackingLocked ? palette::kSuccessGreen : palette::kCyanAccent});

    snprintf(buf, sizeof(buf), "%+.2f m", slot.pose.position.x);
    settingsTab->keyValues.push_back({"Position X", buf, palette::kAmberBright});

    snprintf(buf, sizeof(buf), "%+.2f m", slot.pose.position.y);
    settingsTab->keyValues.push_back({"Position Y", buf, palette::kAmberBright});

    snprintf(buf, sizeof(buf), "%+.2f m", slot.pose.position.z);
    settingsTab->keyValues.push_back({"Position Z", buf, palette::kAmberBright});

    snprintf(buf, sizeof(buf), "%.0f deg", slot.pitchDeg);
    settingsTab->keyValues.push_back({"Pitch", buf, palette::kAmberBright});

    snprintf(buf, sizeof(buf), "%.0f deg", slot.yawDeg);
    settingsTab->keyValues.push_back({"Yaw", buf, palette::kAmberBright});

    snprintf(buf, sizeof(buf), "%.0f%%", slot.opacity * 100.0f);
    settingsTab->keyValues.push_back({"Glass Opacity", buf, palette::kCyanAccent});

    snprintf(buf, sizeof(buf), "%.2fx", slot.scale);
    settingsTab->keyValues.push_back({"Display Scale", buf, palette::kAmberNormal});

    const char* colorModeLabel = "Default (Amber)";
    MfdColor colorModeColor = palette::kAmberBright;
    if (slot.useCustomColor) {
        colorModeLabel = "Custom (RGBA)";
        colorModeColor = palette::kSuccessGreen;
    } else if (slot.theme != MfdColorTheme::kDefaultAmber) {
        colorModeLabel = "Preset Theme";
        colorModeColor = palette::kCyanAccent;
    }

    settingsTab->keyValues.push_back({"Color Mode", colorModeLabel, colorModeColor});

    if (slot.useCustomColor) {
        snprintf(buf, sizeof(buf), "#%02X%02X%02X (%d)", slot.customColor.r, slot.customColor.g, slot.customColor.b, slot.customColor.r);
        settingsTab->keyValues.push_back({"Custom Red (R)", buf, MfdColor(255, 60, 60)});

        snprintf(buf, sizeof(buf), "#%02X%02X%02X (%d)", slot.customColor.r, slot.customColor.g, slot.customColor.b, slot.customColor.g);
        settingsTab->keyValues.push_back({"Custom Green (G)", buf, MfdColor(60, 255, 60)});

        snprintf(buf, sizeof(buf), "#%02X%02X%02X (%d)", slot.customColor.r, slot.customColor.g, slot.customColor.b, slot.customColor.b);
        settingsTab->keyValues.push_back({"Custom Blue (B)", buf, MfdColor(60, 180, 255)});

        snprintf(buf, sizeof(buf), "%.0f%% (%d)", (slot.customColor.a / 255.0f) * 100.0f, slot.customColor.a);
        settingsTab->keyValues.push_back({"Custom Brightness (A)", buf, palette::kCyanAccent});
        settingsTab->keyValues.push_back({"Reset Colors", "[RESTORE DEFAULTS]", palette::kAmberDim});
    } else {
        settingsTab->keyValues.push_back({"Preset Theme", themeName(slot.theme), palette::kCyanAccent});
        if (slot.theme != MfdColorTheme::kDefaultAmber) {
            settingsTab->keyValues.push_back({"Reset Colors", "[RESTORE DEFAULTS]", palette::kAmberDim});
        }
    }

    settingsTab->keyValues.push_back({"Auto-Hide Gaze", slot.autoHideUntilGaze ? "Enabled (Fade)" : "Disabled (Always On)",
                                      slot.autoHideUntilGaze ? palette::kSuccessGreen : palette::kAmberDim});

    snprintf(buf, sizeof(buf), "%.2f s", slot.gazeTracker.dwellTimeThreshold());
    settingsTab->keyValues.push_back({"Gaze Dwell Delay", buf, palette::kAmberNormal});

    snprintf(buf, sizeof(buf), "%.2f s", slot.gazeTracker.dwellExitTime());
    settingsTab->keyValues.push_back({"Gaze Release Delay", buf, palette::kAmberNormal});

    snprintf(buf, sizeof(buf), "%.2fx", slot.gazeTracker.hitMargin());
    settingsTab->keyValues.push_back({"Focus Cone Margin", buf, palette::kAmberNormal});

    snprintf(buf, sizeof(buf), "%+.2f", slot.gazeTracker.anchorOffsetY());
    settingsTab->keyValues.push_back({"Off-Center Y", buf, palette::kAmberNormal});
}
} // namespace

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

MfdManager& MfdManager::instance() {
    static MfdManager s_instance;
    return s_instance;
}

MfdManager::MfdManager() {
    m_compositor = std::make_unique<OpenXrQuadCompositor>();
}

MfdManager::~MfdManager() {
    shutdown();
}

void MfdManager::ensureDefaultSlots() {
    int requestedSlots = edvr::Config::get().getInt("fix.mfd_slots", 1);
    if (requestedSlots < 1) requestedSlots = 1;
    if (requestedSlots > 3) requestedSlots = 3;

    float scale = edvr::Config::get().getFloat("fix.mfd_scale", 1.0f);
    float s = (scale > 0.1f ? scale : 1.0f);
    bool defaultHeadLocked = edvr::Config::get().getBool("fix.mfd_head_locked", false);

    // Slot 0: Center Console (main_mfd - Spansh Neutron Router)
    if (!findSlot("main_mfd")) {
        float posX = edvr::Config::get().getFloat("fix.mfd_pos_x", 0.0f);
        float posY = edvr::Config::get().getFloat("fix.mfd_pos_y", -0.16f);
        float posZ = edvr::Config::get().getFloat("fix.mfd_pos_z", -0.55f);
        float pitchDeg = edvr::Config::get().getFloat("fix.mfd_pitch", -20.0f);
        float yawDeg = edvr::Config::get().getFloat("fix.mfd_yaw", 0.0f);

        MfdPose poseCenter;
        poseCenter.position = Vec3(posX, posY, posZ);
        poseCenter.orientation = Quat::fromEulerDegrees(pitchDeg, yawDeg, 0.0f);
        poseCenter.widthM = 0.28f * s;
        poseCenter.heightM = 0.18f * s;

        const char* jsonCenter = "{"
            "\"title\": \"SPANSH NEUTRON ROUTER\","
            "\"subtitle\": \"WAYPOINT 2 OF 14\","
            "\"statusBadge\": \"ONLINE\","
            "\"footerHint\": \"[Q/E] TABS   [UP/DN] SELECT   [SPACE] COPY\","
            "\"tabs\": ["
                "{"
                    "\"title\": \"ROUTE\","
                    "\"type\": \"list\","
                    "\"items\": ["
                        "{\"id\": \"wp1\", \"label\": \"Jackson's Lighthouse\", \"value\": \"12.4 LY\", \"sublabel\": \"Neutron Star\", \"badge\": \"CURRENT\"},"
                        "{\"id\": \"wp2\", \"label\": \"Prua Phoe NC-D d12-14\", \"value\": \"148.1 LY\", \"sublabel\": \"Supercharge Ready\", \"badge\": \"NEXT\"},"
                        "{\"id\": \"wp3\", \"label\": \"Dryooe Prou CS-B d1-2\", \"value\": \"294.0 LY\", \"sublabel\": \"Class M Star\", \"badge\": \"REFUEL\"}"
                    "]"
                "},"
                "{"
                    "\"title\": \"TELEMETRY\","
                    "\"type\": \"keyvalue\","
                    "\"items\": ["
                        "{\"key\": \"Current System\", \"value\": \"SOL\"},"
                        "{\"key\": \"Target System\", \"value\": \"Prua Phoe NC-D d12-14\"},"
                        "{\"key\": \"Route Distance\", \"value\": \"2,410.8 LY\"},"
                        "{\"key\": \"Fuel Reserve\", \"value\": \"98%\"},"
                        "{\"key\": \"Jump Range\", \"value\": \"64.2 LY\"}"
                    "]"
                "}"
            "]"
        "}";
        auto providerCenter = std::make_unique<DeclarativeMfdProvider>("main_mfd", "SPANSH ROUTER");
        providerCenter->loadFromJson(jsonCenter);
        providerCenter->setActionCallback([prov = providerCenter.get()](const std::string& itemId, const std::string& itemLabel) {
            if (!itemLabel.empty()) {
                copyToClipboard(itemLabel);
                auto* tab = prov->viewModel().currentTab();
                if (tab && tab->type == MfdTabType::kList) {
                    for (auto& it : tab->items) {
                        if (it.id == itemId) {
                            it.badge = "COPIED";
                            it.badgeColor = palette::kSuccessGreen;
                            break;
                        }
                    }
                }
            }
        });
        addSlot("main_mfd", std::move(providerCenter), poseCenter);
        auto* slot0 = findSlot("main_mfd");
        if (slot0) {
            slot0->pitchDeg = pitchDeg;
            slot0->yawDeg = yawDeg;
            slot0->scale = s;
            slot0->opacity = edvr::Config::get().getFloat("fix.mfd_opacity", 0.75f);
            slot0->baselinePose = poseCenter;
            slot0->trackingLocked = !defaultHeadLocked;
            slot0->activityMask = kActivityAlways;
            if (slot0->provider && slot0->provider->type() == MfdProviderType::kDeclarativeJson) {
                auto* decl = static_cast<DeclarativeMfdProvider*>(slot0->provider.get());
                decl->setKeyValueActionCallback([name = std::string("main_mfd")](const std::string& /*tabTitle*/, int /*index*/, const std::string& key, int adjustDelta) {
                    auto* s = MfdManager::instance().findSlot(name);
                    if (s) handleSettingsAdjustment(*s, key, adjustDelta);
                });
            }
        }
    }

    // Slot 1: Left Console (left_mfd - Power & Engineering)
    if (requestedSlots >= 2 && !findSlot("left_mfd")) {
        MfdPose poseLeft;
        poseLeft.position = Vec3(-0.45f, -0.22f, -0.48f);
        poseLeft.orientation = Quat::fromEulerDegrees(-22.0f, 32.0f, 0.0f);
        poseLeft.widthM = 0.24f * s;
        poseLeft.heightM = 0.16f * s;

        auto providerLeft = std::make_unique<DeclarativeMfdProvider>("left_mfd", "ENGINEERING");
        const char* jsonLeft = "{"
            "\"title\": \"SHIP ENGINEERING\","
            "\"subtitle\": \"POWER & SYSTEMS\","
            "\"statusBadge\": \"NOMINAL\","
            "\"footerHint\": \"[LOOK] GAZE FOCUS\","
            "\"tabs\": ["
                "{"
                    "\"title\": \"POWER\","
                    "\"type\": \"keyvalue\","
                    "\"items\": ["
                        "{\"key\": \"Power Plant\", \"value\": \"100% (34.2 MW)\"},"
                        "{\"key\": \"SYS Distributor\", \"value\": \"4.0 PIP\"},"
                        "{\"key\": \"ENG Distributor\", \"value\": \"0.0 PIP\"},"
                        "{\"key\": \"WEP Distributor\", \"value\": \"2.0 PIP\"},"
                        "{\"key\": \"Fuel Main\", \"value\": \"32.0 T\"},"
                        "{\"key\": \"Cargo Count\", \"value\": \"0 T\"}"
                    "]"
                "},"
                "{"
                    "\"title\": \"MODULES\","
                    "\"type\": \"list\","
                    "\"items\": ["
                        "{\"id\": \"m1\", \"label\": \"Frame Shift Drive\", \"value\": \"100%\", \"sublabel\": \"Class 5A Overcharged\", \"badge\": \"ACTIVE\"},"
                        "{\"id\": \"m2\", \"label\": \"Thrusters\", \"value\": \"100%\", \"sublabel\": \"Class 5A Dirty Drive\", \"badge\": \"ACTIVE\"},"
                        "{\"id\": \"m3\", \"label\": \"Shield Generator\", \"value\": \"100%\", \"sublabel\": \"Class 5C Bi-Weave\", \"badge\": \"ONLINE\"}"
                    "]"
                "}"
            "]"
        "}";
        providerLeft->loadFromJson(jsonLeft);
        providerLeft->setUpdateHook([](MfdViewModel& model, float /*dtSeconds*/) {
            static uint64_t lastPollMs = 0;
            uint64_t now = GetTickCount64();
            if (now - lastPollMs < 250) return;
            lastPollMs = now;

            EliteStatusData status = readEliteStatus();
            for (auto& tab : model.tabs) {
                if (tab.title == "POWER") {
                    char buf[64];
                    float sys = status.pipsSysHalf * 0.5f;
                    float eng = status.pipsEngHalf * 0.5f;
                    float wep = status.pipsWepHalf * 0.5f;
                    for (auto& kv : tab.keyValues) {
                        if (kv.key == "SYS Distributor") {
                            snprintf(buf, sizeof(buf), "%.1f PIP", sys);
                            kv.value = buf;
                            kv.valueColor = sys >= 4.0f ? palette::kSuccessGreen : palette::kAmberNormal;
                        } else if (kv.key == "ENG Distributor") {
                            snprintf(buf, sizeof(buf), "%.1f PIP", eng);
                            kv.value = buf;
                            kv.valueColor = eng >= 4.0f ? palette::kSuccessGreen : palette::kAmberNormal;
                        } else if (kv.key == "WEP Distributor") {
                            snprintf(buf, sizeof(buf), "%.1f PIP", wep);
                            kv.value = buf;
                            kv.valueColor = wep >= 4.0f ? palette::kSuccessGreen : palette::kAmberNormal;
                        } else if (kv.key == "Fuel Main") {
                            snprintf(buf, sizeof(buf), "%.1f T", status.fuelMain >= 0.0f ? status.fuelMain : 32.0f);
                            kv.value = buf;
                        } else if (kv.key == "Cargo Count") {
                            snprintf(buf, sizeof(buf), "%d T", status.cargoCount);
                            kv.value = buf;
                        }
                    }
                }
            }
        });
        addSlot("left_mfd", std::move(providerLeft), poseLeft);
        auto* slot1 = findSlot("left_mfd");
        if (slot1) {
            slot1->pitchDeg = -22.0f;
            slot1->yawDeg = 32.0f;
            slot1->scale = s;
            slot1->opacity = 0.75f;
            slot1->baselinePose = poseLeft;
            slot1->trackingLocked = !defaultHeadLocked;
            slot1->activityMask = kActivityShip | kActivityFighter;
            if (slot1->provider && slot1->provider->type() == MfdProviderType::kDeclarativeJson) {
                auto* decl = static_cast<DeclarativeMfdProvider*>(slot1->provider.get());
                decl->setKeyValueActionCallback([name = std::string("left_mfd")](const std::string& /*tabTitle*/, int /*index*/, const std::string& key, int adjustDelta) {
                    auto* s = MfdManager::instance().findSlot(name);
                    if (s) handleSettingsAdjustment(*s, key, adjustDelta);
                });
            }
        }
    }

    // Slot 2: Right Console (right_mfd - Exobiology & Sector Analysis)
    if (requestedSlots >= 3 && !findSlot("right_mfd")) {
        MfdPose poseRight;
        poseRight.position = Vec3(0.45f, -0.22f, -0.48f);
        poseRight.orientation = Quat::fromEulerDegrees(-22.0f, -32.0f, 0.0f);
        poseRight.widthM = 0.24f * s;
        poseRight.heightM = 0.16f * s;

        auto providerRight = std::make_unique<DeclarativeMfdProvider>("right_mfd", "EXOBIOLOGY");
        const char* jsonRight = "{"
            "\"title\": \"EXOBIOLOGY SCANNER\","
            "\"subtitle\": \"SECTOR SURVEY\","
            "\"statusBadge\": \"SCANNING\","
            "\"footerHint\": \"[LOOK] GAZE FOCUS\","
            "\"tabs\": ["
                "{"
                    "\"title\": \"SURVEY\","
                    "\"type\": \"keyvalue\","
                    "\"items\": ["
                        "{\"key\": \"Body\", \"value\": \"Synuefe GT-X b42-3 A 1\"},"
                        "{\"key\": \"Atmosphere\", \"value\": \"Carbon Dioxide\"},"
                        "{\"key\": \"Gravity\", \"value\": \"0.42 G\"},"
                        "{\"key\": \"Bio Signals\", \"value\": \"4 Detected\"},"
                        "{\"key\": \"Sample Value\", \"value\": \"18.5M CR\"}"
                    "]"
                "},"
                "{"
                    "\"title\": \"SIGNALS\","
                    "\"type\": \"list\","
                    "\"items\": ["
                        "{\"id\": \"s1\", \"label\": \"Stratum Tectonicas\", \"value\": \"19.2M CR\", \"sublabel\": \"Sample Complete (3/3)\", \"badge\": \"LOGGED\"},"
                        "{\"id\": \"s2\", \"label\": \"Tubeworms Emerald\", \"value\": \"4.8M CR\", \"sublabel\": \"Sample 1/3 Required\", \"badge\": \"SEEKING\"},"
                        "{\"id\": \"s3\", \"label\": \"Bacterium Cerbrus\", \"value\": \"1.2M CR\", \"sublabel\": \"Unsampled\", \"badge\": \"NEW\"}"
                    "]"
                "}"
            "]"
        "}";
        providerRight->loadFromJson(jsonRight);
        addSlot("right_mfd", std::move(providerRight), poseRight);
        auto* slot2 = findSlot("right_mfd");
        if (slot2) {
            slot2->pitchDeg = -22.0f;
            slot2->yawDeg = -32.0f;
            slot2->scale = s;
            slot2->opacity = 0.75f;
            slot2->baselinePose = poseRight;
            slot2->trackingLocked = !defaultHeadLocked;
            slot2->activityMask = kActivityAlways;
            if (slot2->provider && slot2->provider->type() == MfdProviderType::kDeclarativeJson) {
                auto* decl = static_cast<DeclarativeMfdProvider*>(slot2->provider.get());
                decl->setKeyValueActionCallback([name = std::string("right_mfd")](const std::string& /*tabTitle*/, int /*index*/, const std::string& key, int adjustDelta) {
                    auto* s = MfdManager::instance().findSlot(name);
                    if (s) handleSettingsAdjustment(*s, key, adjustDelta);
                });
            }
        }
    }

    // Adjust visibility based on requestedSlots
    for (auto& slot : m_slots) {
        if (slot.name == "main_mfd") {
            slot.isVisible = (requestedSlots >= 1);
        } else if (slot.name == "left_mfd") {
            slot.isVisible = (requestedSlots >= 2);
        } else if (slot.name == "right_mfd") {
            slot.isVisible = (requestedSlots >= 3);
        }
    }

    loadSettings();
}

static std::wstring getMfdIniPath() {
    std::wstring exeDir = edvr::executableDirectory();
    std::wstring pluginsDir = exeDir + L"\\plugins";
    CreateDirectoryW(pluginsDir.c_str(), nullptr);
    return pluginsDir + L"\\edvr_mfd.ini";
}

static std::wstring getMfdLocalAppDataIniPath() {
    wchar_t localAppData[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) {
        std::wstring dir = std::wstring(localAppData) + L"\\EDVR";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\edvr_mfd.ini";
    }
    return L"";
}

bool MfdManager::loadSettings() {
    std::wstring path = getMfdIniPath();
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        path = getMfdLocalAppDataIniPath();
        attr = GetFileAttributesW(path.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            return false;
        }
    }

    for (auto& slot : m_slots) {
        std::wstring secW = L"slot." + std::wstring(slot.name.begin(), slot.name.end());
        wchar_t buf[128];

        if (GetPrivateProfileStringW(secW.c_str(), L"pos_x", L"", buf, 128, path.c_str()) > 0) {
            slot.pose.position.x = static_cast<float>(_wtof(buf));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"pos_y", L"", buf, 128, path.c_str()) > 0) {
            slot.pose.position.y = static_cast<float>(_wtof(buf));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"pos_z", L"", buf, 128, path.c_str()) > 0) {
            slot.pose.position.z = static_cast<float>(_wtof(buf));
        }
        slot.baselinePose.position = slot.pose.position;

        if (GetPrivateProfileStringW(secW.c_str(), L"pitch", L"", buf, 128, path.c_str()) > 0) {
            slot.pitchDeg = static_cast<float>(_wtof(buf));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"yaw", L"", buf, 128, path.c_str()) > 0) {
            slot.yawDeg = static_cast<float>(_wtof(buf));
        }
        slot.pose.orientation = Quat::fromEulerDegrees(slot.pitchDeg, slot.yawDeg, 0.0f);
        slot.baselinePose.orientation = slot.pose.orientation;

        if (GetPrivateProfileStringW(secW.c_str(), L"scale", L"", buf, 128, path.c_str()) > 0) {
            slot.scale = (std::clamp)(static_cast<float>(_wtof(buf)), 0.50f, 2.0f);
            float baseW = (slot.name == "main_mfd") ? 0.28f : 0.24f;
            float baseH = (slot.name == "main_mfd") ? 0.18f : 0.16f;
            slot.pose.widthM = baseW * slot.scale;
            slot.pose.heightM = baseH * slot.scale;
            slot.baselinePose.widthM = slot.pose.widthM;
            slot.baselinePose.heightM = slot.pose.heightM;
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"opacity", L"", buf, 128, path.c_str()) > 0) {
            slot.opacity = (std::clamp)(static_cast<float>(_wtof(buf)), 0.0f, 1.0f);
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"locked", L"", buf, 128, path.c_str()) > 0) {
            slot.trackingLocked = (_wtoi(buf) != 0);
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"auto_hide", L"", buf, 128, path.c_str()) > 0) {
            slot.autoHideUntilGaze = (_wtoi(buf) != 0);
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"use_custom_color", L"", buf, 128, path.c_str()) > 0) {
            slot.useCustomColor = (_wtoi(buf) != 0);
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"theme", L"", buf, 128, path.c_str()) > 0) {
            int t = _wtoi(buf);
            if (t >= 0 && t <= 5) slot.theme = static_cast<MfdColorTheme>(t);
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"custom_r", L"", buf, 128, path.c_str()) > 0) {
            slot.customColor.r = static_cast<uint8_t>(std::clamp(_wtoi(buf), 0, 255));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"custom_g", L"", buf, 128, path.c_str()) > 0) {
            slot.customColor.g = static_cast<uint8_t>(std::clamp(_wtoi(buf), 0, 255));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"custom_b", L"", buf, 128, path.c_str()) > 0) {
            slot.customColor.b = static_cast<uint8_t>(std::clamp(_wtoi(buf), 0, 255));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"custom_a", L"", buf, 128, path.c_str()) > 0) {
            slot.customColor.a = static_cast<uint8_t>(std::clamp(_wtoi(buf), 10, 255));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"dwell_delay", L"", buf, 128, path.c_str()) > 0) {
            slot.gazeTracker.setDwellTimeThreshold((std::clamp)(static_cast<float>(_wtof(buf)), 0.05f, 4.0f));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"release_delay", L"", buf, 128, path.c_str()) > 0) {
            slot.gazeTracker.setDwellExitTime((std::clamp)(static_cast<float>(_wtof(buf)), 0.05f, 4.0f));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"cone_margin", L"", buf, 128, path.c_str()) > 0) {
            slot.gazeTracker.setHitMargin((std::clamp)(static_cast<float>(_wtof(buf)), 0.80f, 2.50f));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"anchor_offset_y", L"", buf, 128, path.c_str()) > 0) {
            slot.gazeTracker.setAnchorOffset(0.0f, (std::clamp)(static_cast<float>(_wtof(buf)), -0.5f, 0.5f));
        }
        if (GetPrivateProfileStringW(secW.c_str(), L"activity_mask", L"", buf, 128, path.c_str()) > 0) {
            slot.activityMask = static_cast<uint32_t>(_wtoi(buf));
        }
    }
    return true;
}

bool MfdManager::saveSettings() {
    std::wstring path = getMfdIniPath();
    std::wstring localPath = getMfdLocalAppDataIniPath();
    std::vector<std::wstring> targets;
    if (!path.empty()) targets.push_back(path);
    if (!localPath.empty()) targets.push_back(localPath);

    for (const auto& target : targets) {
        for (const auto& slot : m_slots) {
            std::wstring secW = L"slot." + std::wstring(slot.name.begin(), slot.name.end());
            wchar_t buf[128];

            swprintf_s(buf, L"%.4f", slot.pose.position.x);
            WritePrivateProfileStringW(secW.c_str(), L"pos_x", buf, target.c_str());

            swprintf_s(buf, L"%.4f", slot.pose.position.y);
            WritePrivateProfileStringW(secW.c_str(), L"pos_y", buf, target.c_str());

            swprintf_s(buf, L"%.4f", slot.pose.position.z);
            WritePrivateProfileStringW(secW.c_str(), L"pos_z", buf, target.c_str());

            swprintf_s(buf, L"%.2f", slot.pitchDeg);
            WritePrivateProfileStringW(secW.c_str(), L"pitch", buf, target.c_str());

            swprintf_s(buf, L"%.2f", slot.yawDeg);
            WritePrivateProfileStringW(secW.c_str(), L"yaw", buf, target.c_str());

            swprintf_s(buf, L"%.2f", slot.scale);
            WritePrivateProfileStringW(secW.c_str(), L"scale", buf, target.c_str());

            swprintf_s(buf, L"%.2f", slot.opacity);
            WritePrivateProfileStringW(secW.c_str(), L"opacity", buf, target.c_str());

            swprintf_s(buf, L"%d", slot.trackingLocked ? 1 : 0);
            WritePrivateProfileStringW(secW.c_str(), L"locked", buf, target.c_str());

            swprintf_s(buf, L"%d", slot.autoHideUntilGaze ? 1 : 0);
            WritePrivateProfileStringW(secW.c_str(), L"auto_hide", buf, target.c_str());

            swprintf_s(buf, L"%d", slot.useCustomColor ? 1 : 0);
            WritePrivateProfileStringW(secW.c_str(), L"use_custom_color", buf, target.c_str());

            swprintf_s(buf, L"%d", static_cast<int>(slot.theme));
            WritePrivateProfileStringW(secW.c_str(), L"theme", buf, target.c_str());

            swprintf_s(buf, L"%u", static_cast<unsigned int>(slot.customColor.r));
            WritePrivateProfileStringW(secW.c_str(), L"custom_r", buf, target.c_str());

            swprintf_s(buf, L"%u", static_cast<unsigned int>(slot.customColor.g));
            WritePrivateProfileStringW(secW.c_str(), L"custom_g", buf, target.c_str());

            swprintf_s(buf, L"%u", static_cast<unsigned int>(slot.customColor.b));
            WritePrivateProfileStringW(secW.c_str(), L"custom_b", buf, target.c_str());

            swprintf_s(buf, L"%u", static_cast<unsigned int>(slot.customColor.a));
            WritePrivateProfileStringW(secW.c_str(), L"custom_a", buf, target.c_str());

            swprintf_s(buf, L"%.2f", slot.gazeTracker.dwellTimeThreshold());
            WritePrivateProfileStringW(secW.c_str(), L"dwell_delay", buf, target.c_str());

            swprintf_s(buf, L"%.2f", slot.gazeTracker.dwellExitTime());
            WritePrivateProfileStringW(secW.c_str(), L"release_delay", buf, target.c_str());

            swprintf_s(buf, L"%.2f", slot.gazeTracker.hitMargin());
            WritePrivateProfileStringW(secW.c_str(), L"cone_margin", buf, target.c_str());

            swprintf_s(buf, L"%.2f", slot.gazeTracker.anchorOffsetY());
            WritePrivateProfileStringW(secW.c_str(), L"anchor_offset_y", buf, target.c_str());

            swprintf_s(buf, L"%u", slot.activityMask);
            WritePrivateProfileStringW(secW.c_str(), L"activity_mask", buf, target.c_str());
        }
    }
    return true;
}

bool MfdManager::isEnabled() const {
    return edvr::Config::get().getBool("fix.cockpit_mfd", m_enabled);
}

bool MfdManager::initialize(int renderWidth, int renderHeight) {
    m_renderWidth = renderWidth;
    m_renderHeight = renderHeight;

    edvr::Config::get().init(edvr::executableDirectory());
    ensureDefaultSlots();
    m_enabled = edvr::Config::get().getBool("fix.cockpit_mfd", false);
    Log::get().note("mfd: initialized renderSize=(%dx%d), enabled=%d, default_slots=%zu\n",
                    m_renderWidth, m_renderHeight, isEnabled() ? 1 : 0, m_slots.size());

    if (m_compositor && m_compositor->backend() == MfdCompositorBackend::kOpenXrQuadLayer) {
        auto* quad = static_cast<OpenXrQuadCompositor*>(m_compositor.get());
        quad->initialize(renderWidth, renderHeight);
    }
    return true;
}

void MfdManager::shutdown() {
    m_slots.clear();
    if (m_compositor) {
        m_compositor->shutdown();
    }
    m_quadVs.Reset();
    m_quadPs.Reset();
    m_quadCb.Reset();
    m_quadSampler.Reset();
    m_blendState.Reset();
}

bool MfdManager::addSlot(std::string name, std::unique_ptr<IMfdProvider> provider, const MfdPose& pose) {
    if (!provider) return false;

    MfdSlot slot;
    slot.name = std::move(name);
    slot.pose = pose;
    slot.baselinePose = pose;
    slot.trackingLocked = true;
    slot.provider = std::move(provider);
    slot.renderer = std::make_unique<MfdRenderer>(m_renderWidth, m_renderHeight);
    slot.isVisible = true;

    m_slots.push_back(std::move(slot));
    return true;
}

MfdSlot* MfdManager::findSlot(const std::string& name) {
    for (auto& slot : m_slots) {
        if (slot.name == name) return &slot;
    }
    return nullptr;
}

MfdSlot* MfdManager::focusedSlot() {
    EliteStatusData status = readEliteStatus();
    for (auto& slot : m_slots) {
        if (slot.isVisible && isActivityActive(slot.activityMask, status.flags) && slot.gazeTracker.isFocused()) {
            return &slot;
        }
    }
    return nullptr;
}

void MfdManager::update(const Vec3& headPos, const Vec3& headForward, float dtSeconds) {
    edvr::Config::get().reloadIfChanged();
    m_enabled = edvr::Config::get().getBool("fix.cockpit_mfd", m_enabled);
    if (!isEnabled()) return;
    ensureDefaultSlots();

    m_lastHeadPos = headPos;

    EliteStatusData status = readEliteStatus();
    bool inCockpit = (status.flags != 0);

    for (auto& slot : m_slots) {
        if (!slot.isVisible || !slot.provider) continue;

        bool active = inCockpit && isActivityActive(slot.activityMask, status.flags);

        // Update provider internal data / timers
        if (active) {
            slot.provider->update(dtSeconds);
        }

        // Ensure persistent SETTINGS tab is up-to-date with live transforms
        ensureSettingsTab(slot);

        if (!active) {
            slot.currentAlpha = 0.0f;
            continue;
        }

        // Track head gaze and evaluate focus state
        MfdPose effectivePose = slot.pose;
        if (!slot.trackingLocked) {
            // In Freemove mode, MFD moves with head
            effectivePose.position = headPos + slot.pose.position;
            effectivePose.orientation = slot.pose.orientation;
        }

        MfdFocusState prevState = slot.gazeTracker.currentState();
        MfdFocusState newState = slot.gazeTracker.update(headPos, headForward, effectivePose, dtSeconds);

        // Update smooth alpha for auto-hide fade transition
        if (slot.autoHideUntilGaze) {
            float targetAlpha = (slot.gazeTracker.isFocused() || slot.gazeTracker.dwellProgress() > 0.05f)
                                ? 1.0f : 0.0f;
            slot.currentAlpha += (targetAlpha - slot.currentAlpha) * std::clamp(dtSeconds * 10.0f, 0.0f, 1.0f);
        } else {
            slot.currentAlpha = 1.0f;
        }

        // Notify provider if focus state crossed threshold
        bool wasFocused = (prevState == MfdFocusState::kFocused);
        bool isFocused = (newState == MfdFocusState::kFocused);
        if (wasFocused != isFocused) {
            slot.provider->onFocusChanged(isFocused);
        }
    }
}

void MfdManager::render() {
    if (!isEnabled()) return;
    ensureDefaultSlots();

    EliteStatusData status = readEliteStatus();
    for (auto& slot : m_slots) {
        if (!slot.isVisible || !slot.provider || !slot.renderer) continue;
        if (!isActivityActive(slot.activityMask, status.flags)) continue;
        if (slot.currentAlpha < 0.01f) continue;

        // Rasterize view model into pixel buffer with slot background opacity and theme/customColor
        slot.renderer->render(slot.provider->viewModel(), slot.opacity, slot.theme, slot.useCustomColor, slot.customColor);

        // Submit rendered frame to active compositor
        if (m_compositor && m_compositor->isReady()) {
            m_compositor->updatePose(slot.pose);
            m_compositor->submitTexture(slot.renderer->pixelData(),
                                        slot.renderer->width(),
                                        slot.renderer->height());
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
    if (!isEnabled() || m_slots.empty() || !device || !context || !rtv) return;
    if (viewportWidth == 0 || viewportHeight == 0) return;

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
    EliteStatusData status = readEliteStatus();

    for (auto& slot : m_slots) {
        if (!slot.isVisible || !slot.renderer) continue;
        if (!isActivityActive(slot.activityMask, status.flags)) continue;
        if (slot.currentAlpha < 0.01f) continue;

        // 4 corners of the quad in 3D oriented by slot.pose.orientation
        Vec3 halfRight = slot.pose.orientation.rotate(Vec3(slot.pose.widthM * 0.5f, 0.0f, 0.0f));
        Vec3 halfUp    = slot.pose.orientation.rotate(Vec3(0.0f, slot.pose.heightM * 0.5f, 0.0f));

        Vec3 corners[4] = {
            slot.pose.position - halfRight + halfUp, // 0: Top-Left
            slot.pose.position + halfRight + halfUp, // 1: Top-Right
            slot.pose.position - halfRight - halfUp, // 2: Bottom-Left
            slot.pose.position + halfRight - halfUp  // 3: Bottom-Right
        };

        MfdQuadCbData cbData{};
        cbData.uv[0][0] = 0.0f; cbData.uv[0][1] = 0.0f;
        cbData.uv[1][0] = 1.0f; cbData.uv[1][1] = 0.0f;
        cbData.uv[2][0] = 0.0f; cbData.uv[2][1] = 1.0f;
        cbData.uv[3][0] = 1.0f; cbData.uv[3][1] = 1.0f;
        cbData.opacity = slot.currentAlpha;

        bool allBehind = true;
        for (int c = 0; c < 4; ++c) {
            Vec3 cornerLocal;
            if (!slot.trackingLocked) {
                // Freemove (Head-HUD mode): corners defined in head space
                cornerLocal = corners[c];
            } else {
                // Cockpit space: transform world coordinates to eye-local
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
        auto* focused = focusedSlot();
        if (focused) {
            focusStateInt = (focused->gazeTracker.currentState() == MfdFocusState::kFocused) ? 2 : 1;
        }
        publishSharedTelemetry(m_debugStats.renderDraws, focusStateInt, m_debugStats.inFrustum,
                               m_debugStats.eyeLocalX, m_debugStats.eyeLocalY, m_debugStats.eyeLocalZ,
                               m_debugStats.screenX, m_debugStats.screenY, m_debugStats.screenW, m_debugStats.screenH);

        ID3D11ShaderResourceView* mfdSrv = nullptr;
        slot.renderer->createOrUpdateD3D11Srv(device, context, &mfdSrv);
        if (!mfdSrv) continue;

        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(viewportWidth);
        vp.Height = static_cast<float>(viewportHeight);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;

        if (m_debugStats.renderDraws <= 10 || (m_debugStats.renderDraws % 300) == 0) {
            Log::get().note("mfd_draw_3d: #%u locked=%d alpha=%.2f vp=%.0fx%.0f\n",
                            m_debugStats.renderDraws, slot.trackingLocked ? 1 : 0, slot.currentAlpha,
                            vp.Width, vp.Height);
        }

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

    if (prevBlendState) {
        context->OMSetBlendState(prevBlendState.Get(), prevBlendFactor, prevSampleMask);
    }
}

const XrCompositionLayerBaseHeader* MfdManager::getOpenXrLayerHeader() {
    return nullptr;
}

} // namespace edvr::mfd
