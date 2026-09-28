# EDVR Cockpit MFD (Multi-Function Display) Addon

Interactive, contextual, vector-drawn in-cockpit MFD displays for **Elite Dangerous: Odyssey in VR**, powered by EDVR's OpenXR native plugin architecture.

![MFD Demo](docs/mfd_preview.png)

## Features

- **True 3D In-Cockpit Spatial Displays**: Multi-slot MFD panels rendered directly into your cockpit's seated 3D space with high-contrast vector wireframe styling.
- **Dynamic Gaze Tracking & Focus**: Eye/head-center gaze cone detection activates and highlights MFDs automatically when you look at them.
- **Auto-Hide & Smooth Fade**: Optional auto-hide hides panels until looked at, smoothly fading in upon gaze dwell.
- **HOTAS & Joystick Input Routing**: Seamlessly route UI navigation (Tabs, Select, Back, Scroll) from your flight stick/throttle without passing unwanted commands to your ship.
- **Spansh Neutron Router & Live Telemetry**: Dynamic multi-tab providers integrating Spansh neutron routing (with one-click clipboard copying) and live ship power distribution/cargo from `Status.json`.
- **Persistent Settings & Custom Theming**: Save custom $X/Y/Z$ positions, angles, scale, glass opacity, tracking mode (Cockpit-Locked vs Head HUD), preset color themes, and custom RGBA vector tuning across game restarts.
- **Contextual Vehicle & HUD Gating**: Customize display rules for Ship, SRV, SLF Fighter, Docked vs In-Flight, and Combat vs Analysis HUD modes.

## Documentation & Custom Display Authoring

- **[MFD Display & Plugin Authoring Guide](docs/mfd-authoring-guide.md)**: Complete JSON schema reference, interactive list/keyvalue/text tabs, clipboard copying, and bidirectional `events.jsonl` integration.
- **[Cockpit MFD Architecture & Design Spec](docs/cockpit-mfd-architecture.md)**: Full technical architecture of the rendering pipeline, OpenXR quad layer compositor, and input router.

## Installation

1. Install **[EDVR](https://github.com/characterecho-sean/edvr-unofficial-patch)** (v0.18.0+).
2. Download or build the MFD plugin.
3. Place `plugin.dll` into the `plugins/edvr_mfd/` directory beside your game executable:
   ```
   <Elite Dangerous Odyssey Folder>\
       d3d11.dll
       Openvr\win64\openvr_api.dll
       plugins\
           edvr_mfd\
               plugin.dll
               settings.ini
               displays\
   ```
4. Launch Elite Dangerous in VR!

## Building from Source

Requirements: Windows 10/11, Visual Studio 2019/2022 with C++ Desktop Development.

```cmd
build.bat
```
The compiled DLL will be placed in `build\plugins\edvr_mfd\plugin.dll`. To build and install directly to your Steam / Frontier game directory:
```cmd
python tools\install_mfd.py --target steam
```

## License

MIT License.
