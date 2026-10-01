# EDVR MFD — Issue & Bug Tracker

## Status
- **Active Open Issues**: 2 (2 Medium Priority)
- **Resolved Issues**: 5
- **Last Updated**: 2026-10-01
- **Jump to Section**:
  - [Open Issues](#open-issues)
    - [MFD-003: Cockpit MFD panels visible while in Galaxy Map](#mfd-003--cockpit-mfd-panels-visible-while-in-galaxy-map)
    - [MFD-005: F8 Plugins menu toggles reset to default immediately upon navigation change](#mfd-005--f8-plugins-menu-toggles-reset-to-default-immediately-upon-navigation-change)
  - [Resolved Issues](#resolved)
    - [MFD-001: Keyboard & HOTAS input suppression failing in latest build](#mfd-001--keyboard--hotas-input-suppression-failing-in-latest-build)
    - [MFD-002: Joystick/vJoy input not suppressed while MFD has focus](#mfd-002--joystickvjoy-input-not-suppressed-while-mfd-has-focus)
    - [MFD-004: UP navigation input (keyboard & HOTAS hat switch) not captured](#mfd-004--up-navigation-input-keyboard--hotas-hat-switch-not-captured)
    - [MFD-R001: DLL unload crash (0xc0000409 in is_stream_flushable_or_commitable)](#mfd-r001--dll-unload-crash-0xc0000409-in-is_stream_flushable_or_commitable)
    - [MFD-R002: Calling convention mismatch (__cdecl vs __stdcall) causing stack corruption](#mfd-r002--calling-convention-mismatch-__cdecl-vs-__stdcall-causing-stack-corruption)

---

## Open Issues

| ID | Priority | Title | Suspected Area | Status |
|---|---|---|---|---|
| **MFD-003** | Medium | Cockpit MFD panels visible while in Galaxy Map | `src/mfd_manager.cpp`, `include/mfd_telemetry.h` | Open |
| **MFD-005** | Medium | F8 Plugins menu toggles reset to default immediately upon navigation change | `src/menu/plugins_page.cpp`, `src/plugins/plugin_manager.cpp` (in EDVR host) / `src/mfd_plugin.cpp` | Open |

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

---

## Resolved

### MFD-001 | Keyboard & HOTAS input suppression failing in latest build

- **ID**: MFD-001
- **Fixed Date**: 2026-10-01
- **Commits**: `ececeb3` (host: `edvr-unofficial-patch`), `8ffb330` (`edvr-mfd`)
- **Root Cause**:
  DirectInput keyboard vtable hooks (`filterDeviceState` and `filterDeviceData`) in the EDVR host checked `g_private` (F8 menu open) but ignored `g_pluginBlock` (plugin keyboard focus). When MFD plugin set `swallowInput = 1`, `g_pluginBlock` became true, but DirectInput keyboard calls bypassed the gate and passed raw keys to Elite.
- **Fix**:
  Updated `filterDeviceState` and `filterDeviceData` in host `src/d3d11/input_gate.cpp` to check `g_pluginBlock.load()` alongside `g_private.load()`. Added unit tests in `mfd_test.cpp` verifying input filter suppression logic.

---

### MFD-002 | Joystick/vJoy input not suppressed while MFD has focus

- **ID**: MFD-002
- **Fixed Date**: 2026-10-01
- **Commits**: `ececeb3` (host: `edvr-unofficial-patch`), `8ffb330` (`edvr-mfd`)
- **Root Cause**:
  `DeclarativeMfdProvider::onInput` auto-focused MFDs when unfocused, while MFD manager router required explicit slot focus before suppressing inputs.
- **Fix**:
  Updated `DeclarativeMfdProvider::onInput` to return `false` when not focused (`if (!m_model.isFocused) return false;`). Combined with host DirectInput gate fix, inputs are properly swallowed when slot focus is active.

---

### MFD-004 | UP navigation input (keyboard & HOTAS hat switch) not captured

- **ID**: MFD-004
- **Fixed Date**: 2026-10-01
- **Commit**: `8ffb330` (`edvr-mfd`)
- **Root Cause**:
  1. Elite's `.binds` XML maps UP key to `"Key_Up"` or `"Key_UpArrow"`. `parseEliteKey` only handled `"Key_UpArrow"`, rejecting `"Key_Up"` (or mapping it to `'U'`).
  2. DirectInput POV hat angle for UP was checked with `dwPOV <= 35900`, missing the exact 36000 (360.0°) boundary angle returned by DirectInput for 0°/360° UP.
- **Fix**:
  Added `"Key_Up"`, `"Key_Down"`, `"Key_Left"`, `"Key_Right"` support to `parseEliteKey`, expanded POV key variations (`"POV1Up"`, `"Hat1Up"`, `"Hat_Up"`), and updated joystick POV polling logic to handle `dwPOV <= 36000`. Added test suite to `mfd_test.cpp`.

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

