# EDVR Cockpit MFD (Multi-Function Display) Addon — v0.1.0 (pre-release)

Interactive, contextual, vector-drawn in-cockpit MFD displays for **Elite Dangerous: Odyssey in VR**, powered by EDVR's OpenXR native plugin architecture.

![MFD Demo](docs/mfd_preview.png)

## Overview & Architecture

EDVR MFD v0.1.0 is a platform for extensible, high-performance in-cockpit displays. The plugin operates as a decoupled rendering engine and input router within the EDVR DirectX 11 / OpenXR pipeline, while external Go/Python companion daemons feed real-time telemetry into declarative JSON models.

### Host Compatibility
- **Required Host**: [EDVR Unofficial Patch](https://github.com/characterecho-sean/edvr-unofficial-patch)
- **Minimum Version**: EDVR **v0.20.0-rc.3** or later (third-party plugin API v2). Tested with [v0.20.0-rc.5](https://github.com/characterecho-sean/edvr-unofficial-patch/releases/tag/v0.20.0-rc.5). EDVR v0.19.x and earlier have no plugin support.
- The plugin loads only if the host's plugin API version matches exactly; otherwise it declines to register.

---

## Bundled Packages

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
Download and unzip `edvr-mfd-v<version>.zip` from [Releases](https://github.com/dmnemec/edvr-mfd/releases), then double-click `install_mfd.exe`:
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
Extract the contents of `edvr-mfd-v<version>.zip` directly into your Elite Dangerous product folder:
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
- Python 3.8+
- Go 1.22+, either for Windows or inside WSL. `build.bat` picks, in order:
  1. `EDVR_GO`, if set to the full path of a `go.exe`;
  2. `go` on the Windows PATH;
  3. Go inside WSL: the login shell's PATH, plus `/usr/local/go/bin`.

  Until [#16](https://github.com/dmnemec/edvr-mfd/issues/16) is fixed, two companion tests fail under native Windows Go, so WSL Go is recommended for now.

### Compile Plugin & Run Tests
```cmd
build.bat
```
The script can be run from any directory. It verifies the tool self-tests, builds `build\plugins\edvr_mfd\plugin.dll`, runs the C++ unit tests (`build\mfd_test.exe`) and the Go companion tests, and builds `build\tools\install_mfd.exe`.

### Contributing
Work is tracked in [GitHub Issues](https://github.com/dmnemec/edvr-mfd/issues). Coding agents (Claude Code, Gemini CLI, and others) and human contributors should read [AGENTS.md](AGENTS.md) first. It covers the build, the claim-before-you-start rule, and why work stops at `needs-test-flight`.

### Create Release ZIP
```cmd
python tools\package_release.py
```
This validates all packages using `mfdkit validate` and packages `dist\edvr-mfd-v<version>.zip`.

---

## Documentation

- **[MFD Authoring Guide](docs/mfd-authoring-guide.md)**: Manifest schema v1, display schema v1, companion integration, and testing with `mfdkit`.
- **[Cockpit MFD Architecture](docs/cockpit-mfd-architecture.md)**: Rendering pipeline, input routing, OpenXR compositor integration, and companion supervision.
- **[Issue Tracker](https://github.com/dmnemec/edvr-mfd/issues)**: Current bugs and planned work. [docs/ISSUES.md](docs/ISSUES.md) archives MFD-001…MFD-018 and their root-cause analyses.

## License

MIT License.
