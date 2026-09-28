# Cockpit MFD Plugin & Display Authoring Guide

## Status
- **Target Audience**: Third-party tool developers, modders, and pilots creating custom in-cockpit MFD displays for Elite Dangerous VR.
- **Engine**: EDVR Cockpit MFD Addon (`plugins/edvr_mfd/plugin.dll`).
- **Data Model**: Declarative JSON Display Schema (`displays/*.json`), hot-reloaded at runtime with bidirectional interaction logging (`events.jsonl`).

---

## 1. Quick Start: Your First Custom MFD in 60 Seconds

You do not need to compile C++ code or build DLLs to create rich, interactive in-cockpit VR displays. You simply write a JSON file.

1. Navigate to your Elite Dangerous Odyssey installation directory:
   ```text
   <Elite Dangerous Odyssey Game Folder>\plugins\edvr_mfd\displays\
   ```
2. Create a new file called `my_tracker.json`.
3. Paste the following JSON content and save:

```json
{
  "title": "MISSION TRACKER",
  "subtitle": "ACTIVE OPERATIONS",
  "statusBadge": "ONLINE",
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
          "badge": "URGENT"
        },
        {
          "id": "target_2",
          "label": "Crimson Gang Enforcer",
          "value": "850,000 CR",
          "sublabel": "System: Sol (Nav Beacon)",
          "badge": "ACTIVE"
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

4. Put on your VR headset in Elite Dangerous. The MFD will immediately render in your cockpit. Look at it to focus, use **Q/E** to switch tabs, **Up/Down** to navigate rows, and press **Spacebar** or your HOTAS trigger to copy the target name to your clipboard.

---

## 2. Directory Layout Standard

All files used by the MFD subsystem reside inside the dedicated `plugins/edvr_mfd/` directory:

```text
<Elite Dangerous Odyssey Game Folder>\
    d3d11.dll                       <-- EDVR DirectX 11 Graphics Proxy
    Openvr\win64\openvr_api.dll      <-- EDVR Native OpenXR VR Runtime
    plugins\
        edvr_mfd\
            plugin.dll              <-- Cockpit MFD Plugin Engine
            settings.ini            <-- Position, rotation, scale, theme overrides
            events.jsonl            <-- Interaction event log emitted by pilot inputs
            displays\               <-- Custom declarative MFD JSON definitions
                spansh_router.json  <-- Center console display
                engineering.json    <-- Left console display
                exobiology.json     <-- Right console display
```

---

## 3. JSON Structure & Schema Reference

An MFD JSON file defines the entire visual structure and interaction model of a display screen.

### Root Object Properties

| Property | Type | Required | Description | Example |
|---|---|---|---|---|
| `title` | `string` | Yes | Top banner header title displayed on the MFD bezel | `"SPANSH NEUTRON ROUTER"` |
| `subtitle` | `string` | No | Secondary contextual subtitle below title | `"WAYPOINT 2 OF 14"` |
| `statusBadge` | `string` | No | Status badge tag displayed in the upper-right corner | `"ONLINE"`, `"NOMINAL"`, `"SCANNING"`, `"WARNING"` |
| `footerHint` | `string` | No | Navigation and control hint shown in bottom footer bar | `"[Q/E] TABS   [UP/DN] SELECT   [SPACE] COPY"` |
| `tabs` | `array` | Yes | Array of one or more tab objects | `[ { ... }, { ... } ]` |

---

### Tab Object Properties

Each entry in the `tabs` array represents a distinct screen tab accessible via tab cycling inputs (**Q/E** or HOTAS Hat Left/Right).

| Property | Type | Required | Description |
|---|---|---|---|
| `title` | `string` | Yes | Tab title displayed in the tab bar header (e.g. `"ROUTE"`, `"POWER"`, `"LOG"`) |
| `type` | `string` | Yes | Tab layout type: `"list"`, `"keyvalue"`, or `"text"` |
| `items` | `array` | Yes | Array of row items (schema depends on `type` below) |

---

### Tab Types & Item Schemas

#### 1. List Tab (`"type": "list"`)
Presents an interactive, scrollable list of rows with selection highlighting and action callbacks.

Each object in `items` supports:

| Field | Type | Description | Example |
|---|---|---|---|
| `id` | `string` | Unique identifier passed to action handlers / event stream | `"wp_01"`, `"comm_palladium"` |
| `label` | `string` | Primary row text / label (left aligned) | `"Jackson's Lighthouse"` |
| `value` | `string` | Metric / numeric value (right aligned) | `"12.4 LY"`, `"42,100 CR"` |
| `sublabel` | `string` | Secondary descriptive text placed under the label | `"Neutron Star - Supercharge Ready"` |
| `badge` | `string` | Optional highlighted badge tag on the right | `"CURRENT"`, `"NEXT"`, `"REFUEL"`, `"NEW"` |

**Example List Tab:**
```json
{
  "title": "MARKET",
  "type": "list",
  "items": [
    {
      "id": "item_gold",
      "label": "Gold",
      "value": "48,250 CR",
      "sublabel": "Supply: 14,200 T (High)",
      "badge": "BUY"
    },
    {
      "id": "item_beryllium",
      "label": "Beryllium",
      "value": "8,120 CR",
      "sublabel": "Supply: 2,400 T (Med)",
      "badge": "BUY"
    }
  ]
}
```

---

#### 2. Key-Value Tab (`"type": "keyvalue"`)
Presents a two-column telemetry / metrics summary grid.

Each object in `items` supports:

| Field | Type | Description | Example |
|---|---|---|---|
| `key` | `string` | Metric name / label (left column) | `"SYS Distributor"`, `"Gravity"` |
| `value` | `string` | Metric value / reading (right column) | `"4.0 PIP"`, `"0.42 G"` |

**Example Key-Value Tab:**
```json
{
  "title": "TELEMETRY",
  "type": "keyvalue",
  "items": [
    { "key": "Current System", "value": "Sol" },
    { "key": "Target System", "value": "Colonia" },
    { "key": "Fuel Reserve", "value": "98% (31.4 T)" },
    { "key": "Cargo Capacity", "value": "64 / 128 T" },
    { "key": "Shield Integrity", "value": "100%" }
  ]
}
```

---

#### 3. Text Tab (`"type": "text"`)
Presents a scrollable multi-line text terminal for mission briefings, lore, captain's logs, or raw event streams.

The `items` array contains plain strings (one per line):

**Example Text Tab:**
```json
{
  "title": "LOG",
  "type": "text",
  "items": [
    "[22:14:05] Hyperspace jump initiated to Synuefe GT-X b42-3",
    "[22:14:22] FSD cool down complete",
    "[22:14:30] Fuel scoop engaged: +1.2 T/s",
    "[22:15:10] High-value bio signal detected on Body A 1"
  ]
}
```

---

## 4. In-Cockpit Interaction & Control Flow

The MFD system is built for hands-on VR flight without needing to touch a mouse or 2D window.

```
                  ┌──────────────────────┐
                  │    PILOT VR HEAD     │
                  │ (Gaze Cone Raycast)  │
                  └──────────┬───────────┘
                             │ Look at display (0.15s dwell)
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                       FOCUSED MFD SLOT                      │
│  - Swallows game inputs to prevent accidental ship firing   │
│  - Unfades display if auto-hide is active                   │
│  - Routes HOTAS / Keyboard navigation commands              │
└────────────────────────────┬────────────────────────────────┘
                             │
            ┌────────────────┴────────────────┐
            ▼                                 ▼
   [Q / E / Hat Left-Right]        [Space / Enter / Trigger]
         Cycle Tabs                     Activate Item
                                              │
                         ┌────────────────────┴────────────────────┐
                         ▼                                         ▼
                 Copy to Clipboard                      Write to `events.jsonl`
             (e.g. System Name for paste)            (for external apps/scripts)
```

### Input Actions Table

| Action | Default Keyboard | Default HOTAS / Controller | Function |
|---|---|---|---|
| **Focus** | Look at MFD (Center / Left / Right) | Look at MFD | Activates input routing for the hovered display |
| **Tab Next** | `E` | Hat Right / Secondary Bumper | Switches to the next tab |
| **Tab Prev** | `Q` | Hat Left / Primary Bumper | Switches to the previous tab |
| **Select Down** | `Down Arrow` | Hat Down | Scrolls selection down |
| **Select Up** | `Up Arrow` | Hat Up | Scrolls selection up |
| **Activate** | `Spacebar` / `Enter` | Primary Trigger / Joy Button 1 | Executes action (copies value to clipboard and emits event) |
| **Adjust Left** | `Left Arrow` | Hat Left (on Settings) | Decrements setting value on Settings tab |
| **Adjust Right** | `Right Arrow` | Hat Right (on Settings) | Increments setting value on Settings tab |

---

## 5. Bidirectional Integration via `events.jsonl`

When a pilot interacts with an MFD list item (e.g. pressing Spacebar / trigger on a commodity or waypoint), the MFD engine automatically:
1. Copies the item's `label` (or primary target text) to the Windows clipboard for instant pasting into the Galaxy Map or System Map.
2. Appends a structured JSON event record to `<game_dir>\plugins\edvr_mfd\events.jsonl`.

### Event Schema
```json
{
  "timestamp": "2026-09-28T22:30:15Z",
  "slot": "main_mfd",
  "display": "market_browser",
  "tab": "COMMODITIES",
  "action": "activate",
  "item_id": "item_palladium",
  "label": "Palladium",
  "value": "42,500 CR"
}
```

### External App Workflow (Python / Go / C# / VoiceAttack)
External apps can tail `events.jsonl` to provide dynamic, two-way interactive experiences:
- **Market Drill-down**: When the pilot selects `"Palladium"`, your Python script catches the event, fetches the top 5 nearest stations buying Palladium from Inara/EDDN, and overwrites `displays/market_stations.json`. The MFD instantly displays the stations in VR.
- **Route Sequencing**: When the pilot reaches a waypoint and presses Select, VoiceAttack or your router script advances the route and updates `spansh_router.json`.

**Python Event Listener Example:**
```python
import json
import time

EVENT_LOG = r"D:\SteamLibrary\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64\plugins\edvr_mfd\events.jsonl"

def watch_events():
    with open(EVENT_LOG, "r", encoding="utf-8") as f:
        f.seek(0, 2) # Seek to end
        while True:
            line = f.readline()
            if line:
                event = json.loads(line.strip())
                print(f"[MFD Interaction] Pilot clicked {event.get('label')} (ID: {event.get('item_id')})")
                handle_mfd_event(event)
            else:
                time.sleep(0.05)

def handle_mfd_event(event):
    if event.get("item_id") == "item_palladium":
        # Overwrite stations JSON with nearest buyers
        pass

if __name__ == "__main__":
    watch_events()
```

---

## 6. Cockpit Spatial Positioning & INI Overrides

Display geometry, positioning, auto-hide dwell times, and color themes are saved per-slot in `settings.ini` inside `plugins/edvr_mfd/` (and backed up to `%LOCALAPPDATA%\EDVR\plugins\edvr_mfd\settings.ini`).

### Default Slot Layouts
- `slot.main_mfd`: Center Console (Position: `X=0.00, Y=-0.16, Z=-0.55`, Pitch: `-20°`)
- `slot.left_mfd`: Left Console (Position: `X=-0.45, Y=-0.22, Z=-0.48`, Pitch: `-22°`, Yaw: `+32°`)
- `slot.right_mfd`: Right Console (Position: `X=+0.45, Y=-0.22, Z=-0.48`, Pitch: `-22°`, Yaw: `-32°`)

### `settings.ini` Configuration Keys

```ini
[slot.main_mfd]
pos_x = 0.0000          ; Lateral position in meters (-left / +right)
pos_y = -0.1600         ; Vertical position in meters (-down / +up)
pos_z = -0.5500         ; Depth position in meters (-forward / +backward)
pitch = -20.00          ; Tilt angle in degrees (-tilt up / +tilt down)
yaw = 0.00              ; Swivel angle in degrees (-pan left / +pan right)
scale = 1.00            ; Size scale factor (0.50 to 2.00)
opacity = 0.75          ; Background bezel & panel opacity (0.00 to 1.00)
locked = 1              ; 1 = Anchored to cockpit 3D space, 0 = Head-locked HUD
auto_hide = 0           ; 1 = Invisible until gazed upon, 0 = Always visible
dwell_delay = 0.15      ; Time in seconds looking at display before focus activates
release_delay = 0.35    ; Time in seconds looking away before focus deactivates
cone_margin = 1.20      ; Hit cone angular tolerance multiplier
theme = 0               ; Built-in palette: 0=Amber, 1=Cyan, 2=Green, 3=White, 4=Red, 5=Blue
use_custom_color = 0    ; 1 = Use custom RGB vector line color below
custom_r = 255
custom_g = 110
custom_b = 0
custom_a = 255
```

---

## 7. F8 In-Headset Menu Integration

The Cockpit MFD Addon registers its controls directly into the **EDVR F8 In-Headset Menu** under the `"Plugins"` tab:

- **Section Header**: `"Cockpit MFD"` (alphabetically organized among installed plugins)
- **Controls**:
  - `Show all windows` (`On` / `Off`): Forces all configured cockpit displays to be visible simultaneously.
  - `Cockpit MFD Displays` (`On` / `Off`): Master enable/disable toggle.
  - `Active displays` (`1 (Center)`, `2 (Center + Left)`, `3 (All Three)`): Sets the number of active in-cockpit screens.
  - `Head-locked HUD` (`On` / `Off`): Toggles between 3D cockpit-anchored displays and head-tracked HUD mode.

---

## 8. Best Practices for External Developers

1. **Atomic File Writes**: Always write updated JSON to a temporary file (e.g. `spansh_router.json.tmp`) and atomically rename / replace `spansh_router.json`. This prevents the MFD engine from reading a partially written JSON file during active frame rendering.
2. **Update Rates**: Update display files only when data changes or at sensible intervals (1 Hz to 4 Hz). Avoid writing files at 60+ Hz to prevent disk I/O churn.
3. **Elite Status Integration**: Use `%USERPROFILE%\Saved Games\Frontier Developments\Elite Dangerous\Status.json` and Journal logs (`Journal.*.log`) to drive live data (current system, fuel level, pips, cargo, exobiology scan status).
4. **Clean IDs**: Use clear, semantic `id` strings for list items (`"system_sol"`, `"nav_route_step_3"`) so external event listeners can dispatch actions unambiguously.
