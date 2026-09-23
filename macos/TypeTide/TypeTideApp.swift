//
//  TypeTideApp.swift
//  TypeTide
//
//  纯菜单栏 App：紧凑 AppKit 状态项 + SwiftUI Settings 场景。
//

import SwiftUI
import AppKit

@main
struct TypeTideApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate

    var body: some Scene {
        Settings {
            SettingsView()
        }
        .windowToolbarStyle(.unifiedCompact)
        .defaultSize(width: 860, height: 640)
    }
}

/// 从 AppKit 代码打开 SwiftUI Settings 场景的桥。
@MainActor
enum SettingsOpener {
    private static var windowController: NSWindowController?

    static func open() {
        NSApp.activate(ignoringOtherApps: true)
        if windowController == nil {
            let host = NSHostingController(rootView: SettingsView())
            let window = NSWindow(
                contentRect: NSRect(x: 0, y: 0, width: 860, height: 640),
                styleMask: [.titled, .closable, .miniaturizable, .resizable],
                backing: .buffered,
                defer: false
            )
            window.title = "TypeTide Settings"
            window.contentViewController = host
            window.isReleasedWhenClosed = false
            window.setFrameAutosaveName("TypeTideSettingsWindow")
            // Clamp restored windows too: older releases saved a shorter setup window.
            window.contentMinSize = NSSize(width: 780, height: 560)
            let contentSize = window.contentRect(forFrameRect: window.frame).size
            window.setContentSize(NSSize(width: max(780, contentSize.width),
                                         height: max(560, contentSize.height)))
            window.center()
            windowController = NSWindowController(window: window)
        }
        windowController?.showWindow(nil)
        windowController?.window?.makeKeyAndOrderFront(nil)
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private var statusItem: NSStatusItem?
    private let statusMenu = NSMenu()

    func applicationDidFinishLaunching(_ notification: Notification) {
        // 纯菜单栏：不在 Dock 显示
        NSApp.setActivationPolicy(.accessory)
        installCompactStatusItem()

        // 系统服务
        _ = SystemServiceProvider.shared

        // 注册全局快捷键 + 划词触发
        TriggerController.shared.start()

#if DEBUG
        startTideDemoIfRequested()
#endif

        // 若配置的 Ollama 模型未安装，自动挑一个已装模型（避免开箱即败）
        Task { @MainActor in
            if let model = await OllamaModelResolver.ensureValidDefault(), AppSettings.backend == .ollama {
                await OllamaModelResolver.preload(model)
            }
        }

        // 首次启动：打开设置窗口，让用户立刻看到界面并完成配置
        if !UserDefaults.standard.bool(forKey: AppSettings.Keys.hasCompletedFirstLaunch) {
            Task { @MainActor in
                // 等 Settings 场景完成注册
                try? await Task.sleep(nanoseconds: 500_000_000)
                SettingsOpener.open()
            }
        }

        // 自动检查更新
        Task { @MainActor in
            if UpdateChecker.shared.shouldAutoCheck() {
                UpdateChecker.shared.checkForUpdates(silent: true)
            }
        }
    }

    /// App 已在运行时被再次打开（Finder/启动台双击）：没有可见窗口就打开设置，
    /// 避免“怎么双击都没反应”。
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if !flag { SettingsOpener.open() }
        return true
    }

    /// SwiftUI MenuBarExtra 在 macOS 26 会强制使用约 50×34 pt 的 Liquid Glass
    /// 容器。NSStatusItem.squareLength 保持与 Wi-Fi、电池等系统项一致的紧凑宽度。
    private func installCompactStatusItem() {
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        if let button = item.button {
            let image = BrandIcon.templateImage.copy() as? NSImage ?? BrandIcon.templateImage
            image.size = NSSize(width: 16, height: 16)
            image.isTemplate = true
            button.image = image
            button.imageScaling = .scaleProportionallyDown
            button.toolTip = "TypeTide"
        }
        statusMenu.autoenablesItems = false
        statusMenu.delegate = self
        item.menu = statusMenu
        statusItem = item
        rebuildStatusMenu()
    }

    private func rebuildStatusMenu() {
        statusMenu.removeAllItems()

        if UpdateChecker.shared.hasNewVersion {
            let update = NSMenuItem(title: "New update available", action: #selector(openUpdate), keyEquivalent: "")
            update.target = self
            update.image = NSImage(systemSymbolName: "arrow.down.circle.fill", accessibilityDescription: nil)
            statusMenu.addItem(update)
            statusMenu.addItem(.separator())
        }

        let enabled = NSMenuItem(title: AppSettings.isEnabled ? "TypeTide is On" : "TypeTide is Off",
                                 action: #selector(toggleEnabled), keyEquivalent: "")
        enabled.target = self
        enabled.state = AppSettings.isEnabled ? .on : .off
        statusMenu.addItem(enabled)
        statusMenu.addItem(.separator())

        let read = NSMenuItem(title: "\(AppSettings.readShortcutAction.displayName):  \(AppSettings.readShortcut?.displayString ?? "Not set")",
                              action: nil, keyEquivalent: "")
        read.isEnabled = false
        statusMenu.addItem(read)
        let rewrite = NSMenuItem(title: "\(AppSettings.rewriteShortcutAction.displayName):  \(AppSettings.rewriteShortcut?.displayString ?? "Not set")",
                                 action: nil, keyEquivalent: "")
        rewrite.isEnabled = false
        statusMenu.addItem(rewrite)
        statusMenu.addItem(.separator())

        let settings = NSMenuItem(title: "Settings…", action: #selector(openSettings), keyEquivalent: ",")
        settings.keyEquivalentModifierMask = [.command]
        settings.target = self
        settings.image = NSImage(systemSymbolName: "gearshape", accessibilityDescription: nil)
        statusMenu.addItem(settings)

        let quit = NSMenuItem(title: "Quit TypeTide", action: #selector(quitApp), keyEquivalent: "q")
        quit.keyEquivalentModifierMask = [.command]
        quit.target = self
        quit.image = NSImage(systemSymbolName: "power", accessibilityDescription: nil)
        statusMenu.addItem(quit)
    }

    @objc private func toggleEnabled() {
        AppSettings.isEnabled.toggle()
        TriggerController.shared.applyEnabledState()
        rebuildStatusMenu()
    }

    @objc private func openUpdate() { UpdateChecker.shared.openReleasesPage() }
    @objc private func openSettings() { SettingsOpener.open() }
    @objc private func quitApp() { NSApp.terminate(nil) }

    func menuWillOpen(_ menu: NSMenu) { rebuildStatusMenu() }

#if DEBUG
    /// README/开发演示入口：使用真实弹窗和真实动画状态机，不触碰用户选区或剪贴板。
    private func startTideDemoIfRequested() {
        guard ProcessInfo.processInfo.arguments.contains("--demo-tide") else { return }
        Task { @MainActor in
            try? await Task.sleep(for: .milliseconds(700))
            let visible = NSScreen.main?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1440, height: 900)
            let anchor = NSRect(x: visible.midX - 190, y: visible.midY + 90, width: 380, height: 24)
            let model = PopupController.shared.show(
                original: "你好世界，今天天气很好。",
                target: .english,
                anchor: anchor,
                onReplace: {},
                dismissOnInteraction: false
            )

            try? await Task.sleep(for: .milliseconds(650))
            for chunk in ["Hello", " world.", " The weather", " is lovely", " today."] {
                model.appendTranslation(chunk)
                try? await Task.sleep(for: .milliseconds(280))
            }
            model.finishTranslation()
            try? await Task.sleep(for: .milliseconds(420))
            model.finishTide()
        }
    }
#endif
}
