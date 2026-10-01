# EDVR MFD — Issue & Bug Tracker

## Status
- **Active Open Issues**: 5 (3 High Priority, 2 Medium Priority)
- **Resolved Issues**: 2
- **Last Updated**: 2026-09-29
- **Jump to Section**:
  - [Open Issues](#open-issues)
    - [MFD-001: Keyboard & HOTAS input suppression failing in latest build](#mfd-001--keyboard--hotas-input-suppression-failing-in-latest-build)
    - [MFD-002: Joystick/vJoy input not suppressed while MFD has focus](#mfd-002--joystickvjoy-input-not-suppressed-while-mfd-has-focus)
    - [MFD-003: Cockpit MFD panels visible while in Galaxy Map](#mfd-003--cockpit-mfd-panels-visible-while-in-galaxy-map)
    - [MFD-004: UP navigation input (keyboard & HOTAS hat switch) not captured](#mfd-004--up-navigation-input-keyboard--hotas-hat-switch-not-captured)
    - [MFD-005: F8 Plugins menu toggles reset to default immediately upon navigation change](#mfd-005--f8-plugins-menu-toggles-reset-to-default-immediately-upon-navigation-change)
  - [Resolved Issues](#resolved)
    - [MFD-R001: DLL unload crash (0xc0000409 in is_stream_flushable_or_commitable)](#mfd-r001--dll-unload-crash-0xc0000409-in-is_stream_flushable_or_commitable)
    - [MFD-R002: Calling convention mismatch (__cdecl vs __stdcall) causing stack corruption](#mfd-r002--calling-convention-mismatch-__cdecl-vs-__stdcall-causing-stack-corruption)

---

## Open Issues

| ID | Priority | Title | Suspected Area | Status |
|---|---|---|---|---|
| **MFD-001** | High | Keyboard & HOTAS input suppression failing in latest build | `src/d3d11/input_gate.cpp`, `src/mfd_input_router.cpp` | In Progress |
| **MFD-002** | High | Joystick/vJoy input not suppressed while MFD has focus | `src/mfd_input_router.cpp`, `src/d3d11/input_gate.cpp` | In Progress |
| **MFD-003** | Medium | Cockpit MFD panels visible while in Galaxy Map | `src/mfd_manager.cpp`, `include/mfd_telemetry.h` | Open |
| **MFD-004** | High | UP navigation input (keyboard & HOTAS hat switch) not captured | `src/mfd_input_router.cpp` | In Progress |
| **MFD-005** | Medium | F8 Plugins menu toggles reset to default immediately upon navigation change | `src/menu/plugins_page.cpp`, `src/plugins/plugin_manager.cpp` (in EDVR host) / `src/mfd_plugin.cpp` | Open |

### MFD-001 | Keyboard & HOTAS input suppression failing in latest build

- **ID**: MFD-001
- **Priority**: High
- **Title**: Keyboard & HOTAS input suppression failing in latest build
- **Date Reported**: 2026-09-28 (Reopened 2026-09-29)
- **Description**:
  Reopened following test flight report on latest build. Keyboard keystrokes and HOTAS inputs still leak through to Elite Dangerous while an MFD panel is focused. Although host-side gate integration was added, input continues passing through to game controls.
- **Reproduction Steps**:
  1. Focus an MFD panel in VR.
  2. Press keyboard or HOTAS controls mapped to MFD navigation.
  3. Observe game controls (e.g. ship menus, throttle, hardpoints) triggering concurrently with MFD actions.
- **Suspected Area of Code**:
  - `src/d3d11/input_gate.cpp` / `menu.cpp` in EDVR host repo — gate evaluation timing or unhooked DirectInput/XInput paths.
  - `src/mfd_input_router.cpp` / `mfd_plugin.cpp` (`mfdPluginFilterInput`).

---

### MFD-004 | UP navigation input (keyboard & HOTAS hat switch) not captured

- **ID**: MFD-004
- **Priority**: High
- **Title**: UP navigation input (keyboard & HOTAS hat switch) not captured
- **Date Reported**: 2026-09-29
- **Description**:
  In the latest build, UP navigation inputs fail to register on both HOTAS hat switch (Hat Up) and keyboard (Up arrow / bound Up key). Other navigation inputs (Down, Left, Right, Select/Interaction) work correctly on the HOTAS.
- **Reproduction Steps**:
  1. Focus an MFD panel in VR.
  2. Actuate UP on the HOTAS hat switch or press the UP key on the keyboard.
  3. Observe that the MFD selection does not move UP, whereas DOWN/LEFT/RIGHT/Select function properly on HOTAS.
- **Suspected Area of Code**:
  - `src/mfd_input_router.cpp` — key/POV hat mapping parser, DirectInput POV hat threshold logic (e.g. 0° POV angle evaluation), or virtual key mapping for UP action.

---

### MFD-005 | F8 Plugins menu toggles reset to default immediately upon navigation change

- **ID**: MFD-005
- **Priority**: Medium
- **Title**: F8 Plugins menu toggles reset to default immediately upon navigation change
- **Date Reported**: 2026-09-29
- **Description**:
  In the F8 menu Plugins tab, toggling settings options (such as "Show All" or "Show MFDs") causes the toggle state to immediately revert/flip back to its default value as soon as navigation moves away from the option.
- **Reproduction Steps**:
  1. Open the F8 EDVR menu and navigate to the Plugins tab.
  2. Toggle either the "Show All" or "Show MFDs" setting toggle.
  3. Move the menu focus cursor away from the toggle option.
  4. Observe that the toggle flips back to its original default state immediately upon leaving the option.
- **Suspected Area of Code**:
  - Host menu rendering/event handler (`src/menu/plugins_page.cpp` or plugin settings callback in `edvr-unofficial-patch`).
  - Plugin settings state getter/setter callback or value backing variable synchronization in `src/mfd_plugin.cpp`.



### MFD-002 | Joystick/vJoy input not suppressed while MFD has focus

- **ID**: MFD-002
- **Priority**: High
- **Title**: Joystick/vJoy input not suppressed while MFD has focus
- **Date Reported**: 2026-09-28
- **Description**:
  Similar to MFD-001, but applies to joystick, HOTAS, and vJoy axes and buttons. When an MFD panel is focused, joystick inputs intended for MFD navigation pass through to the game engine instead of being swallowed.
- **Reproduction Steps**:
  1. Acquire gaze focus on any cockpit MFD panel.
  2. Actuate HOTAS buttons, hats, or vJoy inputs mapped to MFD navigation actions.
  3. Observe that Elite Dangerous continues processing the joystick/HOTAS inputs (e.g., shifting power pips, deploying hardpoints, or targeting) while the MFD processes the same inputs.
- **Suspected Area of Code**:
  - `src/mfd_input_router.cpp` — `filterInput` callback, `edvrFilterInput` export, and HOTAS polling (`pollJoystickInputs`, `pollAndRoute`).
  - DirectInput / XInput hook handling in EDVR host runtime.

---

### MFD-003 | Cockpit MFD panels visible while in Galaxy Map

- **ID**: MFD-003
- **Priority**: Medium
- **Title**: Cockpit MFD panels visible while in Galaxy Map
- **Date Reported**: 2026-09-28
- **Description**:
  MFD panels rendered in cockpit 3D space remain visible when the player opens the Galaxy Map (or System Map). The Galaxy Map is a separate game state where cockpit-bound MFDs should not render (or should be rendered separately in map space).
- **Reproduction Steps**:
  1. Launch Elite Dangerous Odyssey in VR with MFD plugin enabled.
  2. Enter cockpit flight mode and confirm MFD panels are rendering.
  3. Open the Galaxy Map or System Map.
  4. Observe that the cockpit 3D MFD quads remain visible floating over or within the map space.
- **Suspected Area of Code**:
  - `src/mfd_manager.cpp` — `update()` / `render()` — lack of game-state awareness.
  - `include/mfd_telemetry.h` (`MfdSharedState`) — shared-memory feed from Elite should expose a `gameMode` or similar flag.
  - *Note*: Requires investigation to confirm whether telemetry exposes a game-mode enum or whether status flags need to be extracted from Elite's `Status.json` or memory hooks.

---

## Resolved

### MFD-R001 | DLL unload crash (0xc0000409 in is_stream_flushable_or_commitable)

- **ID**: MFD-R001
- **Fixed Date**: 2026-09-28
- **Commit**: `aa2226d`
- **Root Cause**:
  The `Log` class used CRT stdio (`FILE*`). When linked with `/MT`, CRT's `_flushall` atexit handler conflicted with `~Log()`'s `fclose()` invocation during DLL teardown, causing a fast-fail crash (0xc0000409).
- **Fix**:
  Replaced `FILE*` with Win32 `HANDLE` (`CreateFileW`/`WriteFile`/`CloseHandle`). Added explicit `Log::get().close()` in `mfdPluginShutdown()` before static destructors run.

---

### MFD-R002 | Calling convention mismatch (__cdecl vs __stdcall) causing stack corruption

- **ID**: MFD-R002
- **Fixed Date**: 2026-09-28 (Plugin architecture integration)
- **Root Cause**:
  Plugin callback function pointers were declared without `EDVR_API` (`__stdcall`), causing stack pointer corruption on call across the DLL boundary.
- **Fix**:
  Added `EDVR_API` macro to all function pointers in `include/edvr_plugin_api.h`; replaced lambdas (which cannot be `__stdcall`) with named static functions in both host and plugin.
