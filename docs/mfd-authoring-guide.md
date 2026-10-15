# Cockpit MFD Package & Display Authoring Guide (v1.0)

## Status
- **Target Audience**: Third-party tool developers, companion authors, and pilots creating custom in-cockpit MFD packages for Elite Dangerous VR.
- **Engine**: EDVR Cockpit MFD Addon (`plugins/edvr_mfd/plugin.dll`).
- **Data Model**: Package Platform Layout (`plugins/edvr_mfd/mfds/<pkg_id>/`), Manifest Schema v1 (`manifest.v1.schema.json`), and Declarative Display Schema v1 (`display.v1.schema.json`).

---

## 1. Quick Start: Your First Custom MFD Package in 60 Seconds

Creating a custom MFD requires no C++ compilation. An MFD is structured as an isolated package folder containing a `manifest.json` and one or more screen `.json` definitions.

### Step 1: Create Package Directory
Navigate to your Elite Dangerous installation:
```text
<Elite Dangerous Odyssey Game Folder>\plugins\edvr_mfd\mfds\my_tracker\
```
Create a `screens\` subdirectory:
```text
<Elite Dangerous Odyssey Game Folder>\plugins\edvr_mfd\mfds\my_tracker\screens\
```

### Step 2: Create `manifest.json`
Inside `mfds\my_tracker\manifest.json`, save:
```json
{
  "$schema": "https://raw.githubusercontent.com/dmnemec/edvr-mfd/main/docs/schema/manifest.v1.schema.json",
  "manifestVersion": 1,
  "id": "my_tracker",
  "name": "Mission & Bounty Tracker",
  "version": "1.0.0",
  "author": "Commander",
  "description": "Active bounty targets and operations summary.",
  "minPluginVersion": "1.0.0",
  "screens": [
    {
      "id": "bounties",
      "file": "screens/bounties.json",
      "title": "BOUNTIES",
      "contexts": ["ship_cockpit", "fighter"],
      "excludeContexts": []
    }
  ]
}
```

### Step 3: Create Screen Definition
Inside `mfds\my_tracker\screens\bounties.json`, save:
```json
{
  "$schema": "https://raw.githubusercontent.com/dmnemec/edvr-mfd/main/docs/schema/display.v1.schema.json",
  "title": "MISSION TRACKER",
  "subtitle": "ACTIVE OPERATIONS",
  "statusBadge": "ONLINE",
  "statusBadgeColor": "green",
  "footerHint": "[Q/E] TABS   [UP/DN] SELECT   [SPACE] COPY",
  "tabs": [
    {
      "title": "BOUNTIES",
      "type": "list",
      "items": [
        {
          "id": "target_1",
          "label": "Pirate Lord Vane",
          "value": "2,450,000 CR",
          "sublabel": "System: HR 1257 (High RES)",
          "badge": "URGENT",
          "badgeColor": "red"
        },
        {
          "id": "target_2",
          "label": "Crimson Gang Enforcer",
          "value": "850,000 CR",
          "sublabel": "System: Sol (Nav Beacon)",
          "badge": "ACTIVE",
          "badgeColor": "amber"
        }
      ]
    },
    {
      "title": "SUMMARY",
      "type": "keyvalue",
      "items": [
        { "key": "Target Count", "value": "2 Targets" },
        { "key": "Total Bounty", "value": "3,300,000 CR" },
        { "key": "Time Remaining", "value": "18h 42m" }
      ]
    }
  ]
}
```

### Step 4: Validate and Fly
Validate your package structure using `mfdkit`:
```cmd
mfdkit validate plugins\edvr_mfd\mfds\my_tracker
```
Put on your VR headset in Elite Dangerous. Your display will render immediately in your cockpit. Look at it to focus, use **Q/E** to switch tabs, **Up/Down** to navigate rows, and press **Spacebar** (or HOTAS trigger) to copy target names to your clipboard.

---

## 2. Package Directory Layout Standard

All packages are placed under `<game_dir>\plugins\edvr_mfd\mfds\<package_id>\`:

```text
<Elite Dangerous Odyssey Game Folder>\
    plugins\
        edvr_mfd\
            plugin.dll
            edvr_mfd.ini
            mfds\
                <package_id>\
                    manifest.json               <-- Author-owned static manifest
                    screens\
                        <screen_id>.json        <-- Live screen definition (written by companion)
                    bin\                        <-- Optional companion executables
                        edvr-mfd-companion.exe
                    events.jsonl                <-- Action stream emitted by pilot inputs
                    companion.log               <-- Redirected stdout/stderr of companion
```

### Path & Security Rules:
- Package folder name **must match** `manifest.json` `id` exactly.
- Package IDs must match `^[a-z0-9][a-z0-9_-]{1,31}$`.
- All screen file paths and companion executable paths must stay within the package folder (paths containing `..` escaping root are rejected). Absolute companion paths are permitted for system-wide tools.

---

## 3. Package Manifest Schema (v1) Reference

| Field | Type | Required | Description |
|---|---|---|---|
| `manifestVersion` | `integer` | Yes | Must be `1`. |
| `id` | `string` | Yes | Unique package ID matching directory name. |
| `name` | `string` | Yes | Human-readable package title. |
| `version` | `string` | Yes | Semantic version string (e.g. `"1.0.0"`). Used by installer for no-overwrite protection. |
| `author` | `string` | No | Author or organization name. |
| `description` | `string` | No | Description of package functionality. |
| `minPluginVersion`| `string` | No | Minimum EDVR MFD plugin version required. |
| `screens` | `array` | Yes | Array of screen definitions (see below). |
| `companions` | `array` | No | Array of companion daemons to supervise (see below). |

### Screen Declaration Schema:
```json
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
```

- `pose`: Optional 3D cockpit transform (`position` in meters $[x, y, z]$, `pitchDeg`, `yawDeg`, `widthM`, `heightM`). If omitted, auto-layout places the screen in the next available console slot.
- `contexts`: List of context strings when screen is rendered.
- `excludeContexts`: List of context strings when screen is hidden.
- `autoHideUntilGaze`: If `true`, panel remains invisible until focused by gaze.

### Companion Declaration Schema:
```json
{
  "id": "ship-feed",
  "exe": "bin/edvr-mfd-ship.exe",
  "args": ["--rate", "2s"],
  "env": { "EDVR_LOG_LEVEL": "info" },
  "restart": "on-failure"
}
```

- `restart`: Restart policy:
  - `"on-failure"`: Restart automatically if exit code != 0 with exponential backoff.
  - `"always"`: Continually restart whenever process exits.
  - `"never"`: Run once, do not restart upon exit.

---

## 4. Declarative Display Schema (v1) Reference

Screen display JSON files define visual styling and layout:

```json
{
  "title": "HEADER TITLE",
  "subtitle": "SUBTITLE",
  "statusBadge": "STATUS",
  "statusBadgeColor": "green",
  "footerHint": "[Q/E] TABS   [UP/DN] SELECT   [SPACE] ACTIVATE",
  "theme": "amber",
  "tabs": [ ... ]
}
```

### Header & Styling Fields:
- `title` (`string`): Top banner title.
- `subtitle` (`string`): Optional subtitle below header.
- `statusBadge` (`string`): Upper-right badge label (e.g. `"LIVE"`, `"SCANNING"`).
- `statusBadgeColor` (`string`): Color for badge: `"amber"`, `"cyan"`, `"green"`, `"red"`, `"white"`, `"blue"`.
- `footerHint` (`string`): Control instructions shown in footer.
- `theme` (`string`): Color theme: `"amber"`, `"cyan"`, `"green"`, `"white"`, `"red"`, `"blue"`.

### Tab Object Structure:
Every tab has:
- `title` (`string`): Name on tab bar.
- `type` (`string`): `"list"`, `"keyvalue"`, or `"text"`.
- `items` (`array`): Content rows according to type.

#### 1. List Tab (`"type": "list"`)
Scrollable row items:
```json
{
  "id": "item_id_01",
  "label": "Primary Label Text",
  "value": "Value / Metric",
  "sublabel": "Secondary description text",
  "badge": "TAG",
  "badgeColor": "cyan"
}
```

#### 2. Key-Value Tab (`"type": "keyvalue"`)
Two-column metric summaries:
```json
{
  "key": "Metric Name",
  "value": "Metric Reading",
  "valueColor": "green"
}
```

#### 3. Text Tab (`"type": "text"`)
Array of strings:
```json
[
  "System telemetry active",
  "No hostile contacts detected"
]
```

---

## 5. Bidirectional Interaction via `events.jsonl`

When a pilot interacts with an item (pressing **Spacebar** / HOTAS trigger):
1. The item's `label` (or primary value) is copied to the Windows OS clipboard for instant pasting into the Galaxy Map.
2. An interaction event is written to `<pkg_dir>\events.jsonl`.

### Event Structure:
```json
{
  "timestamp": "2026-10-15T19:30:15Z",
  "slot": "main_mfd",
  "package": "my_tracker",
  "screen": "bounties",
  "tab": "BOUNTIES",
  "action": "activate",
  "item_id": "target_1",
  "label": "Pirate Lord Vane",
  "value": "2,450,000 CR"
}
```

External tools or companion daemons tail `events.jsonl` to advance waypoints, fetch remote data, or update checklist states.

---

## 6. Developer Tooling (`mfdkit`)

EDVR MFD provides `mfdkit` for validating and testing packages without launching the game:

### Package Validation
```cmd
mfdkit validate packages\ship
```
Checks:
- Valid `manifest.json` schema and regex identifiers.
- Screen files exist and pass display schema checks.
- Companion executables exist and have safe paths.

### Session Recording & Replay
Record live telemetry and replay it deterministically:
```cmd
mfdkit record --out ./fixtures/session_jump --duration 60s
mfdkit replay --in ./fixtures/session_jump --out ./test_journals --speed 2x
```

---

## 7. Best Practices for Companion Developers

1. **Atomic File Writes**: Write updated screen content to `<file>.tmp` and atomically rename/replace the target `.json` file. This prevents the MFD engine from reading partial writes.
2. **Throttled Updates**: Write files only when telemetry changes or at 1–4 Hz intervals. Do not hammer the disk at 60 Hz.
3. **Telemetry Sources**: Monitor `%USERPROFILE%\Saved Games\Frontier Developments\Elite Dangerous\Status.json`, `NavRoute.json`, `ModulesInfo.json`, `Cargo.json`, and `Journal.*.log`.
4. **Graceful Teardown**: Listen for standard termination signals (`SIGINT`, `SIGTERM`, `CTRL_CLOSE_EVENT`) to cleanly flush caches and close open file handles.
