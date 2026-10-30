//
//  Keyboard.swift
//  TypeTide
//
//  合成键盘事件（⌘C/⌘V/⌘A 等）。
//

import CoreGraphics

enum Keyboard {
    static let aKey: CGKeyCode = 0
    static let cKey: CGKeyCode = 8
    static let vKey: CGKeyCode = 9
    static let rightArrowKey: CGKeyCode = 124

    /// 发送一次按键，可带 ⌘ 修饰。
    @discardableResult
    static func press(_ keyCode: CGKeyCode, command: Bool = false) -> Bool {
        let source = CGEventSource(stateID: .combinedSessionState)
        guard let down = CGEvent(keyboardEventSource: source, virtualKey: keyCode, keyDown: true),
              let up = CGEvent(keyboardEventSource: source, virtualKey: keyCode, keyDown: false) else { return false }
        if command {
            down.flags = .maskCommand
            up.flags = .maskCommand
        }
        down.post(tap: .cghidEventTap)
        up.post(tap: .cghidEventTap)
        return true
    }
}
