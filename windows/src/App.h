//
//  App.h — 主编排（对应 macOS 的 TriggerController + AppDelegate）：
//  隐藏主窗口接收热键 / 托盘 / 工作线程消息，串起取词 → 翻译 → 弹窗/替换。
//
#pragma once
#include <windows.h>
#include "Language.h"
#include "SelectionCapture.h"
#include "TrayIcon.h"
#include <optional>
#include <string>

enum class ShortcutAction;

// 主窗口自定义消息
constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_TR_DELTA = WM_APP + 2;    // wParam=reqId, lParam=std::wstring*（接收方释放）
constexpr UINT WM_APP_TR_DONE = WM_APP + 3;     // wParam=reqId, lParam=DoneMsg*
constexpr UINT WM_APP_REWRITE_DONE = WM_APP + 4;// lParam=RewriteResult*
constexpr UINT WM_APP_UPDATE = WM_APP + 5;      // 有新版本
constexpr UINT WM_APP_OPEN_SETTINGS = WM_APP + 6;
constexpr UINT WM_APP_MODELS = WM_APP + 7;          // lParam=std::vector<std::wstring>*（接收方释放）
constexpr UINT WM_APP_READ_CAPTURED = WM_APP + 8;   // lParam=capture::Capture*（可空；接收方释放）
constexpr UINT WM_APP_REWRITE_CAPTURED = WM_APP + 9;// 同上

constexpr wchar_t kAppWindowClass[] = L"TypeTideApp";

class App {
public:
    static App& shared();

    bool init(HINSTANCE inst);
    HWND hwnd() const { return hwnd_; }
    bool shortcutsRegistered() const { return shortcutsReady_; }
    const std::wstring& shortcutIssue() const { return shortcutIssue_; }

    /// 根据开关 + 快捷键 + 划词设置重新装配触发（设置变化后调用）
    void applyEnabledState();
    void openSettings();

private:
    App() = default;
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    struct DoneMsg {
        bool ok;
        std::wstring error;
    };
    struct RewriteResult {
        bool ok;
        std::wstring text;    // 译文（ok 时）
        std::wstring error;
        bool isWholeField;
        RECT anchor;
        bool hasAnchor;
        std::wstring original;
        Language target;
        std::optional<Language> source;
        RewriteStyle style;
    };
    struct Direction {
        std::optional<Language> from;
        Language to;
    };

    void performShortcutAction(ShortcutAction action);
    void handleRead(std::optional<Language> targetOverride = std::nullopt);
    void handleRewrite(std::optional<Language> targetOverride = std::nullopt);
    void presentRead(const capture::Capture& cap, std::optional<Language> targetOverride = std::nullopt);
    void proceedRewrite(const capture::Capture& cap, std::optional<Language> targetOverride = std::nullopt);
    /// 超长输入直接弹错误提示（LLM 又慢又贵，弹窗也放不下）；返回 true = 已拦截
    bool rejectIfTooLong(const capture::Capture& cap);
    Direction resolveDirection(const std::wstring& text, TranslationDirection mode, bool isWrite) const;
    void startStream(const std::wstring& text, std::optional<Language> from, Language to, RewriteStyle style);
    void configurePopupCallbacks();
    void setupSelectionTrigger();
    bool foregroundAppSkipped() const;
    void showErrorPopup(const std::wstring& original, Language target, RECT anchor, const std::wstring& error);
    void quit();

    HWND hwnd_ = nullptr;
    TrayIcon tray_;
    uint64_t currentReq_ = 0;
    std::optional<Language> currentSource_;   // 弹窗重定向语言时沿用
    std::wstring currentText_;
    RewriteStyle currentStyle_ = RewriteStyle::Faithful;
    bool currentReplaceSelectAll_ = false;    // 弹窗 Replace 时是否先全选
    std::wstring shortcutIssue_;
    bool readShortcutRegistered_ = false;
    bool rewriteShortcutRegistered_ = false;
    bool shortcutsReady_ = false;
    UINT taskbarCreatedMsg_ = 0;   // explorer 重启后重挂托盘图标
};
