#include "mfd_input_router.h"
#include "log.h"
#include <windows.h>
#include <mmsystem.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "winmm.lib")

namespace edvr::mfd {

namespace {

int parseEliteKey(const std::string& keyStr) {
    if (keyStr.empty()) return 0;
    if (keyStr == "Key_Space") return VK_SPACE;
    if (keyStr == "Key_Backspace") return VK_BACK;
    if (keyStr == "Key_Enter" || keyStr == "Key_Return") return VK_RETURN;
    if (keyStr == "Key_Escape") return VK_ESCAPE;
    if (keyStr == "Key_Tab") return VK_TAB;
    if (keyStr == "Key_UpArrow") return VK_UP;
    if (keyStr == "Key_DownArrow") return VK_DOWN;
    if (keyStr == "Key_LeftArrow") return VK_LEFT;
    if (keyStr == "Key_RightArrow") return VK_RIGHT;

    // Direct single letter / number keys (e.g. "Key_W", "Key_S", "Key_A", "Key_D", "Key_C", "Key_Z", "Key_E", "Key_Q")
    if (keyStr.rfind("Key_", 0) == 0 && keyStr.size() == 5) {
        char ch = static_cast<char>(std::toupper(keyStr[4]));
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            return ch;
        }
    }
    return 0;
}

void parseDeviceKey(const std::string& device, const std::string& key, MfdActionBinding& binding) {
    if (key.empty()) return;

    if (device == "Keyboard") {
        int vk = parseEliteKey(key);
        if (vk > 0) binding.addVk(vk);
    } else if (device.find("Joy") != std::string::npos || device == "vJoy" || device == "GamePad") {
        // Check for POV directions: e.g. "Joy_POV1Up", "Joy_POV1Down", "Joy_POV1Left", "Joy_POV1Right"
        if (key.find("POV") != std::string::npos && key.find("Up") != std::string::npos) {
            binding.povUp = true;
        } else if (key.find("POV") != std::string::npos && key.find("Down") != std::string::npos) {
            binding.povDown = true;
        } else if (key.find("POV") != std::string::npos && key.find("Left") != std::string::npos) {
            binding.povLeft = true;
        } else if (key.find("POV") != std::string::npos && key.find("Right") != std::string::npos) {
            binding.povRight = true;
        } else if (key.rfind("Joy_", 0) == 0) {
            // e.g. "Joy_4", "Joy_25"
            try {
                int btnNum = std::stoi(key.substr(4));
                binding.addJoyButton(btnNum);
            } catch (...) {}
        }
    }
}

std::string extractAttribute(const std::string& line, const std::string& attrName) {
    std::string needle = attrName + "=\"";
    size_t pos = line.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    size_t endPos = line.find('"', pos);
    if (endPos == std::string::npos) return "";
    return line.substr(pos, endPos - pos);
}

bool parseBindsXml(const std::string& filepath, MfdBindingsConfig& config) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    std::string line;
    std::string currentAction;

    while (std::getline(file, line)) {
        // Trim leading whitespace
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        std::string trimmed = line.substr(first);

        if (trimmed.rfind("<UI_Up", 0) == 0) currentAction = "UI_Up";
        else if (trimmed.rfind("</UI_Up>", 0) == 0) currentAction.clear();
        else if (trimmed.rfind("<UI_Down", 0) == 0) currentAction = "UI_Down";
        else if (trimmed.rfind("</UI_Down>", 0) == 0) currentAction.clear();
        else if (trimmed.rfind("<UI_Left", 0) == 0) currentAction = "UI_Left";
        else if (trimmed.rfind("</UI_Left>", 0) == 0) currentAction.clear();
        else if (trimmed.rfind("<UI_Right", 0) == 0) currentAction = "UI_Right";
        else if (trimmed.rfind("</UI_Right>", 0) == 0) currentAction.clear();
        else if (trimmed.rfind("<UI_Select", 0) == 0) currentAction = "UI_Select";
        else if (trimmed.rfind("</UI_Select>", 0) == 0) currentAction.clear();
        else if (trimmed.rfind("<UI_Back", 0) == 0) currentAction = "UI_Back";
        else if (trimmed.rfind("</UI_Back>", 0) == 0) currentAction.clear();
        else if (trimmed.rfind("<CycleNextPage", 0) == 0 || trimmed.rfind("<CycleNextPanel", 0) == 0) currentAction = "CycleNextPage";
        else if (trimmed.rfind("</CycleNextPage>", 0) == 0 || trimmed.rfind("</CycleNextPanel>", 0) == 0) currentAction.clear();
        else if (trimmed.rfind("<CyclePreviousPage", 0) == 0 || trimmed.rfind("<CyclePreviousPanel", 0) == 0) currentAction = "CyclePreviousPage";
        else if (trimmed.rfind("</CyclePreviousPage>", 0) == 0 || trimmed.rfind("</CyclePreviousPanel>", 0) == 0) currentAction.clear();

        if (!currentAction.empty() && (trimmed.rfind("<Primary", 0) == 0 || trimmed.rfind("<Secondary", 0) == 0)) {
            std::string device = extractAttribute(trimmed, "Device");
            std::string key = extractAttribute(trimmed, "Key");

            if (currentAction == "UI_Up") parseDeviceKey(device, key, config.up);
            else if (currentAction == "UI_Down") parseDeviceKey(device, key, config.down);
            else if (currentAction == "UI_Left") parseDeviceKey(device, key, config.left);
            else if (currentAction == "UI_Right") parseDeviceKey(device, key, config.right);
            else if (currentAction == "UI_Select") parseDeviceKey(device, key, config.select);
            else if (currentAction == "UI_Back") parseDeviceKey(device, key, config.back);
            else if (currentAction == "CycleNextPage") parseDeviceKey(device, key, config.nextTab);
            else if (currentAction == "CyclePreviousPage") parseDeviceKey(device, key, config.prevTab);
        }
    }
    return true;
}

} // namespace

MfdBindingsConfig MfdBindingsConfig::createDefault() {
    MfdBindingsConfig cfg;
    // Standard keyboard arrow keys & WASD
    cfg.up.addVk(VK_UP); cfg.up.addVk('W'); cfg.up.povUp = true;
    cfg.down.addVk(VK_DOWN); cfg.down.addVk('S'); cfg.down.povDown = true;
    cfg.left.addVk(VK_LEFT); cfg.left.addVk('A'); cfg.left.povLeft = true;
    cfg.right.addVk(VK_RIGHT); cfg.right.addVk('D'); cfg.right.povRight = true;

    // Select & Back
    cfg.select.addVk(VK_SPACE); cfg.select.addVk(VK_RETURN);
    cfg.select.addJoyButton(1); cfg.select.addJoyButton(3); cfg.select.addJoyButton(4); // Trigger / A button / secondary

    cfg.back.addVk(VK_BACK); cfg.back.addVk(VK_ESCAPE);
    cfg.back.addJoyButton(2); cfg.back.addJoyButton(3); // Cancel / B button

    // Tab Cycling (E / Q, C / Z, LB / RB, Bumpers)
    cfg.nextTab.addVk('E'); cfg.nextTab.addVk('C');
    cfg.nextTab.addJoyButton(6); cfg.nextTab.addJoyButton(25);

    cfg.prevTab.addVk('Q'); cfg.prevTab.addVk('Z');
    cfg.prevTab.addJoyButton(5); cfg.prevTab.addJoyButton(27);

    cfg.loadedFromBinds = false;
    cfg.presetName = "Default";
    return cfg;
}

MfdInputRouter::MfdInputRouter() {
    reloadBindingsFromGame();
}

void MfdInputRouter::reloadBindingsFromGame() {
    m_bindings = MfdBindingsConfig::createDefault();

    char localAppData[MAX_PATH]{};
    if (!GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH)) return;

    std::string bindingsDir = std::string(localAppData) + "\\Frontier Developments\\Elite Dangerous\\Options\\Bindings\\";

    // 1. Read active preset from StartPreset.4.start or StartPreset.start
    std::string preset = "Custom";
    std::string startFile = bindingsDir + "StartPreset.4.start";
    std::ifstream startFs(startFile);
    if (!startFs.is_open()) {
        startFile = bindingsDir + "StartPreset.start";
        startFs.open(startFile);
    }
    if (startFs.is_open()) {
        std::string p;
        if (std::getline(startFs, p)) {
            // Trim whitespace
            size_t f = p.find_first_not_of(" \t\r\n");
            size_t l = p.find_last_not_of(" \t\r\n");
            if (f != std::string::npos && l != std::string::npos) {
                preset = p.substr(f, l - f + 1);
            }
        }
    }

    // 2. Search for matching binds file
    std::vector<std::string> candidates = {
        bindingsDir + preset + ".4.2.binds",
        bindingsDir + preset + ".4.0.binds",
        bindingsDir + preset + ".binds",
        bindingsDir + "Custom.4.2.binds",
        bindingsDir + "Custom.4.0.binds",
        bindingsDir + "Custom.binds"
    };

    for (const auto& filepath : candidates) {
        if (parseBindsXml(filepath, m_bindings)) {
            m_bindings.loadedFromBinds = true;
            m_bindings.presetName = preset;
            Log::get().note("mfd_input: loaded Elite bindings from '%s' (preset: %s, up=0x%X/pov=%d, dn=0x%X/pov=%d, sel=0x%X, back=0x%X, next=0x%X, prev=0x%X)\n",
                            filepath.c_str(), preset.c_str(),
                            m_bindings.up.joyButtonMask, m_bindings.up.povUp ? 1 : 0,
                            m_bindings.down.joyButtonMask, m_bindings.down.povDown ? 1 : 0,
                            m_bindings.select.joyButtonMask, m_bindings.back.joyButtonMask,
                            m_bindings.nextTab.joyButtonMask, m_bindings.prevTab.joyButtonMask);
            break;
        }
    }
}

void MfdInputRouter::pollJoystickInputs(bool& up, bool& down, bool& left, bool& right,
                                        bool& sel, bool& back, bool& next, bool& prev) {
    UINT numDevs = joyGetNumDevs();
    if (numDevs > 16) numDevs = 16;
    for (UINT i = 0; i < numDevs; ++i) {
        JOYINFOEX ji{};
        ji.dwSize = sizeof(JOYINFOEX);
        ji.dwFlags = JOY_RETURNALL;
        if (joyGetPosEx(i, &ji) == JOYERR_NOERROR) {
            // Check POV Hat (dwPOV: hundredths of degrees, 65535 = centered)
            if (ji.dwPOV != 65535 && ji.dwPOV <= 35900) {
                if (ji.dwPOV >= 31500 || ji.dwPOV <= 4500) {
                    if (m_bindings.up.povUp) up = true;
                }
                if (ji.dwPOV >= 4500 && ji.dwPOV <= 13500) {
                    if (m_bindings.right.povRight) right = true;
                }
                if (ji.dwPOV >= 13500 && ji.dwPOV <= 22500) {
                    if (m_bindings.down.povDown) down = true;
                }
                if (ji.dwPOV >= 22500 && ji.dwPOV <= 31500) {
                    if (m_bindings.left.povLeft) left = true;
                }
            }
            // Check dynamically mapped button bitmasks
            if (ji.dwButtons & m_bindings.select.joyButtonMask) sel = true;
            if (ji.dwButtons & m_bindings.back.joyButtonMask) back = true;
            if (ji.dwButtons & m_bindings.nextTab.joyButtonMask) next = true;
            if (ji.dwButtons & m_bindings.prevTab.joyButtonMask) prev = true;
        }
    }
}

bool MfdInputRouter::processInput(bool isMfdFocused, IMfdProvider* activeProvider,
                                 bool keyDownUp, bool keyDownDown, bool keyDownLeft, bool keyDownRight,
                                 bool keyDownSelect, bool keyDownBack, bool keyDownNextTab, bool keyDownPrevTab) {
    // Detect rising edges:
    bool edgeUp = keyDownUp && !m_prevUp;
    bool edgeDown = keyDownDown && !m_prevDown;
    bool edgeLeft = keyDownLeft && !m_prevLeft;
    bool edgeRight = keyDownRight && !m_prevRight;
    bool edgeSelect = keyDownSelect && !m_prevSelect;
    bool edgeBack = keyDownBack && !m_prevBack;
    bool edgeNextTab = keyDownNextTab && !m_prevNextTab;
    bool edgePrevTab = keyDownPrevTab && !m_prevPrevTab;

    // Update stored state:
    m_prevUp = keyDownUp;
    m_prevDown = keyDownDown;
    m_prevLeft = keyDownLeft;
    m_prevRight = keyDownRight;
    m_prevSelect = keyDownSelect;
    m_prevBack = keyDownBack;
    m_prevNextTab = keyDownNextTab;
    m_prevPrevTab = keyDownPrevTab;

    m_lastSwallowed = false;

    // If an MFD is not focused, inputs pass directly to game flight controls:
    if (!isMfdFocused) {
        return false;
    }

    // Build the action mask for this frame:
    MfdInputAction action = MfdInputAction::kNone;
    if (edgeUp) action = action | MfdInputAction::kUp;
    if (edgeDown) action = action | MfdInputAction::kDown;
    if (edgeLeft) action = action | MfdInputAction::kLeft;
    if (edgeRight) action = action | MfdInputAction::kRight;
    if (edgeSelect) action = action | MfdInputAction::kSelect;
    if (edgeBack) action = action | MfdInputAction::kBack;
    if (edgeNextTab) action = action | MfdInputAction::kNextTab;
    if (edgePrevTab) action = action | MfdInputAction::kPrevTab;

    bool anyKeyDown = (keyDownUp || keyDownDown || keyDownLeft || keyDownRight ||
                       keyDownSelect || keyDownBack || keyDownNextTab || keyDownPrevTab);

    if (action != MfdInputAction::kNone && activeProvider) {
        Log::get().note("mfd_input: routed action mask 0x%04X to provider '%s'\n",
                        static_cast<uint32_t>(action), activeProvider->id());
        activeProvider->onInput(action);
    }

    // When focused, all UI navigation keys are swallowed to protect ship flight systems
    m_lastSwallowed = anyKeyDown;
    return m_lastSwallowed;
}

} // namespace edvr::mfd
