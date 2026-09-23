<h1 align="center">TypeTide</h1>
<p align="center"><b>Type in your language. Rewrite in place — in any app on macOS &amp; Windows.</b></p>

<p align="center">
  <img src="https://img.shields.io/badge/macOS-26+-black.svg" />
  <img src="https://img.shields.io/badge/Windows-10+-0078d4.svg" />
  <img src="https://img.shields.io/badge/Swift-6.2+-orange.svg" />
  <img src="https://img.shields.io/badge/C++-20-00599c.svg" />
  <img src="https://img.shields.io/badge/AI-Ollama%20%7C%20OpenAI--compatible-7c5cff.svg" />
  <img src="https://img.shields.io/badge/License-MIT-green.svg" />
  <a href="https://discord.gg/eGzEaP6TzR"><img src="https://img.shields.io/badge/Discord-Join-5865F2?logo=discord&logoColor=white" /></a>
</p>

<p align="center">
  <a href="https://xnu.app/TypeTide/">🌐 Website</a> ·
  <a href="docs/blog/introducing-typetide.md">📝 Read the intro</a> ·
  <b>English</b> · <a href="README.zh-CN.md">简体中文</a>
</p>

TypeTide turns translation into an in-place system action. It lives in your menu bar (macOS) or system tray (Windows) and works in **any** app:

- **Read** — select foreign text, hit a shortcut, and a translation pops up next to it.
- **Write** — **type in your own language**, hit a shortcut, and it’s rewritten in place into the target language, ready to send.

No browser tab and no copy-paste loop. On Apple Silicon Macs, translation defaults to an **integrated MLX model**, downloaded once and run offline without Ollama. You can also choose **Ollama** or an **OpenAI-compatible** endpoint. Windows continues to use Ollama or an API.

<p align="center">
  <img src="docs/screenshots/tide-transition.gif" alt="TypeTide's particle tide sweeps across selected text while the translation streams in" width="760" />
  <br/><sub><i>The TypeTide transition · a particle tide carries the original text into its streamed translation.</i></sub>
</p>

<p align="center">
  <img src="docs/screenshots/rewrite.gif" alt="Type Chinese, press the rewrite shortcut, and it is rewritten into English in place" width="760" />
  <br/><sub><i>Write · type in your language, press <kbd>⌥R</kbd> on macOS or <kbd>Alt+R</kbd> on Windows, and it’s rewritten in place.</i></sub>
</p>

<p align="center">
  <img src="docs/screenshots/translate.gif" alt="Select text and the translation streams into a popup" width="760" />
  <br/><sub><i>Read · select text and the translation streams into a popup — Copy or Replace.</i></sub>
</p>

<p align="center">
  <img src="docs/screenshots/icon.png" alt="Floating translate icon next to a selection" width="520" />
  <br/><sub><i>Optional floating icon appears right next to your selection.</i></sub>
</p>

---

## ✨ Features

- **Read · translate** — select text anywhere → popup with the translation. Trigger by shortcut, a floating icon next to the selection, or auto-translate on select.
- **Write · rewrite in place** — write in your native language, press the rewrite shortcut, and the input field is replaced with the translation. Replace immediately or preview first.
- **Works in every app** — uses the Accessibility API (macOS) / UI Automation (Windows) to read the selection, with a clipboard-copy fallback for Electron/web apps, so it works even where text APIs don’t. The original clipboard is always restored.
- **Undo-safe replacement** — replacements are pasted, preserving each app’s native undo stack.
- **Local or cloud** — pluggable backends: **Built-in MLX** (Apple Silicon Mac), **Ollama** (offline, private), or any **OpenAI-compatible** API (`/chat/completions`, streaming). Switch in Settings.
- **Model picker** — Settings lists your installed Ollama models, recommends a comfortable size from physical memory, and auto-selects an installed model when the configured one is missing. Thinking models (qwen3 family, …) are handled — hidden reasoning is disabled so translations stay instant.
- **Styles** — Faithful, Formal, Casual, or Polished, independently for read and rewrite.
- **Shortcut actions** — each of the two global shortcuts can independently use smart direction or a fixed native/foreign target, then show a popup or replace in place.
- **Built-in checks** — first-run setup verifies permissions, backend translation, and shortcut registration. The backend check uses fixed synthetic text, never the clipboard.
- **Private diagnostics** — optional operational events stay on the device and can be reviewed, exported, or cleared. They contain timing, outcome, backend, and capture method only—never selected text, translations, clipboard contents, app names, URLs, or credentials.
- **Menu-bar / tray only** — no Dock or taskbar clutter. Global shortcuts, launch at login, per-app skip list.
- **Native on both platforms** — SwiftUI on macOS, Win32 C++ on Windows. No Electron, no runtime.

## 💡 Why TypeTide

| | TypeTide | Browser translate sites | OS built-in translate |
|---|---|---|---|
| Works in any app (mail, chat, IDE, terminal) | ✅ | ❌ (paste in/out) | ⚠️ menu only |
| Rewrite **in place** for replies | ✅ | ❌ | ❌ |
| Runs 100% offline / private | ✅ (Ollama) | ❌ | ⚠️ |
| Bring your own model / endpoint | ✅ | ❌ | ❌ |
| Streaming output | ✅ | ⚠️ | ❌ |

## 🌍 Languages

Detection and translation use the same 10-language set on macOS and Windows: **English (`en`), 中文 (`zh`), हिन्दी (`hi`), Español (`es`), Français (`fr`), العربية (`ar`), বাংলা (`bn`), Русский (`ru`), Português (`pt`), and Bahasa Indonesia (`id`)**.

Pick your native and foreign language in **Settings → Language**. Each Read/Write shortcut (`⌥D / ⌥R` on macOS, `Alt+D / Alt+R` on Windows) has its own direction: **auto** detects the selected text's language and translates the other way, or pin a **fixed** direction for mixed-language text.

## 🚀 Quick start

**1. Pick a backend**

**macOS (Apple Silicon):** keep the default **Built-in (Offline)** backend and click **Download Model** in first-run setup or **Settings → Backend**. The pinned TranslateGemma 4B 4-bit model downloads about 2.22 GB from Hugging Face; selected text never leaves the Mac. No Ollama, Python, account, or API key is needed. Model memory is released after two idle minutes. This backend supports Faithful translation and in-place replacement; other writing styles require Ollama/API. Long selections over the model's 2K input-token limit produce an actionable error instead of being silently truncated. Model use is subject to the [Gemma terms](https://ai.google.dev/gemma/terms).

**Windows or optional Mac backend:** install [Ollama](https://ollama.com/download), then:
```bash
# macOS: brew install ollama · Windows: winget install Ollama.Ollama
ollama pull qwen3.5:4b   # or any compact chat model that fits your hardware
ollama serve             # usually already running as a service
```
…or cloud: in **Settings → Backend**, choose *Cloud API*. Windows includes presets for
OpenAI, OpenRouter, and DeepSeek plus a custom OpenAI-compatible endpoint; API keys are
kept in Windows Credential Manager rather than the settings JSON.

TypeTide lists your installed models in **Settings → Backend** and auto-picks an installed one if the configured model is missing — pulling *any* chat model is enough to get going.

**2. Install TypeTide**

macOS — with [Homebrew](https://brew.sh) (recommended):
```bash
brew install --cask everettjf/tap/typetide
```
…or download the latest `.dmg` from [Releases](https://github.com/everettjf/typetide/releases/latest), drag it to Applications, and launch it. The build is signed and notarized by Apple.

Windows — download and run the latest `TypeTide-Setup-x.y.z.exe` installer from [Releases](https://github.com/everettjf/typetide/releases/latest) (per-user, no admin needed; a portable `TypeTide-Windows-x.y.z.zip` is also available). If SmartScreen warns about an unrecognized app, choose **More info → Run anyway**.

**3. First run**

macOS: allow TypeTide under **System Settings → Privacy & Security → Accessibility** (required to read selections and replace text). Windows needs no special permission. Then:

- Select text → **⌥D** (macOS) / **Alt+D** (Windows) → see the translation.
- Type in your language → **⌥R** / **Alt+R** → it’s rewritten in place.

Shortcuts, their actions, triggers, styles, and languages are all configurable in **Settings**. Either shortcut can be cleared completely; keep at least one configured if you want keyboard triggering. Windows combinations may also include the Windows key. Use **Test Connection** and **Verify Shortcuts** to confirm the complete path before relying on it.

## ⌨️ Default shortcuts

| Action | macOS | Windows |
|---|---|---|
| Translate selection (read) | <kbd>⌥</kbd><kbd>D</kbd> | <kbd>Alt</kbd><kbd>D</kbd> |
| Rewrite & replace (write) | <kbd>⌥</kbd><kbd>R</kbd> | <kbd>Alt</kbd><kbd>R</kbd> |

These are defaults, not requirements. Clear either shortcut in **Settings → Shortcuts**; if both are cleared, TypeTide warns that keyboard triggering is unavailable.

## 🧠 How it works

```
Select / type  ─►  Shortcut · floating icon · auto
                          │
      Capture (AX / UI Automation selection ─► clipboard fallback)
                          │
              Translate (Ollama / OpenAI-compatible, streaming)
                          │
        Read: popup  ·  Write: paste in place (undo-safe)
```

## 🔒 Privacy and system access

TypeTide can read and replace selected text across applications, so macOS requires Accessibility permission; Windows uses UI Automation without a separate permission prompt. Capture happens only after a configured shortcut, floating-icon click, or auto-translate selection event. Apps containing sensitive text can be disabled in **Settings → Excluded apps**; use **shortcut only** mode when you do not want passive selection monitoring.

- With **Ollama**, captured text is sent only to the Ollama service on `127.0.0.1`. TypeTide has no hosted relay or telemetry service.
- With an **OpenAI-compatible backend**, captured text is sent to the configured base URL when translation is triggered. That provider's retention and privacy policy applies; do not use a cloud backend for secrets or sensitive text.
- Some apps do not expose their selection through accessibility APIs. TypeTide then briefly copies the selection, reads it, and restores the previous clipboard contents. Clipboard managers may still record that temporary copy.
- API keys are stored in Keychain on macOS and Windows Credential Manager on Windows.

## 🏗️ Project structure

```
macos/                 # SwiftUI app (Xcode project)
├── TypeTide/
│   ├── Core/          # SelectionCapture, TextReplacer, TriggerController,
│   │                  # SelectionMonitor, PopupPositioner, LaunchAtLogin, …
│   ├── Translation/   # TranslationProvider, Ollama / OpenAI providers,
│   │                  # TranslationService, cache, model resolver
│   ├── UI/            # Translation popup + floating selection icon
│   ├── Config/        # AppSettings, LanguageConfig
│   ├── Services/      # GlobalShortcutCenter, UpdateChecker, system service
│   └── Views/         # Menu bar + Settings
└── scripts/           # Release / notarization scripts

windows/               # Native Win32 C++20 app (CMake)
├── src/               # Tray, hotkeys, UIA capture, popup, providers,
│                      # settings window, replacer, language detection
└── assets/            # Icon

docs/                  # GitHub Pages site + blog + screenshots
```

## 🔨 Build & release

macOS:
```bash
cd macos
open TypeTide.xcodeproj          # ⌘R to run

# Signed release + notarized DMG (export APPLE_ID, APPLE_SPECIFIC_PASSWORD, APPLE_TEAM_ID first)
./scripts/build-release.sh      # → build/TypeTide-x.y.z.dmg
```

Requirements: macOS 26+, Xcode 26+ with Swift 6.2+ and the Metal Toolchain (`xcodebuild -downloadComponent MetalToolchain`). Built-in MLX translation requires Apple Silicon. The app is **not** sandboxed (it needs Accessibility + synthetic key events). For local dev builds, sign with your Apple Development team so the Accessibility grant persists across rebuilds.

Windows:
```powershell
cd windows
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build             # → build/TypeTide.exe
```

Requirements: Windows 10+, Visual Studio 2022+ (MSVC, CMake, Ninja). No third-party dependencies — pure Win32 + WinHTTP + UI Automation. See [windows/README.md](windows/README.md) for architecture and the test harness.

**Releasing** — both platforms share one version (root `VERSION` file) and attach to the same GitHub release tag, in either order:

```bash
./scripts/bump-version.sh            # patch +1, syncs pbxproj / CMake / rc / manifest
# commit, then on each platform:
./scripts/release-windows.ps1        # Windows: builds installer + zip, creates/updates release
./scripts/release-macos.sh           # macOS: builds notarized DMG, creates/updates release
```

Whichever platform releases first creates the `vX.Y.Z` tag; the other uploads its asset to the same release later. Requires `gh auth login` once.

## 🔧 Troubleshooting

- **Shortcut does nothing** → macOS: confirm Accessibility is granted (Settings → General shows *Granted*) and TypeTide is enabled in the menu bar. Windows: another app may own the hotkey — pick a different one in Settings → Shortcuts.
- **No translation** → Ollama: is `ollama serve` running and the model installed? (TypeTide auto-picks an installed model if your configured one is missing.) OpenAI: check base URL / key / model.
- **Translation is very slow on a qwen3-class model** → TypeTide disables hidden “thinking”, preloads the selected model, and keeps it warm for 10 minutes. If it still crawls, the model may be too big for your hardware — try a smaller one from **Settings → Backend**.
- **Misaligned popup in some apps** → those apps don’t expose text bounds; the popup falls back to the cursor position.
- **“Can’t be opened on this Mac” on Sequoia or earlier** → TypeTide requires **macOS 26+**. It’s built against the current SwiftUI menu-bar and Settings APIs, and keeping a single modern baseline is what lets a small project stay reliable. Support for older macOS isn’t planned right now.
- **Which language goes where?** → In **Settings → Language**, pick your **native** and **foreign** language (there is no “source/target” pair to get backwards). Each shortcut has its own direction; **auto** detects the selected text and translates the other way.

## 🤝 Contributing

Issues and PRs welcome. The maintained README languages are English and Simplified Chinese; translations into other languages are welcome when contributors can help keep them current. Join the [Discord](https://discord.gg/eGzEaP6TzR).

## 📄 License

[MIT](LICENSE) · Made with ❤️ for macOS & Windows. If TypeTide helps you, a ⭐️ is appreciated!

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=everettjf/typetide&type=Date)](https://star-history.com/#everettjf/typetide&Date)
