# EDVR MFD Plugin — working notes for coding agents

An EDVR plugin — a Windows DLL (`plugin.dll`) loaded by the EDVR host (`d3d11.dll`) at game startup. It renders floating MFD (Multi-Function Display) panels in VR using the game's D3D11 pipeline, fed by Elite Dangerous telemetry via shared memory.

The expensive resource on this project is not tokens, it is **test flights**. Every wrong hypothesis costs a build, an install, a headset session and a log. Everything below exists to spend fewer of them.

## Shell

Use the **PowerShell tool**, not Bash. Windows PowerShell 5.1 (same host machine as edvr-unofficial-patch): no `&&` or `||` (use `;` and `if ($?)`), no ternary, no `??`. Don't redirect a native exe's stderr with `2>&1` — 5.1 wraps each line in an ErrorRecord and sets `$?` false on a clean exit.

`build.bat` must be launched **by absolute path**; a `cmd` invocation that never ran the batch still exits 0, which reads as a successful build.

## Build and install

- **Build**: `cmd /c "C:\Users\csasn\github.com\dmnemec\edvr-mfd\build.bat"` (must run by absolute path)
- **Install**: `python "C:\Users\csasn\github.com\dmnemec\edvr-mfd\tools\install_mfd.py" --target "D:\SteamLibrary\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64"`
- **Verify install**: compare PE timestamp of installed `plugins\edvr_mfd\plugin.dll` against the build output.
- **Host requirement**: The EDVR host (`edvr-unofficial-patch` repo, `feat/plugin-architecture` branch) must also be installed for the plugin to load. Use `python tools\install_edvr.py --target ...` from that repo.

## Layout

| Path | What |
|---|---|
| `src\mfd_plugin.cpp` | Plugin entry point: exports `EdvrPluginRegister`, all lifecycle callbacks |
| `src\mfd_manager.cpp` | `MfdManager` singleton: slot management, render, telemetry loop, default providers |
| `src\mfd_provider.h` | `DeclarativeMfdProvider`: JSON-driven display definition, action/KV callbacks |
| `src\mfd_input_router.cpp` | Elite bindings parser, joystick polling, HOTAS input routing to focused slot |
| `src\mfd_renderer.cpp` | Software rasteriser: text, boxes, bars, icons |
| `src\mfd_compositor.h` | `OpenXrQuadCompositor`: stub that holds pose/pixel data for the host to composite |
| `src\mfd_telemetry.h` | Shared-memory telemetry reader (Elite Status JSON + named pipe) |
| `include\edvr_plugin_api.h` | Plugin ABI (keep in sync with edvr-unofficial-patch copy) |
| `include\log.h` | Win32 HANDLE logger — NOT CRT stdio (see MFD-R001 in docs/ISSUES.md) |
| `tools\install_mfd.py` | Installs plugin.dll to game plugins dir; supports `--dry-run` |
| `docs\ISSUES.md` | Bug and issue tracker — **read this first when picking up work** |
| `docs\MFD_PLUGIN_GUIDE.md` | End-user and plugin-author documentation |

## Critical constraints

- **Built with `/MT` (static CRT).** Do NOT use CRT stdio (`FILE*`, `fprintf`, `fopen`) — use Win32 `HANDLE` / `WriteFile` instead. CRT stdio atexit handlers conflict with DLL unload. See `include/log.h` for the pattern.
- **All plugin callback function pointers must use `EDVR_API` (`__stdcall`).** Lambdas cannot be used for `__stdcall` slots — use named static functions. Calling-convention mismatches cause silent stack corruption.
- **`edvr_plugin_api.h` is shared with the host.** Any ABI change must be mirrored to `c:\Users\csasn\github.com\characterecho-sean\edvr-unofficial-patch\include\edvr_plugin_api.h`. Bump `EDVR_PLUGIN_API_VERSION` when structs change.
- **No raw `Copy-Item` to the game directory.** Always use `tools\install_mfd.py`.

## Issue tracking

All known bugs and open work items live in `docs/ISSUES.md`. **Before starting any bug fix:**
1. Read `docs/ISSUES.md` `## Status` block.
2. Find the issue ID (`MFD-NNN`).
3. State the hypothesis and the specific log line or telemetry field that would confirm it before editing code.
4. Mark the issue `In Progress` in the doc while working.
5. Move it to `## Resolved` with the commit hash when done.

Do not start work on a bug that is not tracked in `docs/ISSUES.md`. If you find a new bug, add it first.

## Diagnosis discipline

- **Check WER crash reports**: Look in `C:\ProgramData\Microsoft\Windows\WER\ReportArchive\*EliteDangerous*` for `FaultModuleTimestamp` and compare it to the installed `plugin.dll` PE timestamp before debugging. A stale DLL is not evidence.
- **Check the MFD log**: The MFD log writes to `<game_dir>\edvr_logs\edvr_mfd.log`. Read it after a crash to see shutdown messages.
- **Isolate host vs plugin**: If the game runs without EDVR installed but crashes with it, the fault is in EDVR or the plugin — not the game.
- **Never ship a fix on an untested hypothesis.** State the hypothesis, identify confirming evidence, and verify.

## Git

- Solo project. Branch → commit → push to `main`. No PRs.
- Write commit messages to a temp file and use `git commit -F` (avoids PowerShell quote escaping).
- The task is not done until `git push` has been confirmed (run `git log origin/main -1`).
- Always verify the installed DLL's PE timestamp matches the build after install.
