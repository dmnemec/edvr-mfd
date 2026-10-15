# Root Cause Analysis: MFD-008 — Display Duplication (6 MFD Screens Instead of 3 Consolidated Consoles)

**Issue ID**: MFD-008  
**Component**: Core Architecture / Package Manifests & Companion Display Publishers  
**Severity**: High (VR Cockpit UX / Immersion)  
**Status**: Resolved  
**Date**: 2026-10-15  
**Author**: EDVR MFD Systems Engineering Team  

---

## 1. Problem Statement & Test Flight Observation

### 1.1 Test Flight Report
During flight testing in VR cockpit seated space, pilots reported unexpected panel proliferation:
> *"I see 6 MFDs: Cargo, Route, Power, Modules, System, and Bio. Seems some are being split to extra screens."*

Instead of finding three structured cockpit multi-function display consoles (Left Console: Ship Systems, Center Console: Navigation Route, Right Console: Exploration Survey), the cockpit was cluttered with six separate floating 3D quads. Panels overlapped or were scattered across auto-layout fallback positions, degrading cockpit visibility, gaze tracking precision, and immersion.

### 1.2 Quantitative Audit
Inspection of the active package manifests revealed:
1. `packages/ship/manifest.json`: Declared **3 separate screens**:
   - `modules` (`screens/modules.json`)
   - `power` (`screens/power.json`)
   - `cargo` (`screens/cargo.json`)
2. `packages/exploration/manifest.json`: Declared **2 separate screens**:
   - `system` (`screens/system.json`)
   - `bio` (`screens/bio.json`)
3. `packages/route/manifest.json`: Declared **1 screen**:
   - `route` (`screens/route.json`)

Total declared screens across default packages:
$$\text{Total 3D Quads} = 3 (\text{ship}) + 2 (\text{exploration}) + 1 (\text{route}) = 6 \text{ quads}$$

---

## 2. Root Cause Analysis

### 2.1 3D Quad Allocation Architecture in `mfd_manager.cpp`
The EDVR MFD core plugin (`src/mfd_manager.cpp` and `src/mfd_package.cpp`) treats each screen declared in a package's `manifest.json` as an independent 3D display quad (`MfdScreen`):
- For every screen entry in `manifest.json["screens"]`:
  - An independent Direct3D 11 texture buffer ($512 \times 341$ px) and eye-pass render target are allocated.
  - An independent `DeclarativeMfdProvider` is instantiated with its own file watcher on `screen.file`.
  - An independent 3D world pose (`Vec3` position, `Quat` orientation, width/height) is assigned, either via explicit manifest `pose` coordinates or through candidate auto-layout slots (§9.4).
  - An independent head-gaze collision bounding box is tracked for focus and opacity hysteresis.

### 2.2 Conceptual Misalignment: Manifest Screen vs. Display Tabs
The MFD display specification (Schema v1, `docs/schema/display.v1.schema.json`) was designed to support up to 16 tabs within a single screen (`Display.Tabs`). Navigation controls (HAT Left / Right, Arrow Left / Right) cycle between tabs on the focused screen, while UP / DOWN controls scroll within the active tab.

However, during initial companion prototyping:
- Subsystems were implemented in isolated modules and emitted separate screen files (`modules.json`, `power.json`, `cargo.json`, `system.json`, `bio.json`).
- Manifests were authored declaring each file as a separate `screen`, rather than consolidating related domain telemetry into tabs of a unified console screen.
- As a direct result, `mfd_manager.cpp` allocated 6 separate 3D quads floating in the cockpit instead of 3 consoles.

---

## 3. Architectural Intention: The Three Cockpit Consoles

Elite Dangerous cockpits feature three primary instrument locations around the pilot's seated tracking position:

```
                  =============================================
                  |              CENTER CONSOLE               |
                  |         ROUTE & NEUTRON HIGHWAY           |
                  |           Pose: [0.0, -0.22, 0.55]        |
                  |          Pitch: -22°, Yaw: 0°             |
                  |          Tabs: [NAVROUTE, SPANSH]         |
                  =============================================
                                       ^
                                       |
    =============================      |      =============================
    |        LEFT CONSOLE       |      |      |       RIGHT CONSOLE       |
    |        SHIP READOUT       |      |      |    EXPLORATION SURVEY     |
    |   Pose: [-0.28,-0.22,0.55]|      |      |   Pose: [0.28,-0.22,0.55] |
    |   Pitch: -22°, Yaw: +32°  |      |      |   Pitch: -22°, Yaw: -32°  |
    |   Tabs: [INTEGRITY,       |      |      |   Tabs: [SYSTEM, BIO]     |
    |          POWER, CARGO]    |      |      |                           |
    =============================      |      =============================
                                       |
                                  [ PILOT ]
                               Seated Origin
```

### 3.1 Left Console: Ship Readout (`packages/ship/`)
- **Screen ID**: `ship`
- **Screen File**: `screens/ship.json`
- **Console Pose**: $x = -0.28\,\text{m}$, $y = -0.22\,\text{m}$, $z = 0.55\,\text{m}$, $\text{yaw} = +32.0^\circ$, $\text{pitch} = -22.0^\circ$.
- **Tabs**:
  1. `integrity` (Title: `"INTEGRITY"`, Type: `"list"`): Module health percentages, damaged modules listed first, malfunction icons, and hull status.
  2. `power` (Title: `"POWER"`, Type: `"bars"`): SYS/ENG/WEP pips, total power draw vs plant capacity, main fuel and reservoir levels.
  3. `cargo` (Title: `"CARGO"`, Type: `"list"`): Manifest inventory, stolen goods indicators, commodity quantities, and Galactic Mean market values.

### 3.2 Center Console: Route & Waypoints (`packages/route/`)
- **Screen ID**: `route`
- **Screen File**: `screens/route.json`
- **Console Pose**: $x = 0.00\,\text{m}$, $y = -0.22\,\text{m}$, $z = 0.55\,\text{m}$, $\text{yaw} = 0.0^\circ$, $\text{pitch} = -22.0^\circ$.
- **Tabs**:
  1. `navroute` (Title: `"NAVROUTE"`, Type: `"text"` or `"list"`): Plotted in-game route waypoints, jump counts, remaining fuel requirements.
  2. `spansh` (Title: `"SPANSH"`, Type: `"text"` or `"list"`): Spansh neutron highway CSV route planner waypoints, neutron boost indicators, refuel warnings.

### 3.3 Right Console: Exploration Survey (`packages/exploration/`)
- **Screen ID**: `exploration`
- **Screen File**: `screens/exploration.json`
- **Console Pose**: $x = +0.28\,\text{m}$, $y = -0.22\,\text{m}$, $z = 0.55\,\text{m}$, $\text{yaw} = -32.0^\circ$, $\text{pitch} = -22.0^\circ$.
- **Tabs**:
  1. `system` (Title: `"SYSTEM"`, Type: `"list"`): Scanned celestial bodies sorted by value, terraformable/landable tags, S/M/B/G survey check boxes, system value estimate.
  2. `bio` (Title: `"BIO"`, Type: `"list"`): Exobiology candidate bodies, detected genus signals, sampling progress (Log / Sample / Analyse), colony diversity distance tracker.

---

## 4. Solution Design & Migration Path

### 4.1 Schema and Package Manifest Consolidation
1. **Ship Package**:
   - Author `packages/ship/screens/ship.json` containing tabs `integrity`, `power`, and `cargo`.
   - Update `packages/ship/manifest.json` to declare exactly 1 screen (`ship`), pointing to `screens/ship.json`, with the left console pose.
   - Delete obsolete `modules.json`, `power.json`, and `cargo.json`.
2. **Exploration Package**:
   - Author `packages/exploration/screens/exploration.json` containing tabs `system` and `bio`.
   - Update `packages/exploration/manifest.json` to declare exactly 1 screen (`exploration`), pointing to `screens/exploration.json`, with the right console pose.
   - Delete obsolete `system.json` and `bio.json`.
3. **Route Package**:
   - Explicitly declare the center console pose ($x = 0.0, y = -0.22, z = 0.55, \text{yaw} = 0^\circ, \text{pitch} = -22^\circ$) in `packages/route/manifest.json`.

### 4.2 Companion Engine Updates
1. **`edvr-mfd-ship` (`companion/cmd/ship/`)**:
   - Unify telemetry queues to emit a single consolidated `screens/ship.json` file.
   - Maintain in-memory models for modules, power/pips, and cargo inventory.
   - Whenever any telemetry event occurs (Journal event, `Status.json`, `Cargo.json`, `Market.json`, or `ModuleInfo.json`), recompute `BuildShipDisplay()` and enqueue atomic write via `writer.CoalescedWriter`.
2. **`edvr-mfd-exploration` (`companion/cmd/exploration/`)**:
   - Unify cartography and exobiology state into a single consolidated `screens/exploration.json` file.
   - Whenever system scan or exobiology sampling state updates, recompute `BuildExplorationDisplay()` and enqueue atomic write via `writer.CoalescedWriter`.

### 4.3 Installer & Packaging Updates
- Update release packaging (`tools/package_release.py`) and installer routines (`companion/cmd/installer/main.go`, `tools/install_mfd.py`) to validate the new consolidated screens and remove stale screen files upon upgrade.

---

## 5. Verification Plan

1. **Go Unit & Subsystem Tests**:
   - Run companion test suite across all packages (`go test -v ./...`).
   - Validate consolidated display models against Schema v1 with zero validation errors.
2. **C++ Framework Tests**:
   - Verify `mfd_test.exe` validates manifests and layout without regressions.
3. **Release Packaging**:
   - Run `python tools/package_release.py` to confirm clean validation and packaging.
4. **Installer Deployment**:
   - Execute `build\tools\install_mfd.exe --batch --force` and verify clean deployment to game folder.
