//
//  Keyboard.h — 合成按键（对应 macOS 的 Keyboard）。
//
#pragma once
#include <windows.h>
#include <vector>

namespace keyboard {

/// 发送 Ctrl+<vk>。先临时抬起用户当前按住的修饰键（如触发热键的 Alt），
/// 避免目标应用收到 Ctrl+Alt+C 之类的组合。
void SendCtrlCombo(WORD vk);

// Inspect generated input without sending keystrokes to the user's desktop.
std::vector<INPUT> BuildCtrlComboInputsForTesting(
    WORD vk, const std::vector<WORD>& heldModifiers);

inline void SendCopy()      { SendCtrlCombo('C'); }
inline void SendPaste()     { SendCtrlCombo('V'); }
inline void SendSelectAll() { SendCtrlCombo('A'); }

} // namespace keyboard
