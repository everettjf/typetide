#include "SettingsWindow.h"
#include "App.h"
#include "BuiltInModel.h"
#include "LaunchAtLogin.h"
#include "LocalDiagnostics.h"
#include "OllamaModels.h"
#include "resource.h"
#include "Settings.h"
#include "Translator.h"
#include "UpdateChecker.h"
#include "Util.h"
#include "UITheme.h"
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <string>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t kClassName[] = L"TypeTideSettings";
// 工作线程取回 Ollama 模型列表：lParam = std::vector<std::wstring>*（接收方释放）
constexpr UINT kMsgOllamaModels = WM_APP + 100;
constexpr UINT kMsgUpdateResult = WM_APP + 101;
constexpr UINT kMsgBackendHealth = WM_APP + 102;
constexpr UINT kMsgBuiltInProgress = WM_APP + 103;

struct BuiltInProgress {
    double fraction = 0;
    std::wstring status;
    bool done = false;
};

enum CtrlId : int {
    kTab = 1000,
    // General
    kEnable, kLogin, kVersionLabel, kCheckUpdate, kFirstRunHint,
    // Backend
    kBackendBuiltIn, kBuiltInDownload, kBuiltInStatus, kBackendOllama, kBackendOpenAI,
    kOllamaModelLabel, kOllamaModel, kOllamaRefresh, kOllamaHint,
    kOaiProviderLabel, kOaiProvider,
    kOaiUrlLabel, kOaiUrl, kOaiKeyLabel, kOaiKey, kOaiModelLabel, kOaiModel, kOaiHint, kPrivacyHint,
    kBackendTest, kBackendStatus,
    // Language
    kNativeLabel, kNative, kForeignLabel, kForeign,
    kReadDirLabel, kReadDir, kRewriteDirLabel, kRewriteDir, kLangWarn,
    // Shortcuts
    kHkReadLabel, kHkRead, kReadWin, kHkReadClear,
    kHkRewriteLabel, kHkRewrite, kRewriteWin, kHkRewriteClear,
    kReadActionLabel, kReadAction, kRewriteActionLabel, kRewriteAction,
    kHkRestoreDefaults, kHkVerify, kHkNote,
    // Behavior
    kTriggerLabel, kTrigger, kPreview,
    kReadStyleLabel, kReadStyle, kRewriteStyleLabel, kRewriteStyle,
    // Skip apps
    kSkipEdit, kSkipAdd, kSkipList, kSkipRemove, kSkipHint,
    // Diagnostics
    kDiagPrivacy, kDiagExport, kDiagClear, kDiagPath,
    // About
    kAboutTitle, kAboutDesc, kAboutLinks,
    // Page headings (must remain contiguous)
    kPageTitle0, kPageTitle1, kPageTitle2, kPageTitle3,
    kPageTitle4, kPageTitle5, kPageTitle6, kPageTitle7,
};

struct State {
    HWND hwnd = nullptr;
    HWND appWindow = nullptr;
    HFONT font = nullptr, fontBold = nullptr, fontTitle = nullptr;
    HIMAGELIST tabImages = nullptr;
    UINT dpi = 96;
    std::vector<std::vector<HWND>> pages;  // 每个 tab 页的控件
    bool loading = false;                  // 初始化填充时不触发保存
    bool saveWarningShown = false;
    bool backendReady = false;
    bool shortcutsReady = false;
    bool dark = false;
    bool highContrast = false;
    std::shared_ptr<std::atomic<bool>> modelDownloadCancel;
    int clientWidth = 0;
    int clientHeight = 0;
};

State g;
WNDPROC gHotkeyWndProc = nullptr;

HWND ctrl(int id);

int px(int v) { return MulDiv(v, (int)g.dpi, 96); }

COLORREF pageBg() { return ui::palette(g.dark, g.highContrast).background; }
COLORREF navBg() { return g.dark ? RGB(0x19, 0x19, 0x1B) : RGB(0xF0, 0xEF, 0xF5); }
COLORREF textColor() { return ui::palette(g.dark, g.highContrast).text; }
COLORREF secondaryColor() { return ui::palette(g.dark, g.highContrast).secondary; }
HBRUSH pageBrush() { static HBRUSH light = CreateSolidBrush(RGB(0xF3, 0xF3, 0xF3)); static HBRUSH dark = CreateSolidBrush(RGB(0x20, 0x20, 0x22)); return g.dark ? dark : light; }
HBRUSH navBrush() { static HBRUSH light = CreateSolidBrush(RGB(0xF0, 0xEF, 0xF5)); static HBRUSH dark = CreateSolidBrush(RGB(0x19, 0x19, 0x1B)); return g.dark ? dark : light; }
HBRUSH editBrush() { static HBRUSH light = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF)); static HBRUSH dark = CreateSolidBrush(RGB(0x2B, 0x2B, 0x2E)); return g.dark ? dark : light; }

LRESULT CALLBACK hotkeyWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g.dark && msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, editBrush());
        HBRUSH border = CreateSolidBrush(GetFocus() == hwnd ? ui::Accent : RGB(0x58, 0x58, 0x5E));
        FrameRect(dc, &rc, border);
        DeleteObject(border);
        const WORD encoded = (WORD)SendMessageW(hwnd, HKM_GETHOTKEY, 0, 0);
        Hotkey hotkey{};
        hotkey.vk = LOBYTE(encoded);
        hotkey.modifiers = 0;
        const BYTE flags = HIBYTE(encoded);
        if (flags & HOTKEYF_CONTROL) hotkey.modifiers |= MOD_CONTROL;
        if (flags & HOTKEYF_SHIFT) hotkey.modifiers |= MOD_SHIFT;
        if (flags & HOTKEYF_ALT) hotkey.modifiers |= MOD_ALT;
        const std::wstring value = hotkey.vk ? hotkey.displayString() : L"None";
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, textColor());
        HFONT oldFont = (HFONT)SelectObject(dc, g.font);
        RECT textRect = rc;
        textRect.left += px(5);
        DrawTextW(dc, value.c_str(), -1, &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (g.dark && msg == WM_ERASEBKGND) return 1;
    return CallWindowProcW(gHotkeyWndProc, hwnd, msg, wp, lp);
}

bool systemUsesDarkMode() {
    DWORD light = 1, size = sizeof(light);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr, (BYTE*)&light, &size);
        RegCloseKey(key);
    }
    return light == 0;
}

void enableCommonControlDarkMode() {
    // Common controls do not opt into their dark renderers merely from the
    // DWM title-bar attribute.  This process-level opt-in must happen before
    // the controls are created (supported on Windows 10 1809 and newer).
    using SetPreferredAppModeFn = int (WINAPI*)(int);
    if (HMODULE ux = GetModuleHandleW(L"uxtheme.dll")) {
        auto setMode = reinterpret_cast<SetPreferredAppModeFn>(
            GetProcAddress(ux, MAKEINTRESOURCEA(135)));
        if (setMode) setMode(1); // PreferredAppMode::AllowDark
    }
}

void applyTheme(HWND hwnd) {
    if (!hwnd) return;
    BOOL dark = g.dark;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    SetWindowTheme(hwnd, g.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    EnumChildWindows(hwnd, [](HWND child, LPARAM darkParam) -> BOOL {
        const bool darkMode = darkParam != 0;
        wchar_t cls[32]{};
        GetClassNameW(child, cls, (int)std::size(cls));
        const bool isHotkey = _wcsicmp(cls, HOTKEY_CLASSW) == 0;
        const bool needsCfd = _wcsicmp(cls, L"ComboBox") == 0 ||
                              _wcsicmp(cls, L"Edit") == 0;
        // The legacy hotkey control has no dark themed renderer.  Disable its
        // theme so WM_CTLCOLOREDIT can supply our dark background and text.
        if (darkMode && isHotkey)
            SetWindowTheme(child, L"", L"");
        else
            SetWindowTheme(child, darkMode ? (needsCfd ? L"DarkMode_CFD" : L"DarkMode_Explorer")
                                           : L"Explorer", nullptr);
        return TRUE;
    }, (LPARAM)g.dark);
    RedrawWindow(hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void updateFirstRunCompletion() {
    Settings& s = Settings::shared();
    if (!s.hasCompletedFirstLaunch && g.backendReady && g.shortcutsReady) {
        s.hasCompletedFirstLaunch = true;
        s.save();
    }
    if (ctrl(kFirstRunHint)) {
        SetWindowTextW(ctrl(kFirstRunHint), s.hasCompletedFirstLaunch
            ? L"Setup checks complete. TypeTide is ready."
            : L"First run: test the backend and verify both shortcuts before relying on TypeTide.");
    }
}

HWND ctrl(int id) { return GetDlgItem(g.hwnd, id); }

HIMAGELIST createTabImages() {
    const int size = px(16);
    HIMAGELIST images = ImageList_Create(size, size, ILC_COLOR24 | ILC_MASK, 8, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    DWORD* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS,
                                      reinterpret_cast<void**>(&pixels), nullptr, 0);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    HBRUSH maskBrush = CreateSolidBrush(RGB(255, 0, 255));
    HPEN pen = CreatePen(PS_SOLID, std::max(1, px(1)), GetSysColor(COLOR_WINDOWTEXT));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    auto p = [](int value) { return px(value); };
    for (int icon = 0; icon < 8; ++icon) {
        RECT rc{0, 0, size, size};
        FillRect(dc, &rc, maskBrush);
        switch (icon) {
        case 0: // General: gear
            Ellipse(dc, p(4), p(4), p(12), p(12));
            Ellipse(dc, p(7), p(7), p(9), p(9));
            MoveToEx(dc, p(8), p(1), nullptr); LineTo(dc, p(8), p(4));
            MoveToEx(dc, p(8), p(12), nullptr); LineTo(dc, p(8), p(15));
            MoveToEx(dc, p(1), p(8), nullptr); LineTo(dc, p(4), p(8));
            MoveToEx(dc, p(12), p(8), nullptr); LineTo(dc, p(15), p(8));
            break;
        case 1: // Backend: server stack
            Rectangle(dc, p(2), p(2), p(14), p(7));
            Rectangle(dc, p(2), p(9), p(14), p(14));
            Ellipse(dc, p(4), p(4), p(6), p(6));
            Ellipse(dc, p(4), p(11), p(6), p(13));
            break;
        case 2: // Language: globe
            Ellipse(dc, p(2), p(2), p(14), p(14));
            MoveToEx(dc, p(2), p(8), nullptr); LineTo(dc, p(14), p(8));
            Ellipse(dc, p(5), p(2), p(11), p(14));
            break;
        case 3: // Shortcuts: keyboard
            Rectangle(dc, p(1), p(3), p(15), p(13));
            for (int x = 4; x <= 12; x += 4) {
                MoveToEx(dc, p(x), p(5), nullptr); LineTo(dc, p(x + 1), p(5));
                MoveToEx(dc, p(x), p(8), nullptr); LineTo(dc, p(x + 1), p(8));
            }
            MoveToEx(dc, p(4), p(11), nullptr); LineTo(dc, p(12), p(11));
            break;
        case 4: // Behavior: sliders
            MoveToEx(dc, p(2), p(4), nullptr); LineTo(dc, p(14), p(4));
            MoveToEx(dc, p(2), p(8), nullptr); LineTo(dc, p(14), p(8));
            MoveToEx(dc, p(2), p(12), nullptr); LineTo(dc, p(14), p(12));
            Ellipse(dc, p(5), p(2), p(8), p(6));
            Ellipse(dc, p(10), p(6), p(13), p(10));
            Ellipse(dc, p(3), p(10), p(6), p(14));
            break;
        case 5: // Skip apps: stacked windows
            Rectangle(dc, p(2), p(4), p(12), p(13));
            MoveToEx(dc, p(5), p(2), nullptr); LineTo(dc, p(14), p(2));
            LineTo(dc, p(14), p(11));
            break;
        case 6: // Diagnostics: pulse
            MoveToEx(dc, p(1), p(8), nullptr); LineTo(dc, p(4), p(8));
            LineTo(dc, p(6), p(3)); LineTo(dc, p(9), p(13));
            LineTo(dc, p(11), p(8)); LineTo(dc, p(15), p(8));
            break;
        case 7: // About: info
            Ellipse(dc, p(2), p(2), p(14), p(14));
            Ellipse(dc, p(7), p(4), p(9), p(6));
            MoveToEx(dc, p(8), p(7), nullptr); LineTo(dc, p(8), p(12));
            break;
        }
        std::vector<BYTE> andMask(((size + 15) / 16) * 2 * size, 0xFF);
        std::vector<BYTE> xorMask(andMask.size(), 0x00);
        const int rowBytes = ((size + 15) / 16) * 2;
        for (int yy = 0; yy < size; ++yy) {
            for (int xx = 0; xx < size; ++xx) {
                if ((pixels[yy * size + xx] & 0x00FFFFFF) != 0x00FF00FF)
                    andMask[yy * rowBytes + xx / 8] &= static_cast<BYTE>(~(0x80 >> (xx % 8)));
            }
        }
        HICON iconHandle = CreateIcon(GetModuleHandleW(nullptr), size, size, 1, 1,
                                      andMask.data(), xorMask.data());
        ImageList_ReplaceIcon(images, -1, iconHandle);
        DestroyIcon(iconHandle);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBitmap);
    DeleteObject(pen);
    DeleteObject(maskBrush);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return images;
}

void applyToApp() {
    if (g.appWindow) {
        // 直接调用（同线程）重装配热键 / 划词触发
        App::shared().applyEnabledState();
    }
}

// ---------- 控件创建辅助 ----------

HWND make(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h,
          int id, DWORD exStyle = 0) {
    HWND c = CreateWindowExW(exStyle, cls, text, WS_CHILD | style, px(x), px(y), px(w), px(h),
                             g.hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    if (_wcsicmp(cls, HOTKEY_CLASSW) == 0) {
        WNDPROC original = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(c, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hotkeyWndProc)));
        if (!gHotkeyWndProc) gHotkeyWndProc = original;
    }
    SendMessageW(c, WM_SETFONT, (WPARAM)g.font, TRUE);
    return c;
}

HWND makeLabel(const wchar_t* text, int x, int y, int w, int id, bool bold = false) {
    HWND c = make(L"STATIC", text, SS_LEFT, x, y, w, 18, id);
    if (bold) SendMessageW(c, WM_SETFONT, (WPARAM)g.fontBold, TRUE);
    return c;
}

void fillLanguageCombo(HWND combo, Language selected) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (Language l : lang::All) {
        int idx = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)lang::DisplayName(l));
        if (l == selected) SendMessageW(combo, CB_SETCURSEL, idx, 0);
    }
}

Language comboLanguage(HWND combo) {
    int idx = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (idx < 0 || idx >= (int)std::size(lang::All)) return Language::English;
    return lang::All[idx];
}

void fillDirectionCombo(HWND combo, TranslationDirection selected) {
    const Settings& s = Settings::shared();
    std::wstring native = lang::ShortName(s.nativeLanguage);
    std::wstring foreign = lang::ShortName(s.foreignLanguage);
    std::wstring items[] = {
        L"Auto · bidirectional",
        native + L" → " + foreign,
        foreign + L" → " + native,
    };
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (auto& it : items) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)it.c_str());
    SendMessageW(combo, CB_SETCURSEL, (int)selected, 0);
}

void fillStyleCombo(HWND combo, RewriteStyle selected) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    const RewriteStyle all[] = {RewriteStyle::Faithful, RewriteStyle::Formal,
                                RewriteStyle::Casual, RewriteStyle::Polished};
    for (RewriteStyle st : all) {
        int idx = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)lang::StyleDisplayName(st));
        if (st == selected) SendMessageW(combo, CB_SETCURSEL, idx, 0);
    }
}

RewriteStyle comboStyle(HWND combo) {
    const RewriteStyle all[] = {RewriteStyle::Faithful, RewriteStyle::Formal,
                                RewriteStyle::Casual, RewriteStyle::Polished};
    int idx = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return (idx >= 0 && idx < 4) ? all[idx] : RewriteStyle::Faithful;
}

void fillShortcutActionCombo(HWND combo, ShortcutAction selected) {
    const wchar_t* names[] = {
        L"Smart translate · popup", L"Translate to native language · popup",
        L"Translate to foreign language · popup", L"Smart translate · replace",
        L"Translate to native language · replace", L"Translate to foreign language · replace",
    };
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto* name : names) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)name);
    SendMessageW(combo, CB_SETCURSEL, (int)selected, 0);
}

std::wstring editText(HWND edit) {
    int n = GetWindowTextLengthW(edit);
    std::wstring s(n, 0);
    if (n) GetWindowTextW(edit, s.data(), n + 1);
    return s;
}

// HOTKEY 控件 ⇄ RegisterHotKey 修饰键转换
WORD hotkeyToControl(const Hotkey& hk) {
    BYTE flags = 0;
    if (hk.modifiers & MOD_SHIFT) flags |= HOTKEYF_SHIFT;
    if (hk.modifiers & MOD_CONTROL) flags |= HOTKEYF_CONTROL;
    if (hk.modifiers & MOD_ALT) flags |= HOTKEYF_ALT;
    return MAKEWORD(hk.vk, flags);
}

Hotkey hotkeyFromControl(WORD w) {
    Hotkey hk;
    hk.vk = LOBYTE(w);
    BYTE flags = HIBYTE(w);
    hk.modifiers = 0;
    if (flags & HOTKEYF_SHIFT) hk.modifiers |= MOD_SHIFT;
    if (flags & HOTKEYF_CONTROL) hk.modifiers |= MOD_CONTROL;
    if (flags & HOTKEYF_ALT) hk.modifiers |= MOD_ALT;
    return hk;
}

void updateShortcutControls() {
    const Settings& s = Settings::shared();
    EnableWindow(ctrl(kReadWin), s.readShortcut.isConfigured());
    EnableWindow(ctrl(kRewriteWin), s.rewriteShortcut.isConfigured());
    EnableWindow(ctrl(kHkReadClear), s.readShortcut.isConfigured());
    EnableWindow(ctrl(kHkRewriteClear), s.rewriteShortcut.isConfigured());
}

// ---------- 页构建 ----------

void updateBackendEnabled() {
    const Settings& s = Settings::shared();
    bool ollama = s.backend == TranslationBackend::Ollama;
    bool cloud = s.backend == TranslationBackend::OpenAI;
    EnableWindow(ctrl(kOllamaModel), ollama);
    EnableWindow(ctrl(kOllamaRefresh), ollama);
    EnableWindow(ctrl(kOaiProvider), cloud);
    EnableWindow(ctrl(kOaiUrl), cloud);
    EnableWindow(ctrl(kOaiKey), cloud);
    EnableWindow(ctrl(kOaiModel), cloud);
}

void updateBuiltInStatus() {
    if (!g.hwnd || !ctrl(kBuiltInDownload)) return;
    const bool downloading = g.modelDownloadCancel != nullptr;
    SetWindowTextW(ctrl(kBuiltInDownload), downloading ? L"Cancel download" :
                   builtin::Installed() ? L"Model ready" : L"Download model · 2.49 GB");
    EnableWindow(ctrl(kBuiltInDownload), downloading || !builtin::Installed());
    if (!downloading)
        SetWindowTextW(ctrl(kBuiltInStatus), builtin::Installed()
            ? L"Ready for private offline translation · Faithful only"
            : L"One-time download from Hugging Face; selected text stays on this PC.");
}

int cloudProviderIndex(const std::string& baseUrl) {
    if (baseUrl.find("api.openai.com") != std::string::npos) return 0;
    if (baseUrl.find("openrouter.ai") != std::string::npos) return 1;
    if (baseUrl.find("api.deepseek.com") != std::string::npos) return 2;
    return 3;
}

void applyCloudProviderPreset(int index) {
    Settings& s = Settings::shared();
    switch (index) {
    case 0:
        s.openAIBaseURL = "https://api.openai.com/v1";
        s.openAIModel = "gpt-5-mini";
        break;
    case 1:
        s.openAIBaseURL = "https://openrouter.ai/api/v1";
        s.openAIModel = "openai/gpt-5-mini";
        break;
    case 2:
        s.openAIBaseURL = "https://api.deepseek.com";
        s.openAIModel = "deepseek-v4-flash";
        break;
    default:
        return;
    }
    SetWindowTextW(ctrl(kOaiUrl), util::Widen(s.openAIBaseURL).c_str());
    SetWindowTextW(ctrl(kOaiModel), util::Widen(s.openAIModel).c_str());
}

/// 后台拉取已装模型列表，回主线程填充下拉框（Settings 快照在主线程拍好）
void fetchOllamaModelsAsync() {
    ollamamodels::FetchInstalledAsync(g.hwnd, kMsgOllamaModels);
}

void updateLangWarn() {
    const Settings& s = Settings::shared();
    SetWindowTextW(ctrl(kLangWarn),
                   s.nativeLanguage == s.foreignLanguage
                       ? L"⚠ Native and foreign are the same language — translation won't do anything useful."
                       : L"TypeTide translates between your native and foreign language. Auto direction detects the text and translates the other way.");
}

void buildPages() {
    const Settings& s = Settings::shared();
    g.pages.assign(8, {});
    const int x = 222, w = 560;
    int y;
    const wchar_t* pageTitles[] = {
        L"General", L"Translation backend", L"Languages", L"Keyboard shortcuts",
        L"Behavior", L"Excluded apps", L"Diagnostics", L"About TypeTide"
    };
    for (int page = 0; page < 8; ++page) {
        HWND title = makeLabel(pageTitles[page], x, 26, w, kPageTitle0 + page);
        SendMessageW(title, WM_SETFONT, (WPARAM)g.fontTitle, TRUE);
        SetWindowPos(title, nullptr, px(x), px(26), px(w), px(36), SWP_NOZORDER);
        g.pages[page].push_back(title);
    }

    // --- 0 General ---
    y = 82;
    {
    std::vector<HWND> controls = {
        makeLabel(L"APP STATUS", x, y, w, -1, true),
        make(L"BUTTON", L"Enable TypeTide", WS_TABSTOP | BS_AUTOCHECKBOX, x, y + 26, 180, 22, kEnable),
        makeLabel(L"STARTUP", x, y + 62, w, -1, true),
        make(L"BUTTON", L"Launch at login", WS_TABSTOP | BS_AUTOCHECKBOX, x, y + 88, 180, 22, kLogin),
        makeLabel(L"UPDATES", x, y + 150, w, -1, true),
        makeLabel(L"Version " TYPETIDE_VERSION_STRING, x, y + 178, 200, kVersionLabel),
        make(L"BUTTON", L"Check for updates", WS_TABSTOP | BS_PUSHBUTTON, x, y + 206, 150, 32, kCheckUpdate),
        makeLabel(L"", x, y + 258, w, kFirstRunHint),
    };
    g.pages[0].insert(g.pages[0].end(), controls.begin(), controls.end());
    }
    CheckDlgButton(g.hwnd, kEnable, s.enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g.hwnd, kLogin, launchatlogin::IsEnabled() ? BST_CHECKED : BST_UNCHECKED);
    updateFirstRunCompletion();

    // --- 1 Backend ---
    y = 82;
    {
    std::vector<HWND> controls = {
        makeLabel(L"PROVIDER", x, y, w, -1, true),
        make(L"BUTTON", L"Built-in (Offline) — TranslateGemma 4B", WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
             x, y + 26, 350, 22, kBackendBuiltIn),
        make(L"BUTTON", L"Download model · 2.49 GB", WS_TABSTOP | BS_PUSHBUTTON,
             x + 20, y + 52, 225, 30, kBuiltInDownload),
        makeLabel(L"One-time download from Hugging Face; selected text stays on this PC.",
                  x + 20, y + 87, w - 20, kBuiltInStatus),
        make(L"BUTTON", L"Local (Ollama) — private, offline", WS_TABSTOP | BS_AUTORADIOBUTTON,
             x, y + 120, 300, 22, kBackendOllama),
        makeLabel(L"Ollama model", x + 20, y + 150, 90, kOllamaModelLabel),
        // 可编辑下拉框：列出已装模型，也允许手输
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWN | WS_VSCROLL, x + 120, y + 146, 250, 240, kOllamaModel),
        make(L"BUTTON", L"Refresh", WS_TABSTOP | BS_PUSHBUTTON, x + 380, y + 145, 80, 32, kOllamaRefresh),
        makeLabel(L"Ollama at http://127.0.0.1:11434 — run `ollama serve` and pull a model first.",
                  x + 20, y + 179, w - 20, kOllamaHint),
        make(L"BUTTON", L"OpenAI-compatible API", WS_TABSTOP | BS_AUTORADIOBUTTON,
             x, y + 209, 260, 22, kBackendOpenAI),
        makeLabel(L"Provider", x + 20, y + 238, 90, kOaiProviderLabel),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x + 120, y + 234, 250, 180, kOaiProvider),
        makeLabel(L"Base URL", x + 20, y + 272, 90, kOaiUrlLabel),
        make(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, x + 120, y + 268, 400, 30, kOaiUrl),
        makeLabel(L"API key", x + 20, y + 306, 90, kOaiKeyLabel),
        make(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD, x + 120, y + 302, 400, 30, kOaiKey),
        makeLabel(L"API model", x + 20, y + 340, 90, kOaiModelLabel),
        make(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, x + 120, y + 336, 280, 30, kOaiModel),
        makeLabel(L"API key is stored securely in Windows Credential Manager.",
                  x + 20, y + 371, w - 20, kOaiHint),
        makeLabel(L"Privacy: triggered selections are sent to the configured cloud endpoint. "
                  L"Use a local backend or exclude sensitive apps for private text.",
                  x + 20, y + 391, w - 20, kPrivacyHint),
        make(L"BUTTON", L"Test connection", WS_TABSTOP | BS_PUSHBUTTON,
             x + 20, y + 422, 130, 30, kBackendTest),
        makeLabel(L"Uses fixed synthetic text, never your clipboard.",
                  x + 165, y + 428, w - 165, kBackendStatus),
    };
    g.pages[1].insert(g.pages[1].end(), controls.begin(), controls.end());
    }
    CheckDlgButton(g.hwnd, s.backend == TranslationBackend::BuiltIn ? kBackendBuiltIn
                        : s.backend == TranslationBackend::Ollama ? kBackendOllama : kBackendOpenAI, BST_CHECKED);
    SetWindowTextW(ctrl(kOllamaModel), util::Widen(s.ollamaModel).c_str());
    SetWindowTextW(ctrl(kOaiUrl), util::Widen(s.openAIBaseURL).c_str());
    SetWindowTextW(ctrl(kOaiKey), util::Widen(s.openAIKey).c_str());
    SetWindowTextW(ctrl(kOaiModel), util::Widen(s.openAIModel).c_str());
    {
        HWND provider = ctrl(kOaiProvider);
        const wchar_t* providers[] = {L"OpenAI", L"OpenRouter", L"DeepSeek", L"Custom endpoint"};
        for (auto* name : providers) SendMessageW(provider, CB_ADDSTRING, 0, (LPARAM)name);
        SendMessageW(provider, CB_SETCURSEL, cloudProviderIndex(s.openAIBaseURL), 0);
    }
    updateBackendEnabled();
    updateBuiltInStatus();
    SetWindowPos(ctrl(kPrivacyHint), nullptr,
                 px(x + 20), px(y + 391), px(w - 20), px(35), SWP_NOZORDER);

    // --- 2 Language ---
    y = 82;
    {
    std::vector<HWND> controls = {
        makeLabel(L"Native language:", x, y + 3, 120, kNativeLabel),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, x + 140, y, 240, 300, kNative),
        makeLabel(L"Foreign language:", x, y + 37, 120, kForeignLabel),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, x + 140, y + 34, 240, 300, kForeign),
        makeLabel(L"Read direction:", x, y + 85, 120, kReadDirLabel),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x + 140, y + 82, 240, 200, kReadDir),
        makeLabel(L"Rewrite direction:", x, y + 119, 120, kRewriteDirLabel),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x + 140, y + 116, 240, 200, kRewriteDir),
        makeLabel(L"", x, y + 156, w, kLangWarn),
    };
    g.pages[2].insert(g.pages[2].end(), controls.begin(), controls.end());
    }
    fillLanguageCombo(ctrl(kNative), s.nativeLanguage);
    fillLanguageCombo(ctrl(kForeign), s.foreignLanguage);
    fillDirectionCombo(ctrl(kReadDir), s.readDirection);
    fillDirectionCombo(ctrl(kRewriteDir), s.rewriteDirection);
    updateLangWarn();
    // 警告区两行高
    SetWindowPos(ctrl(kLangWarn), nullptr, px(x), px(y + 156), px(w), px(40), SWP_NOZORDER);

    // --- 3 Shortcuts ---
    y = 82;
    {
    std::vector<HWND> controls = {
        makeLabel(L"Translate selection (read):", x, y + 4, 180, kHkReadLabel),
        make(HOTKEY_CLASSW, L"", WS_TABSTOP, x + 195, y, 155, 24, kHkRead),
        make(L"BUTTON", L"Win key", WS_TABSTOP | BS_AUTOCHECKBOX, x + 365, y + 1, 80, 22, kReadWin),
        make(L"BUTTON", L"Clear", WS_TABSTOP | BS_PUSHBUTTON, x + 455, y, 64, 24, kHkReadClear),
        makeLabel(L"Rewrite && replace (write):", x, y + 38, 180, kHkRewriteLabel),
        make(HOTKEY_CLASSW, L"", WS_TABSTOP, x + 195, y + 34, 155, 24, kHkRewrite),
        make(L"BUTTON", L"Win key", WS_TABSTOP | BS_AUTOCHECKBOX, x + 365, y + 35, 80, 22, kRewriteWin),
        make(L"BUTTON", L"Clear", WS_TABSTOP | BS_PUSHBUTTON, x + 455, y + 34, 64, 24, kHkRewriteClear),
        makeLabel(L"Translate action:", x, y + 76, 180, kReadActionLabel),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x + 195, y + 72, 300, 220, kReadAction),
        makeLabel(L"Rewrite action:", x, y + 110, 180, kRewriteActionLabel),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x + 195, y + 106, 300, 220, kRewriteAction),
        make(L"BUTTON", L"Restore defaults", WS_TABSTOP | BS_PUSHBUTTON, x, y + 150, 120, 28, kHkRestoreDefaults),
        make(L"BUTTON", L"Verify shortcuts", WS_TABSTOP | BS_PUSHBUTTON, x + 130, y + 150, 120, 28, kHkVerify),
        makeLabel(L"Changes apply immediately. Keep at least one shortcut; configured shortcuts must be different.",
                  x, y + 190, w, kHkNote),
    };
    g.pages[3].insert(g.pages[3].end(), controls.begin(), controls.end());
    }
    SendMessageW(ctrl(kHkRead), HKM_SETHOTKEY, hotkeyToControl(s.readShortcut), 0);
    SendMessageW(ctrl(kHkRewrite), HKM_SETHOTKEY, hotkeyToControl(s.rewriteShortcut), 0);
    CheckDlgButton(g.hwnd, kReadWin, s.readShortcut.modifiers & MOD_WIN ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g.hwnd, kRewriteWin, s.rewriteShortcut.modifiers & MOD_WIN ? BST_CHECKED : BST_UNCHECKED);
    updateShortcutControls();
    fillShortcutActionCombo(ctrl(kReadAction), s.readShortcutAction);
    fillShortcutActionCombo(ctrl(kRewriteAction), s.rewriteShortcutAction);

    // --- 4 Behavior ---
    y = 82;
    {
    std::vector<HWND> controls = {
        makeLabel(L"On text selection:", x, y + 3, 130, kTriggerLabel),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x + 150, y, 230, 200, kTrigger),
        make(L"BUTTON", L"Preview before replacing (rewrite)", WS_TABSTOP | BS_AUTOCHECKBOX,
             x, y + 44, w, 22, kPreview),
        makeLabel(L"Read style:", x, y + 87, 130, kReadStyleLabel),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x + 150, y + 84, 230, 200, kReadStyle),
        makeLabel(L"Rewrite style:", x, y + 121, 130, kRewriteStyleLabel),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x + 150, y + 118, 230, 200, kRewriteStyle),
    };
    g.pages[4].insert(g.pages[4].end(), controls.begin(), controls.end());
    }
    {
        HWND trig = ctrl(kTrigger);
        const wchar_t* items[] = {L"Off (shortcut only)", L"Show floating icon", L"Auto-translate"};
        for (auto* it : items) SendMessageW(trig, CB_ADDSTRING, 0, (LPARAM)it);
        SendMessageW(trig, CB_SETCURSEL, (int)s.selectionTrigger, 0);
    }
    CheckDlgButton(g.hwnd, kPreview, s.rewritePreview ? BST_CHECKED : BST_UNCHECKED);
    fillStyleCombo(ctrl(kReadStyle), s.readStyle);
    fillStyleCombo(ctrl(kRewriteStyle), s.rewriteStyle);

    // --- 5 Skip Apps ---
    y = 82;
    {
    std::vector<HWND> controls = {
        make(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, x, y, 280, 24, kSkipEdit),
        make(L"BUTTON", L"Add", WS_TABSTOP | BS_PUSHBUTTON, x + 290, y - 1, 70, 26, kSkipAdd),
        make(L"LISTBOX", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, x, y + 34, 360, 240, kSkipList),
        make(L"BUTTON", L"Remove", WS_TABSTOP | BS_PUSHBUTTON, x + 370, y + 34, 80, 26, kSkipRemove),
        makeLabel(L"TypeTide stays inactive in these apps (executable name, e.g. notepad.exe or code.exe).",
                  x, y + 284, w, kSkipHint),
    };
    g.pages[5].insert(g.pages[5].end(), controls.begin(), controls.end());
    }
    for (auto& app : s.skipApps)
        SendMessageW(ctrl(kSkipList), LB_ADDSTRING, 0, (LPARAM)app.c_str());

    // --- 6 Diagnostics ---
    y = 82;
    {
    std::vector<HWND> controls = {
        makeLabel(L"Stored only on this PC. Never includes selected text, translations, clipboard contents, app names, URLs, or credentials.",
                  x, y, w, kDiagPrivacy),
        makeLabel(L"Report file:", x, y + 52, 90, -1),
        makeLabel(diagnostics::Path().c_str(), x + 90, y + 52, w - 90, kDiagPath),
        make(L"BUTTON", L"Export report…", WS_TABSTOP | BS_PUSHBUTTON, x, y + 92, 130, 30, kDiagExport),
        make(L"BUTTON", L"Clear diagnostics", WS_TABSTOP | BS_PUSHBUTTON, x + 142, y + 92, 130, 30, kDiagClear),
    };
    g.pages[6].insert(g.pages[6].end(), controls.begin(), controls.end());
    }
    SetWindowPos(ctrl(kDiagPrivacy), nullptr, px(x), px(y), px(w), px(40), SWP_NOZORDER);

    // --- 7 About ---
    y = 82;
    {
    std::vector<HWND> controls = {
        makeLabel(L"TypeTide " TYPETIDE_VERSION_STRING, x, y, 300, kAboutTitle, true),
        makeLabel(L"System-wide AI translation && inline rewrite for Windows.\n"
                  L"Private local models or secure OpenAI-compatible cloud APIs.",
                  x, y + 28, w, kAboutDesc),
        make(L"SysLink",
             L"<a href=\"https://github.com/everettjf/typetide\">GitHub</a>   ·   "
             L"<a href=\"https://xnu.app/TypeTide/\">Website</a>   ·   "
             L"<a href=\"https://discord.gg/eGzEaP6TzR\">Discord</a>",
             WS_TABSTOP, x, y + 84, w, 22, kAboutLinks),
    };
    g.pages[7].insert(g.pages[7].end(), controls.begin(), controls.end());
    }
    // 描述区两行高
    SetWindowPos(ctrl(kAboutDesc), nullptr, px(x), px(y + 28), px(w), px(40), SWP_NOZORDER);
}

void showPage(int index) {
    for (int p = 0; p < (int)g.pages.size(); ++p)
        for (HWND c : g.pages[p])
            ShowWindow(c, p == index ? SW_SHOW : SW_HIDE);
}

void layoutForClientSize(int width, int height) {
    if (!g.hwnd || width <= 0 || height <= 0) return;
    if (g.clientWidth == 0 || g.clientHeight == 0) {
        g.clientWidth = width;
        g.clientHeight = height;
        return;
    }

    const int dx = width - g.clientWidth;
    const int dy = height - g.clientHeight;
    if (dx == 0 && dy == 0) return;

    HWND nav = ctrl(kTab);
    if (nav) {
        RECT r{};
        GetWindowRect(nav, &r);
        MapWindowPoints(nullptr, g.hwnd, reinterpret_cast<POINT*>(&r), 2);
        const int navWidth = static_cast<int>(r.right - r.left);
        const int navHeight = std::max(px(300), static_cast<int>(r.bottom - r.top) + dy);
        SetWindowPos(nav, nullptr, r.left, r.top, navWidth, navHeight, SWP_NOZORDER);
    }

    for (const auto& page : g.pages) {
        for (HWND control : page) {
            RECT r{};
            GetWindowRect(control, &r);
            MapWindowPoints(nullptr, g.hwnd, reinterpret_cast<POINT*>(&r), 2);
            int controlWidth = r.right - r.left;
            int controlHeight = r.bottom - r.top;
            const int rightGap = g.clientWidth - r.right;
            const int id = GetDlgCtrlID(control);
            if (rightGap <= px(48) || (id >= kPageTitle0 && id <= kPageTitle7))
                controlWidth = std::max(px(80), controlWidth + dx);
            if (id == kSkipList)
                controlHeight = std::max(px(120), controlHeight + dy);
            SetWindowPos(control, nullptr, r.left, r.top, controlWidth, controlHeight,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    g.clientWidth = width;
    g.clientHeight = height;
    InvalidateRect(g.hwnd, nullptr, TRUE);
}

// ---------- 变更处理 ----------

void onCommand(int id, int code) {
    if (g.loading) return;
    Settings& s = Settings::shared();
    bool save = true, reapply = false;

    switch (id) {
    case kTab:
        if (code == LBN_SELCHANGE)
            showPage((int)SendMessageW(ctrl(kTab), LB_GETCURSEL, 0, 0));
        return;
    case kEnable:
        s.enabled = IsDlgButtonChecked(g.hwnd, kEnable) == BST_CHECKED;
        reapply = true;
        break;
    case kLogin:
        launchatlogin::SetEnabled(IsDlgButtonChecked(g.hwnd, kLogin) == BST_CHECKED);
        save = false;
        break;
    case kCheckUpdate:
        EnableWindow(ctrl(kCheckUpdate), FALSE);
        SetWindowTextW(ctrl(kCheckUpdate), L"Checking…");
        updatechecker::CheckNowInteractive(g.hwnd, kMsgUpdateResult);
        save = false;
        break;

    case kBackendBuiltIn:
    case kBackendOllama:
    case kBackendOpenAI:
        s.backend = id == kBackendBuiltIn ? TranslationBackend::BuiltIn
                  : id == kBackendOllama ? TranslationBackend::Ollama : TranslationBackend::OpenAI;
        updateBackendEnabled();
        break;
    case kBuiltInDownload:
        if (g.modelDownloadCancel) {
            g.modelDownloadCancel->store(true);
        } else if (!builtin::Installed()) {
            auto cancel = std::make_shared<std::atomic<bool>>(false);
            g.modelDownloadCancel = cancel;
            updateBuiltInStatus();
            HWND target = g.hwnd;
            std::thread([cancel, target] {
                std::wstring error;
                auto send = [target](double fraction, const std::wstring& status, bool done) {
                    auto* payload = new BuiltInProgress{fraction, status, done};
                    if (!PostMessageW(target, kMsgBuiltInProgress, 0, (LPARAM)payload)) delete payload;
                };
                bool ok = builtin::Download(*cancel, [&](double fraction, const std::wstring& status) {
                    send(fraction, status, false);
                }, &error);
                send(ok ? 1.0 : 0.0, ok ? L"Model ready for offline translation" : error, true);
            }).detach();
        }
        save = false;
        break;
    case kOllamaModel:
        if (code == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(ctrl(kOllamaModel), CB_GETCURSEL, 0, 0);
            if (sel < 0) return;
            int len = (int)SendMessageW(ctrl(kOllamaModel), CB_GETLBTEXTLEN, sel, 0);
            std::wstring m(len, 0);
            SendMessageW(ctrl(kOllamaModel), CB_GETLBTEXT, sel, (LPARAM)m.data());
            s.ollamaModel = util::Narrow(m);
        } else if (code == CBN_EDITCHANGE) {
            s.ollamaModel = util::Narrow(editText(ctrl(kOllamaModel)));
        } else {
            return;
        }
        break;
    case kOllamaRefresh:
        fetchOllamaModelsAsync();
        save = false;
        break;
    case kOaiProvider:
        if (code != CBN_SELCHANGE) return;
        applyCloudProviderPreset((int)SendMessageW(ctrl(kOaiProvider), CB_GETCURSEL, 0, 0));
        break;
    case kOaiUrl:
        if (code != EN_CHANGE) return;
        s.openAIBaseURL = util::Narrow(editText(ctrl(kOaiUrl)));
        break;
    case kOaiKey:
        if (code != EN_KILLFOCUS) return;
        s.openAIKey = util::Narrow(editText(ctrl(kOaiKey)));
        break;
    case kOaiModel:
        if (code != EN_CHANGE) return;
        s.openAIModel = util::Narrow(editText(ctrl(kOaiModel)));
        break;
    case kBackendTest:
        EnableWindow(ctrl(kBackendTest), FALSE);
        SetWindowTextW(ctrl(kBackendTest), L"Testing…");
        SetWindowTextW(ctrl(kBackendStatus), L"Connecting and translating synthetic text…");
        {
        HWND target = g.hwnd;
        translator::CheckHealthAsync([target](translator::HealthCheckResult result) {
            auto* payload = new translator::HealthCheckResult(std::move(result));
            if (!PostMessageW(target, kMsgBackendHealth, 0, (LPARAM)payload)) delete payload;
        });
        }
        save = false;
        break;

    case kNative:
        if (code != CBN_SELCHANGE) return;
        s.nativeLanguage = comboLanguage(ctrl(kNative));
        fillDirectionCombo(ctrl(kReadDir), s.readDirection);
        fillDirectionCombo(ctrl(kRewriteDir), s.rewriteDirection);
        updateLangWarn();
        break;
    case kForeign:
        if (code != CBN_SELCHANGE) return;
        s.foreignLanguage = comboLanguage(ctrl(kForeign));
        fillDirectionCombo(ctrl(kReadDir), s.readDirection);
        fillDirectionCombo(ctrl(kRewriteDir), s.rewriteDirection);
        updateLangWarn();
        break;
    case kReadDir:
        if (code != CBN_SELCHANGE) return;
        s.readDirection = (TranslationDirection)SendMessageW(ctrl(kReadDir), CB_GETCURSEL, 0, 0);
        break;
    case kRewriteDir:
        if (code != CBN_SELCHANGE) return;
        s.rewriteDirection = (TranslationDirection)SendMessageW(ctrl(kRewriteDir), CB_GETCURSEL, 0, 0);
        break;

    case kHkRead:
        if (code != EN_CHANGE) return;
        {
            Hotkey hk = hotkeyFromControl((WORD)SendMessageW(ctrl(kHkRead), HKM_GETHOTKEY, 0, 0));
            if (hk.vk == 0) {
                s.readShortcut = {0, 0};
                CheckDlgButton(g.hwnd, kReadWin, BST_UNCHECKED);
                updateShortcutControls();
                reapply = true;
                break;
            }
            if (IsDlgButtonChecked(g.hwnd, kReadWin) == BST_CHECKED) hk.modifiers |= MOD_WIN;
            if (s.rewriteShortcut.isConfigured() && hk == s.rewriteShortcut) {
                MessageBoxW(g.hwnd, L"Translate and rewrite must use different shortcuts.",
                            L"TypeTide — duplicate shortcut", MB_OK | MB_ICONWARNING);
                SendMessageW(ctrl(kHkRead), HKM_SETHOTKEY, hotkeyToControl(s.readShortcut), 0);
                return;
            }
            s.readShortcut = hk;
            updateShortcutControls();
            reapply = true;
        }
        break;
    case kHkRewrite:
        if (code != EN_CHANGE) return;
        {
            Hotkey hk = hotkeyFromControl((WORD)SendMessageW(ctrl(kHkRewrite), HKM_GETHOTKEY, 0, 0));
            if (hk.vk == 0) {
                s.rewriteShortcut = {0, 0};
                CheckDlgButton(g.hwnd, kRewriteWin, BST_UNCHECKED);
                updateShortcutControls();
                reapply = true;
                break;
            }
            if (IsDlgButtonChecked(g.hwnd, kRewriteWin) == BST_CHECKED) hk.modifiers |= MOD_WIN;
            if (s.readShortcut.isConfigured() && hk == s.readShortcut) {
                MessageBoxW(g.hwnd, L"Translate and rewrite must use different shortcuts.",
                            L"TypeTide — duplicate shortcut", MB_OK | MB_ICONWARNING);
                SendMessageW(ctrl(kHkRewrite), HKM_SETHOTKEY, hotkeyToControl(s.rewriteShortcut), 0);
                return;
            }
            s.rewriteShortcut = hk;
            updateShortcutControls();
            reapply = true;
        }
        break;
    case kReadWin:
        if (!s.readShortcut.isConfigured()) {
            CheckDlgButton(g.hwnd, kReadWin, BST_UNCHECKED);
            return;
        }
        if (IsDlgButtonChecked(g.hwnd, kReadWin) == BST_CHECKED) s.readShortcut.modifiers |= MOD_WIN;
        else s.readShortcut.modifiers &= ~MOD_WIN;
        if (s.rewriteShortcut.isConfigured() && s.readShortcut == s.rewriteShortcut) {
            MessageBoxW(g.hwnd, L"Translate and rewrite must use different shortcuts.",
                        L"TypeTide — duplicate shortcut", MB_OK | MB_ICONWARNING);
            s.readShortcut.modifiers ^= MOD_WIN;
            CheckDlgButton(g.hwnd, kReadWin,
                           s.readShortcut.modifiers & MOD_WIN ? BST_CHECKED : BST_UNCHECKED);
            return;
        }
        reapply = true;
        break;
    case kRewriteWin:
        if (!s.rewriteShortcut.isConfigured()) {
            CheckDlgButton(g.hwnd, kRewriteWin, BST_UNCHECKED);
            return;
        }
        if (IsDlgButtonChecked(g.hwnd, kRewriteWin) == BST_CHECKED) s.rewriteShortcut.modifiers |= MOD_WIN;
        else s.rewriteShortcut.modifiers &= ~MOD_WIN;
        if (s.readShortcut.isConfigured() && s.readShortcut == s.rewriteShortcut) {
            MessageBoxW(g.hwnd, L"Translate and rewrite must use different shortcuts.",
                        L"TypeTide — duplicate shortcut", MB_OK | MB_ICONWARNING);
            s.rewriteShortcut.modifiers ^= MOD_WIN;
            CheckDlgButton(g.hwnd, kRewriteWin,
                           s.rewriteShortcut.modifiers & MOD_WIN ? BST_CHECKED : BST_UNCHECKED);
            return;
        }
        reapply = true;
        break;
    case kHkReadClear:
        s.readShortcut = {0, 0};
        SendMessageW(ctrl(kHkRead), HKM_SETHOTKEY, 0, 0);
        CheckDlgButton(g.hwnd, kReadWin, BST_UNCHECKED);
        updateShortcutControls();
        reapply = true;
        break;
    case kHkRewriteClear:
        s.rewriteShortcut = {0, 0};
        SendMessageW(ctrl(kHkRewrite), HKM_SETHOTKEY, 0, 0);
        CheckDlgButton(g.hwnd, kRewriteWin, BST_UNCHECKED);
        updateShortcutControls();
        reapply = true;
        break;
    case kReadAction:
        if (code != CBN_SELCHANGE) return;
        s.readShortcutAction = (ShortcutAction)SendMessageW(ctrl(kReadAction), CB_GETCURSEL, 0, 0);
        break;
    case kRewriteAction:
        if (code != CBN_SELCHANGE) return;
        s.rewriteShortcutAction = (ShortcutAction)SendMessageW(ctrl(kRewriteAction), CB_GETCURSEL, 0, 0);
        break;
    case kHkRestoreDefaults:
        s.readShortcut = {MOD_ALT, 'D'};
        s.rewriteShortcut = {MOD_ALT, 'R'};
        s.readShortcutAction = ShortcutAction::SmartPopup;
        s.rewriteShortcutAction = ShortcutAction::SmartReplace;
        SendMessageW(ctrl(kHkRead), HKM_SETHOTKEY, hotkeyToControl(s.readShortcut), 0);
        SendMessageW(ctrl(kHkRewrite), HKM_SETHOTKEY, hotkeyToControl(s.rewriteShortcut), 0);
        CheckDlgButton(g.hwnd, kReadWin, BST_UNCHECKED);
        CheckDlgButton(g.hwnd, kRewriteWin, BST_UNCHECKED);
        fillShortcutActionCombo(ctrl(kReadAction), s.readShortcutAction);
        fillShortcutActionCombo(ctrl(kRewriteAction), s.rewriteShortcutAction);
        updateShortcutControls();
        reapply = true;
        break;
    case kHkVerify:
        applyToApp();
        g.shortcutsReady = App::shared().shortcutsRegistered();
        updateFirstRunCompletion();
        MessageBoxW(g.hwnd,
                    g.shortcutsReady ? L"Every configured shortcut is registered and ready."
                                     : (!s.readShortcut.isConfigured() && !s.rewriteShortcut.isConfigured()
                                        ? L"Set at least one shortcut so TypeTide can be triggered from the keyboard."
                                        : L"One or more shortcuts are unavailable. Choose a different combination."),
                    L"TypeTide — shortcut verification",
                    MB_OK | (g.shortcutsReady ? MB_ICONINFORMATION : MB_ICONWARNING));
        save = false;
        break;

    case kTrigger:
        if (code != CBN_SELCHANGE) return;
        s.selectionTrigger = (SelectionTrigger)SendMessageW(ctrl(kTrigger), CB_GETCURSEL, 0, 0);
        reapply = true;
        break;
    case kPreview:
        s.rewritePreview = IsDlgButtonChecked(g.hwnd, kPreview) == BST_CHECKED;
        break;
    case kReadStyle:
        if (code != CBN_SELCHANGE) return;
        s.readStyle = comboStyle(ctrl(kReadStyle));
        break;
    case kRewriteStyle:
        if (code != CBN_SELCHANGE) return;
        s.rewriteStyle = comboStyle(ctrl(kRewriteStyle));
        break;

    case kSkipAdd: {
        std::wstring app = util::Trim(editText(ctrl(kSkipEdit)));
        if (app.empty()) { save = false; break; }
        for (auto& e : s.skipApps)
            if (util::ToLower(e) == util::ToLower(app)) { save = false; break; }
        if (!save) break;
        s.skipApps.push_back(app);
        SendMessageW(ctrl(kSkipList), LB_ADDSTRING, 0, (LPARAM)app.c_str());
        SetWindowTextW(ctrl(kSkipEdit), L"");
        break;
    }
    case kSkipRemove: {
        int sel = (int)SendMessageW(ctrl(kSkipList), LB_GETCURSEL, 0, 0);
        if (sel < 0 || sel >= (int)s.skipApps.size()) { save = false; break; }
        s.skipApps.erase(s.skipApps.begin() + sel);
        SendMessageW(ctrl(kSkipList), LB_DELETESTRING, sel, 0);
        break;
    }
    case kDiagExport: {
        wchar_t path[MAX_PATH] = L"TypeTide-Diagnostics.json";
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = g.hwnd;
        dialog.lpstrFilter = L"JSON report\0*.json\0All files\0*.*\0";
        dialog.lpstrFile = path;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrDefExt = L"json";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (GetSaveFileNameW(&dialog)) {
            if (!CopyFileW(diagnostics::Path().c_str(), path, FALSE))
                MessageBoxW(g.hwnd, L"No diagnostics have been recorded yet.",
                            L"TypeTide — export", MB_OK | MB_ICONINFORMATION);
        }
        save = false;
        break;
    }
    case kDiagClear:
        diagnostics::Clear();
        MessageBoxW(g.hwnd, L"Local diagnostics were cleared.", L"TypeTide — diagnostics",
                    MB_OK | MB_ICONINFORMATION);
        save = false;
        break;

    default:
        return;
    }

    if (save) {
        if (s.save()) {
            g.saveWarningShown = false;
        } else if (!g.saveWarningShown) {
            g.saveWarningShown = true;
            MessageBoxW(g.hwnd,
                        L"TypeTide couldn't save your settings. Your existing settings file was "
                        L"left unchanged.\n\nCheck that the settings folder is writable, then try again.",
                        L"TypeTide — settings not saved", MB_OK | MB_ICONWARNING);
        }
    }
    if (reapply) applyToApp();
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize = {px(760), px(520)};
        return 0;
    }
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED)
            layoutForClientSize(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_COMMAND:
        onCommand(LOWORD(wp), HIWORD(wp));
        return 0;
    case kMsgOllamaModels: {
        auto* models = (std::vector<std::wstring>*)lp;
        ollamamodels::ApplyResolvedModels(*models);
        HWND combo = ctrl(kOllamaModel);
        if (combo) {
            std::wstring current = util::Widen(Settings::shared().ollamaModel);
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            for (auto& m : *models)
                SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)m.c_str());
            SetWindowTextW(combo, current.c_str());
        }
        ULONGLONG memoryKb = 0;
        double cap = 0;
        const uint64_t memoryBytes = GetPhysicallyInstalledSystemMemory(&memoryKb)
            ? (uint64_t)memoryKb * 1024 : 16ULL * 1024 * 1024 * 1024;
        const std::wstring recommended = ollamamodels::RecommendedForMemory(*models, memoryBytes, &cap);
        if (!recommended.empty()) {
            std::wstring hint = L"Recommended for this PC: " + recommended +
                L" (comfortable target up to about " + std::to_wstring((int)cap) +
                L"B). Ollama runs locally.";
            SetWindowTextW(ctrl(kOllamaHint), hint.c_str());
        }
        ollamamodels::PreloadAsync();
        delete models;
        return 0;
    }
    case kMsgUpdateResult:
        EnableWindow(ctrl(kCheckUpdate), TRUE);
        SetWindowTextW(ctrl(kCheckUpdate), L"Check for updates");
        if (wp == 1) {
            if (MessageBoxW(hwnd, L"A new TypeTide version is available. Open the download page?",
                            L"TypeTide update", MB_YESNO | MB_ICONINFORMATION) == IDYES)
                updatechecker::OpenReleasesPage();
        } else if (wp == 0) {
            MessageBoxW(hwnd, L"You're using the latest version.", L"TypeTide update",
                        MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(hwnd,
                        L"TypeTide couldn't check for updates. Check your internet connection and try again.",
                        L"TypeTide update", MB_OK | MB_ICONWARNING);
        }
        return 0;
    case kMsgBackendHealth: {
        auto* result = (translator::HealthCheckResult*)lp;
        EnableWindow(ctrl(kBackendTest), TRUE);
        SetWindowTextW(ctrl(kBackendTest), L"Test connection");
        std::wstring status = result->message;
        if (result->ok) {
            status += L"  ";
            if (result->firstTokenMs >= 0)
                status += L"First token " + std::to_wstring(result->firstTokenMs) + L" ms · ";
            status += L"Total " + std::to_wstring(result->totalMs) + L" ms";
        }
        SetWindowTextW(ctrl(kBackendStatus), status.c_str());
        g.backendReady = result->ok;
        updateFirstRunCompletion();
        delete result;
        return 0;
    }
    case kMsgBuiltInProgress: {
        auto* progress = reinterpret_cast<BuiltInProgress*>(lp);
        if (progress->done) g.modelDownloadCancel.reset();
        if (g.hwnd && ctrl(kBuiltInStatus)) {
            std::wstring status = progress->status;
            if (!progress->done) status += L" " + std::to_wstring(int(progress->fraction * 100)) + L"%";
            SetWindowTextW(ctrl(kBuiltInStatus), status.c_str());
            if (progress->done) {
                updateBuiltInStatus();
                if (!builtin::Installed()) SetWindowTextW(ctrl(kBuiltInStatus), status.c_str());
            }
        }
        delete progress;
        return 0;
    }
    case WM_NOTIFY: {
        auto* hdr = (NMHDR*)lp;
        if (hdr->idFrom == kAboutLinks && (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
            auto* link = (NMLINK*)lp;
            ShellExecuteW(nullptr, L"open", link->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (item->CtlID != kTab || item->itemID == (UINT)-1) break;
        const bool selected = (item->itemState & ODS_SELECTED) != 0;
        HBRUSH bg = CreateSolidBrush(selected ? (g.dark ? RGB(0x3A, 0x31, 0x59) : RGB(0xE4, 0xDF, 0xFF)) : navBg());
        FillRect(item->hDC, &item->rcItem, bg);
        DeleteObject(bg);
        if (selected) {
            RECT accent{item->rcItem.left, item->rcItem.top + px(7),
                        item->rcItem.left + px(3), item->rcItem.bottom - px(7)};
            HBRUSH ab = CreateSolidBrush(ui::Accent);
            FillRect(item->hDC, &accent, ab);
            DeleteObject(ab);
        }
        wchar_t text[64]{};
        SendMessageW(item->hwndItem, LB_GETTEXT, item->itemID, (LPARAM)text);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, textColor());
        SelectObject(item->hDC, g.font);
        RECT tr = item->rcItem;
        if (g.tabImages && item->itemID < 8) {
            ImageList_Draw(g.tabImages, static_cast<int>(item->itemID), item->hDC,
                           item->rcItem.left + px(16), item->rcItem.top + px(13), ILD_TRANSPARENT);
        }
        tr.left += px(42);
        DrawTextW(item->hDC, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        wchar_t cls[32]{};
        GetClassNameW((HWND)lp, cls, (int)std::size(cls));
        if (_wcsicmp(cls, L"Edit") == 0) {
            SetBkColor((HDC)wp, g.dark ? RGB(0x25, 0x25, 0x28) : RGB(0xEE, 0xEE, 0xF1));
            SetTextColor((HDC)wp, secondaryColor());
            return (LRESULT)pageBrush();
        }
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, textColor());
        return (LRESULT)pageBrush();
    }
    case WM_CTLCOLOREDIT: {
        SetBkColor((HDC)wp, g.dark ? RGB(0x2B, 0x2B, 0x2E) : RGB(0xFF, 0xFF, 0xFF));
        SetTextColor((HDC)wp, textColor());
        return (LRESULT)editBrush();
    }
    case WM_CTLCOLORBTN:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, textColor());
        return (LRESULT)pageBrush();
    case WM_CTLCOLORLISTBOX:
        SetBkColor((HDC)wp, navBg());
        SetTextColor((HDC)wp, textColor());
        return (LRESULT)navBrush();
    case WM_ERASEBKGND: {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, pageBrush());
        RECT nav{0, 0, px(184), rc.bottom};
        FillRect((HDC)wp, &nav, navBrush());
        return 1;
    }
    case WM_SETTINGCHANGE:
        g.dark = systemUsesDarkMode();
        g.highContrast = ui::highContrastEnabled();
        applyTheme(hwnd);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g.modelDownloadCancel) {
            g.modelDownloadCancel->store(true);
            g.modelDownloadCancel.reset();
        }
        if (g.font) { DeleteObject(g.font); g.font = nullptr; }
        if (g.fontBold) { DeleteObject(g.fontBold); g.fontBold = nullptr; }
        if (g.fontTitle) { DeleteObject(g.fontTitle); g.fontTitle = nullptr; }
        if (g.tabImages) { ImageList_Destroy(g.tabImages); g.tabImages = nullptr; }
        g.hwnd = nullptr;
        g.pages.clear();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

namespace SettingsWindow {

void open(HWND appWindow) {
    if (g.hwnd) {
        ShowWindow(g.hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g.hwnd);
        return;
    }
    g.appWindow = appWindow;
    enableCommonControlDarkMode();

    static bool registered = [] {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TAB_CLASSES | ICC_HOTKEY_CLASS | ICC_LINK_CLASS |
                                                  ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);
        WNDCLASSW wc{};
        wc.lpfnWndProc = wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APPICON));
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        return true;
    }();
    (void)registered;

    g.dpi = GetDpiForSystem();
    int w = px(820), h = px(560);
    RECT wr{0, 0, w, h};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    g.hwnd = CreateWindowExW(0, kClassName, L"TypeTide Settings",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             wr.right - wr.left, wr.bottom - wr.top,
                             nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g.hwnd) return;
    g.dark = systemUsesDarkMode();
    g.highContrast = ui::highContrastEnabled();

    HICON smallIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON),
                                        IMAGE_ICON, px(16), px(16), LR_DEFAULTCOLOR | LR_SHARED);
    SendMessageW(g.hwnd, WM_SETICON, ICON_BIG,
                 (LPARAM)LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON)));
    SendMessageW(g.hwnd, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);

    // 字体
    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW bold = ncm.lfMessageFont;
    bold.lfWeight = FW_SEMIBOLD;
    bold.lfHeight = MulDiv(bold.lfHeight, 5, 4);
    g.fontBold = CreateFontIndirectW(&bold);
    LOGFONTW title = ncm.lfMessageFont;
    title.lfWeight = FW_SEMIBOLD;
    title.lfHeight = -MulDiv(20, (int)g.dpi, 72);
    g.fontTitle = CreateFontIndirectW(&title);

    g.loading = true;
    g.clientWidth = w;
    g.clientHeight = h;

    // Windows 11-style left navigation.
    HWND tab = CreateWindowExW(0, L"LISTBOX", L"",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY |
                                   LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                               0, px(14), px(184), h - px(28), g.hwnd,
                               (HMENU)kTab, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(tab, WM_SETFONT, (WPARAM)g.font, TRUE);
    SendMessageW(tab, LB_SETITEMHEIGHT, 0, px(42));
    g.tabImages = createTabImages();
    const wchar_t* names[] = {L"General", L"Translation", L"Languages", L"Shortcuts",
                              L"Behavior", L"Excluded apps", L"Diagnostics", L"About"};
    for (int i = 0; i < 8; ++i) {
        SendMessageW(tab, LB_ADDSTRING, 0, (LPARAM)names[i]);
    }
    SendMessageW(tab, LB_SETCURSEL, 0, 0);

    buildPages();
    applyTheme(g.hwnd);
    showPage(0);
    g.loading = false;

    // 异步填充 Ollama 模型下拉框
    fetchOllamaModelsAsync();

    SetForegroundWindow(g.hwnd);
}

} // namespace SettingsWindow
