#pragma once

#include "mfd_types.h"
#include "mfd_provider.h"
#include <cstdint>
#include <vector>
#include <string>

namespace edvr::mfd {

// Single UI action binding mapping multiple keyboard virtual keys, joystick buttons, and POV hats.
struct MfdActionBinding {
    std::vector<int> vkeys;
    uint32_t joyButtonMask = 0;   // Bit 0 = Joy_1, Bit 31 = Joy_32
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
                      bool keyDownSelect, bool keyDownBack, bool keyDownNextTab, bool keyDownPrevTab);

    void pollJoystickInputs(bool& up, bool& down, bool& left, bool& right,
                            bool& sel, bool& back, bool& next, bool& prev);

    // Convenience poller using virtual keys, joystick/vJoy devices, and external key-state checker.
    // isKeyDownFn is a predicate (e.g. [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; })
    template <typename Fn>
    bool pollAndRoute(bool isMfdFocused, IMfdProvider* activeProvider, Fn&& isKeyDownFn) {
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

        return processInput(isMfdFocused, activeProvider, up, down, left, right, sel, back, next, prev);
    }

    // Indicates whether the most recent input tick swallowed game controls.
    bool lastInputSwallowed() const { return m_lastSwallowed; }

private:
    MfdBindingsConfig m_bindings;

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
};

} // namespace edvr::mfd
