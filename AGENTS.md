# EDVR MFD Plugin — working notes for coding agents

An EDVR plugin — a Windows DLL (`plugin.dll`) loaded by the EDVR host (`d3d11.dll`) built for [EDVR (edvr-unofficial-patch)](https://github.com/characterecho-sean/edvr-unofficial-patch) at game startup. It renders floating MFD (Multi-Function Display) panels in VR using the game's D3D11 pipeline, fed by Elite Dangerous telemetry via shared memory.

The expensive resource on this project is not tokens, it is **test flights**. Every wrong hypothesis costs a build, an install, a headset session and a log. Everything below exists to spend fewer of them.

## Shell

Use the **PowerShell tool**, not Bash. Windows PowerShell 5.1 (same host machine as edvr-unofficial-patch): no `&&` or `||` (use `;` and `if ($?)`), no ternary, no `??`. Don't redirect a native exe's stderr with `2>&1` — 5.1 wraps each line in an ErrorRecord and sets `$?` false on a clean exit.

`build.bat` must be launched **by absolute path**; a `cmd` invocation that never ran the batch still exits 0, which reads as a successful build.

## Build and install

- **Build**: `cmd /c "C:\Users\csasn\github.com\dmnemec\edvr-mfd\build.bat"` (must run by absolute path)
- **Install**: `python "C:\Users\csasn\github.com\dmnemec\edvr-mfd\tools\install_mfd.py" --target "D:\SteamLibrary\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64"` (supports `--dry-run`)
- **Verify install**: compare PE timestamp of installed `plugins\edvr_mfd\plugin.dll` against the build output.
- **Host requirement**: the EDVR host must also be installed for the plugin to load (see below).

## EDVR host repo

The plugin runs inside the EDVR host DLL. Understanding the host is required for any fix touching input, rendering, or the plugin ABI. For how the host's keyboard gate works, see [docs/INPUT_GATE.md](docs/INPUT_GATE.md).

| Item | Detail |
|---|---|
| **Repo** | `https://github.com/characterecho-sean/edvr-unofficial-patch` |
| **Local path** | `c:\Users\csasn\github.com\characterecho-sean\edvr-unofficial-patch` |
| **Active branch** | `feat/third-party-plugins` |
| **Build** | `cmd /c "c:\Users\csasn\github.com\characterecho-sean\edvr-unofficial-patch\build.bat"` (absolute path) |
| **Install** | `python tools\install_edvr.py --target <game_dir>` from the EDVR repo root |
| **Verify** | `python tools\install_edvr.py --target <game_dir> --verify-only` |

### Cross-repo workflow

When a fix requires changes in both repos (new ABI field, host-side gate fix, etc.):
1. Make host changes on `feat/third-party-plugins` in the EDVR repo.
2. Build EDVR via absolute path (see above).
3. Install EDVR: `python tools\install_edvr.py --target <game_dir>`.
4. Build and install MFD.
5. Test flight. Verify both installed DLL PE timestamps match their builds.
6. Commit both repos. Push both. Task is not done until both pushes confirmed.

## Where things are

Source is in `src\`, the plugin ABI in `include\`, tooling in `tools\`, and design docs in `docs\`. Non-obvious entries:

- `docs\ISSUES.md` — bug and issue tracker. **Read this first when picking up work.**
- `include\log.h` — Win32 HANDLE logger, NOT CRT stdio (see MFD-R001 in `docs/ISSUES.md`).
- `include\edvr_plugin_api.h` — shared with the host; see constraints below.
- `src\mfd_compositor.h` — `OpenXrQuadCompositor` is a stub that only holds pose/pixel data; the host does the compositing.

## Critical constraints

- **Built with `/MT` (static CRT).** Do NOT use CRT stdio (`FILE*`, `fprintf`, `fopen`) — use Win32 `HANDLE` / `WriteFile` instead. CRT stdio atexit handlers conflict with DLL unload. See `include/log.h` for the pattern.
- **All plugin callback function pointers must use `EDVR_API` (`__stdcall`).** Lambdas cannot be used for `__stdcall` slots — use named static functions. Calling-convention mismatches cause silent stack corruption.
- **`edvr_plugin_api.h` is shared with the host.** The host copy is canonical. Any ABI change must be mirrored to `c:\Users\csasn\github.com\characterecho-sean\edvr-unofficial-patch\include\edvr_plugin_api.h` (and vice versa). Bump `EDVR_PLUGIN_API_VERSION` when structs change.
- **Host keyboard gate:** when editing `input_gate.cpp`, add checks to ALL four hooks (`hookGetAsyncKeyState`, `hookGetKeyState`, `hookGetKeyboardState`, `hookPeekMessageA`). Missing one leaves a leak. Details in [docs/INPUT_GATE.md](docs/INPUT_GATE.md).
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
