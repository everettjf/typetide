//
//  PopupWindow.h — 翻译弹窗（对应 macOS 的 PopupController + TranslationPopupView）。
//  无激活置顶窗口：流式展示译文，Copy / Replace / 改目标语言 / Esc / 点外部关闭。
//
#pragma once
#include <windows.h>
#include "Language.h"
#include <functional>
#include <string>
#include <optional>

class PopupWindow {
public:
    static PopupWindow& shared();

    /// 在锚点（屏幕像素矩形)附近显示弹窗并进入 loading 态。
    /// showReplace 控制是否显示 Replace 按钮。
    void show(const std::wstring& original, Language target, RECT anchor, bool showReplace);

    void appendDelta(const std::wstring& delta);
    /// 流结束：完成最后一次潮汐扫过；空结果由 App 转成错误。
    void finishTranslation();
    void setError(const std::wstring& error);
    void setTarget(Language target);
    void setSource(std::optional<Language> source) { source_ = source; }
    std::optional<Language> source() const { return source_; }
    void selectSource(Language source);
    /// 重新进入 loading（重定向语言后重新翻译）
    void resetForRetranslate();

    void close();
    bool isVisible() const { return hwnd_ != nullptr; }
    const std::wstring& translation() const { return translation_; }
    Language target() const { return target_; }

    /// 点击 Replace（App 负责关弹窗 + 替换）
    std::function<void()> onReplace;
    /// 弹窗里改选目标语言
    std::function<void(Language)> onRetarget;
    /// Choose a source language for this request, without changing settings.
    std::function<void(Language)> onSourceChange;
    /// 错误状态下重试当前请求
    std::function<void()> onRetry;
    /// 弹窗关闭（App 取消未完成的流）
    std::function<void()> onClosed;

private:
    PopupWindow() = default;
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    enum class Region { None, Close, Lang, Source, Copy, Replace, Retry };

    void layoutAndResize();
    void paint(HDC dc);
    Region hitTest(POINT pt) const;
    void showLangMenu(bool source = false);
    void installDismissMonitors();
    void removeDismissMonitors();
    void updateFonts();

    HWND hwnd_ = nullptr;
    RECT anchor_{};
    bool placedAbove_ = false;
    bool showReplace_ = false;
    bool dark_ = false;
    bool highContrast_ = false;
    UINT dpi_ = 96;

    std::wstring original_;
    std::wstring translation_;
    std::wstring error_;
    bool loading_ = true;
    Language target_ = Language::English;
    std::optional<Language> source_;
    bool copiedFlash_ = false;
    enum class TidePhase { Waiting, Flowing, Settling, Complete, Failed };
    TidePhase tidePhase_ = TidePhase::Waiting;
    bool animationsEnabled_ = true;
    unsigned tideTick_ = 0;
    unsigned settleTicks_ = 0;
    int width_ = 420;
    int textHeight_ = 0;
    int viewportHeight_ = 0;
    int scrollY_ = 0;

    Region hover_ = Region::None;
    bool replacePressed_ = false;
    bool trackingMouse_ = false;
    bool menuOpen_ = false;

    RECT rcClose_{}, rcLang_{}, rcSource_{}, rcCopy_{}, rcReplace_{}, rcRetry_{};
    HFONT fontHeader_ = nullptr, fontBody_ = nullptr, fontSmall_ = nullptr;

    int mouseHookId_ = 0, keyHookId_ = 0;
};
