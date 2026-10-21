#pragma once

#include "mfd_types.h"
#include "mfd_provider.h"
#include <cstdint>
#include <vector>
#include <string>

namespace edvr::mfd {

int parseEliteKey(const std::string& keyStr);

// Single UI action binding mapping multiple keyboard virtual keys, joystick buttons, and POV hats.
struct MfdActionBinding {
    std::vector<int> vkeys;
    uint32_t joyButtonMask = 0;   // Bit 0 = Joy_1, Bit 31 = Joy_32
    uint32_t axis = 0;
    bool axisNegative = false;
    uint32_t axisLastValue[50] = {32767};
    bool povUp = false;
    bool povDown = false;
    bool povLeft = false;
    bool povRight = false;

    void addVk(int vk) {
        if (vk > 0) vkeys.push_back(vk);
    }

    void addJoyButton(int btnIndex1Based) {
        if (btnIndex1Based >= 1 && btnIndex1Based <= 32) {
            joyButtonMask |= (1u << (btnIndex1Based - 1));
        }
    }
};

void parseDeviceKey(const std::string& device, const std::string& key, MfdActionBinding& binding);

// Complete binding set for cockpit MFD UI navigation.
struct MfdBindingsConfig {
    MfdActionBinding up;
    MfdActionBinding down;
    MfdActionBinding left;
    MfdActionBinding right;
    MfdActionBinding select;
    MfdActionBinding back;
    MfdActionBinding nextTab;
    MfdActionBinding prevTab;

    bool loadedFromBinds = false;
    std::string presetName;

    static MfdBindingsConfig createDefault();
};

// Hold-to-repeat timing for Up/Down/Left/Right (#17). One place to tune.
struct MfdRepeatTiming {
    static constexpr uint64_t kInitialDelayMs = 400;  // press -> first repeat
    static constexpr uint64_t kSlowIntervalMs = 80;   // interval during the first kSlowPhaseMs of repeating
    static constexpr uint64_t kSlowPhaseMs = 2000;
    static constexpr uint64_t kRampMs = 3000;         // linear ease kSlowIntervalMs -> kFastIntervalMs
    static constexpr uint64_t kFastIntervalMs = 40;

    // Interval to wait after a repeat that fired repeatingMs after the first repeat.
    static uint64_t intervalAt(uint64_t repeatingMs) {
        if (repeatingMs < kSlowPhaseMs) return kSlowIntervalMs;
        uint64_t into = repeatingMs - kSlowPhaseMs;
        if (into >= kRampMs) return kFastIntervalMs;
        return kSlowIntervalMs - (kSlowIntervalMs - kFastIntervalMs) * into / kRampMs;
    }
};

// Routes UI navigation inputs to the focused MFD while suppressing
// them from the game engine so flight controls (pips, thrusters) remain safe.
class MfdInputRouter {
public:
    MfdInputRouter();

    void setBindings(const MfdBindingsConfig& bindings) { m_bindings = bindings; }
    const MfdBindingsConfig& bindings() const { return m_bindings; }

    void reloadBindingsFromGame();

    // Edge-triggered button update:
    // Takes raw down states (from GetAsyncKeyState, DirectInput, or XInput),
    // detects rising edges, and if mfdFocused is true, dispatches actions
    // to provider and returns true (indicating input was swallowed).
    bool processInput(bool isMfdFocused, IMfdProvider* activeProvider,
                      bool keyDownUp, bool keyDownDown, bool keyDownLeft, bool keyDownRight,
                      bool keyDownSelect, bool keyDownBack, bool keyDownNextTab, bool keyDownPrevTab,
                      uint64_t nowMs = kNowAuto);

    // Pass as nowMs to use the monotonic GetTickCount64() clock. Tests inject their own.
    static constexpr uint64_t kNowAuto = ~0ULL;

    void pollJoystickInputs(bool& up, bool& down, bool& left, bool& right,
                            bool& sel, bool& back, bool& next, bool& prev);

    // Map one device's POV reading onto hat directions. A device without a hat must be
    // ignored: winmm leaves its dwPOV at 0, which would otherwise read as Up held (#13).
    static void decodePov(bool deviceHasPov, uint32_t dwPov, bool& up, bool& right, bool& down, bool& left);

    // Convenience poller using virtual keys, joystick/vJoy devices, and external key-state checker.
    // isKeyDownFn is a predicate (e.g. [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; })
    template <typename Fn>
    bool pollAndRoute(bool isMfdFocused, IMfdProvider* activeProvider, Fn&& isKeyDownFn,
                      uint64_t nowMs = kNowAuto) {
        bool jUp = false, jDown = false, jLeft = false, jRight = false;
        bool jSel = false, jBack = false, jNext = false, jPrev = false;
        pollJoystickInputs(jUp, jDown, jLeft, jRight, jSel, jBack, jNext, jPrev);

        auto checkVks = [&](const MfdActionBinding& b) {
            for (int vk : b.vkeys) {
                if (isKeyDownFn(vk)) return true;
            }
            return false;
        };

        bool up = checkVks(m_bindings.up) || jUp;
        bool down = checkVks(m_bindings.down) || jDown;
        bool left = checkVks(m_bindings.left) || jLeft;
        bool right = checkVks(m_bindings.right) || jRight;
        bool sel = checkVks(m_bindings.select) || jSel;
        bool back = checkVks(m_bindings.back) || jBack;
        bool next = checkVks(m_bindings.nextTab) || jNext;
        bool prev = checkVks(m_bindings.prevTab) || jPrev;

        return processInput(isMfdFocused, activeProvider, up, down, left, right, sel, back, next, prev, nowMs);
    }

    // Indicates whether the most recent input tick swallowed game controls.
    bool lastInputSwallowed() const { return m_lastSwallowed; }

private:
    MfdBindingsConfig m_bindings;

    // Per winmm device id: -1 unknown (re-query caps), 0 no hat, 1 has hat.
    static constexpr unsigned kMaxJoyDevices = 16;
    int8_t m_devHasPov[kMaxJoyDevices] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

    // Previous frame down states for rising-edge detection:
    bool m_prevUp = false;
    bool m_prevDown = false;
    bool m_prevLeft = false;
    bool m_prevRight = false;
    bool m_prevSelect = false;
    bool m_prevBack = false;
    bool m_prevNextTab = false;
    bool m_prevPrevTab = false;

    bool m_lastSwallowed = false;

    // Hold-to-repeat state: only the most recently pressed direction repeats.
    MfdInputAction m_holdDir = MfdInputAction::kNone;
    uint64_t m_nextRepeatMs = 0;
    uint64_t m_firstRepeatMs = 0;
    int m_repeatCount = 0;
};

} // namespace edvr::mfd
