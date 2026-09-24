# Windows release-candidate test matrix

Run this checklist from a clean Windows 10 or 11 VM and once on a daily-use
machine. Keep the app data isolated while testing:

```powershell
$env:TYPETIDE_DATA_DIR = "$env:TEMP\typetide-rc"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\TypeTide.exe --selftest
```

## Automated gates

- `--selftest` passes: JSON/fuzzing, ten-language detection, settings round-trip
  and sanitization, shortcut actions, shared prompt/detection contracts, and
  diagnostics privacy schema.
- Start `scripts\mock-openai.ps1`, then run `scripts\test-e2e.ps1` and
  `scripts\test-e2e-wpf.ps1`. Both clipboard fallback and UI Automation paths
  must capture, stream, replace, and restore the original clipboard.
- With a real configured backend, `--selftest-translate` passes twice; the
  second result must match the cached first result.
- Prepare the bundled runtime with `scripts\prepare-llama-runtime.ps1`, then
  run `build\TypeTide.exe --selftest-builtin` for the opt-in 2.49 GB download
  and bidirectional offline inference gate.

## Manual gates

- Fresh launch stays in first-run mode until **Test connection** succeeds and
  **Verify shortcuts** confirms at least one configured shortcut and every
  configured shortcut is registered.
- Defaults are `Alt+D` (smart popup) and `Alt+R` (smart replace). Exercise all
  six action choices: smart/native/foreign × popup/replace, plus at least one
  combination that includes the Windows key.
- Assign the same combination to both shortcuts: the UI rejects it. Reserve a
  shortcut in another app: TypeTide reports it unavailable without crashing.
- Clear either shortcut: the other still works and verification succeeds. Clear
  both: neither is registered, verification warns that at least one is required,
  and restoring defaults brings back Alt+D / Alt+R.
- Test Notepad, a Chromium/Electron app, a WPF app, and a browser text field.
  Confirm Copy, Replace, Escape/click-outside cancellation, and Ctrl+Z undo.
- Switch Windows between light and dark mode while Settings is open. Navigation,
  fields, labels, popup, and title bar remain legible and update without restart.
- Verify Ollama model discovery, missing-model auto-selection, preload, and a
  qwen3-family translation without a hidden-reasoning stall.
- Verify the built-in download progress, cancellation, SHA-256 verification,
  offline translation, Faithful-only style guard, and no Ollama process needed.
- Verify one OpenAI-compatible SSE endpoint, including an invalid key and an
  unreachable host. Errors are visible and the main UI remains responsive.
- Open Diagnostics: export valid JSON, confirm it contains no user text, URL,
  key, app name, or clipboard content, then clear it successfully.
- Enable an excluded executable and confirm shortcuts/selection triggers do
  nothing there. Toggle launch at login and verify the HKCU Run entry behavior.
- Restart Explorer and confirm the tray icon returns. Suspend/resume and repeat
  both shortcuts to catch low-level hook removal.
- Install with `TypeTide-Setup-x.y.z.exe`, launch, upgrade over it, uninstall,
  and smoke-test the portable zip. No admin prompt is expected.

Record Windows version, architecture, DPI/scaling, backend/model, and the exact
failed row for every issue. Attach `%APPDATA%\TypeTide\crashes` minidumps when
present; never attach settings or diagnostic data without reviewing it first.
