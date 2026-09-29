# EDVR MFD — Issue & Bug Tracker

## Status
- **Active Open Issues**: 2 (1 High Priority, 1 Medium Priority)
- **Resolved Issues**: 3
- **Last Updated**: 2026-09-29
- **Jump to Section**:
  - [Open Issues](#open-issues)
    - [MFD-002: Joystick/vJoy input not suppressed while MFD has focus](#mfd-002--joystickvjoy-input-not-suppressed-while-mfd-has-focus)
    - [MFD-003: Cockpit MFD panels visible while in Galaxy Map](#mfd-003--cockpit-mfd-panels-visible-while-in-galaxy-map)
  - [Resolved Issues](#resolved)
    - [MFD-001: Keyboard input not suppressed while MFD has focus](#mfd-001--keyboard-input-not-suppressed-while-mfd-has-focus)
    - [MFD-R001: DLL unload crash (0xc0000409 in is_stream_flushable_or_commitable)](#mfd-r001--dll-unload-crash-0xc0000409-in-is_stream_flushable_or_commitable)
    - [MFD-R002: Calling convention mismatch (__cdecl vs __stdcall) causing stack corruption](#mfd-r002--calling-convention-mismatch-__cdecl-vs-__stdcall-causing-stack-corruption)

---

## Open Issues

| ID | Priority | Title | Suspected Area | Status |
|---|---|---|---|---|
| **MFD-002** | High | Joystick/vJoy input not suppressed while MFD has focus | `src/mfd_input_router.cpp` | Open |
| **MFD-003** | Medium | Cockpit MFD panels visible while in Galaxy Map | `src/mfd_manager.cpp`, `include/mfd_telemetry.h` | Open |

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

### MFD-001 | Keyboard input not suppressed while MFD has focus

- **ID**: MFD-001
- **Fixed Date**: 2026-09-29
- **Commits**: `059cf4c3` (edvr-unofficial-patch `feat/plugin-architecture`), no MFD plugin changes needed
- **Root Cause**:
  The plugin's `mfdPluginFilterInput` correctly set `swallowInput = 1` when a panel had gaze focus, and `PluginManager::onFilterInput` correctly returned `true`. However, `inputGateSetPluginBlock` did not exist — the result was never fed to the keyboard gate, so all three keyboard doors (`GetAsyncKeyState`, `GetKeyState`/`GetKeyboardState`, `PeekMessageA`) remained open.
- **Fix** (in `edvr-unofficial-patch`):
  - Added `std::atomic<bool> g_pluginBlock` in `input_gate.cpp`, independent of `g_private` (the F8 menu flag).
  - Added `g_pluginBlock` check to all four keyboard hooks in `input_gate.cpp`.
  - Exported `inputGateSetPluginBlock(bool)` from `input_gate.cpp` / `input_gate.h`.
  - In `menu.cpp`: include `plugin_manager.h`; after each `inputGateTick()` call, query `PluginManager::onFilterInput(0, nullptr)` and pass result to `inputGateSetPluginBlock`. Also clear it in the fault path (`g_budget` retired) so a faulting tick cannot strand the keyboard taken.
- **No MFD plugin changes required**: `mfdPluginFilterInput` already worked correctly.

---

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
