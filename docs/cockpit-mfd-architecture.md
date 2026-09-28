# EDVR Cockpit MFD — Architecture & Design Specification

## Status
- **Architecture State**: Decoupled, file-driven in-cockpit VR rendering engine and input router.
- **Directory Standard**: `plugins/edvr_mfd/plugin.dll` with per-plugin asset isolation.
- **Data Model**: JSON Declarative Display Schema (`displays/*.json`) with live hot-reloading and bidirectional event stream (`events.jsonl`).

---

## 1. System Overview & Core Philosophy

The EDVR Cockpit MFD addon transforms Elite Dangerous VR by providing hardware-accelerated, vector-drawn, interactive Multi-Function Displays positioned in seated 3D cockpit space.

```
┌─────────────────────────────────────────────────────────────┐
│                       EXTERNAL APPS                         │
│  (EDDiscovery, EDDI, Spansh Tools, VoiceAttack, Python)     │
└──────────────┬──────────────────────────────▲───────────────┘
  writes JSON  │                              │ reads events
  display data │                              │ (interactions)
               ▼                              │
┌──────────────────────────────┬──────────────┴──────────────┐
│  plugins/edvr_mfd/           │ plugins/edvr_mfd/           │
│  displays/*.json             │ events.jsonl                │
└──────────────┬───────────────┴─────────────────────────────┘
               │ hot-reloads on file write
               ▼
┌─────────────────────────────────────────────────────────────┐
│                 EDVR MFD RENDERING ENGINE                   │
│   - 3D Cockpit Projective Quad Layer Compositor (OpenXR)    │
│   - Head/Eye Gaze Cone Tracking with Auto-Hide & Fade       │
│   - DirectInput / HOTAS Input Gate & Tab Router             │
│   - Vector Wireframe Rasterizer & High-Legibility Font      │
│   - In-Cockpit Interactive SETTINGS Tab & Persistence       │
└─────────────────────────────────────────────────────────────┘
```

### Core Tenets:
1. **Decoupled Data Architecture**: The MFD plugin is purely a high-performance **in-cockpit rendering engine and input router**. It does not fetch remote API data directly; instead, third-party apps, scripts, or local daemons manage data by writing declarative `.json` files.
2. **Language-Agnostic Extensibility**: Anyone can build custom MFDs using Python, C#, Go, Node.js, or simple text editors—zero C++ or DLL compilation required.
3. **Hot-Reloading in VR**: Modifying a `.json` file on a 2D monitor immediately re-renders the display inside the VR headset on the next frame.
4. **Bidirectional Interaction**: Pilot interactions (HOTAS/joystick buttons, Spacebar, Hat switches) trigger tab navigation, checkbox toggles, clipboard copies, and write structured events to `events.jsonl` for external apps to consume.

---

## 2. Directory Layout Standard

To maintain clean separation and eliminate plugin clutter, all plugin binaries, configs, templates, and event streams live within a dedicated plugin subdirectory:

```
<Elite Dangerous Odyssey Game Folder>\
    d3d11.dll
    Openvr\win64\openvr_api.dll
    plugins\
        edvr_mfd\
            plugin.dll                <-- MFD plugin binary
            edvr_mfd.ini              <-- User coordinate overrides & settings persistence
            events.jsonl              <-- Action stream emitted by pilot in-headset interactions
            displays\                 <-- Declarative MFD JSON display files
                spansh_router.json    <-- Center console Spansh neutron route display
                engineering.json      <-- Left console power & module status display
                exobiology.json       <-- Right console survey & exobiology display
                trade_market.json     <-- Custom pilot / tool displays
```

---

## 3. JSON Declarative Display Schema

Every `.json` file in `displays/` defines an independent in-cockpit MFD display. Default positions, rotation angles, scale, gaze timings, and contextual activity gating are declared within the file itself.

### Full Schema Definition:

```json
{
  "id": "trade_market",
  "title": "SYSTEM COMMODITY MARKET",
  "subtitle": "BEST SELL ROUTES (30 LY)",
  "statusBadge": "LIVE",
  "statusBadgeColor": "green",
  "footerHint": "[Q/E] TABS   [UP/DN] SELECT   [SPACE] INTERACT",
  
  "defaultSlot": "left_mfd",
  "defaultPose": {
    "position": [-0.45, -0.22, -0.48],
    "pitch": -22.0,
    "yaw": 32.0,
    "scale": 1.0,
    "opacity": 0.75,
    "trackingLocked": true
  },
  
  "defaultGaze": {
    "autoHide": false,
    "dwellDelay": 0.15,
    "releaseDelay": 0.35,
    "coneMargin": 1.15,
    "anchorOffsetY": 0.0
  },
  
  "activityGating": [
    "ship",
    "in_flight",
    "analysis_mode"
  ],
  
  "theme": "cyan_ice",
  "customColor": {
    "r": 60,
    "g": 200,
    "b": 255,
    "a": 255
  },
  
  "tabs": [
    {
      "title": "PRICES",
      "type": "list",
      "items": [
        {
          "id": "item_tritium",
          "label": "Tritium",
          "value": "52,400 CR",
          "sublabel": "Carrier Fuel - High Demand",
          "badge": "+18%",
          "badgeColor": "green",
          "onInteract": {
            "type": "switch_tab",
            "targetTab": "STATIONS",
            "copyText": "Tritium",
            "emitEvent": true
          }
        },
        {
          "id": "item_gold",
          "label": "Gold",
          "value": "48,200 CR",
          "sublabel": "Metals - Medium Demand",
          "badge": "+5%",
          "badgeColor": "amber",
          "onInteract": {
            "type": "switch_tab",
            "targetTab": "STATIONS",
            "copyText": "Gold",
            "emitEvent": true
          }
        }
      ]
    },
    {
      "title": "STATIONS",
      "type": "keyvalue",
      "items": [
        {"key": "Target Commodity", "value": "Tritium", "valueColor": "cyan"},
        {"key": "Best Buyer", "value": "Ray Gateway (Diaguandri)"},
        {"key": "Buy Price", "value": "52,400 CR", "valueColor": "green"},
        {"key": "Demand", "value": "14,200 T"},
        {"key": "Distance", "value": "12.4 LY (1 Jump)"},
        {"key": "Pad Size", "value": "Large (L)"}
      ]
    },
    {
      "title": "CHECKLIST",
      "type": "checklist",
      "items": [
        {
          "id": "chk_limpets",
          "label": "Restock Collector Limpets",
          "checked": true,
          "onInteract": {
            "type": "toggle_state",
            "emitEvent": true
          }
        },
        {
          "id": "chk_fss",
          "label": "FSS Honk Star System",
          "checked": false,
          "onInteract": {
            "type": "toggle_state",
            "emitEvent": true
          }
        }
      ]
    }
  ]
}
```

---

## 4. Interactive Action System (`onInteract`)

When the pilot presses the **Interact** input (Spacebar, Primary Joystick Button / HOTAS Button A), the MFD engine evaluates the item's `onInteract` definition:

### Supported Action Types:

1. **`switch_tab` (Navigation & Deep-Dive)**:
   - Switches the active MFD tab to `targetTab`.
   - Allows master-detail navigation workflows (e.g. clicking a system in a route list switches to a telemetry or market tab for that specific system).

2. **`toggle_state` (Interactive Checklists & Booleans)**:
   - Inverts the item's checked/active state.
   - Automatically re-renders with visual indicators: checked `[X]` (green) vs unchecked `[ ]` (dim).

3. **`cycle_filter` (Sorting & Mode Toggles)**:
   - Cycles through a list of filter states (e.g. `Sort by: [Profit] -> [Distance] -> [Supply]`).

4. **`copy` (Clipboard Injection)**:
   - Copies `copyText` (or `label` / `value`) directly into the Windows OS clipboard for pasting into the game's Galaxy Map search.

5. **`emit_event` (External App Bridge)**:
   - Appends a structured JSON event to `<game_dir>\plugins\edvr_mfd\events.jsonl`:
     ```json
     {
       "timestamp": 1727560123,
       "mfdId": "trade_market",
       "tabTitle": "PRICES",
       "itemId": "item_tritium",
       "action": "switch_tab",
       "label": "Tritium",
       "value": "52,400 CR"
     }
     ```

6. **`uri` (External Protocol Launch)**:
   - Launches an external URI in the background (e.g. `https://spansh.co.uk/system/...` or desktop companion tools).

---

## 5. Contextual Activity Gating

The MFD engine continuously evaluates the vehicle and flight state from the game's `Status.json` Flags bitmask. A display only renders when its `activityGating` conditions are met:

| Gating Identifier | Trigger Condition |
| :--- | :--- |
| `"always"` | Active at all times in-cockpit. |
| `"ship"` | Active when piloting a main Starship. |
| `"srv"` | Active when driving a Surface Recon Vehicle (SRV). |
| `"fighter"` | Active when piloting a Ship-Launched Fighter (SLF). |
| `"on_foot"` | Active during On-Foot exploration. |
| `"in_flight"` | Active only during space or atmospheric flight (hidden when docked/landed). |
| `"docked"` | Active only when docked at a station, outpost, or landed on a planetary surface. |
| `"hardpoints"` | Active only when hardpoints are deployed. |
| `"combat_mode"` | Active when in Combat HUD mode (Orange reticle). |
| `"analysis_mode"` | Active when in Analysis HUD mode (Blue scanner reticle). |

---

## 6. Dynamic In-Cockpit SETTINGS Tab & Persistence

The MFD engine automatically injects a dynamic `SETTINGS` tab as the last tab of every MFD.

- **Dynamic Defaults**: Initial values for coordinates, rotation angles, scale, opacity, tracking mode, and themes are seeded from the display's JSON file.
- **Pilot Customization**: Pilots can fine-tune Position X/Y/Z, Pitch, Yaw, Scale, Glass Opacity, Gaze Dwell Delays, Focus Margin, and Custom RGBA vector wireframe colors directly in VR using their HOTAS hat switch or keyboard arrows.
- **Persistence & Fallback**: Any user adjustment is written to `<game_dir>\plugins\edvr_mfd\edvr_mfd.ini` (with mirror to `%LOCALAPPDATA%\EDVR\edvr_mfd.ini`). On reload, user overrides take precedence over the JSON defaults.
- **Reset to Defaults**: The `[RESTORE DEFAULTS]` option allows pilots to revert individual MFDs back to the author's JSON specifications at any time.

---

## 7. External Tool Integration & Polling Protocol

1. **Atomic File Writes**:
   - External tools should write changes to `<file>.tmp` and perform an atomic `rename` / `replace` to prevent partial-read parse errors.
   - If the MFD engine encounters a partial JSON read during a file write, it retains the previous frame's valid view-model and retries on the next poll cycle without dropping frames or flashing.
2. **Low-Overhead Polling**:
   - The MFD engine inspects file modification timestamps (`GetFileAttributesEx`) at a throttled rate of 4–10 Hz (every 100–250 ms), keeping CPU and disk I/O overhead near 0.0%.
