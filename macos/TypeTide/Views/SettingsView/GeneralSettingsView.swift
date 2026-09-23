//
//  GeneralSettingsView.swift
//  TypeTide
//

import SwiftUI

struct GeneralSettingsView: View {
    @AppStorage(AppSettings.Keys.enabled) private var isEnabled = true
    @State private var hasPermission = AccessibilityPermission.isGranted
    @State private var launchAtLogin = LaunchAtLogin.isEnabled

    var body: some View {
        Form {
            Section {
                Toggle(isOn: $isEnabled) {
                    SettingsLabel(symbol: "power", color: .green, title: "Enable TypeTide")
                }
                .onChange(of: isEnabled) { _, _ in
                    TriggerController.shared.applyEnabledState()
                }

                Toggle(isOn: $launchAtLogin) {
                    SettingsLabel(symbol: "arrow.up.forward.app.fill", color: .indigo, title: "Launch at login")
                }
                .onChange(of: launchAtLogin) { _, newValue in
                    if !LaunchAtLogin.set(newValue) {
                        // 失败时回滚到真实状态
                        launchAtLogin = LaunchAtLogin.isEnabled
                    }
                }

                HStack {
                    SettingsLabel(symbol: hasPermission ? "checkmark.shield.fill" : "exclamationmark.shield.fill",
                                  color: hasPermission ? .blue : .orange,
                                  title: "Accessibility Permission")
                    Spacer()
                    if hasPermission {
                        Text("Granted").foregroundColor(.blue).fontWeight(.medium)
                    } else {
                        Button("Grant Permission") { AccessibilityPermission.request() }
                            .buttonStyle(.borderedProminent)
                            .controlSize(.small)
                    }
                }

                if !hasPermission {
                    VStack(alignment: .leading, spacing: 8) {
                        SettingsNote(text: "TypeTide needs Accessibility permission to read selected text and replace text in other apps.",
                                     symbol: "exclamationmark.triangle.fill", tint: .orange)
                        Text("In the macOS prompt, choose Open System Settings and enable TypeTide. If it is missing, Show Current App reveals the running copy; add that copy with + or drag it into the list.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        HStack {
                            Button("Open System Settings") { AccessibilityPermission.openSystemSettings() }
                            Button("Show Current App") { AccessibilityPermission.revealCurrentApp() }
                        }
                        .controlSize(.small)
                    }
                }
            } header: {
                SettingsSectionHeader(symbol: "gearshape.fill", color: .blue,
                                      title: "Status", subtitle: "Core toggles and permissions")
            }

            Section {
                SettingsLabel(symbol: "text.magnifyingglass", color: .blue,
                              title: AppSettings.readShortcut.map {
                                  "Select text, then press \($0.displayString) to run its configured action."
                              } ?? "The selection shortcut is not set. Configure it in Shortcuts if needed.")
                SettingsLabel(symbol: "arrow.left.arrow.right", color: .green,
                              title: AppSettings.rewriteShortcut.map {
                                  "Type or select text, then press \($0.displayString) to run its configured action."
                              } ?? "The rewrite shortcut is not set. Configure it in Shortcuts if needed.")
            } header: {
                SettingsSectionHeader(symbol: "book.fill", color: .purple, title: "How to use")
            }
        }
        .settingsPage("General")
        .task {
            while !Task.isCancelled {
                hasPermission = AccessibilityPermission.isGranted
                do { try await Task.sleep(for: .seconds(1)) }
                catch { return }
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didBecomeActiveNotification)) { _ in
            hasPermission = AccessibilityPermission.isGranted
        }
    }
}
