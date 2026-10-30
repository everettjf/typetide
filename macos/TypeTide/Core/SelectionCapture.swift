//
//  SelectionCapture.swift
//  TypeTide
//
//  取词：优先 AX kAXSelectedText，兜底模拟 ⌘C 复制并还原剪贴板。
//

import ApplicationServices
import AppKit

/// 改写模式抓取的上下文
struct RewriteCapture {
    let text: String
    /// true = 取的是整个输入框（替换时需先全选）；false = 取的是当前选区
    let isWholeField: Bool
    let element: AXUIElement?
    let selectedRange: CFRange?
    let method: CaptureMethod
}

enum CaptureMethod: String {
    case accessibility
    case clipboardFallback
}

/// A snapshot lets the rewrite decision be tested without granting Accessibility.
struct RewriteCaptureContext {
    var element: AXUIElement? = nil
    var selectedText: String? = nil
    var selectedRange: CFRange? = nil
    var value: String? = nil
    var isEditable = false
    var isProtected = false
}

struct ClipboardCaptureOperations {
    var press: (CGKeyCode) -> Bool
    var hasFocus: () -> Bool
    var restoreSelection: () -> Void
}

enum SelectionCapture {

    // MARK: 读模式：拿到选中的文字

    /// 读模式取词：AX 选区优先，兜底复制。返回 (文字, 元素, 选区range) 供定位。
    @MainActor
    static func readSelection() async -> (text: String, element: AXUIElement?, range: CFRange?, method: CaptureMethod)? {
        let element = focusedElement()
        if let element {
            if let t = axSelectedText(element), !t.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                recordCapture(outcome: "success", method: .accessibility, characterCount: t.count)
                return (t, element, axSelectedRange(element), .accessibility)
            }
        }
        if let copied = await copyViaPasteboard(),
           !copied.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            recordCapture(outcome: "success", method: .clipboardFallback, characterCount: copied.count)
            return (copied, element, element.flatMap { axSelectedRange($0) }, .clipboardFallback)
        }
        recordCapture(outcome: "failure", method: nil, characterCount: nil)
        return nil
    }

    // MARK: 写模式：拿到选区或整个输入框

    @MainActor
    static func captureForRewrite() async -> RewriteCapture? {
        var context = RewriteCaptureContext()
        if let element = focusedElement() {
            context.element = element
            context.selectedText = axSelectedText(element)
            context.selectedRange = axSelectedRange(element)
            context.value = axValue(element)
            let role = axString(element, kAXRoleAttribute)
            let subrole = axString(element, kAXSubroleAttribute)
            context.isProtected = subrole == kAXSecureTextFieldSubrole
            context.isEditable = canSelectWholeField(role: role, subrole: subrole)
        }
        let capture = await captureForRewrite(context: context) { selectAll in
            await copyViaPasteboard(selectAll: selectAll, element: context.element)
        }
        recordCapture(outcome: capture == nil ? "failure" : "success",
                      method: capture?.method, characterCount: capture?.text.count)
        return capture
    }

    @MainActor
    static func captureForRewrite(context: RewriteCaptureContext,
                                  copy: (Bool) async -> String?) async -> RewriteCapture? {
        guard !context.isProtected, !Task.isCancelled else { return nil }
        func capture(_ text: String?, wholeField: Bool, method: CaptureMethod) -> RewriteCapture? {
            guard let text, !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return nil }
            return RewriteCapture(text: text, isWholeField: wholeField, element: context.element,
                                  selectedRange: wholeField ? nil : context.selectedRange, method: method)
        }
        if (context.selectedRange?.length ?? 1) > 0,
           let selected = capture(context.selectedText, wholeField: false, method: .accessibility) {
            return selected
        }
        // A known selection must never silently expand to the entire editor.
        if let range = context.selectedRange, range.length > 0 {
            return capture(await copy(false), wholeField: false, method: .clipboardFallback)
        }
        if let whole = capture(context.value, wholeField: true, method: .accessibility) {
            return whole
        }
        // Unknown AX selection information can still hide a user-selected range.
        if context.selectedRange == nil,
           let selected = capture(await copy(false), wholeField: false, method: .clipboardFallback) {
            return selected
        }
        guard context.isEditable, !Task.isCancelled else { return nil }
        return capture(await copy(true), wholeField: true, method: .clipboardFallback)
    }

    static func canSelectWholeField(role: String?, subrole: String?) -> Bool {
        guard subrole != kAXSecureTextFieldSubrole else { return false }
        return role == kAXTextFieldRole || role == kAXTextAreaRole || role == kAXComboBoxRole
    }

    static func axString(_ element: AXUIElement, _ attribute: String) -> String? {
        var value: AnyObject?
        guard AXUIElementCopyAttributeValue(element, attribute as CFString, &value) == .success else { return nil }
        return value as? String
    }

    // MARK: - AX helpers

    static func focusedElement() -> AXUIElement? {
        let systemWide = AXUIElementCreateSystemWide()
        var focused: AnyObject?
        let err = AXUIElementCopyAttributeValue(systemWide, kAXFocusedUIElementAttribute as CFString, &focused)
        guard err == .success, let element = focused else { return nil }
        return (element as! AXUIElement)
    }

    static func axSelectedText(_ element: AXUIElement) -> String? {
        var value: AnyObject?
        let err = AXUIElementCopyAttributeValue(element, kAXSelectedTextAttribute as CFString, &value)
        guard err == .success else { return nil }
        return value as? String
    }

    static func axValue(_ element: AXUIElement) -> String? {
        var value: AnyObject?
        let err = AXUIElementCopyAttributeValue(element, kAXValueAttribute as CFString, &value)
        guard err == .success else { return nil }
        return value as? String
    }

    static func axSelectedRange(_ element: AXUIElement) -> CFRange? {
        var value: AnyObject?
        let err = AXUIElementCopyAttributeValue(element, kAXSelectedTextRangeAttribute as CFString, &value)
        guard err == .success, let value else { return nil }
        var range = CFRange()
        if AXValueGetValue(value as! AXValue, .cfRange, &range) {
            return range
        }
        return nil
    }

    // MARK: - 模拟复制兜底

    @MainActor
    static func copyViaPasteboard(selectAll: Bool = false, element: AXUIElement? = nil) async -> String? {
        let originalRange = element.flatMap { axSelectedRange($0) }
        let originalPID = NSWorkspace.shared.frontmostApplication?.processIdentifier
        let hasFocus = {
            guard NSWorkspace.shared.frontmostApplication?.processIdentifier == originalPID else { return false }
            guard let element else { return true }
            guard let focused = focusedElement() else { return false }
            return CFEqual(element, focused)
        }
        let operations = ClipboardCaptureOperations(
            press: { Keyboard.press($0, command: true) },
            hasFocus: hasFocus,
            restoreSelection: {
                guard hasFocus() else { return }
                if let element, var range = originalRange,
                   let value = AXValueCreate(.cfRange, &range),
                   AXUIElementSetAttributeValue(element, kAXSelectedTextRangeAttribute as CFString, value) == .success {
                    return
                }
                // Some web editors do not expose a restorable AX range.
                Keyboard.press(Keyboard.rightArrowKey)
            })
        return await copyViaPasteboard(selectAll: selectAll, pasteboard: .general, operations: operations)
    }

    @MainActor
    static func copyViaPasteboard(selectAll: Bool, pasteboard: NSPasteboard,
                                  operations: ClipboardCaptureOperations) async -> String? {
        let saved = PasteboardHelper.snapshot(from: pasteboard)
        defer { PasteboardHelper.restore(saved, to: pasteboard) }
        guard !Task.isCancelled, operations.hasFocus() else { return nil }
        if selectAll {
            guard operations.press(Keyboard.aKey) else { return nil }
        }
        defer { if selectAll { operations.restoreSelection() } }
        do {
            if selectAll { try await Task.sleep(for: .milliseconds(40)) }
            guard !Task.isCancelled, operations.hasFocus() else { return nil }
            let beforeCount = pasteboard.changeCount
            guard operations.press(Keyboard.cKey) else { return nil }
            for _ in 0..<20 {
                try await Task.sleep(for: .milliseconds(20))
                guard operations.hasFocus() else { return nil }
                if pasteboard.changeCount != beforeCount {
                    return pasteboard.string(forType: .string)
                }
            }
        } catch { return nil }
        return nil
    }

    private static func recordCapture(outcome: String, method: CaptureMethod?, characterCount: Int?) {
        let event = DiagnosticEvent(
            name: .selectionCapture,
            outcome: outcome,
            captureMethod: method?.rawValue,
            inputCharacterCount: characterCount
        )
        Task { await LocalDiagnostics.shared.record(event) }
    }
}
