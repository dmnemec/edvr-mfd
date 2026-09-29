# EDVR MFD — Issue & Bug Tracker

## Status
- **Active Open Issues**: 3 (2 High Priority, 1 Medium Priority)
- **Resolved Issues**: 2
- **Last Updated**: 2026-09-28
- **Jump to Section**:
  - [Open Issues](#open-issues)
    - [MFD-001: Keyboard input not suppressed while MFD has focus](#mfd-001--keyboard-input-not-suppressed-while-mfd-has-focus)
    - [MFD-002: Joystick/vJoy input not suppressed while MFD has focus](#mfd-002--joystickvjoy-input-not-suppressed-while-mfd-has-focus)
    - [MFD-003: Cockpit MFD panels visible while in Galaxy Map](#mfd-003--cockpit-mfd-panels-visible-while-in-galaxy-map)
  - [Resolved Issues](#resolved)
    - [MFD-R001: DLL unload crash (0xc0000409 in is_stream_flushable_or_commitable)](#mfd-r001--dll-unload-crash-0xc0000409-in-is_stream_flushable_or_commitable)
    - [MFD-R002: Calling convention mismatch (__cdecl vs __stdcall) causing stack corruption](#mfd-r002--calling-convention-mismatch-__cdecl-vs-__stdcall-causing-stack-corruption)

---

## Open Issues

| ID | Priority | Title | Suspected Area | Status |
|---|---|---|---|---|
| **MFD-001** | High | Keyboard input not suppressed while MFD has focus | `src/mfd_input_router.cpp` | Open |
| **MFD-002** | High | Joystick/vJoy input not suppressed while MFD has focus | `src/mfd_input_router.cpp` | Open |
| **MFD-003** | Medium | Cockpit MFD panels visible while in Galaxy Map | `src/mfd_manager.cpp`, `include/mfd_telemetry.h` | Open |

### MFD-001 | Keyboard input not suppressed while MFD has focus

- **ID**: MFD-001
- **Priority**: High
- **Title**: Keyboard input not suppressed while MFD has focus
- **Date Reported**: 2026-09-28
- **Description**:
  When an MFD panel is focused (receiving gaze/input), keyboard keystrokes still pass through to Elite Dangerous. Expected behavior is that keyboard input mapped to or captured by the MFD should be swallowed and not forwarded to the game while the MFD maintains focus.
- **Reproduction Steps**:
  1. Look directly at an MFD panel in VR to acquire gaze focus.
  2. Press keys bound to MFD navigation (e.g., arrow keys, UI select, or custom bound keys).
  3. Observe that game controls (such as ship menus, throttle, or landing gear) trigger concurrently with MFD navigation actions.
- **Suspected Area of Code**:
  - `src/mfd_input_router.cpp` — `filterInput` callback / keyboard interception logic.
  - `src/mfd_plugin.cpp` (`mfdPluginFilterInput` / `onFilterInput`) and the host hook dispatch in EDVR's `d3d11.dll`.

---

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
