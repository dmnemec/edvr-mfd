# EDVR MFD — Issue & Bug Tracker

## Status
- **Active Open Issues**: 3
- **Resolved Issues**: 17
- **Last Updated**: 2026-10-16
- **Jump to Section**:
  - [Open Issues](#open-issues)
    - [MFD-015: Media Player MFD with system media control (GSMTC) & HOTAS navigation](#mfd-015--media-player-mfd-with-system-media-control-gsmtc--hotas-navigation)
    - [MFD-016: Dynamic companion-driven color overrides & state-reactive theming](#mfd-016--dynamic-companion-driven-color-overrides--state-reactive-theming)
    - [MFD-017: Configurable MFD viewable size, aspect ratios, and compact layout templates](#mfd-017--configurable-mfd-viewable-size-aspect-ratios-and-compact-layout-templates)
  - [Resolved Issues](#resolved)
    - [MFD-018: Power readout shows Max potential draw instead of Current active draw after priority shedding](#mfd-018--power-readout-shows-max-potential-draw-instead-of-current-active-draw-after-priority-shedding)
    - [MFD-014: System and Bio survey display verification after journal recovery](#mfd-014--system-and-bio-survey-display-verification-after-journal-recovery)
    - [MFD-013: Power tab shows 0MW used due to ModuleInfo.json pluralization mismatch](#mfd-013--power-tab-shows-0mw-used-due-to-moduleinfojson-pluralization-mismatch)
    - [MFD-012: Journal discovery selects stale 2022 journal due to ASCII string sorting](#mfd-012--journal-discovery-selects-stale-2022-journal-due-to-ascii-string-sorting)
    - [MFD-011: FSD integrity not reporting reduced after neutron/white dwarf jumps](#mfd-011--fsd-integrity-not-reporting-reduced-after-neutronwhite-dwarf-jumps)
    - [MFD-009: Color preset name 1-off when scrolling in Settings tab](#mfd-009--color-preset-name-1-off-when-scrolling-in-settings-tab)
    - [MFD-010: List row subtext overlaps main text due to insufficient row height and font collision](#mfd-010--list-row-subtext-overlaps-main-text-due-to-insufficient-row-height-and-font-collision)
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

### MFD-015 | Media Player MFD with system media control (GSMTC) & HOTAS navigation

- **ID**: MFD-015
- **Type**: Feature Request
- **Created Date**: 2026-10-16
- **Status**: Open
- **Description**:
  Provide an in-cockpit MFD for playback and control of external media players (Spotify, YouTube, browser playback, etc.) using Windows Global System Media Transport Controls (GSMTC) and synthetic key fallback (`VK_MEDIA_*`).
- **Architecture & Technical Requirements**:
  1. **Companion Process**: Implemented as a standalone Go companion executable (`companion/cmd/mediaplayer`).
  2. **Metadata & Status Display**: Uses Windows WinRT GSMTC session APIs to capture current media track name, artist/channel, playback status (playing/paused), timeline/scrub position, and volume level into the `MfdViewModel` schema.
  3. **VR-Accessible Controls**: Interactive action controls for Play/Pause, Next Track, Previous Track, Volume Up/Down, and Mute navigable entirely via cockpit HOTAS/joystick hat switches and keyboard navigation without breaking immersion or taking off the VR headset.
  4. **Input Gating**: Router swallows all mapped navigation and action execution events while the media player MFD is focused to avoid leaking inputs to flight controls.
- **Acceptance Criteria**:
  - Live media sessions on Windows (Spotify, YouTube in Chrome/Edge, etc.) populate track title and artist in the MFD.
  - Triggering Next/Prev/Play-Pause via MFD UI updates the external media session.
  - No inputs leak to ship controls while interacting with media player buttons in VR.

---

### MFD-016 | Dynamic companion-driven color overrides & state-reactive theming

- **ID**: MFD-016
- **Type**: Feature Request
- **Created Date**: 2026-10-16
- **Status**: Open
- **Description**:
  Enable companion packages to dynamically control MFD rendering colors based on real-time game telemetry (e.g. ship module integrity, combat state, hardpoints deployed, shield status) or scripted animations (e.g. cycling disco mode).
- **Architecture & Technical Requirements**:
  1. **View Model Schema Extension**: Extend the MFD schema and view model (`mfd_view_model.h` / companion JSON) to support dynamic color override fields (`dynamic_color`, `alert_mode`, `alert_color`).
  2. **State Reactive Interpolation**:
     - Implement smooth gradient transitions (e.g. lerping from default cockpit amber `(255, 110, 0)` down to warning red `(255, 30, 20)` in proportion to module health/integrity).
     - Color returns cleanly to default theme upon module repair/restoration.
  3. **Render Throttling & Animation Performance**:
     - MFD dirty flagging and GPU upload frequency must be throttled to prevent VR frame pacing drops during rapid color transitions or disco animations.
     - Build a prototype testing MFD specifically to evaluate upload tick rates (e.g., 10 Hz vs 30 Hz vs 60 Hz) to establish performance thresholds.
  4. **Priority Hierarchy**: Dynamic game-state alerts can be configured to take priority over standard user color presets, while preserving user customization override when explicitly chosen.
- **Acceptance Criteria**:
  - Companion emitting a degraded integrity or alert status smoothly fades MFD accent and background colors toward alarm palettes.
  - Rapid color transitions maintain 90+ FPS in VR without causing D3D11 device contention or micro-stutter.
  - Restoring normal ship state seamlessly reverts the MFD back to the configured default theme.

---

### MFD-017 | Configurable MFD viewable size, aspect ratios, and compact layout templates

- **ID**: MFD-017
- **Type**: Feature Request
- **Created Date**: 2026-10-16
- **Status**: Open
- **Description**:
  Support custom physical dimensions (width and height in meters), customizable render aspect ratios, and compact layout templates for MFDs so displays can be thin horizontal bars (such as a compact media player banner) rather than fixed 4:3 rectangles.
- **Architecture & Technical Requirements**:
  1. **Manifest Sizing Declarations**:
     - Extend `manifest.json` schema to allow packages to declare default physical dimensions (`width_m`, `height_m`) and render texture resolution (`render_width`, `render_height`).
     - Example: Media player banner with `0.24m x 0.06m` physical quad and `512x128` pixel buffer.
  2. **In-Cockpit Settings Customization**:
     - Add Width and Height adjustment rows to the MFD Settings tab with "Default" and "Custom" modes.
     - Custom mode exposes numeric/stepped adjustment sliders for physical quad dimensions (and corresponding render buffer dimensions).
     - Includes a "Reset to Default" action matching the color settings pattern.
  3. **Compact / Banner Layout Templates**:
     - Provide layout templates (`"standard"`, `"compact_banner"`, `"minimal"`) in the rendering pipeline (`MfdRenderer`) so UI components (headers, tab bars, content items) reflow cleanly into horizontal banners without overlapping or clipping text.
  4. **Compositor & D3D11 Pipeline Support**:
     - Dynamically recreate or resize the D3D11 render target texture and OpenXR quad layer dimensions per screen instead of hardcoding uniform quad proportions.
- **Acceptance Criteria**:
  - A package can define a banner aspect ratio in its manifest and render with crisp non-distorted texels.
  - Settings tab allows the user to adjust and reset MFD width and height live in the headset.
  - Banner template displays media controls and status legibly within a condensed vertical space.

## Resolved

### MFD-018 | Power readout shows Max potential draw instead of Current active draw after priority shedding

- **ID**: MFD-018
- **Discovered Date**: 2026-10-16
- **Fixed Date**: 2026-10-16
- **Root Cause**:
  `BuildShipDisplay` and `BuildPowerDisplay` in `companion/cmd/ship/state.go` calculated power draw by summing all modules where `m.On == true` without accounting for hardpoint retraction state (`Status.json` bit 6) or ship power priority shedding (where lower priority tiers 5 down to 1 are disabled by the ship's computer when active power exceeds power plant capacity). Furthermore, `HandleModuleInfo` and `EventLoadout` guarded priority assignment with `entry.Priority > 0`, ignoring Priority 0 (which represents Priority 1 in Elite Dangerous). This caused the POWER tab to show max potential deployed draw rather than true current usage, displaying an over-limit CRIT red bar even when the ship was safely powered.
- **Fix**:
  1. Added `HardpointsDeployed bool` to `ShipState`, and helper method `HardpointsDeployed()` on `elite.StatusSnapshot` in `companion/pkg/mfdkit/elite/watchers.go`.
  2. In `companion/cmd/ship/state.go`:
     - Updated `HandleStatus` to track `snap.HardpointsDeployed()` and trigger re-render on changes.
     - Fixed priority assignment in `HandleModuleInfo` and `EventLoadout` to properly update `Priority` even when `entry.Priority == 0`.
     - Implemented `CalculatePowerUsage() (currentUsage, maxUsage float64)` to group active demanding modules by priority tier (accounting for 0 MW weapon draw when hardpoints are retracted) and simulate ship priority shedding up to power plant capacity.
     - Updated `BuildShipDisplay()` and `BuildPowerDisplay()` to display `currentUsage` on the power bar and added `"Current Usage"` and `"Max Deployed"` KeyValues to the Power status tab.
  3. Added comprehensive unit tests in `companion/cmd/ship/ship_test.go` covering priority shedding, hardpoint retraction/deployment, tier 0 brownout, and priority 0 indexing.

---

### MFD-014 | System and Bio survey display verification after journal recovery

- **ID**: MFD-014
- **Discovered Date**: 2026-10-16
- **Fixed Date**: 2026-10-16
- **Root Cause**:
  Coupled with MFD-012: because `TailJournal` was tailing a dormant 2022 journal file, all live `FSSDiscoveryScan`, `Scan`, and `ScanOrganic` events during flight never reached `companion/cmd/exploration`. The companion remained stuck on the 2022 checkpoint without ever populating live scanned bodies or organic samples.
- **Fix**:
  Resolved by fixing journal discovery in MFD-012. Tailing active journal files immediately restores live stream ingestion for FSS honks, celestial body scans, DSS probes, and exobiology genetic samplers.

---

### MFD-013 | Power tab shows 0MW used due to ModuleInfo.json pluralization mismatch

- **ID**: MFD-013
- **Discovered Date**: 2026-10-16
- **Fixed Date**: 2026-10-16
- **Root Cause**:
  Elite Dangerous writes `ModulesInfo.json` (plural with an 's') in `%USERPROFILE%\Saved Games\Frontier Developments\Elite Dangerous`, while `companion/pkg/mfdkit/elite/watchers.go` exclusively watched `ModuleInfo.json` (singular). Power draw and priority data were never read, leaving module power at 0.0 MW on the POWER tab.
- **Fix**:
  1. Updated `WatchModuleInfo` and `resolveSnapshotPath` in `companion/pkg/mfdkit/elite/watchers.go` to inspect the journal directory for `ModulesInfo.json` and watch it, falling back to `ModuleInfo.json` if absent.
  2. Updated `ReadModuleInfo` to support both plural and singular sibling filenames.
  3. Added unit tests in `companion/pkg/mfdkit/elite/elite_test.go` and verified against the user's real `ModulesInfo.json` (43 modules parsed).

---

### MFD-012 | Journal discovery selects stale 2022 journal due to ASCII string sorting

- **ID**: MFD-012
- **Discovered Date**: 2026-10-16
- **Fixed Date**: 2026-10-16
- **Root Cause**:
  In `companion/pkg/mfdkit/elite/discovery.go`, `ListJournalFiles` used `sort.Strings(files)`. Legacy journals named `Journal.221117191657.01.log` (YYMMDD format from 2022) sort after modern journals named `Journal.2026-10-16T001949.01.log` (YYYY-MM-DD format) because character `'2'` > `'0'`. As a result, `FindLatestJournal` picked the dormant 2022 journal, breaking live telemetry across all companions: the route planner stuck on `Alioth` (route[0]), exploration reported `Neto` (from 2022), and system/bio scans stayed empty.
- **Fix**:
  1. Updated `ListJournalFiles` to retrieve file modification times (`FileInfo.ModTime()`) and sort primarily by modification timestamp ascending, with secondary fallback to parsed journal timestamps (`ParseJournalTimestamp`), and tertiary fallback to filename strings.
  2. Added unit tests in `companion/pkg/mfdkit/elite/elite_test.go` verifying legacy journal ordering and verified against the live player journal directory.

### MFD-011 | FSD integrity not reporting reduced after neutron/white dwarf jumps

- **ID**: MFD-011
- **Discovered Date**: 2026-10-16
- **Fixed Date**: 2026-10-16
- **Root Cause**:
  Elite Dangerous emits `JetConeBoost` with `BoostValue` when supercharging in a jet cone, inflicting 1% FSD damage, rather than emitting a full `Loadout` event. `companion/pkg/mfdkit/elite` did not parse `JetConeBoost` into strongly typed events, and `companion/cmd/ship/state.go` did not handle it, leaving FSD module integrity unchanged.
- **Fix**:
  1. Added `EventJetConeBoost` struct in `companion/pkg/mfdkit/elite/types.go` and handled `"JetConeBoost"` in `ParseJournalEntry`.
  2. In `companion/cmd/ship/state.go`, added `*elite.EventJetConeBoost` case in `HandleJournalEntry` to locate the `FrameShiftDrive` (or item containing `"hyperdrive"`), decrement health by 0.01 (clamped at `math.Max(0.0, mod.Health - 0.01)`), recompute integrity hash, and set `modulesChanged = true`.
  3. Added unit tests in `companion/cmd/ship/ship_test.go` and `companion/pkg/mfdkit/elite/elite_test.go`.

---

### MFD-009 | Color preset name 1-off when scrolling in Settings tab

- **ID**: MFD-009
- **Discovered Date**: 2026-10-15
- **Fixed Date**: 2026-10-15
- **Root Cause**:
  In `src/mfd_manager.cpp`, `handleSettingsAdjustment()` updated `screen.theme` (cycling through theme presets) when the user adjusted "Preset Theme", but did not call `ensureSettingsTab(screen)`. Because `ensureSettingsTab()` only executed during `MfdManager::update()` (which precedes input polling), the view model's settings key-value pair retained the previous theme name during the render pass of that frame. Furthermore, subsequent frames without input skipped rendering because `dirty` remained false, causing the displayed theme name to lag exactly one step behind the active rendered colors.
- **Fix**:
  Added synchronous call to `ensureSettingsTab(screen)` immediately at the conclusion of `handleSettingsAdjustment()` so all updated labels and values are reflected synchronously in the view model before rasterization.

---

### MFD-010 | List row subtext overlaps main text due to insufficient row height and font collision

- **ID**: MFD-010
- **Discovered Date**: 2026-10-15
- **Fixed Date**: 2026-10-15
- **Root Cause**:
  In `src/mfd_renderer.cpp` `renderListTab()`, list items were drawn with fixed row height `rowH = 26` px. The main label was drawn at `y + 1` and the sublabel was drawn at `y + 12`. Because `MfdFont::kCharHeight` is 16 pixels, the label spanned `y + 1` to `y + 16` and the sublabel spanned `y + 12` to `y + 27`. This caused an unavoidable 5-pixel vertical collision where sublabel glyphs overwrote the bottom pixels of the main label text.
- **Fix**:
  In `renderListTab()`, dynamically expand row height to 36 px whenever a tab contains sublabels (`hasAnySublabel`). Draw the main label on Line 1 (`y + 2`) and the sublabel on Line 2 (`y + 19`), completely eliminating vertical overlap and providing crisp 8x16 font rendering for both lines with a 1px separation gap. Right-hand values and badges are aligned on Line 1. Single-line tabs retain compact 24-pixel rows.

---

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
