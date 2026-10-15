# Keyboard gate and plugin input suppression (MFD-001)

How the EDVR host stops Elite Dangerous from seeing keystrokes while an MFD panel holds keyboard focus. The gate lives in the host repo (`edvr-unofficial-patch`, branch `feat/third-party-plugins`); the plugin only reports whether it wants input swallowed.

## Host files involved

| File | Role |
|---|---|
| `src/plugins/plugin_manager.cpp` | Loads `plugin.dll`, calls all lifecycle callbacks, dispatches `onFilterInput` |
| `src/d3d11/input_gate.cpp` | Three-door keyboard gate. `g_private` blocks during F8 menu. `g_pluginBlock` blocks when a plugin (MFD) holds keyboard focus. |
| `src/d3d11/input_gate.h` | Gate public API: `inputGateSetPluginBlock(bool)` (added for MFD-001). |
| `src/openxr/d3d11_stereo.cpp` | Render loop: calls `onUpdate` (~line 518), `onRenderEye` (~line 540), and after the MFD-001 fix, `onFilterInput` + `inputGateSetPluginBlock` per frame. |
| `include/edvr_plugin_api.h` | Plugin ABI header — the canonical copy. Mirror any changes to `edvr-mfd/include/edvr_plugin_api.h`. |

Line numbers are approximate and will drift.

## The three doors

Elite Dangerous reads the keyboard through exactly three doors (measured from the EXE import table):

1. **DirectInput8 keyboard device** — `GetDeviceState` / `GetDeviceData` vtable hooks
2. **user32 trio** — IAT hooks on `GetAsyncKeyState`, `GetKeyState`, `GetKeyboardState`
3. **PeekMessageA** — IAT hook; keyboard messages become `WM_NULL` in place

All three check `g_private` (F8 menu open) and `g_pluginBlock` (plugin keyboard focus). Both are `std::atomic`. Setting either makes the gate return all-keys-up to the game. The gate is fail-open: a door that faults retires to pass-through for the session.

## Plugin keyboard suppression flow

1. Each frame, `d3d11_stereo.cpp` calls `PluginManager::instance().onFilterInput(0, nullptr)`.
2. `onFilterInput` calls each plugin's `mfdPluginFilterInput`; the plugin checks `MfdManager::focusedSlot()`.
3. If any plugin returns `swallowInput = 1`, `onFilterInput` returns `true`.
4. `d3d11_stereo.cpp` calls `inputGateSetPluginBlock(true/false)`.
5. All three gate doors block keyboard input for that frame.

## Editing `input_gate.cpp`

Always add checks to **all four** hooks — `hookGetAsyncKeyState`, `hookGetKeyState`, `hookGetKeyboardState`, and `hookPeekMessageA`. Missing one door leaves a leak.
