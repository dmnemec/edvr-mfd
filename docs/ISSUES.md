# EDVR MFD — Issue & Bug Tracker

## Status
- **Active Open Issues**: 0
- **Resolved Issues**: 10
- **Last Updated**: 2026-10-15
- **Jump to Section**:
  - [Open Issues](#open-issues)
  - [Resolved Issues](#resolved)
    - [MFD-008: Multi-screen packages render as separate 3D quads instead of consolidated multi-tab displays](#mfd-008--multi-screen-packages-render-as-separate-3d-quads-instead-of-consolidated-multi-tab-displays)
    - [MFD-001: Directional HAT switch repeat press input suppression bleeding through to ship power pips](#mfd-001--directional-hat-switch-repeat-press-input-suppression-bleeding-through-to-ship-power-pips)
    - [MFD-002: Joystick/vJoy input not suppressed while MFD has focus](#mfd-002--joystickvjoy-input-not-suppressed-while-mfd-has-focus)
    - [MFD-003: Cockpit MFD panels visible while in Galaxy Map](#mfd-003--cockpit-mfd-panels-visible-while-in-galaxy-map)
    - [MFD-004: UP navigation input (keyboard & HOTAS hat switch) not captured](#mfd-004--up-navigation-input-keyboard--hotas-hat-switch-not-captured)
    - [MFD-005: F8 Plugins menu toggles reset to default immediately upon navigation change](#mfd-005--f8-plugins-menu-toggles-reset-to-default-immediately-upon-navigation-change)
    - [MFD-006: MFD panels remain visible while in main pause / system menu](#mfd-006--mfd-panels-remain-visible-while-in-main-pause--system-menu)
    - [MFD-007: Show all windows setting does not disable gaze fade feature across all MFD panels](#mfd-007--show-all-windows-setting-does-not-disable-gaze-fade-feature-across-all-mfd-panels)
    - [MFD-R001: DLL unload crash (0xc0000409 in is_stream_flushable_or_commitable)](#mfd-r001--dll-unload-crash-0xc0000409-in-is_stream_flushable_or_commitable)
    - [MFD-R002: Calling convention mismatch (__cdecl vs __stdcall) causing stack corruption](#mfd-r002--calling-convention-mismatch-__cdecl-vs-__stdcall-causing-stack-corruption)

---

## Open Issues

*(No open issues)*

---

## Resolved

### MFD-008 | Multi-screen packages render as separate 3D quads instead of consolidated multi-tab displays

- **ID**: MFD-008
- **Discovered Date**: 2026-10-15
- **Fixed Date**: 2026-10-15
- **Root Cause & Architecture Analysis**:
  See full RCA document at `docs/rca/MFD-008-display-duplication.md`.
  `packages/ship/manifest.json` declared 3 separate screens (`modules`, `power`, `cargo`) and `packages/exploration/manifest.json` declared 2 separate screens (`system`, `bio`). Because `mfd_manager.cpp` instantiates a dedicated 3D Direct3D 11 quad for every screen declared in any package manifest, 6 floating quads were spawned in the cockpit instead of the intended 3 consolidated multi-tab consoles (Left: Ship Readout, Center: Route & Spansh, Right: Exploration Survey).
- **Fix**:
  1. Consolidate `packages/ship/` into a single multi-tab screen `screens/ship.json` containing tabs `integrity` (list), `power` (bars), and `cargo` (list). Deleted obsolete separate screen files. Updated `packages/ship/manifest.json` to declare exactly 1 screen with default left console pose (`[-0.28, -0.22, 0.55]`, yaw: 32°, pitch: -22°).
  2. Updated `companion/cmd/ship` to maintain all three domains in memory and atomically emit updates to `screens/ship.json`.
  3. Consolidate `packages/exploration/` into a single multi-tab screen `screens/exploration.json` containing tabs `system` (list) and `bio` (list). Deleted obsolete separate screen files. Updated `packages/exploration/manifest.json` to declare exactly 1 screen with default right console pose (`[0.28, -0.22, 0.55]`, yaw: -32°, pitch: -22°).
  4. Updated `companion/cmd/exploration` to maintain system and bio domains and atomically emit updates to `screens/exploration.json`.
  5. Updated `packages/route/manifest.json` with explicit center console pose (`[0.0, -0.22, 0.55]`, yaw: 0°, pitch: -22°).
  6. Updated `tools/install_mfd.py` and `companion/cmd/installer/main.go` to purge obsolete screen files during upgrades.
  7. Re-tested all Go companion packages and C++ unit tests, rebuilt release package and verified installer.
---

### MFD-003 | Cockpit MFD panels visible while in Galaxy Map

- **ID**: MFD-003
- **Fixed Date**: 2026-10-15
- **Commits**: `edvr-mfd`
- **Root Cause**:
  Cockpit 3D MFD panels were rendered regardless of active game modal state (Galaxy Map, System Map, Orrery, FSS, SAA, Codex).
- **Fix**:
  Addressed by `ContextEvaluator` reading `GuiFocus` from `Status.json`. MFD rendering is suppressed whenever GuiFocus indicates Galaxy Map (GuiFocus=6), System Map (7), Orrery (8), FSS (9), SAA (10), or Codex (11).

---

### MFD-006 | MFD panels remain visible while in main pause / system menu

- **ID**: MFD-006
- **Fixed Date**: 2026-10-15
- **Commits**: `edvr-mfd`
- **Root Cause**:
  The main pause / system menu (ESC) in Elite Dangerous does not emit a distinct bit in `Status.json` Flags without brittle memory hooks.
- **Fix**:
  Resolved by introducing an explicit **"Hide All"** toggle (`mfd_hide_all_toggle`) under Cockpit MFD in F8 settings (and optional ini hotkey fallback), allowing the pilot to immediately clear all MFDs on demand.

---

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

### MFD-002 | Joystick/vJoy input not suppressed while MFD has focus

- **ID**: MFD-002
- **Fixed Date**: 2026-10-01
- **Commits**: `fork/feat/third-party-plugins` (`edvr-unofficial-patch`), `edvr-mfd`
- **Root Cause**:
  `DeclarativeMfdProvider::onInput` auto-focused MFDs when unfocused, while MFD manager router required explicit slot focus before suppressing inputs.
- **Fix**:
  Updated `DeclarativeMfdProvider::onInput` to return `false` when not focused (`if (!m_model.isFocused) return false;`). Combined with host DirectInput gate fix, inputs are properly swallowed when slot focus is active.

---

### MFD-004 | UP navigation input (keyboard & HOTAS hat switch) not captured

- **ID**: MFD-004
- **Fixed Date**: 2026-10-01
- **Commit**: `edvr-mfd`
- **Root Cause**:
  1. Elite's `.binds` XML maps UP key to `"Key_Up"` or `"Key_UpArrow"`. `parseEliteKey` only handled `"Key_UpArrow"`, rejecting `"Key_Up"` (or mapping it to `'U'`).
  2. DirectInput POV hat angle for UP was checked with `dwPOV <= 35900`, missing the exact 36000 (360.0°) boundary angle returned by DirectInput for 0°/360° UP.
- **Fix**:
  Added `"Key_Up"`, `"Key_Down"`, `"Key_Left"`, `"Key_Right"` support to `parseEliteKey`, expanded POV key variations (`"POV1Up"`, `"Hat1Up"`, `"Hat_Up"`), and updated joystick POV polling logic to handle `dwPOV <= 36000`. Added test suite to `mfd_test.cpp`.

---

### MFD-R001 | DLL unload crash (0xc0000409 in is_stream_flushable_or_commitable)

- **ID**: MFD-R001
- **Fixed Date**: 2026-09-28
- **Commit**: `edvr-mfd`
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
