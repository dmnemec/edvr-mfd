# EDVR Cockpit MFD (Multi-Function Display) Addon — v1.0.0

Interactive, contextual, vector-drawn in-cockpit MFD displays for **Elite Dangerous: Odyssey in VR**, powered by EDVR's OpenXR native plugin architecture.

![MFD Demo](docs/mfd_preview.png)

## Overview & Architecture

EDVR MFD v1.0.0 is a complete platform for extensible, high-performance in-cockpit displays. The plugin operates as a decoupled rendering engine and input router within the EDVR DirectX 11 / OpenXR pipeline, while external Go/Python companion daemons feed real-time telemetry into declarative JSON models.

### Host Compatibility
- **Required Host**: [EDVR Unofficial Patch](https://github.com/characterecho-sean/edvr-unofficial-patch)
- **Minimum Version**: v0.18.0+
- **Host Branch**: `feat/third-party-plugins` (Commit Pin: `ececeb3`)

---

## Bundled v1.0 Packages

EDVR MFD ships with three fully functional, supervised companion packages located in `plugins/edvr_mfd/mfds/`:

### 1. `ship` (Ship Readout)
- **Screens**:
  - `MODULES`: Real-time module integrity %, power draw (MW), priority group ratings (1–5).
  - `POWER`: Power distributor capacitor pips, power plant capacity & usage, deployed/retracted load %, and engineering modifier support.
  - `CARGO`: Manifest inventory list, cargo count, tonnage, and market mean price calculation.
- **Companion**: `bin/edvr-mfd-ship.exe` (Go companion monitoring `Status.json`, `ModulesInfo.json`, and `Cargo.json`).

### 2. `route` (Navigation & Spansh Router)
- **Screens**:
  - `NAVROUTE`: Waypoints parsed from `NavRoute.json`, remaining jump distance, jump count, star class, and system names with one-click clipboard copying for Galaxy Map routing.
  - Spansh neutron router integration with auto-copy support.
- **Companion**: `bin/edvr-mfd-route.exe` (Go companion tracking navigation route and journal jumps).

### 3. `exploration` (System Survey & Exobiology)
- **Screens**:
  - `SYSTEM`: Scanned stellar bodies, terraformable candidates, estimated cartographic scan/mapping value formulas, landable indicators.
  - `BIO`: Surface exobiology signals, genus/species tables, estimated payout values, and live sample distance tracking (verifying required colonial separation).
- **Companion**: `bin/edvr-mfd-exploration.exe` (Go companion parsing scan telemetry and surface navigation).

---

## In-Headset F8 Menu Controls

The MFD addon integrates directly into the EDVR F8 in-headset menu under **Cockpit MFD**:

- **Hide All (`mfd_hide_all_toggle`)**: Instantly clears or restores all MFD panels on demand (ideal for pause menus, external camera, or screenshot capture).
- **Cockpit MFD Displays (`On` / `Off`)**: Master plugin enable/disable toggle.
- **Show all windows (`On` / `Off`)**: Bypasses gaze fade, keeping all active MFD panels fully opaque regardless of look direction.
- **Active displays (`1 (Center)`, `2 (Center + Left)`, `3 (All Three)`)**: Selects the active slot configuration.
- **Head-locked HUD (`On` / `Off`)**: Toggles between 3D cockpit-anchored displays and head-tracked HUD mode.
- **Per-Package Enable Toggles**: Dynamically enable or disable individual packages (`ship`, `route`, `exploration`).

---

## Installation

### Option A: One-Click Native Installer (Zero Dependencies)
Download and unzip `edvr-mfd-v1.0.0.zip`, then double-click `install_mfd.exe`:
- **Auto-Detects**: Scans the Windows Registry and Steam library folders (`libraryfolders.vdf`), Epic Games, Frontier Launchers, and common drives to locate your Elite Dangerous Odyssey folder automatically.
- **Verifies EDVR**: Confirms the EDVR host (`d3d11.dll`) is installed.
- **Zero Python Required**: Single standalone Windows executable.

You can also run it from the command line:
```cmd
install_mfd.exe
install_mfd.exe --dry-run
install_mfd.exe --target "D:\SteamLibrary\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64"
```

### Option B: Python Tool (Developers / Repo Clone)
After building or cloning:
```cmd
python tools\install_mfd.py --target steam
```
Flags available:
- `--target <path|steam|frontier>`: Target installation folder.
- `--dry-run`: Preview changes without writing to disk.
- `--verify-only`: Verify installed plugin and packages match build artifacts.
- `--force`: Force overwrite bundled packages even if target version is >= repo version.

### Option C: Manual Extraction
Extract the contents of `edvr-mfd-v1.0.0.zip` directly into your Elite Dangerous product folder:
```text
<Elite Dangerous Odyssey Product Folder>\
    d3d11.dll
    openvr\win64\openvr_api.dll
    plugins\
        edvr_mfd\
            plugin.dll
            edvr_mfd.ini
            mfds\
                ship\
                    manifest.json
                    screens\
                    bin\edvr-mfd-ship.exe
                route\
                    manifest.json
                    screens\
                    bin\edvr-mfd-route.exe
                exploration\
                    manifest.json
                    screens\
                    bin\edvr-mfd-exploration.exe
```

---

## Building from Source

### Prerequisites
- Windows 10/11
- Visual Studio 2019/2022 (MSVC C++ Desktop Development)
- Go 1.22+ (via WSL or Windows)
- Python 3.8+

### Compile Plugin & Run Tests
```cmd
build.bat
```
This builds `build\plugins\edvr_mfd\plugin.dll`, executes C++ unit tests (`build\mfd_test.exe`), runs Go companion tests via WSL, and verifies tool self-tests.

### Create Release ZIP
```cmd
python tools\package_release.py
```
This validates all packages using `mfdkit validate` and packages `dist\edvr-mfd-v1.0.0.zip`.

---

## Documentation

- **[MFD Authoring Guide](docs/mfd-authoring-guide.md)**: Manifest schema v1, display schema v1, companion integration, and testing with `mfdkit`.
- **[Cockpit MFD Architecture](docs/cockpit-mfd-architecture.md)**: Rendering pipeline, input routing, OpenXR compositor integration, and companion supervision.
- **[Issue Tracker](docs/ISSUES.md)**: Current bug tracking and resolution history.

## License

MIT License.
