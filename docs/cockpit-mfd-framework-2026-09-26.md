# Cockpit Multi-Function Display (MFD) Framework

## Status

- **State**: Option A implemented; Option B architectural slots reserved and tested.
- **Branch**: `feat/cockpit-mfd`
- **Active Method (Option A)**:
  - Declarative JSON data ingestion via local IPC / sockets / files.
  - Native Elite HUD rasterizer (amber/cyan palette, chamfered header, tab banners, inverted selection bars, built-in 8x16 bitmap typography).
  - Head-gaze focus tracker with angular dwell-time hysteresis (enter threshold: 120ms, exit grace: 200ms).
  - Look-to-focus input router intercepting cockpit UI navigation controls (`UI_Up`, `UI_Down`, `UI_Left`, `UI_Right`, `UI_Select`, `UI_Back`, `CycleNextPage`, `CyclePreviousPage`) and swallowing them from game flight systems when focused.
  - OpenXR Quad Composition Layer (`XrCompositionLayerQuad`) projection in seated cockpit space.
- **Reserved Method (Option B)**:
  - `ScriptedMfdProvider` interface contract ready for embedded Lua/WASM/C++ code plugins.
  - `D3D11SceneCompositor` backend slot ready for depth-tested in-game draw injection.
- **Gates & Verification**:
  - `gen_mfd_font.py --self-test`: PASSED.
  - `mfd_test.exe`: PASSED all 7 test categories.
  - `check_config_contract.py`: PASSED (all keys agree).
- **Next Flight**:
  - Load sample Inara / Spansh JSON route onto cockpit lower console.
  - Verify gaze acquisition when glancing down-right at MFD.
  - Verify HOTAS hat switch cycles tabs and scrolls waypoints without adjusting ship pips or firing thrusters.

---

## Flight Test Checklist

### 1. What Has to Be Tested
- Head-gaze focus acquisition and de-acquisition on the cockpit MFD.
- Look-to-focus input hijacking: verifying UI controls drive MFD tabs and items while suppressing underlying ship flight reactions.
- Native HUD rendering: legibility of amber/cyan vector elements, chamfered bezel, and inverted selection highlights.

### 2. How to Test It
1. Launch Elite Dangerous into a ship cockpit (e.g. Krait Mk II, Cobra Mk III, or Anaconda) wearing a VR headset with HOTAS.
2. Observe the configured MFD slot (default: lower center or right console).
3. **Gaze test**: Glance down at the MFD for $>150\text{ ms}$.
4. **Input test**: While looking at the MFD, use your standard UI navigation buttons or HOTAS hat switch (Next Tab `E`, Prev Tab `Q`, `Up`, `Down`, `Select` / `Space`).
5. **Flight safety test**: Glance back up through the main canopy while pressing the same hat switch / keys.

### 3. Positive vs. Negative Signals
- **Positive Signals**:
  - Looking at the MFD causes its border to brighten and display `[FOCUSED]`.
  - Pressing NextTab/PrevTab cycles the MFD tabs cleanly.
  - Pressing Up/Down scrolls the waypoint list with an orange inverted highlight.
  - While focused on the MFD, the ship's power distributor pips (SYS/ENG/WEP) do NOT change.
  - Looking back up at the canopy restores normal flight controls within $200\text{ ms}$; the same hat switch now diverts pips / thrusters normally.
- **Negative Signals**:
  - Glancing at the MFD flickers focus erratically (hysteresis timing too short).
  - Pressing UI buttons while looking at the MFD simultaneously adjusts pips or fires thrusters (input was not swallowed).
  - Text is blurry or jittery in tracking space.

---

## Architecture Overview

### Option A: Declarative JSON & OpenXR Quad Layer (Implemented)
Option A treats the MFD as a structured avionics screen whose state is updated via JSON over local networking, named pipes, or files:
- `DeclarativeMfdProvider`: Ingests JSON payloads containing headers, tabs, item lists, and key-value telemetry.
- `MfdRenderer`: Pure software rasterizer generating an RGBA8 buffer styled identically to Frontier's orange/cyan vector HUD.
- `MfdGazeTracker`: OpenXR seated-space ray/cone intersection with dwell-time state machine (`kUnfocused` $\rightarrow$ `kAcquiring` $\rightarrow$ `kFocused` $\rightarrow$ `kReleasing`).
- `MfdInputRouter`: Intercepts cockpit navigation inputs and routes them to the active MFD while focused.
- `OpenXrQuadCompositor`: Places the display as an `XrCompositionLayerQuad` in `XR_REFERENCE_SPACE_TYPE_LOCAL`.

### Option B: Scripted Code & D3D11 Mesh Injection (Reserved Architecture)
Option B provides complete extension slots for future advanced use-cases:
- `ScriptedMfdProvider`: Implements `IMfdProvider` with `MfdProviderType::kScriptedCode`, ready to wrap a Lua, WASM, or native C++ scripting runtime.
- `D3D11SceneCompositor`: Implements `IMfdCompositor` with `MfdCompositorBackend::kD3D11SceneMesh`, ready to render the MFD quad directly into Elite's D3D11 render targets with scene depth testing.
