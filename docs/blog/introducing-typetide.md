# Introducing TypeTide — system-wide AI translation that lives in your menu bar

> Select foreign text and read it. Type in your own language and send it in another. One shortcut each, in **any** app, with a model that can run entirely on your Mac.

If you work across languages all day — reading a Slack thread in English, replying to a customer in Spanish, skimming docs in Russian — you know the tax. You copy text out of the app you're in, paste it into a translation tab, copy the result, switch back, paste again. Every message is a context switch.

**TypeTide** removes that loop. It's a small macOS menu-bar app that does two things, each behind a single global shortcut, everywhere on your system.

## Two verbs: Read and Write

### Read — understand anything you select

Select text in any app, press <kbd>⌥</kbd><kbd>D</kbd>, and a popup streams the translation right next to your selection. From there you can **Copy** it or **Replace** the original in place. Prefer not to touch the keyboard? Turn on the floating icon that appears beside your selection, or go full **auto-translate** so the popup shows the moment you highlight something.

This is the everyday case: a foreign word in a code comment, a paragraph in a PDF, a chat message — read it without leaving the app.

### Write — reply in another language, in place

This is the part most translation tools miss. Type your reply in your **native** language, press <kbd>⌥</kbd><kbd>R</kbd>, and the text field is **rewritten in place** in the target language — ready to send. No popup to copy from, no round trip. TypeTide pastes the result so your app's native **undo** still works; one <kbd>⌘</kbd><kbd>Z</kbd> brings your original back.

Want to read before you send? Flip on rewrite **preview** in Settings and approve the result first.

## Local-first, or bring your own model

Apple Silicon Macs default to a built-in offline model; three backend options are available:

- **Built-in (Apple Silicon Mac)** — download TranslateGemma 4B once (about 2.22 GB), then translate offline without an account, API key, Ollama, or Python setup. Supports Faithful translation and in-place replacement; selections above the 2K input-token limit need to be shortened.
- **Ollama (local)** — run a model like `qwen2.5:3b` on your own machine. Nothing leaves your Mac, it works on a plane, and there's no per-token bill. If your configured model isn't installed, TypeTide auto-picks one that is.
- **OpenAI-compatible (cloud)** — point it at any `/chat/completions` endpoint with a base URL, key, and model name. Great when you want maximum speed or a frontier model.

The backends stream their output token by token, so translations appear as they're generated.

## Works *everywhere* — even where text APIs don't

Most "select to translate" tools break the moment you leave a native text view. TypeTide reads your selection through the macOS **Accessibility API**, and when an app doesn't expose its text (looking at you, Electron and web apps), it falls back to a synthetic copy — **always restoring your original clipboard** afterward. The result: it works in Mail, Notes, Xcode, your terminal, browsers, and Electron chat apps alike.

## Styles for the tone you need

The built-in model supports Faithful translation. Ollama and API backends also support Formal, Casual, and Polished rewriting:

- **Faithful** — a plain, accurate translation.
- **Formal** — professional tone for work email.
- **Casual** — natural, conversational, for chat.
- **Polished** — improves clarity and flow so it reads native, while keeping your meaning.

## Languages

Detection and translation cover the ten most-spoken languages: English, Chinese, Hindi, Spanish, French, Arabic, Bengali, Russian, Portuguese, and Indonesian. Pick your native language and a target once; the read direction is detected automatically.

## How it works, in one diagram

```
Select / type  ─►  Shortcut · floating icon · auto
                          │
                 Capture (AX selection ─► clipboard fallback)
                          │
              Translate (built-in MLX / Ollama / OpenAI-compatible, streaming)
                          │
        Read: popup  ·  Write: paste in place (undo-safe)
```

Under the hood it's a focused, native Swift app: a `SelectionCapture` layer over the Accessibility API, a `TranslationService` that routes to the selected provider with caching and style injection, and an undo-safe `TextReplacer`. It's menu-bar only (`LSUIElement`), so there's no Dock clutter — just global shortcuts, launch-at-login, and a per-app skip list for the apps where you don't want it.

## Get started in three steps

1. **Install.** Grab the latest `.dmg` from the [Releases](https://github.com/everettjf/typetide/releases) page and drag it to Applications.
2. **Pick a backend.** On Apple Silicon Macs, keep **Built-in (Offline)** and download the model once. Intel Macs use Ollama or an OpenAI-compatible API.
3. **Grant Accessibility.** Allow TypeTide under **System Settings → Privacy & Security → Accessibility** (it's what lets the app read selections and replace text). Then select text → <kbd>⌥</kbd><kbd>D</kbd>, or type → <kbd>⌥</kbd><kbd>R</kbd>.

Everything — shortcuts, triggers, styles, languages — is configurable in Settings.

## Why I built it

I wanted translation to feel like a system feature, not a destination. No tab to open, no paste dance, and — when I'm offline or handling something private — no data leaving my laptop. TypeTide is that: a quiet menu-bar helper that turns "go translate this" into a single keystroke.

It's **open source (MIT)**. If it saves you a few context switches a day, a ⭐️ on [GitHub](https://github.com/everettjf/typetide) is appreciated, and the [Discord](https://discord.gg/eGzEaP6TzR) is open for questions and ideas.

---

*TypeTide · system-wide AI translation & inline rewrite for macOS 26+. [Website](https://xnu.app/typetide/) · [GitHub](https://github.com/everettjf/typetide) · [中文版](introducing-typetide.zh.md)*
