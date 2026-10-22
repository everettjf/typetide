#include "PopupWindow.h"
#include "BrandMark.h"
#include "Clipboard.h"
#include "Hooks.h"
#include "Util.h"
#include "UITheme.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace {

constexpr wchar_t kClassName[] = L"TypeTidePopup";
constexpr UINT_PTR kCopiedTimer = 1;
constexpr UINT_PTR kTideTimer = 2;

bool systemPrefersDark() {
    DWORD v = 1, size = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size);
    return v == 0;
}

} // namespace

PopupWindow& PopupWindow::shared() {
    static PopupWindow p;
    return p;
}

LRESULT CALLBACK PopupWindow::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PopupWindow* self = (PopupWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->handle(msg, wp, lp);
}

void PopupWindow::show(const std::wstring& original, Language target, RECT anchor, bool showReplace) {
    close();

    original_ = original;
    translation_.clear();
    error_.clear();
    loading_ = true;
    copiedFlash_ = false;
    tidePhase_ = TidePhase::Waiting;
    tideTick_ = settleTicks_ = 0;
    BOOL clientAnimations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &clientAnimations, 0);
    animationsEnabled_ = clientAnimations == TRUE;
    scrollY_ = 0;
    target_ = target;
    source_.reset();
    onSourceChange = nullptr;
    anchor_ = anchor;
    showReplace_ = showReplace;
    hover_ = Region::None;
    dark_ = systemPrefersDark();
    highContrast_ = ui::highContrastEnabled();

    static bool registered = [] {
        WNDCLASSW wc{};
        wc.lpfnWndProc = wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        return true;
    }();
    (void)registered;

    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            kClassName, L"TypeTide", WS_POPUP,
                            0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    util::Log("popup show hwnd=%p", (void*)hwnd_);
    if (!hwnd_) return;

    dpi_ = GetDpiForWindow(hwnd_);
    updateFonts();

    // Win11 圆角
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    BOOL darkAttr = dark_;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkAttr, sizeof(darkAttr));

    placedAbove_ = false;
    layoutAndResize();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    // Match macOS: full motion gets the particle tide; reduced-motion mode
    // still gets a low-motion activity indicator instead of static text.
    SetTimer(hwnd_, kTideTimer, animationsEnabled_ ? 33 : 160, nullptr);
    installDismissMonitors();
}

void PopupWindow::close() {
    if (!hwnd_) return;
    KillTimer(hwnd_, kTideTimer);
    removeDismissMonitors();
    HWND h = hwnd_;
    hwnd_ = nullptr;
    DestroyWindow(h);
    if (onClosed) onClosed();
}

void PopupWindow::appendDelta(const std::wstring& delta) {
    if (!hwnd_) return;
    translation_ += delta;
    loading_ = false;
    tidePhase_ = TidePhase::Flowing;
    layoutAndResize();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void PopupWindow::finishTranslation() {
    if (!hwnd_ || translation_.empty()) return;
    loading_ = false;
    if (animationsEnabled_) {
        tidePhase_ = TidePhase::Settling;
        settleTicks_ = 0;
        SetTimer(hwnd_, kTideTimer, 33, nullptr);
    } else {
        tidePhase_ = TidePhase::Complete;
        KillTimer(hwnd_, kTideTimer);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::setError(const std::wstring& error) {
    if (!hwnd_) return;
    error_ = error;
    loading_ = false;
    tidePhase_ = TidePhase::Failed;
    KillTimer(hwnd_, kTideTimer);
    layoutAndResize();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void PopupWindow::setTarget(Language target) {
    target_ = target;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void PopupWindow::selectSource(Language source) {
    if (!hwnd_ || !onSourceChange) return;
    source_ = source;
    onSourceChange(source);
}

void PopupWindow::resetForRetranslate() {
    if (!hwnd_) return;
    translation_.clear();
    error_.clear();
    loading_ = true;
    tidePhase_ = TidePhase::Waiting;
    tideTick_ = settleTicks_ = 0;
    SetTimer(hwnd_, kTideTimer, animationsEnabled_ ? 33 : 160, nullptr);
    layoutAndResize();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void PopupWindow::updateFonts() {
    if (fontHeader_) DeleteObject(fontHeader_);
    if (fontBody_) DeleteObject(fontBody_);
    if (fontSmall_) DeleteObject(fontSmall_);
    auto make = [&](int pt, int weight) {
        return CreateFontW(-MulDiv(pt, (int)dpi_, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    };
    fontHeader_ = make(9, FW_SEMIBOLD);
    fontBody_ = make(11, FW_NORMAL);
    fontSmall_ = make(9, FW_NORMAL);
}

void PopupWindow::layoutAndResize() {
    if (!hwnd_) return;
    const int s100 = (int)dpi_;
    auto px = [&](int v) { return MulDiv(v, s100, 96); };

    HMONITOR sizeMon = MonitorFromRect(&anchor_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO sizeInfo{sizeof(sizeInfo)};
    GetMonitorInfoW(sizeMon, &sizeInfo);
    const int workWidth = sizeInfo.rcWork.right - sizeInfo.rcWork.left;
    width_ = std::clamp(workWidth / 3, px(ui::control::PopupMinWidth),
                        px(ui::control::PopupMaxWidth));
    const int width = width_;
    const int headerH = px(36);
    const int padX = px(ui::spacing::Large);
    const int padY = px(ui::spacing::Medium);
    const int contentW = width - padX * 2;

    // 正文高度
    HDC dc = GetDC(hwnd_);
    HFONT old = (HFONT)SelectObject(dc, fontBody_);
    std::wstring bodyText = !error_.empty() ? error_
                          : (translation_.empty() && loading_) ? original_
                          : translation_;
    RECT rc{0, 0, contentW, 0};
    DrawTextW(dc, bodyText.c_str(), -1, &rc, DT_WORDBREAK | DT_CALCRECT);
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    textHeight_ = rc.bottom > px(20) ? rc.bottom : px(20);
    int textH = textHeight_;

    // 高度钳制：正文最多占所在显示器工作区的 60%，超出部分画省略号
    // （Copy 复制的仍是完整译文）
    {
        HMONITOR monClamp = MonitorFromRect(&anchor_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO miClamp{sizeof(miClamp)};
        GetMonitorInfoW(monClamp, &miClamp);
        int maxTextH = (miClamp.rcWork.bottom - miClamp.rcWork.top) * 6 / 10;
        if (textH > maxTextH) textH = maxTextH;
    }
    viewportHeight_ = textH;
    scrollY_ = std::clamp(scrollY_, 0, std::max(0, textHeight_ - viewportHeight_));

    bool hasButtons = (!translation_.empty() && error_.empty()) ||
                      (!error_.empty() && static_cast<bool>(onRetry));
    const int btnH = px(32);
    const int btnGap = px(ui::spacing::Medium);

    int height = headerH + 1 + padY + textH + (hasButtons ? btnGap + btnH : 0) + padY;

    // 命中区域
    const int chipH = px(20);
    rcClose_ = {width - padX - px(32), (headerH - px(32)) / 2,
                width - padX, (headerH + px(32)) / 2};
    int langW = px(86);
    rcLang_ = {rcClose_.left - px(10) - langW, (headerH - chipH) / 2, rcClose_.left - px(10), (headerH + chipH) / 2};

    int btnY = height - padY - btnH;
    int replaceW = showReplace_ ? px(84) : 0;
    int copyW = px(70);
    rcSource_ = {};
    if (!error_.empty() && onRetry) {
        rcCopy_ = rcReplace_ = RECT{};
        rcRetry_ = {width - padX - px(76), btnY, width - padX, btnY + btnH};
        if (onSourceChange)
            rcSource_ = {padX, btnY, rcRetry_.left - px(12), btnY + btnH};
    } else if (hasButtons) {
        rcRetry_ = {};
        int x = width - padX;
        if (showReplace_) {
            rcReplace_ = {x - replaceW, btnY, x, btnY + btnH};
            x = rcReplace_.left - px(10);
        } else {
            rcReplace_ = {};
        }
        rcCopy_ = {x - copyW, btnY, x, btnY + btnH};
    } else {
        rcCopy_ = rcReplace_ = rcRetry_ = RECT{};
    }

    // 定位：优先锚点下方，放不下改上方；夹在锚点所在显示器工作区内
    HMONITOR mon = MonitorFromRect(&anchor_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    const RECT& wa = mi.rcWork;

    int x = anchor_.left;
    int y;
    if (!placedAbove_) {
        y = anchor_.bottom + px(8);
        if (y + height > wa.bottom - px(8)) placedAbove_ = true;
    }
    if (placedAbove_) {
        y = anchor_.top - px(8) - height;
        if (y < wa.top + px(8)) y = wa.top + px(8);
    }
    x = std::max((int)wa.left + px(8), std::min(x, (int)wa.right - width - px(8)));
    y = std::max((int)wa.top + px(8), std::min(y, (int)wa.bottom - height - px(8)));

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
}

void PopupWindow::paint(HDC dc) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const ui::Palette th = ui::palette(dark_, highContrast_);
    auto px = [&](int v) { return MulDiv(v, (int)dpi_, 96); };

    // 背景 + 边框
    HBRUSH bg = CreateSolidBrush(th.surface);
    FillRect(dc, &client, bg);
    DeleteObject(bg);
    HBRUSH border = CreateSolidBrush(th.divider);
    FrameRect(dc, &client, border);
    DeleteObject(border);

    SetBkMode(dc, TRANSPARENT);
    const int headerH = px(36);
    const int padX = px(ui::spacing::Large);
    const int padY = px(ui::spacing::Medium);

    // ---- 头部 ----
    SelectObject(dc, fontHeader_);
    RECT rcMark{padX, px(8), padX + px(18), px(26)};
    brand::DrawMark(dc, rcMark, ui::Accent);
    SetTextColor(dc, th.secondary);
    RECT rcBrand{rcMark.right + px(4), 0, client.right, headerH};
    DrawTextW(dc, L"TypeTide", -1, &rcBrand, DT_SINGLELINE | DT_VCENTER);

    // 目标语言 chip
    {
        HBRUSH chipBg = CreateSolidBrush(hover_ == Region::Lang ? th.button : th.surface);
        HPEN chipPen = CreatePen(PS_SOLID, 1, hover_ == Region::Lang ? th.buttonBorder : th.surface);
        HGDIOBJ oldBrush = SelectObject(dc, chipBg);
        HGDIOBJ oldPen = SelectObject(dc, chipPen);
        RoundRect(dc, rcLang_.left, rcLang_.top, rcLang_.right, rcLang_.bottom, px(8), px(8));
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(chipPen);
        DeleteObject(chipBg);
        SelectObject(dc, fontSmall_);
        SetTextColor(dc, th.secondary);
        std::wstring label = L"🌐 " + lang::ShortName(target_) + L" ▾";
        DrawTextW(dc, label.c_str(), -1, (RECT*)&rcLang_,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    }

    // 关闭按钮
    SelectObject(dc, fontSmall_);
    SetTextColor(dc, hover_ == Region::Close ? th.text : th.secondary);
    DrawTextW(dc, L"✕", -1, (RECT*)&rcClose_, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

    // 分隔线
    RECT divider{0, headerH, client.right, headerH + 1};
    HBRUSH divBrush = CreateSolidBrush(th.divider);
    FillRect(dc, &divider, divBrush);
    DeleteObject(divBrush);

    // ---- 正文 ----
    SelectObject(dc, fontBody_);
    int textBottom = headerH + 1 + padY + viewportHeight_;
    RECT clipText{padX, headerH + 1 + padY, client.right - padX, textBottom};
    int saved = SaveDC(dc);
    IntersectClipRect(dc, clipText.left, clipText.top, clipText.right, clipText.bottom);
    RECT rcText{padX, headerH + 1 + padY - scrollY_, client.right - padX,
                headerH + 1 + padY - scrollY_ + textHeight_};
    if (!error_.empty()) {
        SetTextColor(dc, th.warning);
        std::wstring msg = L"⚠ " + error_;
        DrawTextW(dc, msg.c_str(), -1, &rcText, DT_WORDBREAK | DT_NOPREFIX);
    } else if (translation_.empty() && loading_) {
        SetTextColor(dc, th.secondary);
        DrawTextW(dc, original_.c_str(), -1, &rcText,
                  DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    } else {
        SetTextColor(dc, th.text);
        DrawTextW(dc, translation_.c_str(), -1, &rcText,
                  DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    }
    RestoreDC(dc, saved);

    // 点阵潮汐：等待/流式输出时循环，完成时快速扫过一次后停下。
    const bool activityActive = tidePhase_ == TidePhase::Waiting ||
        tidePhase_ == TidePhase::Flowing || tidePhase_ == TidePhase::Settling;
    const bool tideActive = animationsEnabled_ && activityActive;
    if (tideActive && clipText.right > clipText.left) {
        saved = SaveDC(dc);
        IntersectClipRect(dc, clipText.left, clipText.top, clipText.right, clipText.bottom);
        const int span = std::max(1, static_cast<int>(clipText.right - clipText.left) + px(90));
        const unsigned speed = tidePhase_ == TidePhase::Settling ? 11u : 4u;
        const int frontier = clipText.left - px(22) + (int)((tideTick_ * speed) % (unsigned)span);
        HPEN oldPen = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
        for (int i = 0; i < 32; ++i) {
            const int row = i % 7;
            const int trail = i / 7;
            const int x = frontier - px(trail * 13) - px((row % 2) * 4);
            const double wave = std::sin((double)tideTick_ * 0.14 + i * 0.72) * px(5);
            const int y = clipText.top + row * std::max(1, static_cast<int>(clipText.bottom - clipText.top - px(5)) / 6)
                          + (int)wave;
            const int radius = px(1 + ((i * 7) % 3));
            COLORREF color = highContrast_ ? th.text : (i % 3 == 0)
                ? (dark_ ? RGB(0x39, 0xD9, 0xE8) : RGB(0x00, 0x9E, 0xC4))
                : (dark_ ? RGB(0x98, 0x83, 0xFF) : RGB(0x6B, 0x4B, 0xE8));
            HBRUSH dot = CreateSolidBrush(color);
            HBRUSH old = (HBRUSH)SelectObject(dc, dot);
            Ellipse(dc, x - radius, y - radius, x + radius + 1, y + radius + 1);
            SelectObject(dc, old);
            DeleteObject(dot);
        }
        SelectObject(dc, oldPen);
        RestoreDC(dc, saved);
    } else if (!animationsEnabled_ && activityActive) {
        // Reduced-motion fallback. Windows can disable client-area animation
        // for accessibility, remote sessions, or the global Animation effects
        // switch. Keep the content stable, but show a gently stepping three-dot
        // activity indicator like macOS's reduced-motion ProgressView.
        SelectObject(dc, fontSmall_);
        SetTextColor(dc, th.secondary);
        RECT progress{clipText.left, clipText.bottom - px(18), clipText.right, clipText.bottom};
        if (translation_.empty()) {
            progress.right -= px(34);
            DrawTextW(dc, L"Translating", -1, &progress,
                      DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }
        const int activeDot = static_cast<int>((tideTick_ / 2) % 3);
        const int dotY = progress.top + (progress.bottom - progress.top) / 2;
        const int firstX = clipText.right - px(25);
        HPEN oldPen = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
        for (int i = 0; i < 3; ++i) {
            const COLORREF color = i == activeDot ? ui::Accent : th.buttonBorder;
            HBRUSH dot = CreateSolidBrush(color);
            HBRUSH old = (HBRUSH)SelectObject(dc, dot);
            const int radius = px(i == activeDot ? 3 : 2);
            const int x = firstX + px(i * 9);
            Ellipse(dc, x - radius, dotY - radius, x + radius + 1, dotY + radius + 1);
            SelectObject(dc, old);
            DeleteObject(dot);
        }
        SelectObject(dc, oldPen);
    }
    if (textHeight_ > viewportHeight_) {
        const int trackTop = headerH + 1 + padY;
        const int trackH = viewportHeight_;
        const int thumbH = std::max(px(24), trackH * viewportHeight_ / textHeight_);
        const int maxScroll = textHeight_ - viewportHeight_;
        const int thumbY = trackTop + (trackH - thumbH) * scrollY_ / std::max(1, maxScroll);
        RECT thumb{client.right - px(5), thumbY, client.right - px(2), thumbY + thumbH};
        HBRUSH sb = CreateSolidBrush(th.buttonBorder);
        FillRect(dc, &thumb, sb);
        DeleteObject(sb);
    }

    // ---- 按钮 ----
    auto drawButton = [&](const RECT& r, const wchar_t* label, bool prominent, bool hovered) {
        if (r.right <= r.left) return;
        COLORREF fill = prominent ? (highContrast_ ? GetSysColor(COLOR_HIGHLIGHT) : ui::Accent) : th.button;
        if (hovered) {
            // 悬停微调亮度
            int rr = GetRValue(fill), gg = GetGValue(fill), bb = GetBValue(fill);
            int up = prominent ? 20 : 10;
            fill = RGB(std::min(255, rr + up), std::min(255, gg + up), std::min(255, bb + up));
        }
        HBRUSH b = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, prominent ? fill : th.buttonBorder);
        HGDIOBJ oldBrush = SelectObject(dc, b);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, px(ui::radius::Control), px(ui::radius::Control));
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(b);
        SelectObject(dc, fontSmall_);
        SetTextColor(dc, prominent ? ui::AccentText : th.text);
        DrawTextW(dc, label, -1, (RECT*)&r, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    };
    if (!translation_.empty() && error_.empty()) {
        drawButton(rcCopy_, copiedFlash_ ? L"Copied ✓" : L"Copy", false, hover_ == Region::Copy);
        if (showReplace_) drawButton(rcReplace_, L"Replace", true, hover_ == Region::Replace);
    }
    if (!error_.empty() && onSourceChange && rcSource_.right > rcSource_.left) {
        std::wstring label = source_ ? L"Source: " + std::wstring(lang::DisplayName(*source_)) + L" ▾"
                                     : L"Source language… ▾";
        drawButton(rcSource_, label.c_str(), false, hover_ == Region::Source);
    }
    if (!error_.empty() && onRetry)
        drawButton(rcRetry_, L"Retry", true, hover_ == Region::Retry);
}

PopupWindow::Region PopupWindow::hitTest(POINT pt) const {
    if (PtInRect(&rcClose_, pt)) return Region::Close;
    if (PtInRect(&rcLang_, pt)) return Region::Lang;
    if (rcSource_.right > rcSource_.left && PtInRect(&rcSource_, pt)) return Region::Source;
    if (rcCopy_.right > rcCopy_.left && PtInRect(&rcCopy_, pt)) return Region::Copy;
    if (showReplace_ && rcReplace_.right > rcReplace_.left && PtInRect(&rcReplace_, pt)) return Region::Replace;
    if (rcRetry_.right > rcRetry_.left && PtInRect(&rcRetry_, pt)) return Region::Retry;
    return Region::None;
}

void PopupWindow::showLangMenu(bool source) {
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < std::size(lang::All); ++i) {
        UINT flags = MF_STRING;
        if (source ? source_ == lang::All[i] : lang::All[i] == target_) flags |= MF_CHECKED;
        AppendMenuW(menu, flags, 100 + i, lang::DisplayName(lang::All[i]));
    }
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    const RECT& trigger = source ? rcSource_ : rcLang_;
    POINT at{wr.left + trigger.left, wr.top + trigger.bottom};

    // TrackPopupMenu 需要前台窗口才能正常关闭；记录并恢复原前台，
    // 保证 Replace 仍粘贴回目标应用。
    HWND prevFg = GetForegroundWindow();
    menuOpen_ = true;
    SetForegroundWindow(hwnd_);
    UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                                    at.x, at.y, 0, hwnd_, nullptr);
    menuOpen_ = false;
    DestroyMenu(menu);
    if (prevFg && prevFg != hwnd_) SetForegroundWindow(prevFg);

    if (cmd >= 100 && cmd < 100 + std::size(lang::All)) {
        Language chosen = lang::All[cmd - 100];
        if (source) {
            selectSource(chosen);
        } else if (chosen != target_ && onRetarget) onRetarget(chosen);
    }
}

void PopupWindow::installDismissMonitors() {
    mouseHookId_ = hooks::AddMouse([this](WPARAM event, POINT pt) {
        if (event != WM_LBUTTONDOWN && event != WM_RBUTTONDOWN) return;
        if (!hwnd_ || menuOpen_) return;
        RECT wr{};
        GetWindowRect(hwnd_, &wr);
        if (!PtInRect(&wr, pt)) close();
    });
    keyHookId_ = hooks::AddKey([this](DWORD vk) {
        if (vk == VK_ESCAPE && hwnd_ && !menuOpen_) {
            close();
            return true;
        }
        return false;
    });
}

void PopupWindow::removeDismissMonitors() {
    if (mouseHookId_) hooks::RemoveMouse(mouseHookId_);
    if (keyHookId_) hooks::RemoveKey(keyHookId_);
    mouseHookId_ = keyHookId_ = 0;
}

LRESULT PopupWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd_, &ps);
        // 双缓冲防闪烁
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);
        paint(mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        if (!trackingMouse_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            trackingMouse_ = true;
        }
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        Region r = hitTest(pt);
        if (r != hover_) {
            hover_ = r;
            SetCursor(LoadCursorW(nullptr, r == Region::None ? IDC_ARROW : IDC_HAND));
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        if (hover_ != Region::None) {
            hover_ = Region::None;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        switch (hitTest(pt)) {
        case Region::Close:
            close();
            return 0;
        case Region::Lang:
            showLangMenu();
            return 0;
        case Region::Source:
            showLangMenu(true);
            return 0;
        case Region::Copy:
            if (!translation_.empty()) {
                clipboard::SetText(translation_);
                copiedFlash_ = true;
                InvalidateRect(hwnd_, nullptr, FALSE);
                SetTimer(hwnd_, kCopiedTimer, 1200, nullptr);
            }
            return 0;
        case Region::Replace:
            // Keep the popup alive until the click is complete. Closing on
            // mouse-down lets its mouse-up reach the editor during paste.
            replacePressed_ = true;
            SetCapture(hwnd_);
            return 0;
        case Region::Retry:
            if (onRetry) onRetry();
            return 0;
        default:
            return 0;
        }
    }
    case WM_LBUTTONUP: {
        const bool pressed = replacePressed_;
        replacePressed_ = false;
        const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const bool replace = pressed && hitTest(pt) == Region::Replace;
        if (GetCapture() == hwnd_) ReleaseCapture();
        if (replace && onReplace) onReplace();
        return 0;
    }
    case WM_CAPTURECHANGED:
        replacePressed_ = false;
        return 0;
    case WM_CANCELMODE:
        replacePressed_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        return 0;
    case WM_TIMER:
        if (wp == kCopiedTimer) {
            KillTimer(hwnd_, kCopiedTimer);
            copiedFlash_ = false;
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (wp == kTideTimer) {
            ++tideTick_;
            if (tidePhase_ == TidePhase::Settling && ++settleTicks_ >= 15) {
                tidePhase_ = TidePhase::Complete;
                KillTimer(hwnd_, kTideTimer);
            }
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (textHeight_ > viewportHeight_) {
            int step = MulDiv(36, (int)dpi_, 96);
            scrollY_ = std::clamp(scrollY_ - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * step,
                                  0, textHeight_ - viewportHeight_);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_GETTEXT: {
        // 供测试读取当前译文
        size_t n = std::min(translation_.size(), wp > 0 ? (size_t)wp - 1 : (size_t)0);
        if (wp > 0 && lp) {
            memcpy((wchar_t*)lp, translation_.c_str(), n * sizeof(wchar_t));
            ((wchar_t*)lp)[n] = 0;
        }
        return (LRESULT)n;
    }
    case WM_GETTEXTLENGTH:
        return (LRESULT)translation_.size();
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DPICHANGED:
        dpi_ = HIWORD(wp);
        updateFonts();
        layoutAndResize();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
