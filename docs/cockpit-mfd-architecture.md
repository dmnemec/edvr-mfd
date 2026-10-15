# EDVR Cockpit MFD — Architecture & Design Specification (v1.0)

## Status
- **Architecture State**: Decoupled, package-driven in-cockpit VR rendering engine, companion supervisor, and input router.
- **Directory Standard**: `plugins/edvr_mfd/mfds/<pkg_id>/` with full package isolation.
- **Data Model**: Manifest Schema v1 (`manifest.json`) and Declarative Display Schema v1 (`screens/<screen_id>.json`) with live hot-reloading, companion supervision, and bidirectional event stream (`events.jsonl`).

---

## 1. System Overview & Core Philosophy

The EDVR Cockpit MFD addon transforms Elite Dangerous VR by providing hardware-accelerated, vector-drawn, interactive Multi-Function Displays positioned in seated 3D cockpit space.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        SUPERVISED COMPANIONS                           │
│   (edvr-mfd-ship, edvr-mfd-route, edvr-mfd-exploration, Python, etc.)  │
└──────────────┬────────────────────────────────────────▲────────────────┘
  writes JSON  │                                        │ reads events
  screen data  │                                        │ (interactions)
               ▼                                        │
┌───────────────────────────────────────┬───────────────┴────────────────┐
│  plugins/edvr_mfd/mfds/<pkg_id>/      │ plugins/edvr_mfd/mfds/<pkg_id>/│
│  screens/<screen_id>.json             │ events.jsonl                   │
└──────────────┬────────────────────────┴────────────────────────────────┘
               │ hot-reloads on file write
               ▼
┌────────────────────────────────────────────────────────────────────────┐
│                  EDVR MFD RENDERING ENGINE & RUNTIME                   │
│   - 3D Cockpit Projective Quad Layer Compositor (OpenXR / D3D11)       │
│   - Head/Eye Gaze Cone Tracking with Auto-Hide & Smooth Fade           │
│   - Game Context Evaluator (Status.json & GuiFocus filtering)          │
│   - Companion Lifecycle Supervisor (Spawn, Backoff Restart, Logging)   │
│   - DirectInput / HOTAS Input Gate & Tab Router                        │
│   - Vector Wireframe Rasterizer & High-Legibility Font Engine          │
│   - F8 In-Headset Settings (Master, Hide All, Slot Count, Per-Package) │
└────────────────────────────────────────────────────────────────────────┘
```

### Core Tenets:
1. **Decoupled Data Architecture**: The MFD plugin is purely a high-performance **in-cockpit rendering engine, companion supervisor, and input router**. It never touches external game APIs or network services directly; instead, background companions write declarative JSON screens and react to JSONL events.
2. **Package Platform Isolation**: Every MFD is an isolated package located in `plugins/edvr_mfd/mfds/<pkg_id>/`. Each package contains its author-owned `manifest.json`, screen views, companion binaries, and runtime logs.
3. **Hot-Reloading in VR**: Modifying a `.json` screen file on disk reloads asynchronously via directory change notifications without blocking the render loop or dropping VR frames.
4. **Supervised Companion Daemons**: The plugin manages the execution lifecycle of declared companion executables (`bin/<exe>`), redirecting stdout/stderr to `companion.log`, supervising exit codes, and restarting failing companions with exponential backoff.
5. **Bidirectional Interaction**: Pilot interactions (HOTAS buttons, keyboard keys, Hat switches) trigger tab cycling, list scrolling, clipboard copying, and append structured event objects to `events.jsonl`.
6. **Robust Context Gating**: Real-time evaluation of `Status.json` flags and `GuiFocus` modal indicators. MFDs automatically hide during Galaxy Map, System Map, Orrery, FSS, SAA, and Codex navigation.

---

## 2. Directory Layout Standard

All MFD components reside within `<game_dir>\plugins\edvr_mfd\`:

```text
<Elite Dangerous Odyssey Game Folder>\
    d3d11.dll                       <-- EDVR DirectX 11 Graphics Proxy
    openvr\win64\openvr_api.dll     <-- EDVR Native OpenXR VR Runtime
    plugins\
        edvr_mfd\
            plugin.dll              <-- MFD plugin binary
            edvr_mfd.ini            <-- User coordinates & settings persistence
            mfds\                   <-- Installed MFD packages
                ship\
                    manifest.json   <-- Package metadata & declarations
                    screens\        <-- Screen definitions
                        modules.json
                        power.json
                        cargo.json
                    bin\
                        edvr-mfd-ship.exe  <-- Supervised companion binary
                    events.jsonl    <-- Action stream emitted by pilot inputs
                    companion.log   <-- Supervised companion process log
                route\
                    manifest.json
                    screens\
                        route.json
                    bin\
                        edvr-mfd-route.exe
                    events.jsonl
                    companion.log
                exploration\
                    manifest.json
                    screens\
                        system.json
                        bio.json
                    bin\
                        edvr-mfd-exploration.exe
                    events.jsonl
                    companion.log
```

---

## 3. Package Manifest Schema (v1)

Each package root contains `manifest.json` describing package identity, screen definitions, and companion specifications:

```json
{
  "manifestVersion": 1,
  "id": "ship",
  "name": "Ship Readout",
  "version": "1.0.0",
  "author": "EDVR MFD",
  "description": "Module integrity, power and cargo.",
  "minPluginVersion": "1.0.0",
  "screens": [
    {
      "id": "modules",
      "file": "screens/modules.json",
      "title": "MODULES",
      "pose": {
        "position": [-0.45, -0.22, -0.48],
        "pitchDeg": -22.0,
        "yawDeg": 32.0,
        "widthM": 0.24,
        "heightM": 0.16
      },
      "contexts": ["ship_cockpit", "fighter"],
      "excludeContexts": [],
      "autoHideUntilGaze": false
    }
  ],
  "companions": [
    {
      "id": "ship-feed",
      "exe": "bin/edvr-mfd-ship.exe",
      "args": [],
      "env": {},
      "restart": "on-failure"
    }
  ]
}
```

### Manifest Specifications:
- `manifestVersion`: Required integer (must equal `1`).
- `id`: Unique package identifier matching regex `^[a-z0-9][a-z0-9_-]{1,31}$`. Must match the package folder name.
- `screens`: List of screen declarations.
  - `id`: Unique screen ID within the package.
  - `file`: Path relative to package root (e.g. `screens/modules.json`). Relative paths that escape the package root (`..`) are rejected.
  - `pose`: Optional 3D spatial positioning override (`position` [x, y, z] in meters, `pitchDeg`, `yawDeg`, `widthM`, `heightM`). If omitted, automatic slot layout is assigned.
  - `contexts`: Context list when screen should be visible.
  - `excludeContexts`: Context list when screen should be suppressed.
  - `autoHideUntilGaze`: Boolean indicating whether screen stays hidden until gazed upon.
- `companions`: List of companion executables to supervise.
  - `id`: Unique companion identifier.
  - `exe`: Executable path relative to package root or absolute path. Paths traversing `..` outside package are rejected.
  - `restart`: Restart policy: `"on-failure"` (restart if exit code != 0), `"always"` (restart on any exit), or `"never"`.

---

## 4. Declarative Screen Display Schema (v1)

Screen files (`screens/<screen_id>.json`) define the visual content, tabs, and interactive elements of each display:

```json
{
  "title": "SHIP MODULES",
  "subtitle": "INTEGRITY & POWER DRAW",
  "statusBadge": "NOMINAL",
  "statusBadgeColor": "green",
  "footerHint": "[Q/E] TABS   [UP/DN] SELECT   [SPACE] ACTIVATE",
  "theme": "cyan_ice",
  "tabs": [
    {
      "title": "MODULES",
      "type": "list",
      "items": [
        {
          "id": "mod_fsd",
          "label": "Frame Shift Drive 5A",
          "value": "100%",
          "sublabel": "Draw: 0.60 MW  (P1)",
          "badge": "ONLINE",
          "badgeColor": "green"
        }
      ]
    },
    {
      "title": "SPECS",
      "type": "keyvalue",
      "items": [
        { "key": "Power Plant", "value": "Overcharged 5A (34.5 MW)" },
        { "key": "Retracted Draw", "value": "22.4 MW (65%)" },
        { "key": "Deployed Draw", "value": "28.9 MW (84%)" }
      ]
    },
    {
      "title": "LOG",
      "type": "text",
      "items": [
        "[22:01:10] Power distributor initialized",
        "[22:04:33] Hardpoints deployed"
      ]
    }
  ]
}
```

### Supported Tab Types:
1. **List Tab (`"type": "list"`)**:
   Interactive, scrollable list of rows with selection cursor, item IDs, values, sublabels, and color-coded status badges.
2. **Key-Value Tab (`"type": "keyvalue"`)**:
   Two-column telemetry readouts formatting metrics, values, and optional value highlights.
3. **Text Tab (`"type": "text"`)**:
   Multi-line text terminal displaying scrollable logs, status reports, or mission notes.

---

## 5. Companion Supervision Architecture

The plugin runtime includes a dedicated `CompanionSupervisor` subsystem:

1. **Lifecycle Management**:
   - Discovered companion processes are launched during plugin initialization or when entering valid game contexts.
   - Standard output and standard error streams are redirected to `<pkg_dir>/companion.log`.
2. **Crash & Restart Handling**:
   - Process exits are polled non-blockingly via Win32 process handles (`GetExitCodeProcess`).
   - If a companion configured with `"on-failure"` exits with non-zero status, or `"always"` terminates, it is restarted using exponential backoff (1s, 2s, 4s, up to 30s) to prevent CPU thrashing.
3. **Clean Teardown**:
   - On game exit, plugin shutdown, or DLL unload, all active companion processes receive graceful termination signals followed by Win32 `TerminateProcess` if unhandled.

---

## 6. Contextual Activity & Modal Gating

The `ContextEvaluator` subsystem samples `%USERPROFILE%\Saved Games\Frontier Developments\Elite Dangerous\Status.json` off the render thread:

### GuiFocus Modal Suppression
Whenever the pilot accesses modal game interfaces, MFD rendering is automatically suppressed:
- `GuiFocus == 6`: Galaxy Map
- `GuiFocus == 7`: System Map
- `GuiFocus == 8`: Orrery
- `GuiFocus == 9`: FSS (Full Spectrum System Scanner)
- `GuiFocus == 10`: SAA (Surface Analysis Area)
- `GuiFocus == 11`: Codex

### Vehicle Context Identifiers
Screens declare applicable environments in their `contexts` list:
- `ship_cockpit`: Piloting a primary starship.
- `srv`: Driving a Surface Recon Vehicle.
- `fighter`: Piloting a Ship-Launched Fighter.
- `on_foot`: On-foot Odyssey exploration.
- `taxi`: Apex Interstellar or shuttle transport.
- `multicrew`: Acting as multicrew crew member.
- `docked`: Docked at station or outpost.
- `landed`: Landed on planetary surface.
- `supercruise`: Traveling in supercruise.
- `normal_space`: In normal space flight.
- `hardpoints`: Combat hardpoints deployed.
- `combat_mode`: Combat HUD active (orange reticle).
- `analysis_mode`: Analysis HUD active (blue scanner reticle).

---

## 7. F8 In-Headset Menu & Settings

The MFD addon integrates directly into the EDVR F8 in-headset menu under **Cockpit MFD**:

- **Hide All (`mfd_hide_all_toggle`)**: Master instant visibility toggle. When enabled, immediately clears all MFD panels across all cockpits (ideal for pause menus, external camera, or cinematic immersion).
- **Cockpit MFD Displays (`On` / `Off`)**: Master plugin toggle enabling or disabling MFD rendering entirely.
- **Show all windows (`On` / `Off`)**: Bypasses gaze cone fading, ensuring all active displays remain 100% opaque.
- **Active displays (`1 (Center)`, `2 (Center + Left)`, `3 (All Three)`)**: Slot count selector.
- **Head-locked HUD (`On` / `Off`)**: Toggles between cockpit 3D projective quad anchors and head-relative HUD tracking.
- **Per-Package Toggles**: Allows enabling or disabling individual packages independently at runtime.
