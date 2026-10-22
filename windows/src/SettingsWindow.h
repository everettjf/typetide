//
//  SettingsWindow.h — 设置窗口（对应 macOS 的 SettingsView）。
//  Tab 页：General / Backend / Language / Shortcuts / Behavior / Skip Apps / About。
//  所有更改即改即存。
//
#pragma once
#include <windows.h>

namespace SettingsWindow {

/// 打开（或前置已打开的）设置窗口。appWindow 用于设置变更后通知重装配。
void open(HWND appWindow, bool showShortcuts = false);
void refreshShortcutStatus();

/// Self-test: verify all navigation icons retain the requested theme color.
bool testNavigationIconColors(bool dark, UINT dpi = 96);

} // namespace SettingsWindow
