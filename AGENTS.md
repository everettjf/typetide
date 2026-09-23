# TypeTide — Agent Guide

System-wide **AI translation + inline rewrite**, native on two platforms:

- `macos/` — SwiftUI menu-bar app (Xcode project)
- `windows/` — Win32 C++20 tray app (CMake, no third-party deps)

Both do the same two things:

- **Read**: select text → shortcut / floating icon / auto → translation popup.
- **Write**: type in native language → shortcut → translated & replaced in place.

## Shared architecture

```
Shortcut / SelectionMonitor ─► SelectionCapture ─► TranslationService ─► provider
                                      │                                     │
                              Popup / TextReplacer  ◄──────────── streamed result
```

Backends: macOS defaults to built-in MLX / TranslateGemma 4B (Apple Silicon, one-time model download). Ollama (`http://127.0.0.1:11434`) remains selectable on both platforms, as does any OpenAI-compatible
endpoint (`/chat/completions`, SSE streaming). 10 languages, native/foreign
pair, per-shortcut direction (auto-detect or fixed), 4 rewrite styles.

## macOS (`macos/`)

- `Core/` — `SelectionCapture` (AX `kAXSelectedText` + synthetic-⌘C fallback, restores clipboard), `TextReplacer` (synthetic paste, undo-safe), `TriggerController` (orchestrates read/write), `SelectionMonitor` (global mouse-up → AX selection), `PopupPositioner`, `Keyboard`, `Pasteboard`, `AccessibilityPermission`, `LaunchAtLogin`.
- `Translation/` — `TranslationProvider` protocol; `LocalTranslationProvider` (MLX, faithful translation), `BuiltInModelManager` (pinned and verified downloads), `OllamaProvider`, `OpenAIProvider` (SSE); `TranslationService` (routing + cache + style); `TranslationCache`; `OllamaModelResolver` (auto-pick installed model).
- `UI/` — `TranslationPopupView` + `PopupController`, `SelectionIconWindow`.
- `Config/` — `AppSettings` (single source of truth, UserDefaults-backed), `LanguageConfig`.
- `Services/` — `GlobalShortcutCenter` (Carbon, multi-hotkey), `UpdateChecker`, `SystemServiceProvider`.
- `Views/` — `MenuBarView`, `SettingsView/*` (General, Behavior, Backend, Language, Models, Shortcuts, Skip Apps, About).

### Conventions & gotchas

- **No sandbox** (`TypeTide.entitlements` empty) — needs Accessibility + CGEvent posting. `LSUIElement = true` (menu-bar only).
- **Sign dev builds** (Apple Development team is configured) so the Accessibility grant persists across rebuilds; an unsigned/ad-hoc build’s grant won’t stick.
- Carbon hotkeys fire without Accessibility, but capture/replace are guarded by `AccessibilityPermission.isGranted`.
- Read direction: detected → `LanguageConfig.sourceLanguage` (native). Write: native → `targetLanguage`.
- Settings via `@AppStorage(AppSettings.Keys.*)`; behavior changes call `TriggerController.shared.applyEnabledState()`.

### Build

```bash
cd macos
xcodebuild -scheme TypeTide -configuration Debug build   # signed
./scripts/build-release.sh                              # notarized DMG (needs .env)
```

## Windows (`windows/`)

Pure Win32 + WinHTTP + UI Automation, C++20, CMake + Ninja, zero third-party
dependencies (tiny JSON parser included in `src/Json.h`).

- `src/App.*` — orchestrator (TriggerController equivalent), owns the message-only window that receives hotkeys and worker notifications.
- `src/SelectionCapture.*` — UI Automation TextPattern selection + synthetic Ctrl+C clipboard fallback (clipboard always restored); also returns the selection's screen rect for popup anchoring.
- `src/TextReplacer.*` — synthetic Ctrl+V paste (undo-safe), clipboard restored after.
- `src/Translator.*` — provider interface; Ollama NDJSON + OpenAI SSE streaming over WinHTTP; in-memory LRU cache. Ollama requests send `"think": false` (retry without on 400) — thinking models (qwen3 family) otherwise stall for minutes on hidden reasoning.
- `src/OllamaModels.*` — `/api/tags` model list (settings dropdown) + auto-replace an uninstalled configured model at startup.
- `src/Language.*` — 10 languages, prompts, detection (script ranges + Latin stopword scoring).
- `src/PopupWindow.*` — streaming translation popup (Copy / Replace, Esc / click-outside dismiss).
- `src/SelectionIcon.*` / `src/SelectionMonitor.*` — floating icon & auto-translate triggers (WH_MOUSE_LL).
- `src/SettingsWindow.*` — native settings dialog (tabs: General, Backend, Language, Shortcuts, Behavior, About).
- `src/Settings.*` — JSON settings at `%APPDATA%\TypeTide\settings.json`.
- `src/TrayIcon.*`, `src/Hotkeys.*`, `src/LaunchAtLogin.*` (HKCU Run key), `src/UpdateChecker.*`.

### Conventions & gotchas

- All UI on the main thread, and the main thread NEVER sleeps (a stalled message loop gets WH_*_LL hooks silently removed). Waiting work (clipboard-fallback capture, paste replace, HTTP) lives on worker threads posting `WM_APP_*` messages with heap payloads (receiver frees).
- Workers must not read `Settings::shared()` — snapshot config on the main thread and pass it in (see `BackendConfig` in Translator.cpp, `FetchInstalledAsync`).
- Paste replace uses clipboard delayed rendering (WM_RENDERFORMAT = "target consumed the paste"); settings saves are atomic (tmp+rename); crashes dump minidumps to `%APPDATA%\TypeTide\crashes`.
- `RegisterHotKey` fails if another app owns the combo — surface it, don't crash.
- Keep feature parity with macOS where it makes sense; follow Windows conventions where they differ (tray vs menu bar, Alt vs ⌥, registry Run key vs SMAppService).

### Build

```powershell
cd windows
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

## Releases

Both platforms share ONE version: the root `VERSION` file, synced everywhere by
`scripts/bump-version.sh` (default patch+1; `--minor` / `--major` / explicit).
It writes pbxproj MARKETING_VERSION, windows CMakeLists/rc/manifest — when
touching the manifest remember only the app's own assemblyIdentity may change;
the Common-Controls `version="6.0.0.0"` must stay or the exe fails SxS at start.

Release per platform, either order, onto the same `vX.Y.Z` tag:
- `scripts/release-windows.ps1` (on Windows) → builds via
  `windows/scripts/build-release.ps1` (Release exe + self-test gate + Inno
  Setup installer from `windows/installer/TypeTide.iss` + portable zip), then
  `gh release create`-if-missing + upload.
- `scripts/release-macos.sh` (on a Mac) → notarized DMG via
  `macos/scripts/build-release.sh`, then create-if-missing + upload
  `TypeTide-X.Y.Z.dmg`.
Both need `gh auth login` once. `UpdateChecker` (both apps) reads
`releases/latest` at `everettjf/typetide`.

Keep `README.md` free of the app's release version — link to `../../releases`,
refer to artifacts as `TypeTide-Setup-x.y.z.exe` / `build/TypeTide.dmg` style,
never a pinned literal version. Platform/dependency versions (macOS 26+,
Windows 10+, Swift 5.9+, C++20) are fine.

## Current Product Priorities

- Use **TypeTide** consistently in repository metadata, update feeds, documentation, UI, package names, and website copy; treat Saypick only as historical naming.
- Track selection-capture success, replacement success, time to first translated token, total latency, cancellation, and provider failure locally during development without collecting user text.
- Keep Ollama as a first-class offline path and make network-provider use obvious before selected text leaves the device.
- Maintain parity tests for language direction, smart detection, prompt construction, streaming assembly, and clipboard restoration on macOS and Windows.
