# Cockpit Multi-Function Displays (MFD) Framework

## Status

- **State**: Operational in-game (3D cockpit world space + head-locked HUD mode). Multi-slot architecture active (1..3 displays). Alpha blending and translucency active. F8 dedicated MFD menu page active. Keyboard UI navigation (W/S/A/D, Q/E, Space, Backspace) verified.
- **Confirmed Working**:
  - Gaze tracking & dwell hysteresis (`MfdGazeTracker`) correctly identifies focused display.
  - Multi-slot rendering (Center Spansh, Left Engineering, Right Exobiology).
  - D3D11 alpha blending with state restoration (`D3D11_BLEND_SRC_ALPHA` / `INV_SRC_ALPHA`).
  - Dedicated in-game F8 "MFD" menu page with live toggling (`fix.cockpit_mfd`, `fix.mfd_slots`, `fix.mfd_opacity`).
  - Cross-DLL shared telemetry (`Local\edvr_mfd_telemetry_v1_<pid>`).
- **Open Issues for Eigent & Local LLM Team**:
  1. *Gaze Raycast Plane Offset*: Live adjustment of `mfd_pos_y`/`z` changes visual render position but `MfdGazeTracker` cone center must track dynamic pose updates per-frame.
  2. *HOTAS / DirectInput Input Interception*: Keyboard navigation works, but joystick/vJoy POV/buttons pass through to the ship while MFD is focused. Intercept and swallow inputs in `MfdInputRouter` or DirectInput hook.
  3. *Per-Slot Transform & Settings Tab*: Allow individual positioning/tilt per slot (e.g. `mfd.0.*`, `mfd.1.*`, `mfd.2.*`) and add in-MFD settings tab.
  4. *Live Game Telemetry Integration*: Wire `status.json` / Elite Journal events into `left_mfd` (Power Distributor pips, fuel, heat, cargo) and `right_mfd` (Exobiology / EDDN).
  5. *Action Handlers & EDHM Theme*: Connect Spacebar to clipboard copy (Spansh next waypoint) and support custom HUD palette overrides / EDHM theme detection.
- **Next Flight Goal**: Test HOTAS input interception and dynamic gaze cone tracking with live Journal telemetry.

---

## Architecture Overview

```
                          [ Elite Dangerous Headset View ]
                                         |
                       [ OpenXR Runtime / openvr_api.dll ]
                                         |
     +-----------------------------------+-----------------------------------+
     |                                   |                                   |
[ MfdGazeTracker ]              [ MfdInputRouter ]                  [ MfdManager ]
(Head look & dwell)            (HOTAS & Keyboard Nav)             (Multi-slot Orchestrator)
     |                                   |                                   |
     +-----------------------------------+-----------------------------------+
                                         |
                         +---------------+---------------+
                         |               |               |
                    [ Slot 0: Spansh ] [ Slot 1: Eng ] [ Slot 2: Exobio ]
                    (Type 1 JSON)      (Type 1 JSON)   (Type 2 Scripted)
                         |               |               |
                         +---------------+---------------+
                                         |
                                  [ MfdRenderer ]
                           (2D Bitmap & Vector Rasterizer)
                                         |
                              [ D3D11 Alpha Blit ]
                      (Projected onto Eye Swapchains)
```

### 1. Multi-Slot Management (`MfdManager`)
- Coordinates $N$ independent MFD displays (default 3 slots):
  - **Slot 0 (`main_mfd`)**: Center forward console (Spansh Neutron Router & Waypoint Nav).
  - **Slot 1 (`left_mfd`)**: Left cockpit console (Ship Engineering, Power Distributor Pips, Modules).
  - **Slot 2 (`right_mfd`)**: Right cockpit console (Exobiology, EDDN sector survey, biological signals).
- Positions are configured in meters relative to seated tracking center:
  - Center: `(0.0, -0.16, -0.55)`, pitch $-20^\circ$.
  - Left: `(-0.45, -0.22, -0.48)`, pitch $-22^\circ$, yaw $+32^\circ$.
  - Right: `(+0.45, -0.22, -0.48)`, pitch $-22^\circ$, yaw $-32^\circ$.

### 2. Rendering & Alpha Blending (`MfdRenderer`)
- Rasterizes `MfdViewModel` into packed RGBA8 bitmap buffers with Elite Dangerous HUD aesthetics.
- D3D11 alpha blending pipeline renders translucent dark glass (`fix.mfd_opacity`) and glowing amber bezels directly onto swapchain RTVs during `D3D11Stereo::renderStereoViews`.
- Automatically captures and restores `ID3D11BlendState` to prevent render state pollution.

### 3. Input Routing & Flight Protection (`MfdInputRouter`)
- Evaluates gaze focus state before consuming inputs.
- Keyboard navigation routes `W`/`S` (up/down list navigation), `Q`/`E` (tab switching), and `Space` (select/interact).
- **Handoff Task**: Intercept DirectInput / XInput device polling when `isFocused == true` to prevent ship thruster/pip changes while navigating MFD screens.

### 4. Data Providers (`IMfdProvider`)
- **Type 1 (Declarative JSON)**: Active. `DeclarativeMfdProvider` parses JSON models containing tabs, lists, key-values, and badges.
- **Type 2 (Scripted WASM/Lua Plugin)**: Reserved architecture slot for external plugins and custom gauge scripts.
- **Live Game Data Integration**: Wire `%USERPROFILE%\Saved Games\Frontier Developments\Elite Dangerous\Status.json` file-watchers into `left_mfd` for real-time pip, fuel, and firegroup updates.

---

## Verified In-Game Flight Results (2026-09-27)

1. `fix.cockpit_mfd = 1`: MFD displays render crisply in 3D cockpit world space.
2. `fix.mfd_head_locked = on`: Displays lock to pilot FOV and rotate perspective naturally with head movement.
3. `fix.mfd_opacity`: Translucency slider verified (0.0 transparent floating HUD to 1.0 solid display).
4. `fix.mfd_slots = 1..3`: Live display addition and retirement verified.
5. `fix.mfd_hud_debug = on`: Real-time telemetry banner displays eye coordinates and draw counts.
6. F8 Settings Menu: Dedicated "MFD" page successfully houses all live controls.
