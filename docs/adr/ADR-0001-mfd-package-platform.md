# ADR-0001 — MFD Package Platform for v1.0 Public Release

- **Status**: Accepted
- **Date**: 2026-10-15
- **Deciders**: dmnemec (owner), design interview (grill-me session)
- **Supersedes**: hardcoded `ensureDefaultSlots()` content model; "Active displays 1/2/3" setting
- **Companion doc**: [`docs/design/mfd-v1-release-design.md`](../design/mfd-v1-release-design.md)

## Context

The public docs (`mfd-authoring-guide.md`, `cockpit-mfd-architecture.md`) promise a `displays/*.json` hot-reload model and a bidirectional `events.jsonl`. The code implements neither:

- Three slots (`main_mfd`, `left_mfd`, `right_mfd`) are C++ string literals in `MfdManager::ensureDefaultSlots()`; the F8 menu caps the count at 3.
- Only `Status.json` `Flags`, pips, fuel, and cargo are read. The Journal is never read, so the "system" screen showed one hardcoded system.
- Nothing writes an event log; apps cannot observe pilot input.
- Context gating ignores `Flags2` and `GuiFocus`, so screens show in the Galaxy/System Map (MFD-003) and the pause menu (MFD-006); on-foot is misclassified as "in ship".
- The JSON parser is a hand-rolled `SimpleJsonParser` without error positions.
- Update hooks poll files on the render thread.

The release needs: unlimited user-imported MFDs, real-time updates from external apps, an input event log, correct context gating, a working F8 menu, and at least three useful, fully live example MFDs.

## Decision

1. **The plugin is a pure renderer + input router + event logger.** It never parses the Elite Journal or talks to the network. All domain data comes from external processes ("companions") writing display JSON files.
2. **The unit of import is a folder package**: `plugins/edvr_mfd/mfds/<package_id>/` with a static `manifest.json` (identity, screens, default poses, contexts, companions), live display content files (rewritten by companions), an optional `bin/`, a per-package `events.jsonl`, and `companion.log`.
3. **Companions are declared in the manifest and supervised by the plugin.** Launched on init via `CreateProcess` into a kill-on-close Job Object (dies with the game), restarted with backoff (1s/5s/30s, give up after 5 crashes in 10 min). Dropping a package in is consent (same trust model as `plugin.dll`); exe path + SHA-256 are logged.
4. **Live reload is file-based.** A watcher thread (`ReadDirectoryChangesW` + 500 ms mtime fallback, 30 ms debounce) parses off the render thread with vendored **nlohmann/json**, validates, and publishes immutable view models via atomic `shared_ptr` swap. Target < 100 ms write→frame. Parse failures keep the last good model, show an ERR badge, and emit `load_error`.
5. **Event log is per package** (`mfds/<id>/events.jsonl`), JSON Lines with `{seq, ts, session, screen, type, …}`, written by a background queue thread, truncated per session, rolled at 1 MB.
6. **Display schema v1** is a versioned public contract (`schemaVersion: 1`) adding checks, bars, named palette colors, built-in icons, required tab/item ids, and item actions (`clipboard` | `event`). JSON Schema files are published in `docs/schema/`.
7. **Context gating** uses a named `contexts` list per screen evaluated from `Status.json` `Flags` + `Flags2` + `GuiFocus`. Maps, FSS, SAA, and main menu are excluded by default (fixes MFD-003). For MFD-006 (pause menu), ship with a "Hide All" toggle in settings (and global hotkey) allowing the pilot to immediately hide/disable MFDs when menus are active, without delaying the release for reverse-engineering game pause state.
8. **Unlimited packages, hard cap of 16 visible screens.** Dirty-flag rasterization, skip hidden/gated/faded screens, lazy render targets, < 0.5 ms CPU p95 budget at 12 visible screens.
9. **F8 menu stays minimal**: global Enabled / Show all windows / Head-locked HUD / Hide All screens, plus per-package Show and Run companions toggles registered at init (host limitation: settings register only during `onInitialize`, so new packages appear after restart).
10. **Per-screen pose overrides** are edited in VR via an auto-injected SETTINGS tab and stored in `%LOCALAPPDATA%\EDVR\edvr_mfd.ini` keyed `<package_id>/<screen_id>`, never inside the package.
11. **Companions + SDK are Go**, in `companion/` of this repo: `pkg/mfdkit` (public SDK) and `cmd/ship`, `cmd/exploration`, `cmd/route`, cross-compiled from WSL2 (`GOOS=windows`) by `build.bat`.
12. **Three example packages** ship in v1.0: **Ship** (MODULES, POWER, CARGO), **Exploration** (SYSTEM, BIO), and **Route** (NavRoute + Spansh route file). No runtime network calls.
13. **Verification ladder**: offline harness + Go golden tests, desktop Journal replay (`mfdkit replay`, `EDVR_MFD_JOURNAL_DIR` with detailed flight recording procedures in ticket B7), then test flights only at 3 milestones.

## Alternatives considered

| Alternative | Why rejected |
|---|---|
| Plugin parses Journal and offers `{{template}}` bindings | Grows the in-process attack/crash surface; duplicates what apps already do; examples wouldn't prove the public app API. |
| Hybrid built-in bindings + companion | Two data paths to document and test; still couples plugin releases to Journal format changes. |
| Single JSON file per MFD (current docs) | Companion rewrites would clobber user/author static config (pose, companions). |
| User launches companions manually | Stale screens become the #1 support complaint. |
| Named pipe / socket push API | Higher barrier for authors; files with atomic rename already meet < 100 ms. Can be added in v1.x. |
| Keep hand-rolled JSON parser | No error line numbers, incomplete escape/Unicode handling, unacceptable for a public schema. |
| Global `events.jsonl` | Every app must filter others' events; noisier contention on one file. |
| No visible-screen cap | Unbounded frame-time risk in VR; owner chose a hard cap of 16. |
| Large F8 menu (dwell, opacity, reload, reset, open folder) | Owner chose minimal; reload/restart is handled automatically by supervision. |
| Complex reverse-engineering spike for pause menu (MFD-006) | Rejected: don't waste time investigating; ship a "Hide All" toggle button in settings. |
| Plugin self-extracts examples from DLL resources | Bloats DLL; zip + installer already covers this. |
| Runtime price/system lookups (Inara/Spansh/EDSM) | Owner requires no external calls; game-provided `MeanPrice` and local Journal are sufficient. |

## Consequences

**Positive**
- Any language can author an MFD (write a JSON file, tail a JSONL file).
- Examples double as reference implementations of the public SDK.
- Plugin stays small and stable across Elite Journal changes.
- Fixes MFD-003, resolves MFD-006 via explicit user hide control, and fixes on-foot misclassification.

**Negative / risks**
- The game process spawns executables: AV false-positive risk. Mitigation: unsigned exes documented, hashes logged, `Run companions` toggle per package.
- New packages require a game restart to appear in F8 (content still hot-reloads).
- Build now depends on WSL2 + Go for the companions.
- Module health and power data are only as fresh as the game's Journal / `ModuleInfo.json` events (accepted by the owner).
- Cargo galactic average is unknown ("—") until the pilot has opened a market listing that commodity (accepted; no seed table).

**ABI impact**: None expected for core work (per-package settings use existing `registerSetting` at init). Target branch in `edvr-unofficial-patch` confirmed as `feat/third-party-plugins`.
