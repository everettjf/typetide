//
//  AccessibilityPermission.swift
//  TypeTide
//
//  辅助功能权限助手。
//

import ApplicationServices
import AppKit

enum AccessibilityPermission {
    static var isGranted: Bool { AXIsProcessTrusted() }

    /// Request the system prompt only. This is asynchronous: opening Settings on a
    /// timer races the prompt and does not guarantee TCC has registered this app.
    /// The user opens Settings from the system prompt or an explicit fallback button.
    static func request() {
        let options: NSDictionary = [
            kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true
        ]
        _ = AXIsProcessTrustedWithOptions(options as CFDictionary)
    }

    /// Reveal the running bundle, not another installed copy with the same name.
    @MainActor
    static func revealCurrentApp() {
        NSWorkspace.shared.activateFileViewerSelecting([Bundle.main.bundleURL])
    }

    static func openSystemSettings() {
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility") {
            NSWorkspace.shared.open(url)
        }
    }
}
