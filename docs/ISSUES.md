# EDVR MFD — Issue & Bug Tracker

## Status
- **Active Open Issues**: 2 (Medium Priority)
- **Resolved Issues**: 7
- **Last Updated**: 2026-10-14
- **Jump to Section**:
  - [Open Issues](#open-issues)
    - [MFD-003: Cockpit MFD panels visible while in Galaxy Map](#mfd-003--cockpit-mfd-panels-visible-while-in-galaxy-map)
    - [MFD-006: MFD panels remain visible while in main pause / system menu](#mfd-006--mfd-panels-remain-visible-while-in-main-pause--system-menu)
  - [Resolved Issues](#resolved)
    - [MFD-001: Directional HAT switch repeat press input suppression bleeding through to ship power pips](#mfd-001--directional-hat-switch-repeat-press-input-suppression-bleeding-through-to-ship-power-pips)
    - [MFD-002: Joystick/vJoy input not suppressed while MFD has focus](#mfd-002--joystickvjoy-input-not-suppressed-while-mfd-has-focus)
    - [MFD-004: UP navigation input (keyboard & HOTAS hat switch) not captured](#mfd-004--up-navigation-input-keyboard--hotas-hat-switch-not-captured)
    - [MFD-005: F8 Plugins menu toggles reset to default immediately upon navigation change](#mfd-005--f8-plugins-menu-toggles-reset-to-default-immediately-upon-navigation-change)
    - [MFD-007: Show all windows setting does not disable gaze fade feature across all MFD panels](#mfd-007--show-all-windows-setting-does-not-disable-gaze-fade-feature-across-all-mfd-panels)
    - [MFD-R001: DLL unload crash (0xc0000409 in is_stream_flushable_or_commitable)](#mfd-r001--dll-unload-crash-0xc0000409-in-is_stream_flushable_or_commitable)
    - [MFD-R002: Calling convention mismatch (__cdecl vs __stdcall) causing stack corruption](#mfd-r002--calling-convention-mismatch-__cdecl-vs-__stdcall-causing-stack-corruption)

---

## Open Issues

| ID | Priority | Title | Suspected Area | Status |
|---|---|---|---|---|
| **MFD-003** | Medium | Cockpit MFD panels visible while in Galaxy Map | `src/mfd_context.cpp`, `src/mfd_manager.cpp` | In Progress (Resolving via ContextEvaluator §7) |
| **MFD-006** | Medium | MFD panels remain visible while in main pause / system menu | `src/mfd_plugin.cpp`, `src/mfd_manager.cpp` | In Progress (Resolving via "Hide All" toggle §7.4, D1) |

---

### MFD-003 | Cockpit MFD panels visible while in Galaxy Map

- **ID**: MFD-003
- **Priority**: Medium
- **Title**: Cockpit MFD panels visible while in Galaxy Map
- **Date Reported**: 2026-09-28
- **Status**: In Progress
- **Description**:
  MFD panels rendered in cockpit 3D space remain visible when the player opens the Galaxy Map (or System Map). The Galaxy Map is a separate game state where cockpit-bound MFDs should not render.
- **Resolution Plan**:
  Addressed by `ContextEvaluator` (design doc §7) reading `GuiFocus` from `Status.json`. Galaxy Map (GuiFocus=6), System Map (7), Orrery (8), FSS (9), SAA (10), Codex (11) are added to the global exclusion list, automatically hiding MFDs.

---

### MFD-006 | MFD panels remain visible while in main pause / system menu

- **ID**: MFD-006
- **Priority**: Medium
- **Title**: MFD panels remain visible while in main pause / system menu
- **Date Reported**: 2026-10-01
- **Status**: In Progress
- **Description**:
  When hitting ESC to bring up the main pause menu, cockpit 3D MFD panels remain active and rendered behind/over the pause menu.
- **Resolution Plan**:
  Per user decision, avoid fragile reverse engineering of game pause internals. Resolved by introducing an explicit **"Hide All"** toggle (`mfd_hide_all_toggle`) under Cockpit MFD in F8 settings (and optional ini hotkey), allowing the pilot to immediately clear all MFDs on demand.

---

## Resolved

### MFD-001 | Directional HAT switch repeat press input suppression bleeding through to ship power pips

- **ID**: MFD-001
- **Fixed Date**: 2026-10-14
- **Commits**: `fork/feat/third-party-plugins` (`edvr-unofficial-patch`), `edvr-mfd`
- **Root Cause**:
  DirectInput keyboard and joystick state hooks in the EDVR host checked private menu state but did not properly suppress repeat presses when custom DirectInput formats were used by Elite Dangerous. Additionally, zeroing joystick buffers placed POV hat angles at 0 (UP), resulting in unwanted UP pip distribution.
- **Fix**:
  Fixed DirectInput state/data filtering to preserve POV centered state (`0xFFFFFFFF` / `-1`) and ensure plugin input block flag `g_pluginBlock` suppresses repeat presses across keyboard and joystick devices.

---

### MFD-005 | F8 Plugins menu toggles reset to default immediately upon navigation change

- **ID**: MFD-005
- **Fixed Date**: 2026-10-14
- **Commits**: `fork/feat/third-party-plugins` (`edvr-unofficial-patch`), `edvr-mfd`
- **Root Cause**:
  In the EDVR host graphics interface module and extension manager (`src/d3d11/menu.cpp`), plugin setting entries in the F8 menu re-queried the plugin getter on every cursor move and overwrote pending/persisted state, or lacked bidirectional synchronization with the live plugin backing fields.
- **Fix**:
  Synchronized plugin settings state management in `src/d3d11/menu.cpp` and `src/plugins/plugin_manager.cpp` so toggles persist their active state during navigation and reflect live plugin values.

---

### MFD-007 | Show all windows setting does not disable gaze fade feature across all MFD panels

- **ID**: MFD-007
- **Fixed Date**: 2026-10-14
- **Commits**: `edvr-mfd`
- **Root Cause**:
  In `MfdManager::updateGaze()`, `slot.currentAlpha` was computed based solely on `slot.autoHideUntilGaze` and head-gaze focus status, ignoring the `m_showAllWindows` setting. Even with "Show all windows" enabled, panels would fade out to 0 opacity when looking away.
- **Fix**:
  Updated `MfdManager::updateGaze()` to check `if (slot.autoHideUntilGaze && !m_showAllWindows)`. When `m_showAllWindows` is true, gaze fade is completely bypassed and all windows are maintained at full opacity (`slot.currentAlpha = 1.0f`). Added `slot.currentAlpha = 1.0f` to `setShowAllWindows(true)` and added regression tests in `mfd_test.cpp`.

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

