# EDVR MFD Plugin — working notes for coding agents

An EDVR plugin — a Windows DLL (`plugin.dll`) loaded by the EDVR host (`d3d11.dll`) built for [EDVR (edvr-unofficial-patch)](https://github.com/characterecho-sean/edvr-unofficial-patch) at game startup. It renders floating MFD (Multi-Function Display) panels in VR using the game's D3D11 pipeline, fed by Elite Dangerous telemetry via shared memory.

The expensive resource on this project is not tokens, it is **test flights**. Every wrong hypothesis costs a build, an install, a headset session and a log. Everything below exists to spend fewer of them.

## Shell

Commands in this file are **Windows PowerShell 5.1**, not Bash or `cmd.exe`, whatever your agent's shell tool is called. If your tool runs `cmd.exe` or Bash, wrap each command as `powershell -NoProfile -Command "..."`. In 5.1: no `&&` or `||` (use `;` and `if ($?)`), no ternary, no `??`. Don't redirect a native exe's stderr with `2>&1`: 5.1 wraps each line in an ErrorRecord and sets `$?` false on a clean exit.

`build.bat` must be launched **by absolute path**; a `cmd` invocation that never ran the batch still exits 0, which reads as a successful build. When editing `build.bat`: `exit /b 1` inside an `if (...) else (...)` block returns **0** to the caller. Use `goto` labels instead (see its Go detection).

## Build and install

The absolute paths below are the **owner's machine**. On another machine, substitute your clone and game directory.

- **Prerequisites**: Visual Studio 2019/2022 C++ build tools, Python 3.8+, Go 1.22+. `build.bat` uses `EDVR_GO` (path to `go.exe`) if set, else Go on the Windows PATH, else Go inside WSL. Until #16 is fixed, three companion tests fail under native Windows Go; WSL Go passes.
- **Build**: `cmd /c "C:\Users\csasn\github.com\dmnemec\edvr-mfd\build.bat"` (must run by absolute path; works from any working directory)
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

- GitHub Issues (`gh issue list`) — the bug and issue tracker. **Check it first when picking up work.** See Issue tracking below.
- `docs\ISSUES.md` — frozen archive of MFD-001…MFD-018 and their root-cause analyses. Read-only.
- `include\log.h` — Win32 HANDLE logger, NOT CRT stdio (see MFD-R001 in `docs/ISSUES.md`).
- `include\edvr_plugin_api.h` — shared with the host; see constraints below.
- `src\mfd_compositor.h` — `OpenXrQuadCompositor` is a stub that only holds pose/pixel data and isn't what puts screens in the headset. The plugin draws each screen itself: `MfdManager::renderToEyeRtv` rasterizes a textured quad into the eye render target the host passes to `onRenderEye`. There's no OpenXR quad layer.

## Critical constraints

- **Built with `/MT` (static CRT).** Do NOT use CRT stdio (`FILE*`, `fprintf`, `fopen`) — use Win32 `HANDLE` / `WriteFile` instead. CRT stdio atexit handlers conflict with DLL unload. See `include/log.h` for the pattern.
- **All plugin callback function pointers must use `EDVR_API` (`__stdcall`).** Lambdas cannot be used for `__stdcall` slots — use named static functions. Calling-convention mismatches cause silent stack corruption.
- **`edvr_plugin_api.h` is shared with the host.** The host copy is canonical. Any ABI change must be mirrored to `c:\Users\csasn\github.com\characterecho-sean\edvr-unofficial-patch\include\edvr_plugin_api.h` (and vice versa). Bump `EDVR_PLUGIN_API_VERSION` when structs change.
- **Host keyboard gate:** when editing `input_gate.cpp`, add checks to ALL four hooks (`hookGetAsyncKeyState`, `hookGetKeyState`, `hookGetKeyboardState`, `hookPeekMessageA`). Missing one leaves a leak. Details in [docs/INPUT_GATE.md](docs/INPUT_GATE.md).
- **No raw `Copy-Item` to the game directory.** Always use `tools\install_mfd.py`.

## Issue tracking

All bugs and open work items live in **GitHub Issues** (`dmnemec/edvr-mfd`), managed with `gh`. `docs/ISSUES.md` is a frozen archive (MFD-001…MFD-018): read it for past RCAs, never add to it.

Several developers and agents may work at once. Agents usually share the owner's `gh` login, so the **assignee can't tell agents apart**. Ownership is the **latest claim comment** that has no release comment after it. **Before starting any work:**
1. `gh issue list --state open`: pick an issue labelled `ready-for-agent` (or `ready-for-human` if you are one) that is **not** `in-progress` and has no assignee.
2. Claim it as your first write, both steps:
   - `gh issue edit <n> --add-assignee @me --add-label in-progress --remove-label ready-for-agent`
   - `gh issue comment <n> --body "Claimed by <agent> (<model>), session <id or start time>"`, e.g. `Claimed by gemini-cli (gemini-2.5-pro), session 2026-10-20T14:05`.
   
   Then re-read the comments. If an earlier unreleased claim exists, it wins: comment `Released by <agent>: claimed concurrently`, leave the labels, and pick another issue. Never work on an issue whose current claim isn't yours.
3. For bugs, comment the hypothesis and the specific log line or telemetry field that would confirm it, before editing code.
4. Code pushed but not yet flown: swap `in-progress` for `needs-test-flight`.
5. Verified: commit with `Fixes #<n>` in the message, push, and confirm the issue closed (or `gh issue close <n> --comment "<hash>"`).
6. Abandoning work: comment `Released by <agent>: <what you learned>`, remove `in-progress`, restore the triage label, and remove the assignee.

Agents can't fly: an agent's finished state is step 4, `needs-test-flight`, with a comment listing exactly what the owner should check in the headset and which log lines confirm it.

Do not start work that has no issue. If you find a new bug, `gh issue create --label needs-triage --label bug` first. Labels and claim rules: `docs/agents/triage-labels.md`, `docs/agents/issue-tracker.md`.

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

## Agent skills

### Issue tracker

GitHub Issues on `dmnemec/edvr-mfd` via `gh`, with assignee-based claiming for concurrent work. See `docs/agents/issue-tracker.md`.

### Triage labels

Default five triage roles plus `in-progress` and `needs-test-flight` work-state labels. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: root `GLOSSARY.md` (created lazily) and `docs/adr/`. See `docs/agents/domain.md`.