#include "TextReplacer.h"
#include "Clipboard.h"
#include "Keyboard.h"
#include "LocalDiagnostics.h"
#include "Util.h"
#include <thread>

namespace replacer {

namespace {

constexpr wchar_t kOwnerClass[] = L"TypeTideClipboardOwner";

struct RenderContext {
    const std::wstring* text = nullptr;
    bool rendered = false;
};

void serveText(const std::wstring& text) {
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) return;
    if (void* p = GlobalLock(h)) {
        memcpy(p, text.c_str(), bytes);
        GlobalUnlock(h);
        if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
    } else {
        GlobalFree(h);
    }
}

LRESULT CALLBACK ownerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = (RenderContext*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_RENDERFORMAT:
        // 目标应用 GetClipboardData → 粘贴真正发生了
        if (ctx && ctx->text && wp == CF_UNICODETEXT) {
            serveText(*ctx->text);
            ctx->rendered = true;
        }
        return 0;
    case WM_RENDERALLFORMATS:
        if (ctx && ctx->text && OpenClipboard(hwnd)) {
            if (GetClipboardOwner() == hwnd) serveText(*ctx->text);
            CloseClipboard();
            ctx->rendered = true;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/// 工作线程主体：设置延迟渲染剪贴板 → 合成粘贴 → 等目标取数据 → 还原剪贴板
void replaceWorker(const std::wstring& text, bool selectAll) {
    DWORD started = GetTickCount();
    // Wait for the triggering gesture to finish. With a fast/cached result,
    // Ctrl+Shift+R can still be held when paste starts; its later key-up
    // events can interfere with the synthetic Ctrl+V.
    auto gestureHeld = [] {
        const int keys[] = {VK_LBUTTON, VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN};
        for (int key : keys)
            if (GetAsyncKeyState(key) & 0x8000) return true;
        return false;
    };
    DWORD mouseDeadline = GetTickCount() + 2000;
    while (gestureHeld()) {
        if (GetTickCount() >= mouseDeadline) {
            diagnostics::Record("replacement", "failure", {}, {}, "triggerStillPressed",
                                -1, (int)(GetTickCount() - started), (int)text.size());
            return;
        }
        Sleep(5);
    }
    Sleep(20);
    clipboard::Snapshot saved = clipboard::Take();

    static bool registered = [] {
        WNDCLASSW wc{};
        wc.lpfnWndProc = ownerProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kOwnerClass;
        RegisterClassW(&wc);
        return true;
    }();
    (void)registered;

    RenderContext ctx;
    ctx.text = &text;
    HWND owner = CreateWindowExW(0, kOwnerClass, L"", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    bool delayed = false;
    if (owner) {
        SetWindowLongPtrW(owner, GWLP_USERDATA, (LONG_PTR)&ctx);
        if (OpenClipboard(owner)) {
            EmptyClipboard();
            SetClipboardData(CF_UNICODETEXT, nullptr);  // 延迟渲染
            CloseClipboard();
            delayed = true;
        }
    }
    if (!delayed) {
        // 兜底：老式直接写 + 固定延迟
        clipboard::SetText(text);
    }

    if (selectAll) {
        keyboard::SendSelectAll();
        Sleep(40);
    }
    keyboard::SendPaste();

    if (delayed) {
        // 泵消息等 WM_RENDERFORMAT；2s 没人取说明粘贴没发生（目标不收粘贴等）
        DWORD deadline = GetTickCount() + 2000;
        MSG m;
        while (!ctx.rendered) {
            DWORD now = GetTickCount();
            if (now >= deadline) break;
            MsgWaitForMultipleObjects(0, nullptr, FALSE, deadline - now, QS_ALLINPUT);
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        }
        util::Log("replace rendered=%d", ctx.rendered);
        // 渲染后小宽限：有些目标会紧接着再读一次
        if (ctx.rendered) Sleep(150);
    } else {
        Sleep(150);
    }

    clipboard::Restore(saved);
    diagnostics::Record("replacement", delayed && !ctx.rendered ? "failure" : "success",
                        {}, {}, delayed && !ctx.rendered ? "pasteNotConsumed" : "",
                        -1, (int)(GetTickCount() - started), (int)text.size());
    if (owner) {
        SetWindowLongPtrW(owner, GWLP_USERDATA, 0);
        DestroyWindow(owner);
    }
}

} // namespace

void ReplaceAsync(std::wstring text, bool selectAll) {
    std::thread([text = std::move(text), selectAll] {
        replaceWorker(text, selectAll);
    }).detach();
}

} // namespace replacer
